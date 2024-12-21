# Workaround for including frozen modules when running micropython with a script argument
# https://github.com/micropython/micropython/issues/6419
import usys as sys
sys.path.append('')

import json

from micropython import const

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

class ReprapMachineStatus:
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
        self.status = MachineStatus.UNKNOWN
        self.wcs = 0
        self.axes_homed = [0, 0, 0]
        self.coords_machine = [None, None, None]
        self.coords_wcs = [None, None, None]
        self.tool = None
        self.output = None
        self.feed_multiplier = 1.0
        self.z_offs = 0.0
        self.probe = 0
        self.probe_secondary = 0
        self.spindles = []

    def machine_status(self):
        return {
            key:value for key, value in self.__dict__.items()
            if not key.startswith('__')
            and not callable(value)
            and not callable(getattr(value, "__get__", None)) # <- important
        }

    def get_machine_status_common(self):
        pass

    def get_machine_status_ext(self):
        pass

    def parse_m409(self, json_resp):
        j = json.loads(json_resp)

        self.status = ReprapMachineStatus.RRF_TO_STATUS[j['status']]
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
