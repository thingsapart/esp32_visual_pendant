
class Plane:
    XY = 0
    XZ = 1
    YZ = 2
    UV = 4
    WU = 8
    VW = 16

class GCodeSimulator:
    # G90 = 0
    # G91 = 1
    # G90_1 = 2
    # G91_1 = 4
    # G94 = 8
    # G21 = 16
    # G54 = 32
    # G55 = 64
    # G56 = 128
    # G57 = 256
    # G58 = 512
    # G59 = 1024
    # G59_1 = 2048
    # G59_2 = 4096
    # G59_3 = 8192
    # M3 = 16384
    # M5 = 32768
    # M8 = 65536
    # M9 = 131072
    # G0 = 262144
    # G1 = 524288
    # G53 = 1048576

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

    def __init__(self, arc_fragments_per_rev=20):
        self.arc_fpr = arc_fragments_per_rev
        self.reset_state()

    def reset_state(self):
        self.motion_segments = []

        self.motion_mode = 'G0'
        self.plane = Plane.XY
        self.abs = True
        self.distance_mode = 'G90'
        self.ijk_abs = False
        self.arc_distance_mode = 'G90.1'
        self.feed_mode = 'G94'
        # In mm/min.
        self.feed_rate = 0.0
        self.metric = True
        self.units = 'G21'
        self.wcs_offset = 0
        self.wcs = 'G53'
        # Initialize WCS origins with 10 coordinate systems (G53-G59.3)
        self.wcs_origins = [[0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0] for _ in range(10)]
        # G92 offsets applied to all WCS
        self.g92_offset = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
        self.spindle_on = False
        self.spindle_rpm = 0
        self.spindle = 'M5'
        self.coolant_on = False
        self.coolant = 'M9'
        self.tool_radius_comp = 0
        self.tool_index = 0
        self.gc_line_no = 0
        self.sub_routine = 0
        self.lathe_radius_mode = True
        self.cutter_comp_on = False
        self.cutter_comp = 0.0
        self.tool_length_comp_on = False
        self.tool_length_comp = 0.0

        # Canned cycle repetitions of G10 key.
        self.l_param = 1
        # Dwell time in canned cycles, dwell time with G04, key used with G10,
        # or tapping depth in M871 through M874.
        self.p_param = 1 #
        # Feed increment in a G83 canned cycle, or repetitions of subroutine call
        self.q_param = 0
        # Arc radius, or canned cycle retract level.
        self.r_param = 0
        self.s_param = 0

        # Internal representation is always in mm.
        self.x = 0.0
        self.y = 0.0
        self.z = 0.0
        # self.a = 0.0, b, c ... not supported

        self.i = 0.0
        self.j = 0.0
        self.k = 0.0

    def get_wcs_offset(self, axis):
        """Get WCS offset for given axis index"""
        if self.wcs == 'G53':
            return 0.0
        wcs_idx = self.wcs_offset
        return self.wcs_origins[wcs_idx][axis] + self.g92_offset[axis]

    def get_axis_pos(self, axis_idx):
        """Get machine position with WCS offset applied"""
        pos = [self.x, self.y, self.z, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0][axis_idx]
        return pos - self.get_wcs_offset(axis_idx)

    def set_axis_pos(self, axis_idx, value):
        """Set machine position with WCS offset applied"""
        pos = value + self.get_wcs_offset(axis_idx)
        if axis_idx == 0:
            self.x = pos
        elif axis_idx == 1:
            self.y = pos
        elif axis_idx == 2:
            self.z = pos

    def process(self, text):
        self.reset_state()
        self.text = text
        self.tlen = len(text)
        self.pos = 0
        self.line_no = 0
        self.last = None
        self.last_word = None
        self.last_v = None


        coords = {}
        code = None

        while self.pos < self.tlen:
            coords = {}
            line = self.next_line()
            self.line = line
            print('::', self.line_no, line)

            llen = len(line)
            words = []
            pos = 0

            while pos < llen:
                word, v, pos = self.next_word(line, llen, pos)
                groups = []

                if word:
                    if word != 'M' and word != 'G':
                        coords[word] = v
                    else:
                        if self.last_word is not None and (word != self.last_word or v != self.last_v):
                            self.apply_coords(code, coords)

                        self.last_word = word
                        self.last_v = v

                        code = word + str(int(v))

                        if code in self.GROUPS:
                            ngroup = self.GROUPS[code]
                            if ngroup in groups:
                                self.error('%s in group (%d) that was already used in this \
                                            block, only one word per group per block. \
                                            Groups seen: %s' % (code, ngroup, groups),
                                            line, pos, self.line_no)

                        # Machine state effects of g-code.
                        if word == 'G':
                            if v == 0:
                                self.motion_mode = 'G0'
                            elif v == 1:
                                self.motion_mode = 'G1'
                            elif v == 2:
                                self.motion_mode = 'G2'
                            elif v == 3:
                                self.motion_mode = 'G3'
                            elif v == 4:
                                pass # Dwell.
                            elif v == 5:
                                self.error('Cubc splines not supported', line, pos,
                                           self.line_no)
                            elif v == 5.1:
                                self.error('Quadratic b-splines not supported', line, pos,
                                           self.line_no)
                            elif v == 5.2:
                                self.error('NURBS splines not supported', line, pos,
                                           self.line_no)
                            elif v == 7:
                                self.lathe_radius_mode = False
                            elif v == 8:
                                self.lathe_radius_mode = True
                            elif v == 10:
                                if self.l_param == 20:
                                    # Update current WCS origin
                                    wcs_idx = int(self.wcs[1:]) - 54
                                    if 'X' in coords:
                                        self.wcs_origins[wcs_idx][0] = coords['X']
                                    if 'Y' in coords:
                                        self.wcs_origins[wcs_idx][1] = coords['Y']
                                    if 'Z' in coords:
                                        self.wcs_origins[wcs_idx][2] = coords['Z']
                            elif v == 17:
                                self.plane = Plane.XY
                            elif v == 18:
                                self.plane = Plane.XZ
                            elif v == 19:
                                self.plane = Plane.YZ
                            elif v == 17.1:
                                self.plane = Plane.UV
                            elif v == 17.2:
                                self.plane = Plane.WU
                            elif v == 17.3:
                                self.plane = Plane.VW
                            elif v == 20:
                                self.metric = False
                                self.units = 'G20'
                            elif v == 21:
                                self.metric = True
                                self.units = 'G21'
                            elif v == 28:
                                coords['X'] = 0.0
                                coords['Y'] = 0.0
                                coords['Z'] = 0.0
                                coords['A'] = 0.0
                                coords['B'] = 0.0
                                coords['C'] = 0.0
                            elif v == 30:
                                pass # G30 ignored for now.
                            elif v == 30.1:
                                pass # G30.1 ignored for now.
                            elif v == 33:
                                self.error('G33 synchr. spindle motion not supported', line, pos, self.line_no)
                            elif v == 33.1:
                                self.error('TODO: rigid tapping support', line, pos, self.line_no)
                            elif v == 38.2 or v == 38.3 or v == 38.4 or v == 38.5:
                                pass # probing.
                            elif v == 40:
                                self.error('TODO: cutter comp support', line, pos, self.line_no)
                                self.cutter_comp_on = True
                            elif v == 41:
                                self.error('TODO: cutter comp support', line, pos, self.line_no)
                                self.cutter_comp_on = 'l'
                            elif v == 42:
                                self.error('TODO: cutter comp support', line, pos, self.line_no)
                                self.cutter_comp_on = 'r'
                            elif v == 43:
                                self.tool_length_comp_on = True
                                pass
                            elif v == 43.1:
                                self.tool_length_comp_on = True
                                pass
                            elif v == 43.2:
                                self.tool_length_comp_on = True
                                pass
                            elif v == 49:
                                self.tool_length_comp_on = False
                            elif v == 53:
                                self.wcs_offset = 0
                                self.wcs = 'G53'
                            elif v == 54:
                                self.wcs_offset = 1
                                self.wcs = 'G54'
                            elif v == 55:
                                self.wcs_offset = 2
                                self.wcs = 'G55'
                            elif v == 56:
                                self.wcs_offset = 3
                                self.wcs = 'G56'
                            elif v == 57:
                                self.wcs_offset = 4
                                self.wcs = 'G57'
                            elif v == 58:
                                self.wcs_offset = 5
                                self.wcs = 'G58'
                            elif v == 59:
                                self.wcs_offset = 6
                                self.wcs = 'G59'
                            elif v == 59.1:
                                self.wcs_offset = 7
                                self.wcs = 'G59.1'
                            elif v == 59.2:
                                self.wcs_offset = 8
                                self.wcs = 'G59.2'
                            elif v == 59.3:
                                self.wcs_offset = 9
                                self.wcs = 'G59.3'
                            elif v == 90:
                                self.abs = True
                                self.distance_mode = 'G90'
                            elif v == 91:
                                self.abs = False
                                self.distance_mode = 'G91'
                            elif v == 90.1:
                                self.ijk_abs = True
                                self.arc_distance_mode = 'G90.1'
                            elif v == 91.1:
                                self.ijk_abs = False
                                self.arc_distance_mode = 'G91.1'
                            elif v == 92:
                                # Add G92 offset to all WCS
                                if 'X' in coords:
                                    self.g92_offset[0] = coords['X'] - self.x
                                if 'Y' in coords:
                                    self.g92_offset[1] = coords['Y'] - self.y
                                if 'Z' in coords:
                                    self.g92_offset[2] = coords['Z'] - self.z
                            else:
                                self.error('Unknown G-Code %s' % gcode, line, pos,
                                      self.line_no)

                    print(word, v)

            self.apply_coords(code, coords)
            self.line_no += 1
        print("MOTION SEGMENTS:")
        print(self.motion_segments)

    # Convert unit from current input space to mm.
    def convert_unit_mm(self, v):
        if self.metric: return v
        return v * 25.4

    # TODO: plane support.
    # Apply distance using current distance mode.
    def apply_distance(self, v, curr):
        if self.abs:
            return self.convert_unit_mm(v)
        else:
            return curr + self.convert_unit_mm(v)

    # Apply arc distance using current distance mode.
    def apply_arc_distance(self, v, curr):
        if self.ijk_abs:
            return self.convert_unit_mm(v)
        else:
            return curr + self.convert_unit_mm(v)

    def output_arc_motion_segment(self, gcode, coords):
        import gc
        gc.collect()

        if gcode in ['G2', 'G3']:
            # Get current position
            x0, y0, z0 = self.get_x(), self.get_y(), self.get_z()
            x1 = self.apply_distance(coords['X'], x0) if 'X' in coords else 0
            y1 = self.apply_distance(coords['Y'], y0) if 'Y' in coords else 0
            z1 = self.apply_distance(coords['Z'], z0) if 'Z' in coords else 0

            # Get arc center
            if self.plane == Plane.XY:
                cx = self.apply_arc_distance(x0, coords['I'])
                cy = self.apply_arc_distance(y0, coords['J'])

                # Calculate start and end angles
                start_angle = math.atan2(y0 - cy, x0 - cx)
                end_angle = math.atan2(y1 - cy, x1 - cx)
                radius = math.sqrt((x0 - cx)**2 + (y0 - cy)**2)

                # Adjust angles for clockwise/counterclockwise
                if gcode == 'G2':  # CW
                    if end_angle >= start_angle:
                        end_angle -= 2*math.pi
                else:  # CCW
                    if end_angle <= start_angle:
                        end_angle += 2*math.pi

                # Calculate number of segments based on arc length
                # arc_length = abs(radius * (end_angle - start_angle))
                arc_deg = abs(end_angle - start_angle)
                num_segments = int(arc_deg / (2*math.pi) * self.arc_fpr)
                #num_segments = max(2, int(arc_length / self.eps_arc_len))
                print('num seg: seg, deg, fpr, R, end, start angle', num_segments, arc_deg, self.arc_fpr, radius, end_angle,
                      start_angle)

                # Generate points along arc
                for i in range(1, num_segments + 1):
                    t = i / num_segments
                    angle = start_angle + t * (end_angle - start_angle)
                    x = cx + radius * math.cos(angle)
                    y = cy + radius * math.sin(angle)
                    z = z0 + t * (self.z - z0)
                    self.motion_segments.append([x, y, z, self.feed_rate, False])
            else:
                self.error('Only XY plane arcs currently supported', self.line, 0, self.line_no)
    def output_motion_segment(self, gcode):
        import gc
        gc.collect()

        if gcode in ['G0', 'G1']:
            self.motion_segments.append(
                [self.get_x(), self.get_y(), self.get_z(), self.feed_rate, gcode == 'G0']
            )
        elif gcode in ['G2', 'G3']:
            # Get current position
            x0, y0, z0 = self.get_x(), self.get_y(), self.get_z()

            # Get arc center
            if self.plane == Plane.XY:
                cx = x0 + self.i
                cy = y0 + self.j
                # Calculate start and end angles
                start_angle = math.atan2(y0 - cy, x0 - cx)
                end_angle = math.atan2(self.y - cy, self.x - cx)
                radius = math.sqrt((x0 - cx)**2 + (y0 - cy)**2)

                # Adjust angles for clockwise/counterclockwise
                if gcode == 'G2':  # CW
                    if end_angle >= start_angle:
                        end_angle -= 2*math.pi
                else:  # CCW
                    if end_angle <= start_angle:
                        end_angle += 2*math.pi

                # Calculate number of segments based on arc length
                # arc_length = abs(radius * (end_angle - start_angle))
                arc_deg = abs(end_angle - start_angle)
                num_segments = int(arc_deg / 2*math.pi * self.arc_fpr)
                #num_segments = max(2, int(arc_length / self.eps_arc_len))
                print('num seg:', num_segments, arc_deg, radius, end_angle,
                      start_angle)

                # Generate points along arc
                for i in range(1, num_segments + 1):
                    t = i / num_segments
                    angle = start_angle + t * (end_angle - start_angle)
                    x = cx + radius * math.cos(angle)
                    y = cy + radius * math.sin(angle)
                    z = z0 + t * (self.z - z0)
                    self.motion_segments.append([x, y, z, self.feed_rate, False])
            else:
                self.error('Only XY plane arcs currently supported', self.line, 0, self.line_no)

    def get_x(self):
        return self.get_axis_pos(0)

    def get_y(self):
        return self.get_axis_pos(1)

    def get_z(self):
        return self.get_axis_pos(2)

    def apply_coords(self, gcode, coords):
        print(coords)
        if gcode in ['G2', 'G3']:
            self.output_arc_motion_segment(gcode, coords)
        for word, v in coords.items():
            self.maybe_process_coord_feed(gcode, word, v)
        if gcode in ['G0', 'G1', 'G28']:
            self.output_motion_segment(gcode)
        self.last_word = None
        self.last_v = None
        # TODO:
        # G10!
        # G43
        # G43.1
        # G43.2


    def maybe_process_coord_feed(self, gcode, word, v):
        to_mm = self.convert_unit_mm
        # A, B, C-Axis of Mill.
        if word == 'A' or word == 'B' or word == 'C' or word == 'U' or \
            word == 'V' or word == 'W':
            self.error('Axis %s not supported' % word, self.line, 0, self.line_no)
        # Tool radius compensation.
        elif word == 'D':
            self.tool_radius_comp = to_mm(v)
        # Feed rate.
        elif word == 'F':
            self.feed_rate = to_mm(v)
        # G-code? No.
        elif word == 'G':
            raise Exception('G-Code not expected here!')
        # Tool length offset index.
        elif word == 'H':
            self.error('Tool length offset function H not supported', self.line, 0,
                  self.line_no)
        # I: X-axis offset for arcs, or X offset in a G87 canned cycle.
        elif word == 'I':
            self.i = self.apply_arc_distance(v, self.i)
        # J: Y-axis offset for arcs, or Y offset in a G87 canned cycle.
        elif word == 'J':
            self.j = self.apply_arc_distance(v, self.j)
        # K: Z-axis offset for arcs, or Z offset in a G87 canned cycle.
        elif word == 'K':
            self.k = self.apply_arc_distance(v, self.k)
        # L: Number of repetitions in canned cycles and subroutines, or key used with G10.
        elif word == 'L':
            self.l_param = v
        # M-code? No.
        elif word == 'G':
            raise Exception('M-Code not expected here!')
        # G-Code Line number.
        elif word == 'N':
            self.gc_line_no = int(v)
        # Subroutine label number.
        elif word == 'O':
            self.subroutine = int(v)
        # Dwell time in canned cycles, dwell time with G04, key used with G10,
        # or tapping depth in M871 through M874.
        elif word == 'P':
            self.p_param = v
        # Feed increment in a G83 canned cycle, or repetitions of subroutine
        # call.
        elif word == 'Q':
            self.q_param = v
        # Arc radius, or canned cycle retract level.
        elif word == 'R':
            self.r_param = v
        # Spindle speed, dwell time, other param keys.
        elif word == 'S':
            if gcode == 'M3':
                self.spindle_rpm = v
            elif gcode == 'M4':
                self.spindle_rpm = -v
            else:
                self.s_param = v
        # Tool select.
        elif word == 'T':
            self.tool_index = int(v)
        # X, Y, Z-axis of Mill.
        elif word == 'X':
            self.set_axis_pos(0, self.apply_distance(v, self.x))
        elif word == 'Y':
            self.set_axis_pos(1, self.apply_distance(v, self.y))
        elif word == 'Z':
            self.set_axis_pos(2, self.apply_distance(v, self.z))

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

    def error(self, s, line, pos, line_no = -1):
        print('ERROR:', s)
        if line_no >= 0:
            print('%d: %s|%s' % (line_no, line[:pos-1], line[pos:]))
        else:
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
            self.error('Expected number on line %d' % self.line_no, line, pos,
                       self.line_no)
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
                           pos,
                           self.line_no)
                return [None, None, len(line)]
        return [None, None, len(line)]

