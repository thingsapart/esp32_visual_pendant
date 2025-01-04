
class Plane:
    XY = 0
    XZ = 1
    YZ = 2

class GCodeSimulator:
    G90 = 0
    G91 = 1
    G90_1 = 2
    G91_1 = 4
    G94 = 8
    G21 = 16
    G54 = 32
    G55 = 64
    G56 = 128
    G57 = 256
    G58 = 512
    G59 = 1024
    G59_1 = 2048
    G59_2 = 4096
    G59_3 = 8192
    M3 = 16384
    M5 = 32768
    M8 = 65536
    M9 = 131072
    G0 = 262144
    G1 = 524288
    G53 = 1048576

    RAPID = 0
    LINEAR = 1
    ARC_CW = 2
    ARC_CCW = 4
    TOOL_CHANGE = 8

    # Motion mode
    GROUP_G1 = ['G00', 'G01', 'G02', 'G03', 'G33', 'G38.1', 'G38.2', 'G38.3',
                  'G38.4', 'G38.5', 'G73', 'G76', 'G80', 'G81', 'G82', 'G84',
                'G85', 'G86', 'G87', 'G88', 'G89']
    # Plane
    GROUP_G2 = ['G17', 'G18', 'G19', 'G17.1', 'G17.2', 'G17.3' ]
    # Distance
    GROUP_G3 = ['G90', 'G91']
    # Arc distance
    GROUP_G4 = [ 'G90.1', 'G91.1' ]
    # Feed rate mode
    GROUP_G5 = ['G93', 'G94']
    # Units
    GROUP_G6 = ['G20', 'G21']
    # Tool wear/radius compensation
    GROUP_G7 = ['G40', 'G41', 'G42', 'G41.1', 'G42.1']
    # Tool length offset
    GROUP_G8 = ['G43', 'G43.1', 'G49']
    # Canned cycle return mode
    GROUP_G10 = ['G98', 'G99']
    # WCS
    GROUP_G12 = ['G54', 'G55', 'G56', 'G57', 'G58', 'G59', 'G59.1', 'G59.2', 'G59.3']
    # Path control
    GROUP_G13 = ['G61', 'G61.1', 'G64']
    # Spindle speed
    GROUP_G14 = ['G96', 'G97']
    # Lathe diameter
    GROUP_G15 = ['G07', 'G08']

    # Stop
    GROUP_M4 = ['M00', 'M01', 'M02', 'M30', 'M60']
    # Spindle
    GROUP_M7 = ['M03', 'M04', 'M05']
    # Coolant
    GROUP_M8 = ['M07', 'M08', 'M09']
    # Feed, speed override
    GROUP_M9 = ['M48', 'M49']

    GROUPS = {
        # 1
        'G00': 1, 'G01': 1, 'G02': 1, 'G03': 1, 'G33': 1, 'G38.1': 1, 'G38.2':1, 'G38.3': 1,
        'G38.4': 1, 'G38.5': 1, 'G73': 1, 'G76': 1, 'G80': 1, 'G81': 1, 'G82': 1, 'G84': 1,
        'G85': 1, 'G86': 1, 'G87': 1, 'G88': 1, 'G89': 1,
        # 2
        'G17': 2, 'G18': 2, 'G19': 2, 'G17.1': 2, 'G17.2': 2, 'G17.3': 2,
        # 3
        'G90': 3, 'G91': 3,
        # 4
        'G90.1': 4, 'G91.1': 4,
        # 5
        'G93': 5, 'G94': 5,
        # 6
        'G20': 6, 'G21': 6,
        # 7
        'G40': 7, 'G41': 7, 'G42': 7, 'G41.1': 7, 'G42.1': 7,
        # 8
        'G43': 8, 'G43.1': 8, 'G49': 8,
        # 9
        'G98': 9, 'G99': 9,
        # 10
        'G54': 10, 'G55': 10, 'G56': 10, 'G57': 10, 'G58': 10, 'G59': 10,
        'G59.1': 10, 'G59.2': 10, 'G59.3': 10,
        # 11
        'G61': 11, 'G61.1': 11, 'G64': 11,
        # 12
        'G96': 12, 'G97': 12,
        # 13
        'G07': 13, 'G08': 13,
        # 14
        'M00': 14, 'M01': 14, 'M02': 14, 'M30': 14, 'M60': 14,
        # 15
        'M03': 15, 'M04': 15, 'M05': 15,
        # 16
        'M07': 16, 'M08': 16, 'M09': 16,
        # 17
        'M48': 17, 'M49': 17,
        }

    def __init__(self):
        self.segments = []
        self.reset_state()

    def reset_state(self):
        self.motion_mode = self.G0
        self.plane = Plane.XY
        self.abs = True
        self.distance_mode = self.G90
        self.ijk_abs = False
        self.arc_distance_mode = self.G91_1
        self.feed_mode = self.G94
        self.feed_rate = 0.0
        self.metric = True
        self.units = self.G21
        self.wcs_offset = 0
        self.wcs = self.G53
        self.spindle_on = False
        self.spindle_rpm = 0
        self.spindle = self.M5
        self.coolant_on = False
        self.coolant = self.M9

        self.group = None

        self.x = 0.0
        self.y = 0.0
        self.z = 0.0
        self.a = 0.0

    def process(self, text):
        self.reset_state()
        self.text = text
        self.tlen = len(text)
        self.pos = 0
        self.line_no = 0

        while self.pos < self.tlen:
            line = self.next_line()
            print('::', self.line_no, line)

            llen = len(line)
            words = []
            pos = 0

            word, v, pos = self.next_word(line, llen, pos)
            print(word, v)
            while pos < llen:
                # print('1:', pos, line, llen)
                word, v, pos = self.next_word(line, llen, pos)
                print(word, v)

            self.line_no += 1

    def next_line(self):
        p = self.pos
        while p < self.tlen:
            if self.text[p] == '\n' or self.text[p] == '\r':
                break
            p += 1
        res = self.text[self.pos:p]
        while p < self.tlen and (self.text[p] == '\n' or self.text[p] == '\r'): p += 1
        self.pos = p

        return res

    def skip_line_num(self, line, lenl, pos):
        pos = self.skip_white(line, lenl, pos)
        if pos >= lenl: return pos

        if line[pos] == 'N':
            v, pos = self.next_real(line, lenl, pos)
        return pos

    def skip_white(self, line, lenl, pos):
        while pos < lenl:
            c = line[pos]
            if c == ' ' or c == '\t':
                pos += 1
            elif c == '(':
                while pos < lenl and line[pos] != ')':
                    pos += 1
                pos += 1
                pos = self.skip_white(line, lenl, pos)
            elif c == ';' or c == '/':
                return len(line)
            else:
                break

        return pos

    def error(self, s, line, pos):
        print(s)
        print('>', line[:pos-1], '|', line[pos:])

    def next_real(self, line, lenl, pos):
        pos = self.skip_white(line, lenl, pos)
        ppos = pos
        if pos >= lenl:
            c = None
        else:
            c = line[pos]
        while c == '+' or c == '-' or c == '1' or c == '2' or c == '3' or \
            c == '4' or c == '5' or c == '6' or c == '7' or c == '8' or c == '9' or \
            c == '0' or c == '.':
            pos += 1

            if pos >= lenl: break

            c = line[pos]

        if pos == ppos:
            self.error('Expected number on line %d' % self.line_no, line, pos)
            print(line[:pos+1], '', line[pos:])
            return [0, len(line)]
        s = line[ppos:pos]
        if '.' in s:
            return [float(s), pos]
        else:
            return [int(s), pos]

    def next_word(self, line, lenl, pos):
        # A: A-axis of mill
        # B: B-axis of mill
        # C: C-axis of mill
        # D: Tool radius compensation number
        # F: Feed rate
        # G: General function
        # H: Tool length offset index
        # I: X-axis offset for arcs, or X offset in a G87 canned cycle
        # J: Y-axis offset for arcs, or Y offset in a G87 canned cycle
        # K: Z-axis offset for arcs, or Z offset in a G87 canned cycle
        # L: Number of repetitions in canned cycles and subroutines, or key used with G10
        # M: Miscellaneous function
        # N: Line number
        # O: Subroutine label number
        # P: Dwell time in canned cycles, dwell time with G04, key used with G10, or tapping depth in M871 through M874
        # Q: Feed increment in a G83 canned cycle, or repetitions of subroutine call
        # R: Arc radius, or canned cycle retract level
        # S: Spindle speed
        # T: Tool selection
        # U: Synonymous with A
        # V: Synonymous with B
        # W: Synonymous with C
        # X: X-axis of mill
        # Y: Y-axis of mill
        # Z: Z-axis of mill
        while pos < lenl:
            pos = self.skip_white(line, lenl, pos)
            if pos >= lenl: break

            c = line[pos]

            if c == 'A' or c == 'B' or c == 'C' or c == 'D' or c == 'F' or c == 'G' or \
                c == 'H' or c == 'I' or c == 'J' or c == 'K' or c == 'L' or \
                c == 'M' or c == 'N' or c == 'O' or c == 'P' or c == 'Q' or c == 'R' or \
                c == 'S' or c == 'T' or c == 'U' or c == 'V' or c == 'W' or \
                c == 'X' or c == 'Y' or c == 'Z':
                v, pos = self.next_real(line, lenl, pos + 1)
                return [c, v, pos]
            else:
                self.error('Unknown word %s on line %d' % (repr(c), self.line_no),
                           line,
                           pos)
                return [None, None, len(line)]
        return [None, None, len(line)]


if __name__ == '__main__':
    gc = GCodeSimulator()
    gcode = """O1000 (Example: Part edge 3" bore full depth)
M6T1
G0G90G54X5.Y0S3000M3
G43H1Z2./M8
Z.1
G1Z0F50.
G91G41D1X1.5F18. (Having G91 started here will allow you to start at any location in X and Y)
G3I-1.5Z-.104L5
I-1.5
G1G40X-1.5
G0G90Z2.
X-5.Y0.(ADDITIONAL LOCATIONS)
Z.1
G1Z0F50.
G91G41D1X1.5F18.
G3I-1.5Z-.104L5
I-1.5
G1G40X-1.5
G0G90Z2.
G91G28Y0Z0
G90
M30"""
    gc.process(gcode)
