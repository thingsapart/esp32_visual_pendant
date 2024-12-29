# Workaround for including frozen modules when running micropython with a script argument
# https://github.com/micropython/micropython/issues/6419
import usys as sys
sys.path.append('')

import json

from micropython import const

from machine import UART, Pin

from machine_interface import MachineInterface, PollState, MachineStatus



    MOVE_GCODE = "M120\nG91\nG1 %s%f.3 F%d\nM121"

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

    def __init__(self):
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

        self.uart = UART(2, 115200, tx=Pin(43), rx=Pin(44), rxbuf=1024*16)

    # Debug summary of all public fields.
    def machine_status(self):
        return {
            key:value for key, value in self.__dict__.items()
            if not key.startswith('__')
            and not callable(value)
            and not callable(getattr(value, "__get__", None)) # <- important
        }

    def get_machine_status_ext(self):
        self.uart.write('M409 K"move.axes[]"\n')
        res = self.uart.read()

    def send(self, gcode):
        self.uart.write(gcode)

    def move(self, axis, feed):
        send("M120\nG91\nG1 X50 F6000\nM121")
        read_machine_position_response()
        #get_machine_position()

    def read_status(self):
        uart.write('M409\n')
        res = uart.read()
        print(res)

    def parse_m409(self, json_resp):
        j = json.loads(json_resp)

        if j['status']: self.status = ReprapMachineStatus.RRF_TO_STATUS[j['status']]
        if j['coords']:
            coords = j['coords']
            self.wcs = coords['wpl']
            self.axes_homed = coords['axesHomed']
            self.coords_machine = coords['machine']
            self.coords_wcs = coords['xyz']

        self.tool = j['currentTool']
        self.output = j['output']

        params = j['params']
        self.feed_multiplier = params['speedFactor'] / 100.0
        self.z_offs = params['babystep']

        sensors = j['sensors']
        self.probe = sensors['probeValue']
        self.probe_secondary = sensors['probeSecondary']

        self.spindles = j['spindles']

    def send_gcode(self, gcode):
        pass

if __name__ == '__main__':
    TEST = '''{
	"status": "O",
	"coords": {
		"axesHomed": [0, 0, 0],
		"wpl": 1,
		"xyz": [0.000, 0.000, 0.000],
		"machine": [0.000, 0.000, 0.000],
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
}'''
    mach_state = ReprapMachineStatus()
    print(mach_state.machine_status())
    mach_state.parse_m409(TEST)
    print(mach_state.machine_status())
