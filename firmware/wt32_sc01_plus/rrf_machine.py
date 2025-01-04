# Workaround for including frozen modules when running micropython with a script argument
# https://github.com/micropython/micropython/issues/6419
import usys as sys
sys.path.append('')

# RUN_SIM => simulated RRF machine for testing.
# => false: use UART to send and read gcode to/from RRF machine.
# => true: use basic GCODE simulator to simulate an RRF machine on fake serial.
RUN_SIM = False

import json
from micropython import const
from machine_interface import MachineInterface, PollState, MachineStatus

platform = sys.platform
if platform == 'win32' or platform == 'darwin' or platform == 'linux' or RUN_SIM:
    import io
    import uasyncio_shim as uasyncio

    print('Basic UART Simulation...')

    class Pin:
        def __init__(self, pin):
            self.pin = pin

    # Minimum-viable UART + RRF Machine Sim:
    # Pretends to be RRF connected via UART.
    # Interprets some gcodes in the most basic way, closely tied to
    # MachineInterface/MachineRRF, to allow testing MachineRRF on platforms
    # that don't support the UART class.
    class UART:
        def __init__(self, pos, baud, tx=None, rx=None, rxbuf=0):
            self.last = None
            self.last_args = None
            self.pos = [0.0, 0.0, 0.0]
            self.axes_homed = [False, False, False]
            self.wcs = 0
            self.wcs_offsets = [
                        [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0],
                        [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0],
                        [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0],
                    ]

        def write(self, gcodes):
            axis_i = { 'X': 0, 'Y': 1, 'Z': 2 }

            for gcode in gcodes.split('\n'):
                #print(gcode)
                if len(gcode) > 0:
                    l = gcode.split()
                    self.last = l[0].upper()
                    self.last_args = l[1:]
                    if self.last == 'G28':
                        print('G28')
                        self.axes_homed = [True, True, True]
                        self.pos = [0.0, 0.0, 0.0]

                    if self.last == 'G53':
                        self.wcs = 0
                    elif self.last == 'G54':
                        self.wcs = 1
                    elif self.last == 'G55':
                        self.wcs = 2
                    elif self.last == 'G56':
                        self.wcs = 3

                    elif self.last == 'G10':
                        wcs = None
                        ax = 0
                        v = 0
                        for cc in self.last_args:
                            cmd = cc[0].upper()
                            if cmd == 'P':
                                wcs = int(cc[1:])
                            elif cmd == 'L':
                                if cc.upper() != 'L20':
                                    raise Exception('Only L20 supported')
                            else:
                                ax = axis_i[cmd]
                                v = float(cc[1:])
                            # print('G10>', cmd)
                        #print('G10', ax, v, wcs)
                        self.wcs_offsets[ax][wcs - 1] = v - self.pos[ax]

                    elif self.last == 'G1' or self.last == 'G0':
                        for ax in self.last_args:
                            axis = ax[0].upper()
                            if axis in axis_i:
                                v = float(ax[1:])
                                self.pos[axis_i[axis]] += v
                        # print('pos:', self.pos, 'homed:', self.axesHomed, 'wcs:', self.wcs,
                        #       'wcs_offs:', self.wcs_offsets)

        def __aiter__(self):
            return self

        async def __anext__(self):
            val = self.read()
            if val == None:
                raise StopAsyncIteration
            return val

        def readline(self):
            return self.read()

        def any(self):
            return self.last is not None

        def read(self):
            if self.last == 'M409':
                k = self.last_args[0][1:].replace('"', '').replace("'", '')
                if k == 'move.axes' or k == 'move.axes[]':
                    x = self.pos[0]
                    xwcs = self.wcs_offsets[0]
                    xh = 'true' if self.axes_homed[0] == 1 else 'false'
                    xu = x + xwcs[self.wcs]
                    y = self.pos[1]
                    yh = 'true' if self.axes_homed[1] == 1 else 'false'
                    ywcs = self.wcs_offsets[1]
                    yu = y + ywcs[self.wcs]
                    z = self.pos[2]
                    zh = 'true' if self.axes_homed[2] == 1 else 'false'
                    zwcs = self.wcs_offsets[2]
                    zu = z + zwcs[self.wcs]

                    self.last = None

                    return '''{"key":"move.axes","flags":"","result":[{"acceleration":900.0,"babystep":0,"backlash":0,"current":1450,"drivers":["0.2"],"homed":%s,"jerk":300.0,"letter":"X","machinePosition":%.3f,"max":200.00,"maxProbed":false,"microstepping":{"interpolated":true,"value":16},"min":0,"minProbed":false,"percentCurrent":100,"percentStstCurrent":100,"reducedAcceleration":900.0,"speed":5000.0,"stepsPerMm":800.00,"userPosition":%.3f,"visible":true,"workplaceOffsets":%s},{"acceleration":900.0,"babystep":0,"backlash":0,"current":1450,"drivers":["0.1"],"homed":%s,"jerk":300.0,"letter":"Y","machinePosition":%.3f,"max":160.00,"maxProbed":false,"microstepping":{"interpolated":true,"value":16},"min":0,"minProbed":false,"percentCurrent":100,"percentStstCurrent":100,"reducedAcceleration":900.0,"speed":5000.0,"stepsPerMm":800.00,"userPosition":%.3f,"visible":true,"workplaceOffsets":%s},{"acceleration":100.0,"babystep":0,"backlash":0,"current":1450,"drivers":["0.3"],"homed":%s,"jerk":30.0,"letter":"Z","machinePosition":%.3f,"max":70.00,"maxProbed":false,"microstepping":{"interpolated":true,"value":16},"min":-1.00,"minProbed":false,"percentCurrent":100,"percentStstCurrent":100,"reducedAcceleration":100.0,"speed":1000.0,"stepsPerMm":400.00,"userPosition":%.3f,"visible":true,"workplaceOffsets":%s}],"next":0}\n'''  % (xh, x, xu, repr(xwcs), yh, y, yu, repr(ywcs), zh, z, zu, repr(zwcs))
                else:
                    raise Exception('Unknown arg to M409')
            elif self.last:
                self.last = None
                return 'ok\n'
            else:
                return None