# Add imports at top of file
import lvgl as lv
import math

class GCodeMotionVisualizer:
    def __init__(self, parent, width, height):
        # super().__init__(parent)

        # Initialize canvas and style
        # 4 bytes per pixel (ARGB8888)
        self.buf = bytearray(width * height * 4)

        canvas = lv.canvas(parent)
        canvas.set_size(width, height)
        canvas.set_buffer(self.buf, width, height, lv.COLOR_FORMAT.ARGB8888)

        self.canvas = canvas
        self.width = width
        self.height = height

        # Create styles for different motion types
        self.style_g0 = lv.style_t()
        self.style_g0.init()
        self.style_g0.set_line_color(lv.color_hex(0xFF808080))  # Gray
        self.style_g0.set_line_dash_gap(8)
        self.style_g0.set_line_dash_width(4)

        self.style_g1 = lv.style_t()
        self.style_g1.init()
        self.style_g1.set_line_color(lv.color_hex(0xFF0000FF))  # Blue

        self.style_g2g3 = lv.style_t()
        self.style_g2g3.init()
        self.style_g2g3.set_line_color(lv.color_hex(0xFF00FF00))  # Green

        # Create zoom buttons
        self.zoom_in_btn = lv.button(parent)
        self.zoom_in_btn.set_size(40, 40)
        self.zoom_in_btn.align(lv.ALIGN.BOTTOM_RIGHT, -10, -10)
        self.zoom_in_btn.add_event_cb(self.on_zoom_in, lv.EVENT.CLICKED, None)
        lv.label(self.zoom_in_btn).set_text("+")

        self.zoom_out_btn = lv.button(parent)
        self.zoom_out_btn.set_size(40, 40)
        self.zoom_out_btn.align_to(self.zoom_in_btn, lv.ALIGN.OUT_TOP_MID, 0, -5)
        self.zoom_out_btn.add_event_cb(self.on_zoom_out, lv.EVENT.CLICKED, None)
        lv.label(self.zoom_out_btn).set_text("-")

        self.grid_btn = lv.button(parent)
        self.grid_btn.set_size(40, 40)
        self.grid_btn.align_to(self.zoom_out_btn, lv.ALIGN.OUT_TOP_MID, 0, -5)
        self.grid_btn.add_event_cb(self.on_toggle_grid, lv.EVENT.CLICKED, None)
        lv.label(self.grid_btn).set_text("#")

        # State variables
        self.show_grid = True
        self.zoom = 1.0
        self.pan_x = 0
        self.pan_y = 0
        self.is_3d = True

        # Add click/drag handling
        self.pressed = False
        self.last_x = 0
        self.last_y = 0
        self.canvas.add_event_cb(self.on_pressed, lv.EVENT.PRESSED, None)
        self.canvas.add_event_cb(self.on_pressing, lv.EVENT.PRESSING, None)
        self.canvas.add_event_cb(self.on_released, lv.EVENT.RELEASED, None)

    def on_pressed(self, evt):
        self.pressed = True
        coords = lv.point_t()
        indev = lv.indev_get_act()
        indev.get_point(coords)
        self.last_x = coords.x
        self.last_y = coords.y

    def on_pressing(self, evt):
        if self.pressed:
            coords = lv.point_t()
            indev = lv.indev_get_act()
            indev.get_point(coords)
            dx = coords.x - self.last_x
            dy = coords.y - self.last_y
            self.pan_x += dx
            self.pan_y += dy
            self.last_x = coords.x
            self.last_y = coords.y
            self.redraw()

    def on_released(self, evt):
        self.pressed = False

    def on_zoom_in(self, evt):
        self.zoom *= 1.2
        self.redraw()

    def on_zoom_out(self, evt):
        self.zoom /= 1.2
        self.redraw()

    def on_toggle_grid(self, evt):
        self.show_grid = not self.show_grid
        self.redraw()

    def set_motion_segments(self, segments):
        # Find extents
        self.min_x = float('inf')
        self.min_y = float('inf')
        self.min_z = float('inf')
        self.max_x = float('-inf')
        self.max_y = float('-inf')
        self.max_z = float('-inf')

        for seg in segments:
            x, y, z = seg[0:3]
            self.min_x = min(self.min_x, x)
            self.min_y = min(self.min_y, y)
            self.min_z = min(self.min_z, z)
            self.max_x = max(self.max_x, x)
            self.max_y = max(self.max_y, y)
            self.max_z = max(self.max_z, z)

        print('MINAXI:', self.min_x, self.min_y, self.max_x, self.max_y, self.min_z,
              self.max_z)
        self.segments = segments
        self.redraw()

    def world_to_screen(self, x, y, z=0):
        # Add margins
        margin = 20
        w = self.canvas.get_width() - 2*margin
        h = self.canvas.get_height() - 2*margin

        # Scale to fit
        scale_x = w / (self.max_x - self.min_x)
        scale_y = h / (self.max_y - self.min_y)
        scale = min(scale_x, scale_y) * self.zoom

        if self.is_3d:
            # Isometric projection
            iso_x = (x - y) * math.cos(math.pi/6)
            iso_y = (x + y) * math.sin(math.pi/6) + z

            screen_x = margin + w/2 + iso_x * scale + self.pan_x
            screen_y = margin + h/2 + iso_y * scale + self.pan_y
        else:
            # 2D projection
            screen_x = margin + (x - self.min_x) * scale + self.pan_x
            screen_y = margin + (y - self.min_y) * scale + self.pan_y

        return int(screen_x), int(self.height - screen_y)

    def redraw(self):
        # Clear canvas
        self.canvas.fill_bg(lv.color_hex(0xFFFFFFFF), lv.OPA.COVER)

        self.layer = lv.layer_t()
        self.canvas.init_layer(self.layer)

        # Draw grid if enabled
        if self.show_grid:
            self.draw_grid()

        # Draw rulers
        self.draw_rulers()

        # Draw WCS origin if in view
        ox, oy = self.world_to_screen(0, 0)
        if 0 <= ox < self.canvas.get_width() and 0 <= oy < self.canvas.get_height():
            self.draw_line(ox-10, oy, ox+10, oy, lv.color_hex(0xFF0000FF))
            self.draw_line(ox, oy-10, ox, oy+10, lv.color_hex(0xFF0000FF))

        print(">> SEGS")
        # Draw motion segments
        last_x = None
        last_y = None

        llen = len(self.segments)
        for i, seg in enumerate(self.segments):
            print("seg", i, 'of', llen)
            x, y, z = seg[0:3]
            is_rapid = seg[4]

            screen_x, screen_y = self.world_to_screen(x, y, z)

            if last_x is not None:
                if is_rapid:
                    self.canvas.add_style(self.style_g0, lv.STATE.DEFAULT )
                else:
                    self.canvas.add_style(self.style_g1, lv.STATE.DEFAULT)
                self.draw_line(last_x, last_y, screen_x, screen_y,
                               lv.color_hex(0xFFFF00FF))

            last_x = screen_x
            last_y = screen_y

        self.canvas.finish_layer(self.layer)

    def draw_grid(self):
        print(">> DRAW GRID")
        # Draw grid lines
        grid_spacing = 2  # mm

        x = self.min_x - (self.min_x % grid_spacing)
        while x <= self.max_x:
            screen_x1, screen_y1 = self.world_to_screen(x, self.min_y)
            screen_x2, screen_y2 = self.world_to_screen(x, self.max_y)
            print('grid line', screen_x1, screen_y1, screen_x2, screen_y2, x,
                  self.min_x, self.max_x)
            self.draw_line(screen_x1, screen_y1, screen_x2, screen_y2,
                          lv.color_hex(0xFFE0E0E0))
            x += grid_spacing

        y = self.min_y - (self.min_y % grid_spacing)
        while y <= self.max_y:
            screen_x1, screen_y1 = self.world_to_screen(self.min_x, y)
            screen_x2, screen_y2 = self.world_to_screen(self.max_x, y)
            self.draw_line(screen_x1, screen_y1, screen_x2, screen_y2,
                          lv.color_hex(0xFFE0E0E0))
            y += grid_spacing
        print("<< DRAW GRID")

    def draw_line(self, x1, y1, x2, y2, color):
        print(">> LINE: ", [x1, y1], [x2, y2])
        dsc = lv.draw_line_dsc_t()
        dsc.init()
        dsc.color = color
        dsc.width = 1
        dsc.round_end = 1
        dsc.round_start = 1
        dsc.p1.x = x1
        dsc.p1.y = y1
        dsc.p2.x = x2
        dsc.p2.y = y2
        lv.draw_line(self.layer, dsc)
        print("<< LINE")

    def draw_rulers(self):
        print(">> DRAW_RULERS")
        # Draw rulers showing extents
        margin = 5

        # X ruler
        x1, y1 = self.world_to_screen(self.min_x, self.min_y)
        x2, y2 = self.world_to_screen(self.max_x, self.min_y)
        self.draw_line(x1, y1+margin, x2, y2+margin, lv.color_hex(0xFF000000))

        # Y ruler
        x1, y1 = self.world_to_screen(self.min_x, self.min_y)
        x2, y2 = self.world_to_screen(self.min_x, self.max_y)
        self.draw_line(x1-margin, y1, x2-margin, y2, lv.color_hex(0xFF000000))

        # Add labels
        label_style = lv.style_t()
        label_style.init()
        label_style.set_text_color(lv.color_hex(0xFF000000))
        print("2")

        # X extents
        x_min_label = lv.label(self.canvas)
        x_min_label.set_text(f"[{self.min_x:.1f},{self.min_y:.1f}]")
        x_min_label.add_style(label_style, 0)
        x_min_label.align_to(self, lv.ALIGN.BOTTOM_LEFT, 0, 0)

        x_max_label = lv.label(self.canvas)
        x_max_label.set_text(f"[{self.max_x:.1f},{self.min_y:.1f}]")
        x_max_label.add_style(label_style, 0)
        x_max_label.align_to(self, lv.ALIGN.BOTTOM_RIGHT, 0, 0)

        # Y extents
        #y_min_label = lv.label(self.canvas)
        #y_min_label.set_text(f"Y_{self.min_y:.1f}")
        #y_min_label.add_style(label_style, 0)
        #y_min_label.align_to(self, lv.ALIGN.BOTTOM_LEFT, 0, -20)

        y_max_label = lv.label(self.canvas)
        y_max_label.set_text(f"[{self.min_x:.1f},{self.max_y:.1f}]")
        y_max_label.add_style(label_style, 0)
        y_max_label.align_to(self, lv.ALIGN.TOP_LEFT, 0, 0)
        print("<< DRAW_RULERS")

