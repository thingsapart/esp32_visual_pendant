import sys
import math
from micropython import const

platform = sys.platform

if not (platform == 'win32' or platform == 'darwin' or platform == 'linux'):
    import uasyncio
    from uasyncio import sleep, create_task, CancelledError, Loop

from collections import deque

class PollState:
    MACHINE_POSITION =     const(1)
    MACHINE_POSITION_EXT = const(2)
    SPINDLE =              const(4)
    PROBES =               const(8)
    TOOLS =                const(16)
    MESSAGES_AND_DIALOGS = const(32)
    END_STOPS =            const(64)
    NETWORK =              const(128)
    JOB_STATUS =           const(256)
    LIST_FILES =           const(512)
    LIST_MACROS =          const(1024)

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
    DEFAULT_FEED = 3000

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
        self.moving_target_position = [None, None, None]
        self.wcs = 1
        self.tool = None
        self.z_offs = 0.0
        self.probes = [0, 0]
        self.end_stops = None
        self.spindles = []
        self.dialogs = None

        self.feed = 0
        self.feed_req = 0

        self.feed_multiplier = 1.0

        self.move_relative = None
        self.move_step = None # None => Continuous

        self.network = []
        self.files = {}

        self.job = {}

        self.message_box = None

        self.probes = []

        self.gcode_queue = deque((), MachineInterface.MAX_GCODE_Q_LEN)

        self.poll_state = MachineInterface.DEFAULT_POLL_STATES

        self.cb = None
        self.pos_changed_cbs = []
        self.home_changed_cbs = []
        self.wcs_changed_cbs = []
        self.feed_changes_cbs = []
        self.sensors_changed_cbs = []
        self.dialogs_changed_cbs = []
        self.spindles_tools_changed_cbs = []
        self.connected_cbs = []
        self.files_changed_cbs = {}

        self.polli = -1

    def set_state_change_callback(self, state_update_callback):
        self.cb = state_update_callback

    def add_files_changed_cb(self, files_change_cb, path):
        if not path in self.files_changed_cbs:
            self.files_changed_cbs[path] = []
        self.files_changed_cbs[path].append(files_change_cb)

    def add_pos_changed_cb(self, pos_change_cb):
        self.pos_changed_cbs.append(pos_change_cb)

    def add_home_changed_cb(self, home_change_cb):
        self.home_changed_cbs.append(home_change_cb)

    def add_wcs_changed_cb(self, wcs_change_cb):
        self.wcs_changed_cbs.append(wcs_change_cb)

    def add_feed_changed_cb(self, feed_change_cb):
        self.feed_changed_cbs.append(feed_change_cb)

    def add_sensors_changed_cb(self, sensors_change_cb):
        self.sensors_changed_cbs.append(sensors_change_cb)

    def add_dialogs_changed_cb(self, dialogs_change_cb):
        self.dialogs_changed_cbs.append(dialogs_change_cb)

    def add_spindles_tools_changed_cb(self, spindles_tools_change_cb):
        self.spindles_tools_changed_cbs.append(spindles_tools_change_cb)

    def add_connected_cb(self, conn_cb):
        self.connected_cbs.append(conn_cb)

    def is_homed(self, axes=None):
        if axes is None: axes = range(len(self.axes_homed))
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

    def process_gcode_q(self, max=1):
        if len(self.gcode_queue) > 0:
            for gcode in self.gcode_queue:
                self._send_gcode(gcode)
                if self._has_response():
                    # TODO: act on response and possible failure.
                    print(self._read_response())
            for i in range(len(self.gcode_queue)):
               self.gcode_queue.pop()

    def next_poll_state(self):
        poll_state = PollState.MACHINE_POSITION_EXT if self.polli % 19 == 0 else PollState.MACHINE_POSITION
        if self.polli % 3 == 0:
            poll_state = poll_state or PollState.JOB_STATUS
        if self.polli % 5 == 0:
            poll_state = poll_state or PollState.MESSAGES_AND_DIALOGS
        if self.polli % 7 == 0:
            poll_state = poll_state or PollState.PROBES
        if self.polli % 11 == 0:
            poll_state = poll_state or  PollState.END_STOPS
        if self.polli % 13 == 0:
            poll_state = poll_state or PollState.SPINDLE
        if self.polli % 17 == 0:
            poll_state = poll_state or PollState.TOOLS
        # Refresh files and macros very infrequently.
        if self.polli % 9973 == 0:
            poll_state = poll_state or PollState.LIST_MACROS or PollState.LIST_FILES

        return poll_state

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

        self.polli += 1
        self.poll_state = self.next_poll_state()

    async def task_loop(self):
        while True:
            # print('.')

            # Send any outstanding commands.
            self.process_gcode_q()

            # Query machine state.
            await self._update_machine_state(self.poll_state)

            self.polli += 1
            self.poll_state = self.next_poll_state()

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

    def maybe_execute_continuous_move(self):
        # Check if there are any continous moves to execute.
        for i in range(len(self.position)):
            wcs = self.wcs_position[i]
            mps = self.moving_target_position[i]
            ps = self.position[i]
            tps = self.target_position[i]
            print(mps, wcs,  mps is not None and wcs is not None and
                  math.isclose(wcs, mps))
            if mps is None or (wcs is not None and math.isclose(ps, mps)):
                print("REACHED", i, ps, wcs, mps)
                self.moving_target_position[i] = None

                has_new_target = tps is not None and ps is not None and not math.isclose(tps, ps)
                if has_new_target and self.is_continuous_move():
                    # Execute any "summed up" continuous moves while other move
                    # was executing.
                    self._move_to(self.AXES[i], self.DEFAULT_FEED, tps-wcs, relative=True)
            else:
                print("NOT REACHED", i, self.position[i], self.wcs_position[i],
                      self.moving_target_position[i])

    def position_updated(self):
        self.maybe_execute_continuous_move()

        for cb in self.pos_changed_cbs: cb(self)

    def home_updated(self):
        for cb in self.home_changed_cbs: cb(self)

    def wcs_updated(self):
        self.moving_target_position = [None] * len(self.AXES)
        self.target_position = [None] * len(self.AXES)

        for cb in self.wcs_changed_cbs: cb(self)

    def feed_updated(self):
        for cb in self.feed_changed_cbs: cb(self)

    def sensors_updated(self):
        for cb in self.sensors_changed_cbs: cb(self)

    def dialogs_updated(self):
        for cb in self.dialogs_changed_cbs: cb(self)

    def spindles_tools_updated(self):
        for cb in self.spindles_tools_changed_cbs: cb(self)

    def files_updated(self, fdir):
        if fdir in self.files_changed_cbs:
            for cb in self.files_changed_cbs[fdir].items(): cb(self, fdir)

    def connected_updated(self):
        for cb in self.connected_cbs: cb(self)

    def update_position(self, values, values_wcs):
        self.position = values
        self.wcs_position = values_wcs

        self.position_updated()

    def is_continuous_move(self):
        return self.move_step is None

    def _move_to(self, axis, feed, value, relative=True):
        if isinstance(axis, str):
            axi = self._axis_idx(axis)
        else:
            axi = axis

        mode = 'G91' if relative else 'G90'
        pos = value
        if relative:
            self.moving_target_position[axi] = self.wcs_position[axi] + value
        else:
            self.moving_target_position[axi] = value
            pos = self.moving_target_position[axi]
        print("SEND: M120\n%s\nG1 %s%.3f F%.3f\nM121" % (mode, axis, pos, feed))
        self.send_gcode("M120\n%s\nG1 %s%.3f F%.3f\nM121" % (mode, axis, pos, feed),
                        PollState.MACHINE_POSITION)

    # Always a differential move, value is an offset from current position.
    def move(self, axis, feed, value):
        axi = self._axis_idx(axis)
        print('move0', self.target_position[axi], value)
        has_target = self.target_position[axi] is not None
        has_pos = self.position[axi] is not None
        continuous = self.is_continuous_move()

        print("move", continuous, has_target,
                  self.target_position[axi], value)
        if not has_target: self.target_position[axi] = self.wcs_position[axi]
        self.target_position[axi] = self.target_position[axi] + value
        print("move2", continuous, has_target,
                  self.target_position[axi], value)

        if not continuous or not has_pos:
            self._move_to(axis, feed, value, relative=True)
        else:
            print("cont move", continuous, has_target,
                  self.target_position[axi], value)
            self.maybe_execute_continuous_move()
        # else:
        #     # Update target position only in continuous mode.
        #     # We send the next machine position when the currentPos ==
        #     targetPos.

    def home_all(self):
        self.send_gcode("G28", PollState.MACHINE_POSITION)
        self.target_position[i] = [None] * len(self.AXES)
        self.position[i] = [None] * len(self.AXES)
        self.wcs_position[i] = [None] * len(self.AXES)

    def home(self, axes):
        if not isinstance(axes, str):
            for ax in axes: self.target_position[self._axis_idx(ax)] = None
            axes = ''.join(axes)
        else:
            self.target_position[self._axis_idx(axes)] = None
        self.send_gcode('G28 ' + axes, PollState.MACHINE_POSITION)

    def get_wcs_str(self, wcs_offs=None):
        wcs = ''
        wcsi = self.wcs if wcs_offs is None else wcs_offs
        if wcsi <= 5:
            wcs = str(54 + wcsi)
        elif wcsi <= 8:
            wcs = '59.' + str(wcsi - 5)
        return 'G' + wcs

    def set_wcs(self, wcs):
        wcs = self.get_wcs_str(wcs_offs=wcs%9)
        self.send_gcode(wcs, PollState.MACHINE_POSITION)

    def set_wcs_zero(self, wcs, axes):
        zer = ' '.join([ax + '0' for ax in axes])
        self.send_gcode('G10 L20 P%d %s' % (wcs, zer),
                        PollState.MACHINE_POSITION)

    def next_wcs(self):
        self.set_wcs(self.wcs + 1)

    def list_files(self, path):
        _childclass_override()

    def run_macro(self, macro_name):
        _childclass_override()

    def start_job(self, job_name):
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