else:
    from machine import UART, Pin
    import uasyncio

class MachineRRF(MachineInterface):
    AXIS_NAMES = { 'X': 0, 'Y': 1, 'Z': 2, 'U': 3, 'V': 4, 'A': 5, 'B': 6 }
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

    def __init__(self, sleep_ms = MachineInterface.DEFAULT_SLEEP_MS):
        super().__init__(sleep_ms)

        self.uart = UART(2, 115200, tx=Pin(43), rx=Pin(44), rxbuf=1024*16)
        self.uart_reader = uasyncio.StreamReader(self.uart)

    def get_machine_status_ext(self):
        self.uart.write('M409 K"move.axes[]"\n')
        res = self.uart.read()

    def _send_gcode(self, gcode):
        gcodes = gcode.split('\n')
        for gcode_ in gcodes:
            print('_send:', gcode)
            self.uart.write(gcode_ + '\n')

    def _has_response(self):
        return self.uart.any()

    def _read_response(self):
        return self.uart.read()

    async def _proc_machine_state(self, cmd):
        self._send_gcode(cmd)
        try:
            res = await uasyncio.wait_for(self.uart_reader.readline(), 0.5)
            self.parse_m409(res)
        except Exception as e:
            print('Timeout', e)

    async def _update_machine_state(self, poll_state):
        if PollState.has_state(poll_state, PollState.MACHINE_POSITION):
            await self._proc_machine_state('M409 K"move.axes[]" F"d5"')
            print(self.is_homed())

    def parse_move_axes(self, res):
        for axis in res:
            # Available but unused fields:
            # acceleration = axis['acceleration']
            # babystep = axis['babystep']
            # backlash = axis['backlash']
            # current = axis['current']
            # drivers = axis['drivers']
            # jerk = axis['jerk']
            # maxProbed = axis['maxProbed']
            # minProbed = axis['minProbed']
            # percentCurrent = axis['percentCurrent']
            # percentStstCurrent = axis['percentStstCurrent']
            # speed = axis['speed']
            # stepsPerMm = axis['stepsPerMm']
            # visible = axis['visible']
            # reducedAcceleration = axis['reducedAcceleration']
            name = axis['letter']
            i = MachineRRF.AXIS_NAMES[name]
            homed = axis['homed']
            machine_pos = axis['machinePosition']
            wcs_pos = axis['userPosition']
            # wcs_offs = axis['workplaceOffsets']
            # print("AXIS", name, i, homed, machine_pos, wcs_pos)
            # print('H:', name, homed, axis['homed'])
            # print('A:', name, axis)

            self.axes_homed[i] = homed
            self.position[i] = machine_pos
            self.wcs_position[i] = wcs_pos

    def parse_m409(self, json_resp):
        # TODO: seq-based major updates.
        j = None
        try:
             j = json.loads(json_resp.strip())
        except ValueError as e:
            print("Failed to parse json", e, json_resp)
            return

        try:
            if j['key'] == 'move.axes' or j['key'] == 'move.axes[]':
                self.parse_move_axes(j['result'])
        except KeyError as e:
            print('Failed to read json: ', json_resp, e)

    def parse_m408(self, json_resp):
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

    def _parse_filelist(self, res):
        try:
             j = json.loads(res.strip())
             dirname = j['dir']
             filenames = j['files']
             return [dirname, filenames]
        except ValueError as e:
            print("Failed to parse json", e, res)

            return [None, None]

    def list_gcode_files(self):
        self._send_gcode('M20 S2 P"/gcodes"')
        res = uasyncio.wait_for(self.uart_reader.readline(), 0.5).send(None)
        return _parse_filelist(res)

    def list_macros(self):
        self._send_gcode('M20 S2 P"/macros"')
        res = uasyncio.wait_for(self.uart_reader.readline(), 0.5).send(None)
        return _parse_filelist(res)

if __name__ == '__main__':
    m = MachineRRF(lambda x: print(x))
    m.move('X', 100.0, 22.0)
    m.set_wcs_zero(1, ['X'])
    print(m.debug_print())
    m.process_gcode_q()
    print(m.debug_print())
    m.home_all()
    print(m.debug_print())
    m.process_gcode_q()
    print(m.debug_print())