if __name__ == '__main__':
    gcs = GCodeSimulator()

    # Random gcode from
    # "https://www.cnczone.com/forums/g-code-programing/16117-g-code-examples.html"

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
    gcode1 = """O2000 (Example: Center of .500 Tool Path 3" bore full depth)
M6T1
G0G90G54X5.Y0S3000M3
G43H1Z2./M8
Z.1
G1Z0F50.
G91G41D1X1.25F18.
G3I-1.25Z-.104L5
I-1.25
G1G40X-1.25
G0G90Z2.
X-5.Y0(ADDITIONAL LOCATIONS)
Z.1
G1Z0F50.
G91G41D1X1.25F18.
G3I-1.25Z-.104L5
I-1.25
G1G40X-1.25
G0G90Z2.
G91G28Y0Z0
G90
M30"""
    gocde2 = """G43 Z50.0 H1 M08
Z2.0
G81 G98/G99 Z-10.0 R2.0 F20.0
G82 G98/G99 Z-10.0 R2.0 P1000 F20.0
G83 G98/G99 Z-10.0 R2.0 Q2.0 F20.0
G73 G98/G99 Z-10.0 R2.0 Q2.0 F20.0
G80"""
    gcode3 = """(climb cut)
G0x0y0 (start point)
z1.
g1z.05f20.
g3x-.7375y0i-.3687j0f5. (ramp on)
g3x-.7375y0i.7375j0 ( full 360 deg cut)
g3x0y0i.3687j0 (ramp off)
g1z1.f20."""
    gcode4 = """G0x1.325y0
z1.
g1z.2f20
z-.05f10.
g3x.8125y0i-.25jof5.
g2x.8125y0i-.8125j0
g3x1.3125y0i.25j0
g1z1.f20."""
    gcode5='G0X0Y0Z0\nG1X10Y0Z0\nG1X10Y10Z10M30'
    gcode6="""N11475 G2 Y1.6156 Z0.7504 J0.0005 K0.0062
N11480 G3 Y1.6129 Z0.751 J-0.0028 K-0.0056
N11485 G17 G2 X3.8826 I-0.01 J0.
N11490 G19 G2 Y1.6156 Z0.7504 J0. K-0.0063
N11495 G3 Y1.6179 Z0.7498 J0.0028 K0.0056
N11500 G1 Y1.6246 Z0.7492
N11505 Y1.6339 Z0.748
N11510 Y1.6485 Z0.7442
N11515 Y1.6606 Z0.7392
N11520 Y1.6688 Z0.735
N11525 Y1.6812 Z0.7267
N11530 Y1.6909 Z0.7182
N11535 Y1.6972 Z0.7117
N11540 Y1.7061 Z0.7002
N11545 Y1.7125 Z0.6892
N11550 Y1.7231 Z0.6679
N11555 Y1.7323 Z0.6515
N11560 Y1.7446 Z0.6324
N11565 Y1.7556 Z0.6179
N11570 Y1.7677 Z0.6037
N11575 Y1.7832 Z0.5875
N11580 Y1.7967 Z0.5753
N11585 Y1.814 Z0.5614
N11590 Y1.8291 Z0.551
N11595 Y1.8447 Z0.5415
N11600 Y1.8642 Z0.5311
N11605 Y1.881 Z0.5237
N11610 Y1.898 Z0.5173
N11615 Y1.9191 Z0.5107
N11620 Y1.9407 Z0.5057
N11625 Y1.9624 Z0.5024
N11630 Y1.9768 Z0.501
N11635 Y1.9982 Z0.5001
N11640 Y2.0212 Z0.5008
N11645 Y2.0363 Z0.5022
N11650 Y2.0588 Z0.5056
N11655 Y2.0809 Z0.5107
N11660 Y2.0954 Z0.515
N11665 Y2.1166 Z0.5226
N11670 Y2.1305 Z0.5287
N11675 Y2.1507 Z0.5389
N11680 Y2.17 Z0.5503
N11685 Y2.1825 Z0.559
N11690 Y2.2003 Z0.5727
N11695 Y2.2169 Z0.5876
N11700 Y2.2275 Z0.5985
N11705 Y2.2424 Z0.6153
N11710 Y2.2557 Z0.633
N11715 Y2.2641 Z0.6456
N11720 Y2.2754 Z0.6649
N11725 Y2.2851 Z0.6845
N11730 Y2.2884 Z0.6907
N11735 Y2.2939 Z0.7002
N11740 Y2.3013 Z0.7099
N11745 Y2.3073 Z0.7166
N11750 Y2.3176 Z0.7258
N11755 Y2.3229 Z0.7296
N11760 Y2.3351 Z0.7372
N11765 Y2.3509 Z0.744
N11770 Y2.3608 Z0.7467
N11775 Y2.3709 Z0.7488
N11780 Y2.3821 Z0.7498
N11785 Y2.3822
N11790 G3 Y2.3844 Z0.7504 J-0.0006 K0.0062
N11795 G2 Y2.3871 Z0.751 J0.0028 K-0.0056
N11800 G17 G3 X3.8626 I-0.01 J0.
N11805 G19 G3 Y2.3843 Z0.7504 J0. K-0.0063
N11810 G2 Y2.3821 Z0.7498 J-0.0028 K0.0056
N11815 G1 Y2.3707 Z0.7488"""
    gcode7="""O32123 (NC Viewer Demo File)
(Using high feed G1 F500. instead of G0.)
(T1 D=0.5 CR=0. - ZMIN=0. - flat end mill)
(T3 D=0.125 CR=0.0625 - ZMIN=0.5 - ball end mill)
N10 G90 G94 G17 G69
N15 G20
N20 G53 G0 Z0.

(Adaptive1)
N30 T1 M6
N35 S7640 M3
N40 G54
N45 M8
N60 G0 X4.4764 Y2.9321
N65 G43 Z1.4 H1
N70 T3
N75 G0 Z0.5063
N80 G1 Z0.4563 F92.
N85 X4.4762 Y2.9319 Z0.4507
N90 X4.4754 Y2.9314 Z0.4452
N95 X4.4741 Y2.9306 Z0.4398
N100 X4.4723 Y2.9295 Z0.4346
N105 X4.47 Y2.928 Z0.4297
N110 X4.4672 Y2.9262 Z0.4251
N115 X4.4641 Y2.9242 Z0.4209
N120 X4.4606 Y2.922 Z0.4172
N125 X4.4567 Y2.9195 Z0.414
N130 X4.4526 Y2.9169 Z0.4113
N135 X4.4482 Y2.9141 Z0.4091
N140 X4.4437 Y2.9112 Z0.4076
N145 X4.439 Y2.9082 Z0.4066
N150 X4.4343 Y2.9052 Z0.4063
N155 G3 X4.2197 Y2.7332 I0.6867 J-1.0761
N160 X4.2259 Y2.6292 I0.0772 J-0.0476
N165 G2 X4.26 Y2.4941 I-0.2361 J-0.1314
N170 G1 Y0.2607
N175 G2 X4.1739 Y0.0562 I-0.2651 J-0.0087
N180 G1 X3.9437 Y-0.174
N185 G2 X3.7391 Y-0.2599 I-0.1954 J0.1788
N190 G1 X3.7091 Y-0.26
N195 X0.2759
N200 G2 X0.0563 Y-0.174 I-0.021 J0.2698
N205 G1 X-0.1739 Y0.0562
N210 G2 X-0.26 Y0.2607 I0.179 J0.1958
N215 G1 Y2.2393
N220 G2 X-0.1739 Y2.4438 I0.2651 J0.0087
N225 G1 X0.0563 Y2.674
N230 G2 X0.2609 Y2.7599 I0.1954 J-0.1788
N235 G1 X0.2909 Y2.76
N240 X3.979
N245 X3.994
N250 X4.0022 Y2.7599
N255 X4.0055 Y2.7604 Z0.4066
N260 X4.0088 Y2.7608 Z0.4075
N265 X4.0118 Y2.7613 Z0.4089
N270 X4.0146 Y2.7617 Z0.4108
N275 X4.0169 Y2.7625 Z0.4132
N280 X4.0187 Y2.7632 Z0.4161
N285 X4.0201 Y2.7637 Z0.4192
N290 X4.021 Y2.764 Z0.4225
N295 X4.0212 Y2.7641 Z0.4259
N300 G0 Z1.
N305 G1 X4.3196 Y2.4536 F500.
N310 G0 Z0.6246
N315 G1 Z0.5746 F92.
N320 X4.3195 Y2.4533 Z0.569
N325 X4.3192 Y2.4524 Z0.5635
N330 X4.3188 Y2.4509 Z0.5581
N335 X4.3182 Y2.4489 Z0.5529
N340 X4.3175 Y2.4462 Z0.548
N345 X4.3166 Y2.4431 Z0.5435
N350 X4.3156 Y2.4395 Z0.5393
N355 X4.3144 Y2.4355 Z0.5355
N360 X4.3132 Y2.4311 Z0.5323
N365 X4.3119 Y2.4264 Z0.5296
N370 X4.3104 Y2.4214 Z0.5274
N375 X4.309 Y2.4162 Z0.5259
N380 X4.3075 Y2.4109 Z0.5249
N385 X4.3059 Y2.4055 Z0.5246
N390 G3 X4.2599 Y2.1344 I1.2282 J-0.3479
N395 G2 X4.2318 Y2.0147 I-0.269 J0.
N400 G3 X4.2366 Y1.9746 I0.0538 J-0.0139
N405 X4.2375 Y1.9735 I0.0028 J0.0014
N410 G2 X4.2517 Y1.9621 I-0.1833 J-0.2431
N415 X4.3345 Y1.8493 I-0.1975 J-0.2317
N420 X4.3384 Y1.8323 I-0.046 J-0.0195
N425 G1 X4.3472 Y1.6576
N430 G2 X4.345 Y1.6402 I-0.0499 J-0.0025
N435 X4.2733 Y1.519 I-0.2908 J0.0902
N440 X4.261 Y1.5069 I-0.219 J0.2114
N445 G3 X4.26 Y1.5048 I0.0021 J-0.0023
N450 G2 X4.1873 Y1.3185 I-0.2687 J-0.0025
N455 G3 X4.1005 Y1.0736 I0.5073 J-0.3177
N460 X4.0718 Y0.7873 I1.9108 J-0.3361
N465 X4.0631 Y0.4732 I6.5791 J-0.3388
N470 G2 X3.9784 Y0.2285 I-0.5135 J0.0407
N475 X3.8856 Y0.1026 I-0.4336 J0.2224
N480 X3.6489 Y-0.0038 I-0.343 J0.4467
N485 X3.3117 Y-0.0462 I-0.3821 J1.6785
N490 X2.7097 Y-0.0582 I-0.5013 J10.0134
N495 G1 X2.5199 Y-0.0594
N500 X2.3301 Y-0.06
N505 X2.1402 Y-0.0602
N510 X1.9504 Y-0.06
N515 X1.6362
N520 X1.2434
N525 X0.4841
N530 G2 X0.2603 Y0.0048 I0.0109 J0.4565
N535 X0.1094 Y0.1076 I0.2763 J0.5679
N540 X-0.0013 Y0.3421 I0.4194 J0.3413
N545 X-0.0441 Y0.6529 I1.5248 J0.3681
N550 X-0.058 Y1.2548 I8.8886 J0.5064
N555 G1 X-0.0591 Y1.4119
N560 X-0.0599 Y1.569
N565 X-0.0602 Y1.7261
N570 X-0.0601 Y1.8832
N575 X-0.0595 Y2.0403
N580 G2 X0.0165 Y2.2611 I0.5081 J-0.0514
N585 X0.1065 Y2.3893 I0.5101 J-0.2625
N590 X0.3405 Y2.5008 I0.3387 J-0.4094
N595 X0.6512 Y2.5439 I0.3672 J-1.5061
N600 X1.1746 Y2.5582 I0.4987 J-8.7142
N605 G3 X1.2707 Y2.6225 I-0.0017 J0.1066
N610 X1.2758 Y2.6413 I-0.0449 J0.0217
N615 G1 X1.2761 Y2.6469 Z0.5249
N620 X1.2764 Y2.6525 Z0.5259
N625 X1.2767 Y2.6578 Z0.5274
N630 X1.277 Y2.663 Z0.5296
N635 X1.2773 Y2.6679 Z0.5323
N640 X1.2775 Y2.6725 Z0.5355
N645 X1.2778 Y2.6766 Z0.5393
N650 X1.278 Y2.6804 Z0.5435
N655 X1.2781 Y2.6836 Z0.548
N660 X1.2783 Y2.6863 Z0.5529
N665 X1.2784 Y2.6885 Z0.5581
N670 X1.2785 Y2.69 Z0.5635
N675 X1.2786 Y2.691 Z0.569
N680 Y2.6913 Z0.5746
N685 G0 Z1.
N690 G1 X4.2674 Y1.366 F500.
N695 G0 Z0.6246
N700 G1 Z0.5746 F92.
N705 X4.2671 Y1.3659 Z0.569
N710 X4.2663 Y1.3656 Z0.5635
N715 X4.2648 Y1.3651 Z0.5581
N720 X4.2628 Y1.3644 Z0.5529
N725 X4.2602 Y1.3635 Z0.548
N730 X4.2571 Y1.3624 Z0.5435
N735 X4.2536 Y1.3612 Z0.5393
N740 X4.2497 Y1.3598 Z0.5355
N745 X4.2453 Y1.3583 Z0.5323
N750 X4.2407 Y1.3567 Z0.5296
N755 X4.2358 Y1.355 Z0.5274
N760 X4.2307 Y1.3532 Z0.5259
N765 X4.2255 Y1.3514 Z0.5249
N770 X4.2202 Y1.3496 Z0.5246
N775 G3 X4.2038 Y1.3387 I0.013 J-0.0374
N780 G2 X4.1012 Y1.2605 I-0.2042 J0.1615
N785 G3 X3.972 Y1.1033 I0.1738 J-0.2745
N790 X3.902 Y0.852 I0.8001 J-0.3581
N795 X3.8778 Y0.644 I2.4888 J-0.396
N800 G2 X3.8215 Y0.443 I-0.7104 J0.0905
N805 X3.6063 Y0.2592 I-0.378 J0.2249
N810 X3.3557 Y0.1865 I-0.3806 J0.843
N815 X2.9125 Y0.1494 I-0.4687 J2.9317
N820 X2.2057 Y0.1414 I-0.5555 J17.7956
N825 G1 X2.0224 Y0.1403
N830 X1.8391 Y0.1399
N835 X1.6558 Y0.1401
N840 X1.394 Y0.14
N845 X0.8441
N850 G2 X0.6347 Y0.1421 I-0.0727 J3.1824
N855 X0.433 Y0.1937 I0.0337 J0.5518
N860 X0.254 Y0.413 I0.2375 J0.3766
N865 X0.1842 Y0.6645 I0.8741 J0.378
N870 X0.1489 Y1.1078 I3.0651 J0.4671
N875 X0.142 Y1.867 I18.1166 J0.5439
N880 X0.1831 Y2.0451 I0.6444 J-0.055
N885 X0.3676 Y2.2243 I0.3562 J-0.1821
N890 X0.6407 Y2.3115 I0.3962 J-0.7695
N895 X0.979 Y2.3463 I0.4108 J-2.331
N900 G3 X1.1619 Y2.3726 I-0.0414 J0.9401
N905 X1.2396 Y2.4941 I-0.0353 J0.1082
N910 G1 X1.2402 Y2.5024
N915 G3 X1.236 Y2.5215 I-0.05 J-0.0008
N920 G1 X1.2338 Y2.5266 Z0.5249
N925 X1.2316 Y2.5317 Z0.5259
N930 X1.2295 Y2.5366 Z0.5274
N935 X1.2274 Y2.5414 Z0.5296
N940 X1.2254 Y2.5459 Z0.5323
N945 X1.2236 Y2.5501 Z0.5355
N950 X1.222 Y2.5539 Z0.5393
N955 X1.2205 Y2.5573 Z0.5435
N960 X1.2192 Y2.5603 Z0.548
N965 X1.2181 Y2.5628 Z0.5529
N970 X1.2173 Y2.5647 Z0.5581
N975 X1.2166 Y2.5662 Z0.5635
N980 X1.2163 Y2.567 Z0.569
N985 X1.2161 Y2.5673 Z0.5746
N990 G0 Z1.
N995 G1 X4.1781 Y1.2601 F500.
N1000 G0 Z0.6246
N1005 G1 Z0.5746 F92.
N1010 X4.1778 Z0.569
N1015 X4.1769 Y1.2603 Z0.5635
N1020 X4.1754 Y1.2605 Z0.5581
N1025 X4.1732 Y1.2607 Z0.5529
N1030 X4.1705 Y1.2611 Z0.548
N1035 X4.1673 Y1.2615 Z0.5435
N1040 X4.1636 Y1.262 Z0.5393
N1045 X4.1595 Y1.2625 Z0.5355
N1050 X4.1549 Y1.2631 Z0.5323
N1055 X4.1501 Y1.2637 Z0.5296
N1060 X4.1449 Y1.2644 Z0.5274
N1065 X4.1396 Y1.2651 Z0.5259
N1070 X4.1341 Y1.2658 Z0.5249
N1075 X4.1285 Y1.2665 Z0.5246
N1080 G3 X4.1091 Y1.2641 I-0.0051 J-0.0392
N1085 G2 X4.0106 Y1.2401 I-0.1121 J0.2458
N1090 G3 X3.8648 Y1.1461 I0.0553 J-0.2458
N1095 X3.7559 Y0.939 I0.4438 J-0.3657
N1100 X3.6999 Y0.7372 I3.6045 J-1.1081
N1105 G2 X3.5007 Y0.5031 I-0.4069 J0.1444
N1110 X3.2064 Y0.3982 I-0.4334 J0.7508
N1115 X2.8165 Y0.3552 I-0.4563 J2.3465
N1120 X2.1097 Y0.3419 I-0.5938 J12.8322
N1125 G1 X1.9133 Y0.3407
N1130 X1.717 Y0.34
N1135 X1.5206 Y0.3398
N1140 X1.3242 Y0.3401
N1145 X1.0362 Y0.34
N1150 G2 X0.7744 Y0.3439 I-0.0728 J3.8845
N1155 X0.5339 Y0.4862 I0.0365 J0.3362
N1160 X0.4218 Y0.7208 I0.5208 J0.3929
N1165 X0.3631 Y1.0819 I1.6162 J0.4478
N1170 X0.3448 Y1.7098 I6.7381 J0.5114
N1175 X0.4752 Y1.9566 I0.3321 J-0.0176
N1180 X0.7324 Y2.081 I0.4062 J-0.5118
N1185 X0.9628 Y2.1297 I0.5279 J-1.927
N1190 G3 X1.14 Y2.1758 I-0.2065 J1.1596
N1195 X1.2048 Y2.2124 I-0.0685 J0.1967
N1200 X1.24 Y2.2962 I-0.0836 J0.0844
N1205 G1 Y2.4642
N1210 G3 X1.2355 Y2.4832 I-0.05 J-0.0017
N1215 G1 X1.2332 Y2.4882 Z0.5249
N1220 X1.2309 Y2.4933 Z0.5259
N1225 X1.2287 Y2.4982 Z0.5274
N1230 X1.2265 Y2.5029 Z0.5296
N1235 X1.2245 Y2.5074 Z0.5323
N1240 X1.2226 Y2.5115 Z0.5355
N1245 X1.2209 Y2.5153 Z0.5393
N1250 X1.2193 Y2.5187 Z0.5435
N1255 X1.218 Y2.5217 Z0.548
N1260 X1.2168 Y2.5241 Z0.5529
N1265 X1.216 Y2.5261 Z0.5581
N1270 X1.2153 Y2.5275 Z0.5635
N1275 X1.2149 Y2.5284 Z0.569
N1280 X1.2148 Y2.5286 Z0.5746
N1285 G0 Z1.
N1290 G1 X4.083 Y1.2137 F500.
N1295 G0 Z0.6246
N1300 G1 Z0.5746 F92.
N1305 X4.0828 Y1.2139 Z0.569
N1310 X4.0819 Y1.2143 Z0.5635
N1315 X4.0806 Y1.215 Z0.5581
N1320 X4.0786 Y1.216 Z0.5529
N1325 X4.0762 Y1.2172 Z0.548
N1330 X4.0733 Y1.2187 Z0.5435
N1335 X4.07 Y1.2204 Z0.5393
N1340 X4.0663 Y1.2223 Z0.5355
N1345 X4.0622 Y1.2244 Z0.5323
N1350 X4.0579 Y1.2266 Z0.5296
N1355 X4.0532 Y1.229 Z0.5274
N1360 X4.0485 Y1.2315 Z0.5259
N1365 X4.0435 Y1.234 Z0.5249
N1370 X4.0386 Y1.2365 Z0.5246
N1375 G3 X4.0194 Y1.2409 I-0.018 J-0.0352
N1380 G2 X3.8838 Y1.2397 I-0.0934 J3.0462
N1385 G3 X3.7178 Y1.1499 I0.0413 J-0.2744
N1390 X3.602 Y0.9767 I0.4295 J-0.4126
N1395 G2 X3.4838 Y0.8047 I-0.6047 J0.2887
N1400 X3.1906 Y0.6384 I-0.4858 J0.5149
N1405 X2.8312 Y0.5706 I-0.4733 J1.5221
N1410 X2.2821 Y0.5454 I-0.5392 J5.7607
N1415 G1 X2.0989 Y0.5431
N1420 X1.9156 Y0.5414
N1425 X1.7323 Y0.5402
N1430 X1.549 Y0.5395
N1435 X1.3657
N1440 X1.1825 Y0.54
N1445 X0.9992 Y0.541
N1450 G2 X0.7518 Y0.6701 I0.0147 J0.3299
N1455 X0.632 Y0.9007 I0.4828 J0.3972
N1460 X0.5671 Y1.2606 I1.5001 J0.4562
N1465 X0.5527 Y1.5219 I2.4048 J0.2636
N1470 X0.6739 Y1.746 I0.3205 J-0.0285
N1475 X0.8769 Y1.8631 I0.4314 J-0.5132
N1480 X1.0725 Y1.9382 I2.0001 J-4.918
N1485 G3 X1.1963 Y2. I-0.2726 J0.7008
N1490 X1.2401 Y2.0917 I-0.0767 J0.0929
N1495 G1 X1.24 Y2.2843
N1500 G3 X1.2355 Y2.3033 I-0.05 J-0.0017
N1505 G1 X1.2332 Y2.3084 Z0.5249
N1510 X1.2309 Y2.3134 Z0.5259
N1515 X1.2287 Y2.3183 Z0.5274
N1520 X1.2265 Y2.323 Z0.5296
N1525 X1.2245 Y2.3275 Z0.5323
N1530 X1.2226 Y2.3316 Z0.5355
N1535 X1.2209 Y2.3354 Z0.5393
N1540 X1.2193 Y2.3388 Z0.5435
N1545 X1.218 Y2.3418 Z0.548
N1550 X1.2168 Y2.3443 Z0.5529
N1555 X1.216 Y2.3462 Z0.5581
N1560 X1.2153 Y2.3476 Z0.5635
N1565 X1.2149 Y2.3485 Z0.569
N1570 X1.2148 Y2.3488 Z0.5746
N1575 G0 Z1.
N1580 G1 X3.9576 Y1.2089 F500.
N1585 G0 Z0.6246
N1590 G1 Z0.5746 F92.
N1595 X3.9573 Y1.2091 Z0.569
N1600 X3.9565 Y1.2096 Z0.5635
N1605 X3.9552 Y1.2103 Z0.5581
N1610 X3.9533 Y1.2114 Z0.5529
N1615 X3.951 Y1.2128 Z0.548
N1620 X3.9482 Y1.2145 Z0.5435
N1625 X3.945 Y1.2164 Z0.5393
N1630 X3.9414 Y1.2185 Z0.5355
N1635 X3.9375 Y1.2209 Z0.5323
N1640 X3.9332 Y1.2234 Z0.5296
N1645 X3.9288 Y1.226 Z0.5274
N1650 X3.9242 Y1.2288 Z0.5259
N1655 X3.9194 Y1.2316 Z0.5249
N1660 X3.9146 Y1.2345 Z0.5246
N1665 G3 X3.8958 Y1.24 I-0.0202 J-0.034
N1670 G1 X3.7391
N1675 G3 X3.5705 Y1.1643 I0.0235 J-0.2779
N1680 X3.4557 Y1.0571 I1.3356 J-1.5455
N1685 G2 X3.0205 Y0.8276 I-0.6511 J0.7074
N1690 X2.6331 Y0.7669 I-0.5069 J1.9691
N1695 X2.0315 Y0.7445 I-0.5797 J7.4716
N1700 X1.1936 Y0.741 I-0.584 J39.3351
N1705 X0.9497 Y0.8775 I0.0288 J0.3376
N1710 X0.8322 Y1.1094 I0.5039 J0.4012
N1715 X0.7874 Y1.3137 I0.9799 J0.3216
N1720 X0.8568 Y1.5065 I0.2873 J0.0055
N1725 X1.0346 Y1.6593 I0.5238 J-0.4294
N1730 G3 X1.1937 Y1.7645 I-5.9543 J9.1816
N1735 X1.24 Y1.8582 I-0.0733 J0.0945
N1740 G1 Y2.0744
N1745 G3 X1.2355 Y2.0934 I-0.05 J-0.0017
N1750 G1 X1.2332 Y2.0985 Z0.5249
N1755 X1.2309 Y2.1035 Z0.5259
N1760 X1.2287 Y2.1084 Z0.5274
N1765 X1.2265 Y2.1132 Z0.5296
N1770 X1.2245 Y2.1176 Z0.5323
N1775 X1.2226 Y2.1218 Z0.5355
N1780 X1.2209 Y2.1256 Z0.5393
N1785 X1.2193 Y2.129 Z0.5435
N1790 X1.218 Y2.1319 Z0.548
N1795 X1.2168 Y2.1344 Z0.5529
N1800 X1.216 Y2.1364 Z0.5581
N1805 X1.2153 Y2.1378 Z0.5635
N1810 X1.2149 Y2.1386 Z0.569
N1815 X1.2148 Y2.1389 Z0.5746
N1820 G0 Z1.
N1825 G1 X3.8009 Y1.2089 F500.
N1830 G0 Z0.6246
N1835 G1 Z0.5746 F92.
N1840 X3.8006 Y1.2091 Z0.569
N1845 X3.7998 Y1.2096 Z0.5635
N1850 X3.7985 Y1.2103 Z0.5581
N1855 X3.7966 Y1.2114 Z0.5529
N1860 X3.7943 Y1.2128 Z0.548
N1865 X3.7915 Y1.2145 Z0.5435
N1870 X3.7883 Y1.2164 Z0.5393
N1875 X3.7847 Y1.2185 Z0.5355
N1880 X3.7808 Y1.2209 Z0.5323
N1885 X3.7766 Y1.2234 Z0.5296
N1890 X3.7721 Y1.226 Z0.5274
N1895 X3.7675 Y1.2288 Z0.5259
N1900 X3.7627 Y1.2316 Z0.5249
N1905 X3.7579 Y1.2345 Z0.5246
N1910 G3 X3.7391 Y1.24 I-0.0202 J-0.034
N1915 G1 X3.5742
N1920 G3 X3.403 Y1.1952 I0.0247 J-0.4435
N1925 G2 X3.1615 Y1.0939 I-3.7446 J8.5957
N1930 X2.5995 Y0.9751 I-0.7265 J2.048
N1935 X2.0506 Y0.9473 I-0.5785 J5.9799
N1940 X1.396 Y0.9416 I-0.5236 J22.5917
N1945 X1.1522 Y1.0785 I0.0306 J0.3402
N1950 X1.104 Y1.2474 I0.1271 J0.1276
N1955 X1.1962 Y1.4332 I0.4279 J-0.0966
N1960 G3 X1.2533 Y1.5115 I-3.7652 J2.8047
N1965 X1.2597 Y1.6097 I-0.1045 J0.056
N1970 G2 X1.2403 Y1.7746 I0.5241 J0.1453
N1975 G1 X1.24 Y1.8582
N1980 G3 X1.2355 Y1.8772 I-0.05 J-0.0017
N1985 G1 X1.2332 Y1.8823 Z0.5249
N1990 X1.2309 Y1.8873 Z0.5259
N1995 X1.2287 Y1.8922 Z0.5274
N2000 X1.2265 Y1.8969 Z0.5296
N2005 X1.2245 Y1.9014 Z0.5323
N2010 X1.2226 Y1.9055 Z0.5355
N2015 X1.2209 Y1.9094 Z0.5393
N2020 X1.2193 Y1.9128 Z0.5435
N2025 X1.218 Y1.9157 Z0.548
N2030 X1.2168 Y1.9182 Z0.5529
N2035 X1.216 Y1.9201 Z0.5581
N2040 X1.2153 Y1.9215 Z0.5635
N2045 X1.2149 Y1.9224 Z0.569
N2050 X1.2148 Y1.9227 Z0.5746
N2055 G0 Z1.
N2060 G1 X3.636 Y1.2089 F500.
N2065 G0 Z0.6246
N2070 G1 Z0.5746 F92.
N2075 X3.6357 Y1.2091 Z0.569
N2080 X3.6349 Y1.2096 Z0.5635
N2085 X3.6336 Y1.2103 Z0.5581
N2090 X3.6317 Y1.2114 Z0.5529
N2095 X3.6294 Y1.2128 Z0.548
N2100 X3.6266 Y1.2145 Z0.5435
N2105 X3.6234 Y1.2164 Z0.5393
N2110 X3.6198 Y1.2185 Z0.5355
N2115 X3.6159 Y1.2209 Z0.5323
N2120 X3.6117 Y1.2234 Z0.5296
N2125 X3.6072 Y1.226 Z0.5274
N2130 X3.6026 Y1.2288 Z0.5259
N2135 X3.5978 Y1.2316 Z0.5249
N2140 X3.593 Y1.2345 Z0.5246
N2145 G3 X3.5742 Y1.24 I-0.0202 J-0.034
N2150 G1 X3.1994
N2155 G2 X3.1153 I-0.0417 J1.1649
N2160 G3 X2.7432 Y1.2076 I0.1841 J-4.267
N2165 G2 X1.7758 Y1.1631 I-0.9609 J10.3414
N2170 X1.5901 Y1.1627 I-0.0974 J2.0553
N2175 X1.4913 Y1.2711 I0.0227 J0.12
N2180 G3 X1.4272 Y1.3544 I-0.1291 J-0.0332
N2185 G2 X1.2747 Y1.5647 I0.3403 J0.4071
N2190 G3 X1.2635 Y1.5807 I-0.0454 J-0.0205
N2195 G1 X1.2594 Y1.5845 Z0.5249
N2200 X1.2554 Y1.5883 Z0.5259
N2205 X1.2515 Y1.592 Z0.5274
N2210 X1.2477 Y1.5955 Z0.5296
N2215 X1.2441 Y1.5989 Z0.5323
N2220 X1.2408 Y1.602 Z0.5355
N2225 X1.2378 Y1.6049 Z0.5393
N2230 X1.235 Y1.6074 Z0.5435
N2235 X1.2327 Y1.6096 Z0.548
N2240 X1.2307 Y1.6115 Z0.5529
N2245 X1.2291 Y1.613 Z0.5581
N2250 X1.228 Y1.614 Z0.5635
N2255 X1.2273 Y1.6147 Z0.569
N2260 X1.2271 Y1.6149 Z0.5746
N2265 G0 Z1.
N2270 G1 X3.1913 Y1.1952 F500.
N2275 G0 Z0.6246
N2280 G1 Z0.5746 F92.
N2285 X3.1911 Y1.1954 Z0.569
N2290 X3.1904 Y1.1961 Z0.5635
N2295 X3.1894 Y1.1973 Z0.5581
N2300 X3.1879 Y1.1988 Z0.5529
N2305 X3.1861 Y1.2008 Z0.548
N2310 X3.1839 Y1.2032 Z0.5435
N2315 X3.1813 Y1.2059 Z0.5393
N2320 X3.1785 Y1.209 Z0.5355
N2325 X3.1754 Y1.2124 Z0.5323
N2330 X3.172 Y1.2159 Z0.5296
N2335 X3.1685 Y1.2197 Z0.5274
N2340 X3.1648 Y1.2237 Z0.5259
N2345 X3.1611 Y1.2277 Z0.5249
N2350 X3.1573 Y1.2318 Z0.5246
N2355 G3 X3.1394 Y1.24 I-0.0188 J-0.0174
N2360 G1 X1.7601
N2365 G2 X1.4752 Y1.3205 I-0.0088 J0.5128
N2370 G1 X1.4721 Y1.3218 Z0.5249
N2375 X1.4691 Y1.3231 Z0.5258
N2380 X1.4663 Y1.3243 Z0.5272
N2385 X1.4637 Y1.3253 Z0.5291
N2390 X1.4613 Y1.3258 Z0.5316
N2395 X1.4594 Y1.3262 Z0.5344
N2400 X1.4579 Y1.3265 Z0.5375
N2405 X1.457 Y1.3266 Z0.5408
N2410 X1.4567 Y1.3267 Z0.5443
N2415 G0 Z1.
N2420 G1 X4.3214 Y2.5282 F500.
N2425 G0 Z0.6638
N2430 G1 Z0.6138 F92.
N2435 X4.3213 Y2.5279 Z0.6082
N2440 X4.321 Y2.527 Z0.6027
N2445 X4.3206 Y2.5255 Z0.5973
N2450 X4.32 Y2.5235 Z0.5921
N2455 X4.3193 Y2.5209 Z0.5872
N2460 X4.3183 Y2.5177 Z0.5826
N2465 X4.3173 Y2.5142 Z0.5784
N2470 X4.3161 Y2.5101 Z0.5747
N2475 X4.3149 Y2.5057 Z0.5714
N2480 X4.3135 Y2.501 Z0.5687
N2485 X4.3121 Y2.4961 Z0.5666
N2490 X4.3106 Y2.4909 Z0.565
N2495 X4.309 Y2.4856 Z0.5641
N2500 X4.3075 Y2.4802 Z0.5638
N2505 G3 X4.2599 Y2.2093 I1.2262 J-0.3549
N2510 G2 X4.1887 Y2.0293 I-0.2626 J-0.0002
N2515 G3 X4.197 Y1.9614 I0.0426 J-0.0292
N2520 G1 X4.1995 Y1.9592 Z0.5641
N2525 X4.2019 Y1.957 Z0.5649
N2530 X4.2042 Y1.955 Z0.5664
N2535 X4.2062 Y1.9531 Z0.5683
N2540 X4.2084 Y1.9519 Z0.5707
N2545 X4.2101 Y1.9509 Z0.5735
N2550 X4.2114 Y1.9502 Z0.5767
N2555 X4.2122 Y1.9497 Z0.58
N2560 X4.2124 Y1.9496 Z0.5834
N2565 G0 Z1.
N2570 G1 X4.3149 Y2.589 F500.
N2575 G0 Z0.7035
N2580 G1 Z0.6535 F92.
N2585 X4.3148 Y2.5887 Z0.6479
N2590 X4.3146 Y2.5877 Z0.6423
N2595 X4.3142 Y2.5862 Z0.6369
N2600 X4.3136 Y2.5842 Z0.6318
N2605 X4.3129 Y2.5816 Z0.6269
N2610 X4.3121 Y2.5784 Z0.6223
N2615 X4.3111 Y2.5748 Z0.6181
N2620 X4.31 Y2.5708 Z0.6144
N2625 X4.3089 Y2.5664 Z0.6111
N2630 X4.3076 Y2.5616 Z0.6084
N2635 X4.3063 Y2.5566 Z0.6063
N2640 X4.3049 Y2.5514 Z0.6047
N2645 X4.3034 Y2.5461 Z0.6038
N2650 X4.302 Y2.5407 Z0.6035
N2655 G3 X4.26 Y2.2689 I1.2332 J-0.3297
N2660 G2 X4.1486 Y2.0446 I-0.2635 J-0.0089
N2665 G3 X4.1588 Y1.9473 I0.0506 J-0.0438
N2670 X4.1761 Y1.9382 I0.037 J0.0482
N2675 X4.2413 Y1.9555 I0.0197 J0.0573
N2680 X4.157 Y2.0513 I-0.0483 J0.0425
N2685 X4.1289 Y2.0258 I0.035 J-0.067
N2690 X4.1469 Y1.9563 I0.0433 J-0.0259
N2695 X4.165 Y1.9491 I0.0268 J0.0421
N2700 G1 X4.1705 Y1.9481 Z0.6038
N2705 X4.176 Y1.9472 Z0.6047
N2710 X4.1813 Y1.9462 Z0.6063
N2715 X4.1864 Y1.9453 Z0.6084
N2720 X4.1912 Y1.9445 Z0.6111
N2725 X4.1957 Y1.9437 Z0.6144
N2730 X4.1999 Y1.943 Z0.6181
N2735 X4.2035 Y1.9423 Z0.6223
N2740 X4.2067 Y1.9417 Z0.6269
N2745 X4.2094 Y1.9413 Z0.6318
N2750 X4.2115 Y1.9409 Z0.6369
N2755 X4.213 Y1.9406 Z0.6423
N2760 X4.214 Y1.9405 Z0.6479
N2765 X4.2143 Y1.9404 Z0.6535
N2770 G0 Z1.
N2775 G1 X2.7369 Y1.1703 F500.
N2780 G0 Z0.7035
N2785 G1 Z0.6535 F92.
N2790 X2.7366 Y1.1704 Z0.6479
N2795 X2.7357 Y1.1706 Z0.6423
N2800 X2.7343 Y1.1711 Z0.6369
N2805 X2.7322 Y1.1718 Z0.6318
N2810 X2.7296 Y1.1726 Z0.6269
N2815 X2.7265 Y1.1736 Z0.6223
N2820 X2.723 Y1.1747 Z0.6181
N2825 X2.719 Y1.176 Z0.6144
N2830 X2.7146 Y1.1774 Z0.6111
N2835 X2.71 Y1.1789 Z0.6084
N2840 X2.705 Y1.1804 Z0.6063
N2845 X2.6999 Y1.1821 Z0.6047
N2850 X2.6946 Y1.1837 Z0.6038
N2855 X2.6893 Y1.1854 Z0.6035
N2860 G3 X2.4198 Y1.2401 I-0.3871 J-1.2164
N2865 X2.3748 Y1.2423 I-0.0374 J-0.3092
N2870 G1 X2.37 Y1.2413
N2875 X2.3652 Y1.2398
N2880 X2.3607 Y1.2378
N2885 X2.3564 Y1.2354
N2890 X2.3516 Y1.2326 Z0.6038
N2895 X2.3468 Y1.2298 Z0.6047
N2900 X2.3422 Y1.227 Z0.6063
N2905 X2.3377 Y1.2244 Z0.6084
N2910 X2.3335 Y1.2219 Z0.6111
N2915 X2.3296 Y1.2196 Z0.6144
N2920 X2.326 Y1.2174 Z0.6181
N2925 X2.3228 Y1.2155 Z0.6223
N2930 X2.32 Y1.2139 Z0.6269
N2935 X2.3176 Y1.2125 Z0.6318
N2940 X2.3158 Y1.2114 Z0.6369
N2945 X2.3145 Y1.2106 Z0.6423
N2950 X2.3137 Y1.2101 Z0.6479
N2955 X2.3134 Y1.21 Z0.6535
N2960 G0 Z1.
N2965 G1 X2.0263 Y2.8233 F500.
N2970 G0 Z0.7035
N2975 G1 Z0.6535 F92.
N2980 X2.0266 Y2.8232 Z0.6479
N2985 X2.0275 Y2.8229 Z0.6423
N2990 X2.029 Y2.8225 Z0.6369
N2995 X2.031 Y2.8219 Z0.6318
N3000 X2.0336 Y2.8211 Z0.6269
N3005 X2.0368 Y2.8202 Z0.6223
N3010 X2.0403 Y2.8191 Z0.6181
N3015 X2.0443 Y2.8179 Z0.6144
N3020 X2.0487 Y2.8166 Z0.6111
N3025 X2.0534 Y2.8152 Z0.6084
N3030 X2.0584 Y2.8138 Z0.6063
N3035 X2.0636 Y2.8122 Z0.6047
N3040 X2.0689 Y2.8107 Z0.6038
N3045 X2.0742 Y2.8091 Z0.6035
N3050 G3 X2.3448 Y2.76 I0.3619 J1.2241
N3055 X2.3898 Y2.7572 I0.1509 J2.1087
N3060 G1 X2.398 Y2.7583
N3065 X2.4013 Y2.7592 Z0.6037
N3070 X2.4044 Y2.7601 Z0.6046
N3075 X2.4074 Y2.7609 Z0.606
N3080 X2.4101 Y2.7617 Z0.608
N3085 X2.4123 Y2.7628 Z0.6104
N3090 X2.414 Y2.7637 Z0.6132
N3095 X2.4154 Y2.7643 Z0.6163
N3100 X2.4162 Y2.7647 Z0.6197
N3105 X2.4164 Y2.7649 Z0.6231
N3110 G0 Z1.
N3115 G1 X4.3174 Y2.6185 F500.
N3120 G0 Z0.7426
N3125 G1 Z0.6926 F92.
N3130 Y2.6182 Z0.687
N3135 X4.3171 Y2.6173 Z0.6815"""
    gcode8="""G0 X0Y0Z0
G1X10Y0
X0Y0
X0Y10
X0Y0
G2 X20Y20I1J1
G0 X0Y0Z2
G2 X20Y20I1J2"""
    gcs.process(gcode8)

    # Initialize LVGL
    lv.init()

    from micropython import const  # NOQA
    import lcd_bus  # NOQA

    _WIDTH = const(480)
    _HEIGHT = const(320)

    bus = lcd_bus.SDLBus(flags=0)

    buf1 = bus.allocate_framebuffer(_WIDTH * _HEIGHT * 3, 0)

    import lvgl as lv  # NOQA
    import sdl_display  # NOQA

    display = sdl_display.SDLDisplay(
        data_bus=bus,
        display_width=_WIDTH,
        display_height=_HEIGHT,
        frame_buffer1=buf1,
        color_space=lv.COLOR_FORMAT.RGB888
    )
    display.init()

    import sdl_pointer
    import task_handler

    mouse = sdl_pointer.SDLPointer()
    # the duration needs to be set to 5 to have a good response from the mouse.
    # There is a thread that runs that facilitates double buffering.
    th = task_handler.TaskHandler(duration=5)

    # Create window
    win = lv.win(lv.screen_active())
    win.set_size(480, 320)

    # Create GCodeMotionVisualizer
    viewer = GCodeMotionVisualizer(win, 480, 320)
    viewer.set_motion_segments(gcs.motion_segments)
    #viewer.canvas.center()

    i = 0
    while True:
        i += 1


