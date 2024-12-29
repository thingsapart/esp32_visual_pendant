# Workaround for including frozen modules when running micropython with a script argument
# https://github.com/micropython/micropython/issues/6419
import usys as sys
sys.path.append('')

import json

from micropython import const


from machine_interface import MachineInterface, PollState, MachineStatus

import sys

platform = sys.platform
if platform == 'win32' or platform == 'darwin' or platform == 'linux':
    class Pin:
        def __init__(self, pin):
            self.pin = pin

    class UART:
        def __init__(self, pos, baud, tx=None, rx=None, rxbuf=0):
            self.last = None
            self.last_args = None
            self.pos = [0.0, 0.0, 0.0]
            self.homed = [False, False, False]

        def write(self, gcodes):
            for gcode in gcodes.split('\n'):
                if len(gcode) > 0:
                    l = gcode.split()
                    self.last = l[0].upper()
                    self.last_args = l[1:]

                    if self.last == 'G1' or self.last == 'G0':
                        axis_i = { 'X': 0, 'Y': 1, 'Z': 2 }
                        for ax in self.last_args:
                            axis = ax[0].upper()
                            if axis in axis_i:
                                v = float(ax[1:])
                                self.pos[axis_i[axis]] += v

        def read(self):
            if self.last == 'M409':
               k = self.last_args[0][1:].replace('"', '').replace("'", '')
               if k == 'move.axes':
                   return '''{
        "status": "O",
        "coords": {
            "axesHomed": [0, 0, 0],
            "wpl": 1,
            "xyz": [%.3f, %.3f, %.3f],
            "machine": [%.3f, %.3f, %.3f],
            "extr": []
        },
        "speeds": {
            "requested": 0.0,
            "top": 0.0
        },
        "currentTool": -1,
        "output": {
            "beepDuration": 1234,
            "beepFrequency": 4567,
            "message": "Test message",
            "msgBox": {
                "msg": "my message",
                "title": "optional title",
                "mode": 0,
                "seq": 5,
                "timeout": 10.0,
                "controls": 0
            }
        },
        "params": {
            "atxPower": -1,
            "fanPercent": [-100],
            "speedFactor": 100.0,
            "extrFactors": [],
            "babystep": 0.000,
            "seq": 1
        },
        "sensors": {
            "probeValue": 1000,
            "probeSecondary": 1000,
            "fanRPM": [-1]
        },
        "temps": {
            "bed": {
                "current": -273.1,
                "active": -273.1,
                "standby": -273.1,
                "state": 0,
                "heater": 0
            },
            "current": [-273.1],
            "state": [0],
            "tools": {
                "active": [],
                "standby": []
            },
            "extra": []
        },
        "time": 596.0,
        "scanner": {
            "status": "D",
            "progress": 0.0
        },
        "spindles": [],
        "laser": 0.0
    }'''  % (self.pos[0], self.pos[1], self.pos[2], self.pos[0], self.pos[1], self.pos[2])

            raise Error('Unknown arg to M409')
else:
    from machine import UART, Pin

class MachineRRF(MachineInterface):
    RRF_TO_STATUS = {
        'C': MachineStatus.INITIALIZING,
        'F': MachineStatus.FLASHING_FIRMWARE,
        'H': MachineStatus.EMERGENCY_HALTED,
        'O': MachineStatus.OFF,
        'D': MachineStatus.PAUSED_DEC,
        'R': MachineStatus.PAUSED_RESUME,
        'S': MachineStatus.PAUSED,
        'M': MachineStatus.SIMULATING,
        'P': MachineStatus.RUNNING,
        'T': MachineStatus.TOOL_CHANGING,
        'B': MachineStatus.BUSY,
    }

    def __init__(self, state_update_callback, sleep_ms = MachineInterface.DEFAULT_SLEEP_MS):
        super().__init__(state_update_callback, sleep_ms)

        self.uart = UART(2, 115200, tx=Pin(43), rx=Pin(44), rxbuf=1024*16)

    def get_machine_status_ext(self):
        self.uart.write('M409 K"move.axes[]"\n')
        res = self.uart.read()

    def _send_gcode(self, gcode):
        self.uart.write(gcode + '\n')

    def _proc_machine_state(self, cmd):
        self._send_gcode(cmd)
        res = self.uart.read()
        self.parse_m409(res)

    def _update_machine_state(self, poll_state):
        if PollState.has_state(poll_state, PollState.MACHINE_POSITION):
            self._proc_machine_state('M409 K"move.axes" F"d2"')

    def parse_m409(self, json_resp):
        j = json.loads(json_resp)

        if j['status']: self.status = MachineRRF.RRF_TO_STATUS[j['status']]
        if j['coords']:
            coords = j['coords']
            self.wcs = coords['wpl']
            self.axes_homed = coords['axesHomed']
            self.position = coords['machine']
            self.wcs_position = coords['xyz']

        self.tool = j['currentTool']
        self.output = j['output']

        params = j['params']
        self.feed_multiplier = params['speedFactor'] / 100.0
        self.z_offs = params['babystep']

        sensors = j['sensors']
        self.probe = sensors['probeValue']
        self.probe_secondary = sensors['probeSecondary']

        self.spindles = j['spindles']

if __name__ == '__main__':
    m = MachineRRF(lambda x: print(x))
    m.move('X', 100.0, 22.0)
    print(m.debug_print())
    m.process_gcode_q()
    print(m.debug_print())
