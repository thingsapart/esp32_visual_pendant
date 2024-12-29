import sys
platform = sys.platform

if not (platform == 'win32' or platform == 'darwin' or platform == 'linux'):
    import uasyncio
    from uasyncio import sleep, create_task, Loop, CancelledError


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
    def __init__(self, state_update_callback, sleep_ms = DEFAULT_SLEEP_MS):
        self.sleep_ms = sleep_ms
        self.cb = state_update_callback

        self.machine_status = MachineStatus.UNKNOWN
        self.axes_homed = [False, False, False]
        self.position = [None, None, None]
        self.wcs_position = [None, None, None]
        self.wcs = None
        self.tool = None
        self.feed_multiplier = 1.0
        self.z_offs = 0.0
        self.probes = [0, 0]
        self.end_stops = None
        self.spindles = []
        self.dialogs = None

        self.gcode_queue = [None, None, None, None, None]
        self.gcode_q_len = 0

        self.poll_state = MachineInterface.DEFAULT_POLL_STATES

    def send_gcode(self, gcode, poll_state):
        if self.gcode_q_len == len(self.gcode_queue):
            process_gcode_q()

        self.gcode_queue[self.gcode_q_len] = gcode
        self.gcode_q_len += 1

        self.poll_state = self.poll_state | poll_state

    def _send_gcode(self, gcode):
        raise Error('implement this method in sub-class')

    def _update_machine_state(self, poll_state):
        raise Error('implement this method in sub-class')

    def process_gcode_q(self):
        if self.gcode_q_len > 0:
            for i in range(self.gcode_q_len):
                self._send_gcode(self.gcode_queue[i])
                self.gcode_queue[i] = None

        self._update_machine_state(self.poll_state)

        self.gcode_q_len = 0
        self.poll_state = MachineInterface.DEFAULT_POLL_STATES

        self.cb(self)

    def task_loop(self):
        while True:
            process_gcode_q()

            await uasyncio.sleep_ms(self.sleep_ms)

    def setup_loop_and_run_forever(self):
        #######################################################################
        # Start event loop
        #######################################################################
        try:
            create_task(self.task_loop())
        except KeyboardInterrupt:
            print("Interrupted")
        finally:
            Loop.run_forever()

    def move(self, axis, feed, value):
        self.send_gcode("M120\nG91\nG1 %s%.3f F%.3f\nM121" % (axis, value, feed),
                        PollState.MACHINE_POSITION)

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
            'gcode_q': self.gcode_queue[0:self.gcode_q_len],
            'poll_state': self.poll_state
            }
