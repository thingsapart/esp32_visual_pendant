import sys
platform = sys.platform

if not (platform == 'win32' or platform == 'darwin' or platform == 'linux'):
    import uasyncio
    from uasyncio import sleep, create_task, CancelledError, Loop

from collections import deque

class PollState:
    MACHINE_POSITION = 1
    SPINDLE = 2
    PROBES = 4
    TOOLS = 8
    MESSAGES_AND_DIALOGS = 16
    END_STOPS = 32

    @classmethod
    def has_state(cls, poll_state, val):
        return poll_state & val != 0

    @classmethod
    def all_states(cls):
        return [PollState.MACHINE_POSITION, PollState.SPINDLE, PollState.PROBES,
         PollState.TOOLS, PollState.MESSAGES_AND_DIALOGS, PollState.END_STOPS]

class MachineStatus:
    INITIALIZING = 0
    FLASHING_FIRMWARE = 1
    EMERGENCY_HALTED = 2
    OFF = 3
    PAUSED_DEC = 4
    PAUSED_RESUME = 5
    PAUSED = 6
    SIMULATING = 7
    RUNNING = 8
    TOOL_CHANGING = 9
    BUSY = 10
    UNKNOWN = 100

# Simple polled machine using asyncio.
#
# We're learning things as we go along with asyncio so feel free to suggest
# impovements!
class MachineInterface:
    DEFAULT_POLL_STATES = PollState.MACHINE_POSITION | PollState.SPINDLE | \
        PollState.PROBES | PollState.TOOLS | \
        PollState.MESSAGES_AND_DIALOGS | PollState.END_STOPS

    DEFAULT_SLEEP_MS = 200
    AXES = ['X', 'Y', 'Z']

    # How many g-code commands to buffer before overwriting them by FIFO.
    MAX_GCODE_Q_LEN = 10

    # state_update_callback(state) => self: object having attributes:
    #        machine_status: MachineStatus.constants => "status of machine",
    #        axes_homed: [Bool] => "wheter each axis is homed",
    #        position: tuple of (axis_name: str, pos: float),
    #        wcs_position: tuple of (axis_name: str, pos: float),
    #        wcs: str => "WCS name/id",
    #        tool: str => "current tool id/name",
    #        z_offs: float => "current z-offset/baby-step",
    #        feed_multiplier: float => "current feed multiplier",
    #        probes: tuple of (id: str, value: float),
    #        end_stops: tuple of (name: str, triggered: bool),
    #        dialogs: ... tbd,
    #        gcode: str => "gcode to be executed next"
    def __init__(self, sleep_ms = DEFAULT_SLEEP_MS):
        self.sleep_ms = sleep_ms

        self.machine_status = MachineStatus.UNKNOWN
        self.axes_homed = [False, False, False]
        self.position = [None, None, None]
        self.wcs_position = [None, None, None]
        self.target_position = [None, None, None]
        self.wcs = 1
        self.tool = None
        self.feed_multiplier = 1.0
        self.z_offs = 0.0
        self.probes = [0, 0]
        self.end_stops = None
        self.spindles = []
        self.dialogs = None

        self.gcode_queue = deque((), MachineInterface.MAX_GCODE_Q_LEN)

        self.poll_state = MachineInterface.DEFAULT_POLL_STATES

        self.cb = None
        self.pos_changed_cbs = []
        self.home_changed_cbs = []
        self.wcs_changed_cbs = []

    def set_state_change_callback(self, state_update_callback):
        self.cb = state_update_callback

    def add_pos_changed_cb(self, pos_change_cb):
        self.pos_changed_cbs.append(pos_change_cb)

    def add_home_changed_cb(self, home_change_cb):
        self.home_changed_cbs.append(home_change_cb)

    def add_wcs_changed_cb(self, wcs_change_cb):
        self.wcs_changed_cbs.append(wcs_change_cb)

    def is_homed(self, axes=None):
        if axes is None: axes = range(len(self.axes_homed))
        print('HOMED', not (False in [self.axes_homed[ax] for ax in axes]),
              'VALS', [self.axes_homed[ax] for ax in axes], self.axes_homed)
        return not (False in [self.axes_homed[ax] for ax in axes])

    def send_gcode(self, gcode, poll_state):
        # Try to process g-code queue out of band if buffer fills up,
        # otherwise done in the main asyncio loop periodically.
        if len(self.gcode_queue) >= MachineInterface.MAX_GCODE_Q_LEN - 2:
            self.process_gcode_q()

        self.gcode_queue.append(gcode)

        self.poll_state = self.poll_state | poll_state

    def _send_gcode(self, gcode):
        _childclass_override()

    async def _update_machine_state(self, poll_state):
        _childclass_override()

    def process_gcode_q(self):
        if len(self.gcode_queue) > 0:
            for gcode in self.gcode_queue:
                self._send_gcode(gcode)
                if self._has_response():
                    # TODO: act on response and possible failure.
                    print(self._read_response())
            for i in range(len(self.gcode_queue)):
               self.gcode_queue.pop()

    def task_loop_iter(self):
        # Send any outstanding commands.
        self.process_gcode_q()

        # Query machine state.
        try:
            t = self._update_machine_state(self.poll_state)
            while True: t.send(None)
        except StopIteration:
            pass
        print(self.debug_print())
        self.cb(self)

        self.poll_state = MachineInterface.DEFAULT_POLL_STATES

    async def task_loop(self):
        while True:
            # print('.')

            # Send any outstanding commands.
            self.process_gcode_q()

            # Query machine state.
            await self._update_machine_state(self.poll_state)
            self.poll_state = MachineInterface.DEFAULT_POLL_STATES
            self.cb(self)

            await uasyncio.sleep_ms(self.sleep_ms)
            #await uasyncio.sleep_ms(5000)

    def setup_loop(self):
        #######################################################################
        # Start event loop
        #######################################################################
        print('Machine evt loop...')
        try:
            create_task(self.task_loop())
        except KeyboardInterrupt:
            print("Interrupted")
        # finally:
        #     Loop.run_forever()

    def position_updated(self):
        for cb in self.pos_changed_cbs: cb(self)

    def home_updated(self):
        for cb in self.home_changed_cbs: cb(self)

    def wcs_updated(self):
        for cb in self.wcs_changed_cbs: cb(self)

    def update_position(self, values, values_wcs):
        self.position = values
        self.wcs_position = values_wcs

        self.position_updated()

    def move(self, axis, feed, value):
        self.send_gcode("M120\nG91\nG1 %s%.3f F%.3f\nM121" % (axis, value, feed),
                        PollState.MACHINE_POSITION)
        self.target_position[self._axis_idx(axis)] = value

    def home_all(self):
        self.send_gcode("G28", PollState.MACHINE_POSITION)
        self.target_position[i] = [None] * len(self.AXES)

    def home(self, axes):
        if not isinstance(axes, str):
            for ax in axes: self.target_position[self._axis_idx(ax)] = None
            axes = ''.join(axes)
        else:
            self.target_position[self._axis_idx(axes)] = None
        print('G28 ' + axes)
        self.send_gcode('G28 ' + axes, PollState.MACHINE_POSITION)

    def set_wcs_zero(self, wcs, axes):
        zer = ' '.join([ax + '0' for ax in axes])
        self.send_gcode('G10 L20 P%d %s' % (wcs, zer),
                        PollState.MACHINE_POSITION)

    def set_wcs(self, wcs):
        self.send_gcode('G' + str(53 + wcs), PollState.MACHINE_POSITION)

    def list_gcode_files(self):
        _childclass_override()

    def list_macros(self):
        _childclass_override()

    def run_macro(self, macro_name):
        _childclass_override()

    def is_connected(self):
        _childclass_override()

    def _childclass_override(self):
        raise Exception('implement this method in sub-class')

    def _axis_idx(self, ax):
        return self.AXES.index(ax)

    def debug_print(self):
        return {
            'status': self.machine_status,
            'homed': self.axes_homed,
            'pos': self.position,
            'wcs_pos': self.wcs_position,
            'wcs': self.wcs,
            'tool': self.tool,
            'feedm': self.feed_multiplier,
            'zoffs': self.z_offs,
            'probes': self.probes,
            'end_stops': self.end_stops,
            'spindles': self.spindles,
            'dialogs': self.dialogs,
            'gcode_q': self.gcode_queue,
            'poll_state': self.poll_state
            }
