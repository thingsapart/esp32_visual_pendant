
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
        print(">> PARSING")
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
            self.line_len = len(line)
            words = []
            self.line_pos = 0

            while self.line_pos < self.line_len:
                word, v = self.next_word()
                # groups = []

                if word:
                    if word != 'M' and word != 'G':
                        coords[word] = v
                    else:

                        if len(coords) > 0 and (word != self.last_word or v != self.last_v):
                            coords = self.apply_coords(code, coords)

                        self.last_word = word
                        self.last_v = v

                        code = word + str(int(v))

                        # if code in self.GROUPS:
                        #    ngroup = self.GROUPS[code]
                        #     if ngroup in groups:
                        #        self.error('%s in group (%d) that was already used in this \
                        #                     block, only one word per group per block. \
                        #                     Groups seen: %s' % (code, ngroup, groups),
                        #                     line, pos, self.line_no)

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
                                self.error('Cubc splines not supported')
                            elif v == 5.1:
                                self.error('Quadratic b-splines not supported')
                            elif v == 5.2:
                                self.error('NURBS splines not supported')
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
                                self.error('G33 synchr. spindle motion not supported')
                            elif v == 33.1:
                                self.error('TODO: rigid tapping support')
                            elif v == 38.2 or v == 38.3 or v == 38.4 or v == 38.5:
                                pass # probing.
                            elif v == 40:
                                self.error('TODO: cutter comp support')
                                self.cutter_comp_on = True
                            elif v == 41:
                                self.error('TODO: cutter comp support')
                                self.cutter_comp_on = 'l'
                            elif v == 42:
                                self.error('TODO: cutter comp support')
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
                                self.error('Unknown G-Code %s' % code)
                    #D:2 print(word, v)

            coords = self.apply_coords(code, coords)
            self.line_no += 1
        print("<< PARSING")
        #D:1 print("MOTION SEGMENTS:")
        #D:1 print(self.motion_segments)

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

    def axis_distance_apply(self, axis_idx, offs):
        if self.abs:
            return self.convert_unit_mm(offs) - self.get_wcs_offset(axis_idx)
        else:
            return self.get_axis_pos(axis_idx) + self.convert_unit_mm(offs)


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
            x1 = self.axis_distance_apply(0, coords['X']) if 'X' in coords else x0
            y1 = self.axis_distance_apply(1, coords['Y']) if 'Y' in coords else y0
            z1 = self.axis_distance_apply(2, coords['Z']) if 'Z' in coords else z0

            # print('ARC:', x0, y0, z0, x1, y1, z1)
            # print('I J', self.apply_arc_distance(coords['I'], x0), x0, coords['I'],
            #       self.apply_arc_distance(coords['J'], y0), y0, coords['J'])

            # Get arc center
            if self.plane == Plane.XY:
                cx = self.apply_arc_distance(coords['I'], x0) if 'I' in coords else 0
                cy = self.apply_arc_distance(coords['J'], y0) if 'J' in coords else 0

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
                arc_deg = abs(end_angle - start_angle)
                num_segments = int(arc_deg / (2*math.pi) * self.arc_fpr)

                # Generate points along arc
                for i in range(1, num_segments + 1):
                    t = i / num_segments
                    angle = start_angle + t * (end_angle - start_angle)
                    x = cx + radius * math.cos(angle)
                    y = cy + radius * math.sin(angle)
                    z = z0 + t * (self.z - z0)
                    self.motion_segments.append([x, y, z, self.feed_rate, False, self.line_no])
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
                num_segments = int(arc_deg / (2*math.pi) * self.arc_fpr)

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
        # print(coords)
        if gcode in ['G2', 'G3']:
            self.output_arc_motion_segment(gcode, coords)
        for word, v in coords.items():
            self.maybe_process_coord_feed(gcode, word, v)
        if gcode in ['G0', 'G1', 'G28']:
            self.output_motion_segment(gcode)
        #D:2 print('Coords:', self.x, self.y, self.z, self.i, self.j, self.k)
        return {}
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
            self.error('Axis %s not supported' % word)
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
            self.error('Tool length offset function H not supported')
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

    def skip_white(self):
        line = self.line
        lenl = self.line_len
        pos = self.line_pos
        while pos < lenl:
            c = line[pos]
            if c == ' ' or c == '\t':
                pos += 1
            elif c == '(':
                while pos < lenl and line[pos] != ')':
                    pos += 1
                pos += 1
            elif c == ';' or c == '/':
                return len(line)
            else:
                break

        return pos

    def error(self, s):
        print('ERROR: %s (line %f, col %d, line %s' % (s, self.line_no, self.line_pos, self.line))
        pos = self.line_pos
        line = self.line
        line_no = self.line_no
        if line_no >= 0:
            print('%d: %s|%s' % (line_no, line[:pos], line[pos:]))
        else:
            print('>', line[:pos], '|', line[pos:])

    def next_real(self):
        line = self.line
        lenl = self.line_len
        pos = self.skip_white()
        ppos = pos
        if pos >= lenl:
            c = None
        else:
            c = line[pos]

        is_float = False
        if  c == '+' or c == '-':
            pos += 1
            c = line[pos]

        while c.isdigit() or c == '.':
            pos += 1
            if pos >= lenl: break
            if c == '.': is_float = True
            c = line[pos]

        if pos == ppos:
            self.error('Expected number on line %d' % self.line_no)
            self.line_pos = lenl
            return 0

        s = line[ppos:pos]
        self.line_pos = pos
        if is_float:
            return float(s)
        else:
            return int(s)

    def next_word(self):
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
        pos = self.line_pos
        lenl = self.line_len
        line = self.line
        while pos < lenl:
            pos = self.skip_white()
            if pos >= lenl: break
            c = line[pos]

            if c == 'A' or c == 'B' or c == 'C' or c == 'D' or c == 'F' or c == 'G' or \
                c == 'H' or c == 'I' or c == 'J' or c == 'K' or c == 'L' or \
                c == 'M' or c == 'N' or c == 'O' or c == 'P' or c == 'Q' or c == 'R' or \
                c == 'S' or c == 'T' or c == 'U' or c == 'V' or c == 'W' or \
                c == 'X' or c == 'Y' or c == 'Z':
                self.line_pos = pos + 1
                v = self.next_real()
                return [c, v]
            else:
                self.error('Unknown word %s on line %d' % (repr(c), self.line_no))
        self.line_pos = self.line_len
        return [None, None]

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
        #self.style_g0.set_line_color(lv.color_hex(0xFF808080))  # Gray
        self.style_g0.set_line_dash_gap(8)
        self.style_g0.set_line_dash_width(4)

        self.style_g1 = lv.style_t()
        self.style_g1.init()
        #self.style_g1.set_line_color(lv.color_hex(0xFF0000FF))  # Blue

        self.style_g2g3 = lv.style_t()
        self.style_g2g3.init()
        #self.style_g2g3.set_line_color(lv.color_hex(0xFF00FF00))  # Green

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
        self.is_3d = False

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
        #self.canvas.fill_bg(lv.color_hex(0xFFFFFFFF), lv.OPA.COVER)

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
            self.draw_line(ox-10, oy, ox+10, oy, lv.color32_make(255, 255, 0, 0))
            self.draw_line(ox, oy-10, ox, oy+10, lv.color_hex(0xFF0000FF))

        # Draw motion segments
        last_x = None
        last_y = None

        import time
        for i, seg in enumerate(self.segments):
            a = time.time()
            x, y, z = seg[0:3]
            is_rapid = seg[4]

            screen_x, screen_y = self.world_to_screen(x, y, z)
            b = time.time()
            if last_x is not None:
                # if is_rapid:
                #    self.canvas.add_style(self.style_g0, lv.STATE.DEFAULT )
                # else:
                #     self.canvas.add_style(self.style_g1, lv.STATE.DEFAULT)
                self.draw_line(last_x, last_y, screen_x, screen_y,
                               lv.color_hex(0xFFFF00FF))
            c = time.time()
            print('seg', i, b-a, c-b, c-a)
            last_x = screen_x
            last_y = screen_y

        self.canvas.finish_layer(self.layer)

    def draw_grid(self):
        # Draw grid lines
        grid_spacing = 10 # mm

        x = self.min_x - (self.min_x % grid_spacing)
        while x <= self.max_x:
            screen_x1, screen_y1 = self.world_to_screen(x, self.min_y)
            screen_x2, screen_y2 = self.world_to_screen(x, self.max_y)
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

    def draw_line(self, x1, y1, x2, y2, color):
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

    def draw_rulers(self):
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

if __name__ == '__main__':
    gcs = GCodeSimulator()

    # Random gcode from
    # "https://www.cnczone.com/forums/g-code-programing/16117-g-code-examples.html"

    import example_gcode
    gcs.process(example_gcode.GCODE)

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
    viewer.canvas.center()

    i = 0
    while True:
        i += 1


