
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

        import time
        while self.pos < self.tlen:
            print(">> LINE", self.line_no)
            a = time.time()

            coords = {}
            line = self.next_line()
            b = time.time()

            self.line = line
            #D:1 print('::', self.line_no, line)

            llen = len(line)
            words = []
            pos = 0

            while pos < llen:
                aa = time.time()
                word, v, pos = self.next_word(line, llen, pos)
                groups = []
                bb = time.time()

                if word:
                    if word != 'M' and word != 'G':
                        coords[word] = v
                    else:
                        if len(coords) > 0 and (word != self.last_word or v != self.last_v):
                            coords = self.apply_coords(code, coords)

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
                    cc = time.time()
                    print('word:', cc-aa, cc-bb, bb-aa)
                    #D:2 print(word, v)
            c = time.time()
            print('line', c-a, c-b, b-a)

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

    gcode = """
    (Exported by Fusion360)
(Post Processor: MillenniumOS v0.4.1 for Milo v1.5 by Millennium Machines, version: v0.4.1)
(Output Time: Thu, 28 Nov 2024 17:31:18 GMT)

(WARNING: This gcode was generated to target a singular firmware configuration for RRF.)
(This firmware implements various safety checks and spindle controls that are assumed by this gcode to exist.)
(DO NOT RUN THIS GCODE ON A MACHINE OR FIRMWARE THAT DOES NOT CONTAIN THESE CHECKS)
(You are solely responsible for any injuries or damage caused by not heeding this warning)

(Begin preamble)

(Check MillenniumOS version matches post-processor version)
M4005 V"v0.4.1"

(Pass tool details to firmware)
M4000 P1 R1.55 S"MC 3.1mm 1-Flute"

(Home before start)
G28

(Probe reference surface if necessary)
G6511

(WCS Probing Mode: ON_CHANGE)

(Movement Configuration)
G90
G21
G94

(When using Fusion for Personal Use, the feedrate of rapid)
(moves is reduced to match the feedrate of cutting moves,)
(which can increase machining time. Unrestricted rapid moves)
(are available with a Fusion Subscription.)
(Park ready for WCS change)
G27

(Switch to WCS 1)
G54

(Probe origin in current WCS)
G6600

(Enable rotation compensation if necessary)
M5011

(TC: MC 3.1mm 1-Flute L=17)
T1

(Enable Variable Spindle Speed Control)
M7000 P4000 V200

(Start spindle at requested RPM and wait for it to accelerate)
M3.9 S16500

(Begin operation adaptive2d: 2D Adaptive4)

(Move to starting position in X and Y)
G0 X28.182 Y-97.305

(Move to starting position in Z)
G0 Z4.0

G1 Z1.0 F900.0

(Linearized circular move - low memory mode enabled)
G1 X28.076 Y-97.199 Z0.996 F152.4
G1 X27.96 Y-97.103 Z0.992
G1 X27.835 Y-97.019 Z0.988
G1 X27.702 Y-96.948 Z0.984
G1 X27.563 Y-96.89 Z0.98
G1 X27.42 Y-96.847 Z0.976
G1 X27.272 Y-96.817 Z0.972
G1 X27.123 Y-96.802 Z0.968
G1 X26.972 Z0.965
G1 X26.823 Y-96.816 Z0.961
G1 X26.675 Y-96.845 Z0.957
G1 X26.531 Y-96.888 Z0.953
G1 X26.392 Y-96.946 Z0.949
G1 X26.259 Y-97.016 Z0.945
G1 X26.134 Y-97.1 Z0.941
G1 X26.018 Y-97.195 Z0.937

(Linearized circular move - low memory mode enabled)
G1 X25.928 Y-97.283 Z0.934
G1 X25.845 Y-97.379 Z0.93
G1 X25.771 Y-97.48 Z0.927
G1 X25.705 Y-97.588 Z0.924
G1 X25.648 Y-97.701 Z0.92

(Linearized circular move - low memory mode enabled)
G1 X25.593 Y-97.841 Z0.917
G1 X25.552 Y-97.985 Z0.913
G1 X25.525 Y-98.133 Z0.909
G1 X25.513 Y-98.283 Z0.905
G1 X25.516 Y-98.433 Z0.901
G1 X25.533 Y-98.582 Z0.897
G1 X25.565 Y-98.729 Z0.893
G1 X25.611 Y-98.872 Z0.889
G1 X25.671 Y-99.01 Z0.885
G1 X25.744 Y-99.141 Z0.881
G1 X25.829 Y-99.264 Z0.877
G1 X25.927 Y-99.379 Z0.873
G1 X26.035 Y-99.483 Z0.869
G1 X26.152 Y-99.577 Z0.865
G1 X26.279 Y-99.658 Z0.861
G1 X26.412 Y-99.726 Z0.857

(Linearized circular move - low memory mode enabled)
G1 X26.529 Y-99.774 Z0.854
G1 X26.65 Y-99.811 Z0.851
G1 X26.773 Y-99.838 Z0.848
G1 X26.897 Y-99.856 Z0.844
G1 X27.023 Y-99.862 Z0.841

(Linearized circular move - low memory mode enabled)
G1 X27.173 Y-99.857 Z0.837
G1 X27.322 Y-99.837 Z0.833
G1 X27.468 Y-99.802 Z0.829
G1 X27.61 Y-99.754 Z0.825
G1 X27.747 Y-99.691 Z0.821
G1 X27.876 Y-99.616 Z0.817
G1 X27.998 Y-99.528 Z0.813
G1 X28.11 Y-99.428 Z0.809
G1 X28.213 Y-99.319 Z0.805
G1 X28.304 Y-99.199 Z0.802
G1 X28.383 Y-99.071 Z0.798
G1 X28.448 Y-98.937 Z0.794
G1 X28.501 Y-98.796 Z0.79
G1 X28.539 Y-98.651 Z0.786
G1 X28.563 Y-98.503 Z0.782
G1 X28.573 Y-98.353 Z0.778

(Linearized circular move - low memory mode enabled)
G1 X28.569 Y-98.227 Z0.775
G1 X28.555 Y-98.102 Z0.771
G1 X28.531 Y-97.978 Z0.768
G1 X28.497 Y-97.857 Z0.765
G1 X28.453 Y-97.739 Z0.761

(Linearized circular move - low memory mode enabled)
G1 X28.388 Y-97.604 Z0.757
G1 X28.31 Y-97.476 Z0.753
G1 X28.22 Y-97.356 Z0.75
G1 X28.118 Y-97.245 Z0.746
G1 X28.007 Y-97.145 Z0.742
G1 X27.886 Y-97.057 Z0.738
G1 X27.757 Y-96.98 Z0.734
G1 X27.621 Y-96.917 Z0.73
G1 X27.479 Y-96.868 Z0.726
G1 X27.334 Y-96.832 Z0.722
G1 X27.185 Y-96.811 Z0.718
G1 X27.035 Y-96.804 Z0.714
G1 X26.886 Y-96.813 Z0.71
G1 X26.737 Y-96.836 Z0.706
G1 X26.592 Y-96.873 Z0.702
G1 X26.451 Y-96.924 Z0.698

(Linearized circular move - low memory mode enabled)
G1 X26.337 Y-96.978 Z0.695
G1 X26.228 Y-97.04 Z0.692
G1 X26.125 Y-97.112 Z0.688
G1 X26.028 Y-97.192 Z0.685
G1 X25.937 Y-97.279 Z0.682

(Linearized circular move - low memory mode enabled)
G1 X25.839 Y-97.393 Z0.678
G1 X25.753 Y-97.515 Z0.674
G1 X25.679 Y-97.646 Z0.67
G1 X25.619 Y-97.783 Z0.666
G1 X25.572 Y-97.925 Z0.662
G1 X25.539 Y-98.071 Z0.658
G1 X25.521 Y-98.22 Z0.654
G1 X25.517 Y-98.37 Z0.65
G1 X25.528 Y-98.519 Z0.646
G1 X25.554 Y-98.667 Z0.642
G1 X25.594 Y-98.811 Z0.638
G1 X25.648 Y-98.951 Z0.635
G1 X25.715 Y-99.085 Z0.631
G1 X25.796 Y-99.211 Z0.627
G1 X25.888 Y-99.329 Z0.623
G1 X25.991 Y-99.438 Z0.619

(Linearized circular move - low memory mode enabled)
G1 X26.086 Y-99.52 Z0.615
G1 X26.187 Y-99.595 Z0.612
G1 X26.294 Y-99.661 Z0.609
G1 X26.406 Y-99.719 Z0.606
G1 X26.522 Y-99.766 Z0.602

(Linearized circular move - low memory mode enabled)
G1 X26.665 Y-99.81 Z0.598
G1 X26.811 Y-99.84 Z0.594
G1 X26.96 Y-99.856 Z0.59
G1 X27.11 Y-99.857 Z0.587
G1 X27.259 Y-99.843 Z0.583
G1 X27.406 Y-99.814 Z0.579
G1 X27.55 Y-99.772 Z0.575
G1 X27.688 Y-99.715 Z0.571
G1 X27.82 Y-99.645 Z0.567
G1 X27.945 Y-99.563 Z0.563
G1 X28.062 Y-99.468 Z0.559
G1 X28.168 Y-99.363 Z0.555
G1 X28.263 Y-99.248 Z0.551
G1 X28.347 Y-99.124 Z0.547
G1 X28.419 Y-98.992 Z0.543
G1 X28.476 Y-98.854 Z0.539

(Linearized circular move - low memory mode enabled)
G1 X28.514 Y-98.734 Z0.536
G1 X28.543 Y-98.612 Z0.533
G1 X28.56 Y-98.488 Z0.529
G1 X28.568 Y-98.362 Z0.526
G1 X28.565 Y-98.237 Z0.523

(Linearized circular move - low memory mode enabled)
G1 X28.549 Y-98.088 Z0.519
G1 X28.517 Y-97.942 Z0.515
G1 X28.472 Y-97.799 Z0.511
G1 X28.413 Y-97.662 Z0.507
G1 X28.34 Y-97.531 Z0.503
G1 X28.256 Y-97.408 Z0.499
G1 X28.159 Y-97.294 Z0.495
G1 X28.052 Y-97.189 Z0.491
G1 X27.935 Y-97.096 Z0.487
G1 X27.81 Y-97.015 Z0.483
G1 X27.677 Y-96.946 Z0.479
G1 X27.538 Y-96.891 Z0.475
G1 X27.394 Y-96.849 Z0.472
G1 X27.247 Y-96.822 Z0.468
G1 X27.098 Y-96.81 Z0.464
G1 X26.948 Y-96.812 Z0.46

(Linearized circular move - low memory mode enabled)
G1 X26.824 Y-96.825 Z0.456
G1 X26.7 Y-96.848 Z0.453
G1 X26.579 Y-96.881 Z0.45
G1 X26.462 Y-96.925 Z0.446
G1 X26.348 Y-96.977 Z0.443

(Linearized circular move - low memory mode enabled)
G1 X26.218 Y-97.052 Z0.439
G1 X26.097 Y-97.139 Z0.435
G1 X25.985 Y-97.238 Z0.431
G1 X25.883 Y-97.347 Z0.427
G1 X25.792 Y-97.465 Z0.423
G1 X25.713 Y-97.592 Z0.42
G1 X25.647 Y-97.726 Z0.416
G1 X25.594 Y-97.866 Z0.412
G1 X25.555 Y-98.01 Z0.408
G1 X25.531 Y-98.158 Z0.404
G1 X25.521 Y-98.307 Z0.4
G1 X25.526 Y-98.456 Z0.396
G1 X25.546 Y-98.604 Z0.392
G1 X25.58 Y-98.75 Z0.388
G1 X25.628 Y-98.891 Z0.384
G1 X25.69 Y-99.027 Z0.38

(Linearized circular move - low memory mode enabled)
G1 X25.751 Y-99.136 Z0.377
G1 X25.822 Y-99.24 Z0.374
G1 X25.901 Y-99.337 Z0.37
G1 X25.987 Y-99.428 Z0.367
G1 X26.081 Y-99.511 Z0.364

(Linearized circular move - low memory mode enabled)
G1 X26.201 Y-99.6 Z0.36
G1 X26.33 Y-99.676 Z0.356
G1 X26.465 Y-99.74 Z0.352
G1 X26.605 Y-99.789 Z0.348
G1 X26.75 Y-99.825 Z0.344
G1 X26.898 Y-99.847 Z0.34
G1 X27.047 Y-99.854 Z0.336
G1 X27.196 Y-99.846 Z0.332
G1 X27.344 Y-99.824 Z0.328
G1 X27.489 Y-99.787 Z0.324
G1 X27.629 Y-99.736 Z0.32
G1 X27.764 Y-99.672 Z0.316
G1 X27.892 Y-99.595 Z0.312
G1 X28.011 Y-99.506 Z0.308
G1 X28.122 Y-99.405 Z0.305
G1 X28.221 Y-99.294 Z0.301

(Linearized circular move - low memory mode enabled)
G1 X28.297 Y-99.194 Z0.297
G1 X28.363 Y-99.088 Z0.294
G1 X28.421 Y-98.977 Z0.291
G1 X28.469 Y-98.861 Z0.287
G1 X28.508 Y-98.742 Z0.284

(Linearized circular move - low memory mode enabled)
G1 X28.541 Y-98.597 Z0.28
G1 X28.56 Y-98.449 Z0.276
G1 X28.564 Y-98.3 Z0.272
G1 X28.553 Y-98.151 Z0.268
G1 X28.528 Y-98.004 Z0.264
G1 X28.489 Y-97.86 Z0.26
G1 X28.435 Y-97.721 Z0.257
G1 X28.369 Y-97.587 Z0.253
G1 X28.289 Y-97.461 Z0.249
G1 X28.198 Y-97.343 Z0.245
G1 X28.095 Y-97.235 Z0.241
G1 X27.982 Y-97.137 Z0.237
G1 X27.861 Y-97.051 Z0.233
G1 X27.731 Y-96.977 Z0.229
G1 X27.595 Y-96.916 Z0.225
G1 X27.453 Y-96.869 Z0.221

(Linearized circular move - low memory mode enabled)
G1 X27.332 Y-96.841 Z0.218
G1 X27.208 Y-96.822 Z0.214
G1 X27.083 Y-96.814 Z0.211
G1 X26.958 Y-96.816 Z0.208
G1 X26.834 Y-96.828 Z0.205

(Linearized circular move - low memory mode enabled)
G1 X26.687 Y-96.856 Z0.201
G1 X26.544 Y-96.898 Z0.197
G1 X26.406 Y-96.954 Z0.193
G1 X26.274 Y-97.023 Z0.189
G1 X26.15 Y-97.105 Z0.185
G1 X26.034 Y-97.198 Z0.181
G1 X25.927 Y-97.303 Z0.177
G1 X25.832 Y-97.417 Z0.173
G1 X25.748 Y-97.54 Z0.169
G1 X25.677 Y-97.671 Z0.165
G1 X25.619 Y-97.808 Z0.161
G1 X25.574 Y-97.951 Z0.157
G1 X25.544 Y-98.096 Z0.153
G1 X25.528 Y-98.245 Z0.149
G1 X25.527 Y-98.394 Z0.145
G1 X25.54 Y-98.542 Z0.142

(Linearized circular move - low memory mode enabled)
G1 X25.563 Y-98.665 Z0.138
G1 X25.595 Y-98.786 Z0.135
G1 X25.637 Y-98.903 Z0.132
G1 X25.689 Y-99.017 Z0.128
G1 X25.75 Y-99.126 Z0.125

(Linearized circular move - low memory mode enabled)
G1 X25.834 Y-99.249 Z0.121
G1 X25.93 Y-99.363 Z0.117
G1 X26.036 Y-99.467 Z0.113
G1 X26.152 Y-99.56 Z0.109
G1 X26.277 Y-99.642 Z0.105
G1 X26.409 Y-99.711 Z0.101
G1 X26.547 Y-99.766 Z0.097
G1 X26.69 Y-99.808 Z0.093
G1 X26.836 Y-99.835 Z0.09
G1 X26.985 Y-99.848 Z0.086
G1 X27.134 Y-99.847 Z0.082
G1 X27.282 Y-99.83 Z0.078
G1 X27.427 Y-99.8 Z0.074
G1 X27.569 Y-99.755 Z0.07
G1 X27.706 Y-99.697 Z0.066
G1 X27.837 Y-99.625 Z0.062

(Linearized circular move - low memory mode enabled)
G1 X27.94 Y-99.555 Z0.059
G1 X28.038 Y-99.477 Z0.055
G1 X28.129 Y-99.392 Z0.052
G1 X28.212 Y-99.299 Z0.049
G1 X28.288 Y-99.199 Z0.045

(Linearized circular move - low memory mode enabled)
G1 X28.367 Y-99.073 Z0.042
G1 X28.433 Y-98.94 Z0.038
G1 X28.486 Y-98.801 Z0.034
G1 X28.524 Y-98.657 Z0.03
G1 X28.549 Y-98.511 Z0.026
G1 X28.559 Y-98.362 Z0.022
G1 X28.555 Y-98.214 Z0.018
G1 X28.536 Y-98.066 Z0.014
G1 X28.502 Y-97.921 Z0.01
G1 X28.455 Y-97.78 Z0.006
G1 X28.394 Y-97.645 Z0.002
G1 X28.32 Y-97.515 Z-0.002
G1 X28.234 Y-97.394 Z-0.006
G1 X28.136 Y-97.282 Z-0.01
G1 X28.028 Y-97.18 Z-0.014
G1 X27.91 Y-97.089 Z-0.018

(Linearized circular move - low memory mode enabled)
G1 X27.805 Y-97.022 Z-0.021
G1 X27.694 Y-96.964 Z-0.024
G1 X27.58 Y-96.915 Z-0.027
G1 X27.461 Y-96.876 Z-0.031
G1 X27.34 Y-96.847 Z-0.034

(Linearized circular move - low memory mode enabled)
G1 X27.193 Y-96.825 Z-0.038
G1 X27.045 Y-96.817 Z-0.042
G1 X26.896 Y-96.825 Z-0.046
G1 X26.749 Y-96.847 Z-0.05
G1 X26.605 Y-96.883 Z-0.054
G1 X26.465 Y-96.933 Z-0.058
G1 X26.331 Y-96.996 Z-0.062
G1 X26.203 Y-97.072 Z-0.066
G1 X26.084 Y-97.161 Z-0.07
G1 X25.974 Y-97.261 Z-0.073
G1 X25.874 Y-97.371 Z-0.077
G1 X25.785 Y-97.49 Z-0.081
G1 X25.709 Y-97.618 Z-0.085
G1 X25.645 Y-97.752 Z-0.089
G1 X25.595 Y-97.892 Z-0.093
G1 X25.559 Y-98.036 Z-0.097

(Linearized circular move - low memory mode enabled)
G1 X25.54 Y-98.159 Z-0.1
G1 X25.531 Y-98.283 Z-0.104
G1 X25.532 Y-98.408 Z-0.107
G1 X25.543 Y-98.532 Z-0.11
G1 X25.565 Y-98.655 Z-0.114

(Linearized circular move - low memory mode enabled)
G1 X25.604 Y-98.798 Z-0.118
G1 X25.656 Y-98.937 Z-0.122
G1 X25.722 Y-99.07 Z-0.125
G1 X25.801 Y-99.196 Z-0.129
G1 X25.892 Y-99.313 Z-0.133
G1 X25.993 Y-99.422 Z-0.137
G1 X26.105 Y-99.519 Z-0.141
G1 X26.226 Y-99.605 Z-0.145
G1 X26.355 Y-99.679 Z-0.149
G1 X26.49 Y-99.74 Z-0.153
G1 X26.631 Y-99.788 Z-0.157
G1 X26.776 Y-99.821 Z-0.161
G1 X26.923 Y-99.84 Z-0.165
G1 X27.071 Y-99.845 Z-0.169
G1 X27.219 Y-99.834 Z-0.173
G1 X27.366 Y-99.81 Z-0.177

(Linearized circular move - low memory mode enabled)
G1 X27.486 Y-99.778 Z-0.18
G1 X27.604 Y-99.737 Z-0.183
G1 X27.717 Y-99.686 Z-0.187
G1 X27.826 Y-99.626 Z-0.19
G1 X27.93 Y-99.557 Z-0.193

(Linearized circular move - low memory mode enabled)
G1 X28.046 Y-99.464 Z-0.197
G1 X28.152 Y-99.361 Z-0.201
G1 X28.247 Y-99.247 Z-0.205
G1 X28.331 Y-99.125 Z-0.209
G1 X28.403 Y-98.995 Z-0.213
G1 X28.461 Y-98.858 Z-0.217
G1 X28.506 Y-98.717 Z-0.221
G1 X28.536 Y-98.572 Z-0.225
G1 X28.552 Y-98.424 Z-0.229
G1 X28.554 Y-98.276 Z-0.233
G1 X28.541 Y-98.128 Z-0.237
G1 X28.514 Y-97.983 Z-0.24
G1 X28.472 Y-97.84 Z-0.244
G1 X28.417 Y-97.702 Z-0.248
G1 X28.349 Y-97.571 Z-0.252
G1 X28.268 Y-97.447 Z-0.256

(Linearized circular move - low memory mode enabled)
G1 X28.191 Y-97.349 Z-0.26
G1 X28.106 Y-97.258 Z-0.263
G1 X28.014 Y-97.174 Z-0.266
G1 X27.915 Y-97.098 Z-0.269
G1 X27.811 Y-97.031 Z-0.273

(Linearized circular move - low memory mode enabled)
G1 X27.68 Y-96.962 Z-0.277
G1 X27.542 Y-96.906 Z-0.281
G1 X27.4 Y-96.864 Z-0.285
G1 X27.254 Y-96.837 Z-0.288
G1 X27.107 Y-96.823 Z-0.292
G1 X26.959 Y-96.824 Z-0.296
G1 X26.811 Y-96.84 Z-0.3
G1 X26.666 Y-96.87 Z-0.304
G1 X26.525 Y-96.914 Z-0.308
G1 X26.388 Y-96.972 Z-0.312
G1 X26.258 Y-97.043 Z-0.316
G1 X26.136 Y-97.126 Z-0.32
G1 X26.022 Y-97.221 Z-0.324
G1 X25.918 Y-97.326 Z-0.328
G1 X25.825 Y-97.442 Z-0.332
G1 X25.743 Y-97.566 Z-0.336

(Linearized circular move - low memory mode enabled)
G1 X25.671 Y-97.703 Z-0.34
G1 X25.614 Y-97.848 Z-0.344
G1 X25.572 Y-97.997 Z-0.348
G1 X25.545 Y-98.15 Z-0.352

(Linearized circular move - low memory mode enabled)
G1 X25.535 Y-98.298 Z-0.356
G1 X25.539 Y-98.446 Z-0.36
G1 X25.557 Y-98.593 Z-0.364
G1 X25.59 Y-98.737 Z-0.368
G1 X25.637 Y-98.878 Z-0.372
G1 X25.697 Y-99.013 Z-0.376
G1 X25.77 Y-99.142 Z-0.38
G1 X25.856 Y-99.262 Z-0.384
G1 X25.953 Y-99.374 Z-0.388
G1 X26.06 Y-99.476 Z-0.392
G1 X26.177 Y-99.567 Z-0.396
G1 X26.302 Y-99.646 Z-0.4
G1 X26.435 Y-99.712 Z-0.403
G1 X26.573 Y-99.765 Z-0.407
G1 X26.716 Y-99.804 Z-0.411
G1 X26.862 Y-99.829 Z-0.415

(Linearized circular move - low memory mode enabled)
G1 X27.016 Y-99.84 Z-0.419
G1 X27.171 Y-99.835 Z-0.424
G1 X27.325 Y-99.814 Z-0.428
G1 X27.476 Y-99.777 Z-0.432

(Linearized circular move - low memory mode enabled)
G1 X27.615 Y-99.728 Z-0.436
G1 X27.749 Y-99.665 Z-0.44
G1 X27.877 Y-99.589 Z-0.444
G1 X27.996 Y-99.501 Z-0.448
G1 X28.106 Y-99.402 Z-0.452
G1 X28.205 Y-99.293 Z-0.455
G1 X28.294 Y-99.175 Z-0.459
G1 X28.37 Y-99.048 Z-0.463
G1 X28.434 Y-98.915 Z-0.467
G1 X28.484 Y-98.776 Z-0.471
G1 X28.521 Y-98.632 Z-0.475
G1 X28.543 Y-98.486 Z-0.479
G1 X28.551 Y-98.338 Z-0.483
G1 X28.544 Y-98.19 Z-0.487
G1 X28.523 Y-98.044 Z-0.491
G1 X28.487 Y-97.9 Z-0.495

(Linearized circular move - low memory mode enabled)
G1 X28.435 Y-97.754 Z-0.499
G1 X28.368 Y-97.614 Z-0.503
G1 X28.287 Y-97.482 Z-0.507
G1 X28.193 Y-97.359 Z-0.511

(Linearized circular move - low memory mode enabled)
G1 X28.093 Y-97.251 Z-0.515
G1 X27.981 Y-97.153 Z-0.519
G1 X27.861 Y-97.067 Z-0.523
G1 X27.733 Y-96.993 Z-0.527
G1 X27.599 Y-96.932 Z-0.531
G1 X27.459 Y-96.884 Z-0.535
G1 X27.315 Y-96.851 Z-0.539
G1 X27.169 Y-96.831 Z-0.543
G1 X27.021 Y-96.826 Z-0.547
G1 X26.873 Y-96.836 Z-0.551
G1 X26.728 Y-96.86 Z-0.555
G1 X26.585 Y-96.898 Z-0.559
G1 X26.447 Y-96.95 Z-0.563
G1 X26.314 Y-97.015 Z-0.567
G1 X26.188 Y-97.093 Z-0.57
G1 X26.071 Y-97.183 Z-0.574

(Linearized circular move - low memory mode enabled)
G1 X25.958 Y-97.289 Z-0.579
G1 X25.857 Y-97.406 Z-0.583
G1 X25.768 Y-97.533 Z-0.587
G1 X25.693 Y-97.669 Z-0.591

(Linearized circular move - low memory mode enabled)
G1 X25.634 Y-97.804 Z-0.595
G1 X25.589 Y-97.945 Z-0.599
G1 X25.558 Y-98.089 Z-0.603
G1 X25.542 Y-98.236 Z-0.607
G1 X25.54 Y-98.384 Z-0.611
G1 X25.552 Y-98.531 Z-0.615
G1 X25.579 Y-98.676 Z-0.618
G1 X25.619 Y-98.818 Z-0.622
G1 X25.674 Y-98.955 Z-0.626
G1 X25.742 Y-99.086 Z-0.63
G1 X25.822 Y-99.21 Z-0.634
G1 X25.914 Y-99.326 Z-0.638
G1 X26.017 Y-99.432 Z-0.642
G1 X26.13 Y-99.527 Z-0.646
G1 X26.251 Y-99.611 Z-0.65
G1 X26.38 Y-99.682 Z-0.654

(Linearized circular move - low memory mode enabled)
G1 X26.523 Y-99.743 Z-0.658
G1 X26.67 Y-99.789 Z-0.662
G1 X26.822 Y-99.82 Z-0.666
G1 X26.976 Y-99.835 Z-0.67

(Linearized circular move - low memory mode enabled)
G1 X27.124 Y-99.834 Z-0.674
G1 X27.27 Y-99.819 Z-0.678
G1 X27.415 Y-99.789 Z-0.682
G1 X27.556 Y-99.746 Z-0.686
G1 X27.692 Y-99.689 Z-0.69
G1 X27.822 Y-99.619 Z-0.694
G1 X27.944 Y-99.536 Z-0.698
G1 X28.058 Y-99.442 Z-0.702
G1 X28.161 Y-99.337 Z-0.706
G1 X28.255 Y-99.223 Z-0.71
G1 X28.336 Y-99.1 Z-0.714
G1 X28.405 Y-98.969 Z-0.718
G1 X28.461 Y-98.833 Z-0.722
G1 X28.503 Y-98.692 Z-0.726
G1 X28.531 Y-98.547 Z-0.73
G1 X28.545 Y-98.4 Z-0.733

(Linearized circular move - low memory mode enabled)
G1 X28.544 Y-98.245 Z-0.738
G1 X28.527 Y-98.092 Z-0.742
G1 X28.494 Y-97.94 Z-0.746
G1 X28.446 Y-97.793 Z-0.75

(Linearized circular move - low memory mode enabled)
G1 X28.386 Y-97.659 Z-0.754
G1 X28.314 Y-97.53 Z-0.758
G1 X28.229 Y-97.41 Z-0.762
G1 X28.133 Y-97.298 Z-0.766
G1 X28.026 Y-97.196 Z-0.77
G1 X27.91 Y-97.106 Z-0.774
G1 X27.786 Y-97.027 Z-0.778
G1 X27.654 Y-96.96 Z-0.782
G1 X27.517 Y-96.907 Z-0.785
G1 X27.375 Y-96.868 Z-0.789
G1 X27.23 Y-96.842 Z-0.793
G1 X27.083 Y-96.831 Z-0.797
G1 X26.935 Y-96.835 Z-0.801
G1 X26.789 Y-96.853 Z-0.805
G1 X26.645 Y-96.885 Z-0.809
G1 X26.505 Y-96.931 Z-0.813

(Linearized circular move - low memory mode enabled)
G1 X26.364 Y-96.994 Z-0.817
G1 X26.23 Y-97.071 Z-0.821
G1 X26.105 Y-97.161 Z-0.825
G1 X25.99 Y-97.264 Z-0.83

(Linearized circular move - low memory mode enabled)
G1 X25.89 Y-97.372 Z-0.833
G1 X25.801 Y-97.49 Z-0.837
G1 X25.725 Y-97.616 Z-0.841
G1 X25.661 Y-97.748 Z-0.845
G1 X25.611 Y-97.887 Z-0.849
G1 X25.574 Y-98.029 Z-0.853
G1 X25.551 Y-98.175 Z-0.857
G1 X25.543 Y-98.322 Z-0.861
G1 X25.549 Y-98.469 Z-0.865
G1 X25.57 Y-98.615 Z-0.869
G1 X25.605 Y-98.758 Z-0.873
G1 X25.654 Y-98.897 Z-0.877
G1 X25.716 Y-99.03 Z-0.881
G1 X25.791 Y-99.157 Z-0.885
G1 X25.878 Y-99.276 Z-0.889
G1 X25.976 Y-99.385 Z-0.893

(Linearized circular move - low memory mode enabled)
G1 X26.09 Y-99.49 Z-0.897
G1 X26.214 Y-99.581 Z-0.901
G1 X26.346 Y-99.66 Z-0.905
G1 X26.487 Y-99.725 Z-0.909

(Linearized circular move - low memory mode enabled)
G1 X26.626 Y-99.772 Z-0.913
G1 X26.769 Y-99.806 Z-0.917
G1 X26.915 Y-99.826 Z-0.921
G1 X27.062 Y-99.832 Z-0.925
G1 X27.209 Y-99.823 Z-0.929
G1 X27.354 Y-99.799 Z-0.933
G1 X27.496 Y-99.762 Z-0.937
G1 X27.634 Y-99.71 Z-0.941
G1 X27.766 Y-99.646 Z-0.945
G1 X27.891 Y-99.569 Z-0.948
G1 X28.008 Y-99.479 Z-0.952
G1 X28.116 Y-99.379 Z-0.956
G1 X28.213 Y-99.269 Z-0.96
G1 X28.3 Y-99.15 Z-0.964
G1 X28.374 Y-99.023 Z-0.968
G1 X28.435 Y-98.889 Z-0.972

(Linearized circular move - low memory mode enabled)
G1 X28.485 Y-98.743 Z-0.976
G1 X28.519 Y-98.593 Z-0.98
G1 X28.538 Y-98.44 Z-0.985
G1 X28.541 Y-98.286 Z-0.989

(Linearized circular move - low memory mode enabled)
G1 X28.529 Y-98.139 Z-0.993
G1 X28.503 Y-97.994 Z-0.997
G1 X28.463 Y-97.853 Z-1.0
G1 X28.409 Y-97.716 Z-1.004
G1 X28.342 Y-97.586 Z-1.008
G1 X28.263 Y-97.462 Z-1.012
G1 X28.171 Y-97.347 Z-1.016
G1 X28.069 Y-97.241 Z-1.02
G1 X27.957 Y-97.146 Z-1.024
G1 X27.837 Y-97.062 Z-1.028
G1 X27.708 Y-96.99 Z-1.032
G1 X27.574 Y-96.932 Z-1.036
G1 X27.434 Y-96.887 Z-1.04
G1 X27.29 Y-96.855 Z-1.044
G1 X27.144 Y-96.838 Z-1.048
G1 X26.997 Y-96.836 Z-1.052

(Linearized circular move - low memory mode enabled)
G1 X26.844 Y-96.849 Z-1.056
G1 X26.692 Y-96.877 Z-1.06
G1 X26.545 Y-96.921 Z-1.064
G1 X26.402 Y-96.98 Z-1.068

(Linearized circular move - low memory mode enabled)
G1 X26.273 Y-97.049 Z-1.072
G1 X26.151 Y-97.131 Z-1.076
G1 X26.037 Y-97.224 Z-1.08
G1 X25.934 Y-97.328 Z-1.084
G1 X25.841 Y-97.442 Z-1.088
G1 X25.759 Y-97.564 Z-1.092
G1 X25.69 Y-97.694 Z-1.096
G1 X25.634 Y-97.829 Z-1.1
G1 X25.592 Y-97.97 Z-1.104
G1 X25.563 Y-98.114 Z-1.108
G1 X25.549 Y-98.26 Z-1.112
G1 Y-98.407 Z-1.115
G1 X25.564 Y-98.553 Z-1.119
G1 X25.593 Y-98.697 Z-1.123
G1 X25.636 Y-98.838 Z-1.127
G1 X25.692 Y-98.973 Z-1.131

(Linearized circular move - low memory mode enabled)
G1 X25.765 Y-99.109 Z-1.135
G1 X25.852 Y-99.236 Z-1.139
G1 X25.951 Y-99.354 Z-1.144
G1 X26.062 Y-99.46 Z-1.148

(Linearized circular move - low memory mode enabled)
G1 X26.177 Y-99.551 Z-1.152
G1 X26.301 Y-99.63 Z-1.156
G1 X26.431 Y-99.697 Z-1.16
G1 X26.568 Y-99.75 Z-1.163
G1 X26.709 Y-99.79 Z-1.167
G1 X26.854 Y-99.815 Z-1.171
G1 X27.0 Y-99.827 Z-1.175
G1 X27.147 Y-99.824 Z-1.179
G1 X27.292 Y-99.806 Z-1.183
G1 X27.436 Y-99.775 Z-1.187
G1 X27.575 Y-99.729 Z-1.191
G1 X27.709 Y-99.67 Z-1.195
G1 X27.837 Y-99.599 Z-1.199
G1 X27.958 Y-99.515 Z-1.203
G1 X28.069 Y-99.419 Z-1.207
G1 X28.171 Y-99.314 Z-1.211

(Linearized circular move - low memory mode enabled)
G1 X28.266 Y-99.192 Z-1.215
G1 X28.347 Y-99.062 Z-1.219
G1 X28.415 Y-98.924 Z-1.223
G1 X28.469 Y-98.78 Z-1.227

(Linearized circular move - low memory mode enabled)
G1 X28.506 Y-98.638 Z-1.231
G1 X28.529 Y-98.494 Z-1.235
G1 X28.537 Y-98.347 Z-1.239
G1 X28.532 Y-98.201 Z-1.243
G1 X28.512 Y-98.056 Z-1.247
G1 X28.477 Y-97.913 Z-1.251
G1 X28.429 Y-97.775 Z-1.255
G1 X28.368 Y-97.642 Z-1.259
G1 X28.294 Y-97.515 Z-1.263
G1 X28.208 Y-97.397 Z-1.267
G1 X28.11 Y-97.287 Z-1.271
G1 X28.003 Y-97.188 Z-1.275
G1 X27.886 Y-97.099 Z-1.278
G1 X27.761 Y-97.023 Z-1.282
G1 X27.629 Y-96.959 Z-1.286
G1 X27.491 Y-96.908 Z-1.29

(Linearized circular move - low memory mode enabled)
G1 X27.343 Y-96.87 Z-1.294
G1 X27.191 Y-96.847 Z-1.299
G1 X27.037 Y-96.84 Z-1.303
G1 X26.884 Y-96.848 Z-1.307

(Linearized circular move - low memory mode enabled)
G1 X26.739 Y-96.871 Z-1.311
G1 X26.598 Y-96.908 Z-1.315
G1 X26.46 Y-96.959 Z-1.319
G1 X26.328 Y-97.022 Z-1.323
G1 X26.204 Y-97.099 Z-1.327
G1 X26.087 Y-97.187 Z-1.33
G1 X25.979 Y-97.286 Z-1.334
G1 X25.882 Y-97.396 Z-1.338
G1 X25.796 Y-97.514 Z-1.342
G1 X25.722 Y-97.641 Z-1.346
G1 X25.66 Y-97.774 Z-1.35
G1 X25.612 Y-97.912 Z-1.354
G1 X25.578 Y-98.054 Z-1.358
G1 X25.558 Y-98.199 Z-1.362
G1 X25.552 Y-98.346 Z-1.366
G1 X25.561 Y-98.492 Z-1.37

(Linearized circular move - low memory mode enabled)
G1 X25.585 Y-98.643 Z-1.374
G1 X25.625 Y-98.792 Z-1.378
G1 X25.679 Y-98.935 Z-1.382
G1 X25.749 Y-99.072 Z-1.386

(Linearized circular move - low memory mode enabled)
G1 X25.827 Y-99.195 Z-1.39
G1 X25.918 Y-99.31 Z-1.394
G1 X26.019 Y-99.416 Z-1.398
G1 X26.13 Y-99.511 Z-1.402
G1 X26.25 Y-99.595 Z-1.406
G1 X26.378 Y-99.666 Z-1.41
G1 X26.512 Y-99.725 Z-1.414
G1 X26.651 Y-99.77 Z-1.418
G1 X26.794 Y-99.802 Z-1.422
G1 X26.939 Y-99.819 Z-1.426
G1 X27.085 Y-99.822 Z-1.43
G1 X27.231 Y-99.811 Z-1.434
G1 X27.375 Y-99.785 Z-1.438
G1 X27.516 Y-99.746 Z-1.442
G1 X27.652 Y-99.693 Z-1.445
G1 X27.782 Y-99.626 Z-1.449

(Linearized circular move - low memory mode enabled)
G1 X27.912 Y-99.544 Z-1.454
G1 X28.031 Y-99.448 Z-1.458
G1 X28.141 Y-99.34 Z-1.462
G1 X28.239 Y-99.222 Z-1.466

(Linearized circular move - low memory mode enabled)
G1 X28.32 Y-99.101 Z-1.47
G1 X28.389 Y-98.972 Z-1.474
G1 X28.445 Y-98.837 Z-1.478
G1 X28.488 Y-98.697 Z-1.482
G1 X28.517 Y-98.554 Z-1.486
G1 X28.531 Y-98.409 Z-1.49
G1 Y-98.263 Z-1.493
G1 X28.517 Y-98.117 Z-1.497
G1 X28.489 Y-97.974 Z-1.501
G1 X28.447 Y-97.834 Z-1.505
G1 X28.391 Y-97.699 Z-1.509
G1 X28.323 Y-97.57 Z-1.513
G1 X28.242 Y-97.448 Z-1.517
G1 X28.149 Y-97.335 Z-1.521
G1 X28.046 Y-97.231 Z-1.525
G1 X27.933 Y-97.138 Z-1.529

(Linearized circular move - low memory mode enabled)
G1 X27.806 Y-97.053 Z-1.533
G1 X27.67 Y-96.982 Z-1.537
G1 X27.528 Y-96.925 Z-1.541
G1 X27.381 Y-96.882 Z-1.545

(Linearized circular move - low memory mode enabled)
G1 X27.237 Y-96.856 Z-1.549
G1 X27.091 Y-96.845 Z-1.553
G1 X26.946 Y-96.847 Z-1.557
G1 X26.8 Y-96.864 Z-1.561
G1 X26.658 Y-96.895 Z-1.565
G1 X26.519 Y-96.94 Z-1.569
G1 X26.385 Y-96.998 Z-1.573
G1 X26.257 Y-97.069 Z-1.577
G1 X26.137 Y-97.152 Z-1.581
G1 X26.026 Y-97.247 Z-1.585
G1 X25.925 Y-97.352 Z-1.589
G1 X25.834 Y-97.466 Z-1.593
G1 X25.755 Y-97.589 Z-1.597
G1 X25.688 Y-97.719 Z-1.601
G1 X25.635 Y-97.855 Z-1.605
G1 X25.595 Y-97.995 Z-1.608

(Linearized circular move - low memory mode enabled)
G1 X25.568 Y-98.146 Z-1.613
G1 X25.557 Y-98.299 Z-1.617
G1 X25.561 Y-98.452 Z-1.621
G1 X25.581 Y-98.603 Z-1.625

(Linearized circular move - low memory mode enabled)
G1 X25.615 Y-98.745 Z-1.629
G1 X25.662 Y-98.883 Z-1.633
G1 X25.723 Y-99.016 Z-1.637
G1 X25.796 Y-99.142 Z-1.641
G1 X25.882 Y-99.26 Z-1.645
G1 X25.978 Y-99.37 Z-1.649
G1 X26.085 Y-99.469 Z-1.653
G1 X26.201 Y-99.557 Z-1.657
G1 X26.325 Y-99.634 Z-1.66
G1 X26.457 Y-99.698 Z-1.664
G1 X26.593 Y-99.749 Z-1.668
G1 X26.734 Y-99.786 Z-1.672
G1 X26.878 Y-99.809 Z-1.676
G1 X27.024 Y-99.818 Z-1.68
G1 X27.17 Y-99.813 Z-1.684
G1 X27.314 Y-99.793 Z-1.688

(Linearized circular move - low memory mode enabled)
G1 X27.463 Y-99.758 Z-1.692
G1 X27.607 Y-99.707 Z-1.696
G1 X27.746 Y-99.642 Z-1.7
G1 X27.876 Y-99.563 Z-1.705

(Linearized circular move - low memory mode enabled)
G1 X27.993 Y-99.475 Z-1.708
G1 X28.1 Y-99.377 Z-1.712
G1 X28.197 Y-99.268 Z-1.716
G1 X28.284 Y-99.15 Z-1.72
G1 X28.358 Y-99.025 Z-1.724
G1 X28.419 Y-98.893 Z-1.728
G1 X28.467 Y-98.755 Z-1.732
G1 X28.502 Y-98.614 Z-1.736
G1 X28.522 Y-98.469 Z-1.74
G1 X28.529 Y-98.324 Z-1.744
G1 X28.521 Y-98.178 Z-1.748
G1 X28.498 Y-98.034 Z-1.752
G1 X28.462 Y-97.893 Z-1.756
G1 X28.412 Y-97.756 Z-1.76
G1 X28.349 Y-97.625 Z-1.764
G1 X28.273 Y-97.5 Z-1.768

(Linearized circular move - low memory mode enabled)
G1 X28.181 Y-97.378 Z-1.772
G1 X28.077 Y-97.267 Z-1.776
G1 X27.962 Y-97.166 Z-1.78
G1 X27.837 Y-97.078 Z-1.784

(Linearized circular move - low memory mode enabled)
G1 X27.711 Y-97.006 Z-1.788
G1 X27.577 Y-96.947 Z-1.792
G1 X27.439 Y-96.902 Z-1.796
G1 X27.297 Y-96.87 Z-1.8
G1 X27.152 Y-96.852 Z-1.804
G1 X27.007 Y-96.849 Z-1.808
G1 X26.862 Y-96.86 Z-1.812
G1 X26.718 Y-96.885 Z-1.816
G1 X26.578 Y-96.923 Z-1.82
G1 X26.442 Y-96.976 Z-1.823
G1 X26.312 Y-97.042 Z-1.827
G1 X26.189 Y-97.12 Z-1.831
G1 X26.074 Y-97.209 Z-1.835
G1 X25.969 Y-97.31 Z-1.839
G1 X25.874 Y-97.42 Z-1.843
G1 X25.79 Y-97.539 Z-1.847

(Linearized circular move - low memory mode enabled)
G1 X25.716 Y-97.672 Z-1.851
G1 X25.655 Y-97.812 Z-1.855
G1 X25.609 Y-97.958 Z-1.86
G1 X25.578 Y-98.107 Z-1.864

(Linearized circular move - low memory mode enabled)
G1 X25.563 Y-98.252 Z-1.868
G1 X25.562 Y-98.397 Z-1.872
G1 X25.576 Y-98.542 Z-1.875
G1 X25.603 Y-98.685 Z-1.879
G1 X25.645 Y-98.824 Z-1.883
G1 X25.7 Y-98.959 Z-1.887
G1 X25.768 Y-99.088 Z-1.891
G1 X25.848 Y-99.209 Z-1.895
G1 X25.94 Y-99.322 Z-1.899
G1 X26.042 Y-99.425 Z-1.903
G1 X26.154 Y-99.518 Z-1.907
G1 X26.275 Y-99.599 Z-1.911
G1 X26.403 Y-99.669 Z-1.915
G1 X26.537 Y-99.725 Z-1.919
G1 X26.676 Y-99.768 Z-1.923
G1 X26.818 Y-99.797 Z-1.927

(Linearized circular move - low memory mode enabled)
G1 X26.97 Y-99.812 Z-1.931
G1 X27.123 Z-1.935
G1 X27.274 Y-99.796 Z-1.939
G1 X27.424 Y-99.764 Z-1.943

(Linearized circular move - low memory mode enabled)
G1 X27.562 Y-99.72 Z-1.947
G1 X27.695 Y-99.663 Z-1.951
G1 X27.823 Y-99.592 Z-1.955
G1 X27.942 Y-99.51 Z-1.959
G1 X28.053 Y-99.416 Z-1.963
G1 X28.155 Y-99.312 Z-1.967
G1 X28.245 Y-99.198 Z-1.971
G1 X28.324 Y-99.076 Z-1.975
G1 X28.391 Y-98.947 Z-1.979
G1 X28.445 Y-98.812 Z-1.983
G1 X28.485 Y-98.672 Z-1.987
G1 X28.511 Y-98.53 Z-1.99
G1 X28.523 Y-98.385 Z-1.994
G1 X28.521 Y-98.239 Z-1.998
G1 X28.505 Y-98.095 Z-2.002
G1 X28.475 Y-97.953 Z-2.006

(Linearized circular move - low memory mode enabled)
G1 X28.428 Y-97.808 Z-2.01
G1 X28.367 Y-97.668 Z-2.014
G1 X28.291 Y-97.536 Z-2.019
G1 X28.203 Y-97.412 Z-2.023

(Linearized circular move - low memory mode enabled)
G1 X28.107 Y-97.303 Z-2.027
G1 X28.001 Y-97.204 Z-2.031
G1 X27.886 Y-97.115 Z-2.035
G1 X27.763 Y-97.039 Z-2.038
G1 X27.632 Y-96.974 Z-2.042
G1 X27.496 Y-96.923 Z-2.046
G1 X27.356 Y-96.886 Z-2.05
G1 X27.213 Y-96.862 Z-2.054
G1 X27.068 Y-96.853 Z-2.058
G1 X26.923 Y-96.858 Z-2.062
G1 X26.779 Y-96.877 Z-2.066
G1 X26.638 Y-96.91 Z-2.07
G1 X26.5 Y-96.957 Z-2.074
G1 X26.368 Y-97.016 Z-2.078
G1 X26.242 Y-97.089 Z-2.082
G1 X26.124 Y-97.174 Z-2.086

(Linearized circular move - low memory mode enabled)
G1 X26.01 Y-97.274 Z-2.09
G1 X25.907 Y-97.386 Z-2.094
G1 X25.816 Y-97.508 Z-2.098
G1 X25.737 Y-97.639 Z-2.102

(Linearized circular move - low memory mode enabled)
G1 X25.676 Y-97.77 Z-2.106
G1 X25.627 Y-97.907 Z-2.11
G1 X25.593 Y-98.048 Z-2.114
G1 X25.572 Y-98.191 Z-2.118
G1 X25.565 Y-98.336 Z-2.122
G1 X25.573 Y-98.481 Z-2.126
G1 X25.594 Y-98.624 Z-2.13
G1 X25.63 Y-98.765 Z-2.134
G1 X25.679 Y-98.901 Z-2.138
G1 X25.742 Y-99.032 Z-2.142
G1 X25.817 Y-99.157 Z-2.146
G1 X25.904 Y-99.273 Z-2.15
G1 X26.001 Y-99.38 Z-2.153
G1 X26.109 Y-99.477 Z-2.157
G1 X26.226 Y-99.563 Z-2.161
G1 X26.351 Y-99.637 Z-2.165

(Linearized circular move - low memory mode enabled)
G1 X26.488 Y-99.702 Z-2.169
G1 X26.632 Y-99.751 Z-2.174
G1 X26.78 Y-99.786 Z-2.178
G1 X26.931 Y-99.805 Z-2.182

(Linearized circular move - low memory mode enabled)
G1 X27.076 Y-99.809 Z-2.186
G1 X27.22 Y-99.799 Z-2.19
G1 X27.363 Y-99.775 Z-2.194
G1 X27.503 Y-99.736 Z-2.198
G1 X27.638 Y-99.684 Z-2.202
G1 X27.768 Y-99.62 Z-2.205
G1 X27.891 Y-99.542 Z-2.209
G1 X28.005 Y-99.453 Z-2.213
G1 X28.11 Y-99.354 Z-2.217
G1 X28.205 Y-99.244 Z-2.221
G1 X28.289 Y-99.126 Z-2.225
G1 X28.361 Y-99.0 Z-2.229
G1 X28.42 Y-98.868 Z-2.233
G1 X28.465 Y-98.73 Z-2.237
G1 X28.498 Y-98.589 Z-2.241
G1 X28.516 Y-98.445 Z-2.245

(Linearized circular move - low memory mode enabled)
G1 X28.519 Y-98.293 Z-2.249
G1 X28.507 Y-98.142 Z-2.253
G1 X28.48 Y-97.992 Z-2.257
G1 X28.437 Y-97.847 Z-2.261

(Linearized circular move - low memory mode enabled)
G1 X28.383 Y-97.712 Z-2.265
G1 X28.316 Y-97.584 Z-2.269
G1 X28.236 Y-97.463 Z-2.273
G1 X28.145 Y-97.35 Z-2.277
G1 X28.044 Y-97.247 Z-2.281
G1 X27.933 Y-97.155 Z-2.285
G1 X27.813 Y-97.073 Z-2.289
G1 X27.686 Y-97.004 Z-2.293
G1 X27.552 Y-96.947 Z-2.297
G1 X27.414 Y-96.904 Z-2.301
G1 X27.272 Y-96.875 Z-2.305
G1 X27.129 Y-96.86 Z-2.309
G1 X26.984 Y-96.858 Z-2.313
G1 X26.84 Y-96.871 Z-2.317
G1 X26.697 Y-96.899 Z-2.32
G1 X26.558 Y-96.939 Z-2.324

(Linearized circular move - low memory mode enabled)
G1 X26.418 Y-96.997 Z-2.329
G1 X26.284 Y-97.068 Z-2.333
G1 X26.158 Y-97.153 Z-2.337
G1 X26.042 Y-97.25 Z-2.341

(Linearized circular move - low memory mode enabled)
G1 X25.94 Y-97.354 Z-2.345
G1 X25.85 Y-97.467 Z-2.349
G1 X25.771 Y-97.588 Z-2.353
G1 X25.704 Y-97.716 Z-2.357
G1 X25.65 Y-97.85 Z-2.361
G1 X25.61 Y-97.989 Z-2.365
G1 X25.583 Y-98.131 Z-2.368
G1 X25.571 Y-98.275 Z-2.372
G1 X25.572 Y-98.42 Z-2.376
G1 X25.588 Y-98.564 Z-2.38
G1 X25.618 Y-98.705 Z-2.384
G1 X25.661 Y-98.843 Z-2.388
G1 X25.718 Y-98.976 Z-2.392
G1 X25.788 Y-99.103 Z-2.396
G1 X25.869 Y-99.223 Z-2.4
G1 X25.962 Y-99.333 Z-2.404

(Linearized circular move - low memory mode enabled)
G1 X26.071 Y-99.439 Z-2.408
G1 X26.19 Y-99.533 Z-2.412
G1 X26.318 Y-99.615 Z-2.416
G1 X26.454 Y-99.682 Z-2.42

(Linearized circular move - low memory mode enabled)
G1 X26.589 Y-99.734 Z-2.424
G1 X26.728 Y-99.771 Z-2.428
G1 X26.871 Y-99.795 Z-2.432
G1 X27.015 Y-99.805 Z-2.436
G1 X27.159 Y-99.801 Z-2.44
G1 X27.303 Y-99.782 Z-2.444
G1 X27.444 Y-99.75 Z-2.448
G1 X27.58 Y-99.704 Z-2.452
G1 X27.712 Y-99.644 Z-2.456
G1 X27.838 Y-99.572 Z-2.46
G1 X27.955 Y-99.489 Z-2.464
G1 X28.064 Y-99.394 Z-2.468
G1 X28.163 Y-99.288 Z-2.472
G1 X28.252 Y-99.174 Z-2.476
G1 X28.328 Y-99.051 Z-2.48
G1 X28.392 Y-98.922 Z-2.483

(Linearized circular move - low memory mode enabled)
G1 X28.446 Y-98.78 Z-2.488
G1 X28.484 Y-98.634 Z-2.492
G1 X28.508 Y-98.484 Z-2.496
G1 X28.516 Y-98.333 Z-2.5

(Linearized circular move - low memory mode enabled)
G1 X28.508 Y-98.188
G1 X28.487 Y-98.045
G1 X28.452 Y-97.905
G1 X28.403 Y-97.769
G1 X28.342 Y-97.639
G1 X28.267 Y-97.515
G1 X28.181 Y-97.399
G1 X28.084 Y-97.291
G1 X27.977 Y-97.194
G1 X27.861 Y-97.108
G1 X27.737 Y-97.034
G1 X27.606 Y-96.972
G1 X27.47 Y-96.924
G1 X27.33 Y-96.888
G1 X27.187 Y-96.867
G1 X27.043 Y-96.86

(Linearized circular move - low memory mode enabled)
G1 X26.899 Y-96.867
G1 X26.756 Y-96.888
G1 X26.615 Y-96.924
G1 X26.479 Y-96.972
G1 X26.349 Y-97.034
G1 X26.225 Y-97.108
G1 X26.109 Y-97.194
G1 X26.002 Y-97.291
G1 X25.905 Y-97.399
G1 X25.819 Y-97.515
G1 X25.744 Y-97.639
G1 X25.682 Y-97.769
G1 X25.634 Y-97.905
G1 X25.599 Y-98.045
G1 X25.577 Y-98.188
G1 X25.57 Y-98.333

(Linearized circular move - low memory mode enabled)
G1 X25.577 Y-98.477
G1 X25.599 Y-98.62
G1 X25.634 Y-98.76
G1 X25.682 Y-98.896
G1 X25.744 Y-99.027
G1 X25.819 Y-99.151
G1 X25.905 Y-99.267
G1 X26.002 Y-99.374
G1 X26.109 Y-99.471
G1 X26.225 Y-99.557
G1 X26.349 Y-99.631
G1 X26.479 Y-99.693
G1 X26.615 Y-99.742
G1 X26.756 Y-99.777
G1 X26.899 Y-99.798
G1 X27.043 Y-99.805

(Linearized circular move - low memory mode enabled)
G1 X27.187 Y-99.798
G1 X27.33 Y-99.777
G1 X27.47 Y-99.742
G1 X27.606 Y-99.693
G1 X27.737 Y-99.631
G1 X27.861 Y-99.557
G1 X27.977 Y-99.471
G1 X28.084 Y-99.374
G1 X28.181 Y-99.267
G1 X28.267 Y-99.151
G1 X28.342 Y-99.027
G1 X28.403 Y-98.896
G1 X28.452 Y-98.76
G1 X28.487 Y-98.62
G1 X28.508 Y-98.477
G1 X28.516 Y-98.333
G1 X28.537 Y-97.916 F900.0

(Linearized circular move - low memory mode enabled)
G1 X28.534 Y-97.788
G1 X28.515 Y-97.661
G1 X28.481 Y-97.537
G1 X28.432 Y-97.418
G1 X28.369 Y-97.306
G1 X28.292 Y-97.203
G1 X28.203 Y-97.111

(Linearized circular move - low memory mode enabled)
G1 X28.087 Y-97.006
G1 X27.963 Y-96.913
G1 X27.83 Y-96.832
G1 X27.69 Y-96.763
G1 X27.545 Y-96.708
G1 X27.395 Y-96.666
G1 X27.242 Y-96.638
G1 X27.086 Y-96.624
G1 X26.931 Y-96.625
G1 X26.776 Y-96.64
G1 X26.623 Y-96.67
G1 X26.473 Y-96.713
G1 X26.328 Y-96.77
G1 X26.189 Y-96.84
G1 X26.057 Y-96.923
G1 X25.934 Y-97.017
G1 X25.819 Y-97.123

(Linearized circular move - low memory mode enabled)
G1 X25.715 Y-97.238
G1 X25.623 Y-97.362
G1 X25.542 Y-97.494
G1 X25.473 Y-97.633
G1 X25.417 Y-97.777
G1 X25.375 Y-97.926
G1 X25.347 Y-98.079
G1 X25.333 Y-98.233
G1 Y-98.388
G1 X25.348 Y-98.542

(Linearized circular move - low memory mode enabled)
G1 X25.373 Y-98.698
G1 X25.414 Y-98.851
G1 X25.468 Y-98.999
G1 X25.535 Y-99.142
G1 X25.615 Y-99.278
G1 X25.708 Y-99.406
G1 X25.812 Y-99.525
G1 X25.926 Y-99.633
G1 X26.05 Y-99.731
G1 X26.183 Y-99.817
G1 X26.323 Y-99.89
G1 X26.469 Y-99.95
G1 X26.62 Y-99.997
G1 X26.774 Y-100.029
G1 X26.931 Y-100.047
G1 X27.089 Y-100.05
G1 X27.246 Y-100.039

(Linearized circular move - low memory mode enabled)
G1 X27.407 Y-100.012
G1 X27.564 Y-99.97
G1 X27.716 Y-99.914
G1 X27.862 Y-99.843
G1 X28.001 Y-99.758
G1 X28.131 Y-99.661
G1 X28.252 Y-99.552
G1 X28.361 Y-99.432
G1 X28.459 Y-99.302
G1 X28.544 Y-99.164
G1 X28.615 Y-99.018
G1 X28.672 Y-98.866
G1 X28.715 Y-98.709

(Linearized circular move - low memory mode enabled)
G1 X28.764 Y-98.553
G1 X28.799 Y-98.393
G1 X28.818 Y-98.23
G1 X28.822 Y-98.066
G1 X28.811 Y-97.903
G1 X28.785 Y-97.741
G1 X28.745 Y-97.583
G1 X28.689 Y-97.429
G1 X28.62 Y-97.281
G1 X28.537 Y-97.139
G1 X28.441 Y-97.006

(Linearized circular move - low memory mode enabled)
G1 X28.323 Y-96.887
G1 X28.194 Y-96.778
G1 X28.057 Y-96.68
G1 X27.911 Y-96.595
G1 X27.759 Y-96.523
G1 X27.6 Y-96.465
G1 X27.438 Y-96.42
G1 X27.272 Y-96.39
G1 X27.104 Y-96.374
G1 X26.935
G1 X26.767 Y-96.387
G1 X26.601 Y-96.416
G1 X26.438 Y-96.459
G1 X26.279 Y-96.516
G1 X26.126 Y-96.586
G1 X25.98 Y-96.67
G1 X25.841 Y-96.766
G1 X25.711 Y-96.873

(Linearized circular move - low memory mode enabled)
G1 X25.588 Y-96.996
G1 X25.476 Y-97.129
G1 X25.377 Y-97.271
G1 X25.29 Y-97.422
G1 X25.218 Y-97.58
G1 X25.16 Y-97.744
G1 X25.117 Y-97.913
G1 X25.09 Y-98.084
G1 X25.077 Y-98.257
G1 X25.081 Y-98.431
G1 X25.1 Y-98.604

(Linearized circular move - low memory mode enabled)
G1 X25.13 Y-98.772
G1 X25.175 Y-98.938
G1 X25.234 Y-99.098
G1 X25.306 Y-99.253
G1 X25.392 Y-99.402
G1 X25.491 Y-99.542
G1 X25.601 Y-99.672
G1 X25.722 Y-99.793
G1 X25.854 Y-99.903
G1 X25.994 Y-100.001
G1 X26.142 Y-100.086
G1 X26.298 Y-100.158
G1 X26.459 Y-100.216
G1 X26.624 Y-100.26
G1 X26.793 Y-100.29
G1 X26.963 Y-100.304
G1 X27.135
G1 X27.305 Y-100.289

(Linearized circular move - low memory mode enabled)
G1 X27.47 Y-100.259
G1 X27.633 Y-100.216
G1 X27.791 Y-100.159
G1 X27.943 Y-100.088
G1 X28.089 Y-100.005
G1 X28.227 Y-99.91
G1 X28.357 Y-99.803
G1 X28.477 Y-99.686
G1 X28.586 Y-99.558
G1 X28.684 Y-99.422
G1 X28.771 Y-99.278
G1 X28.844 Y-99.127

(Linearized circular move - low memory mode enabled)
G1 X28.921 Y-98.968
G1 X28.985 Y-98.803
G1 X29.034 Y-98.634
G1 X29.068 Y-98.461
G1 X29.088 Y-98.285
G1 X29.092 Y-98.109
G1 X29.081 Y-97.932
G1 X29.055 Y-97.758
G1 X29.014 Y-97.586
G1 X28.959 Y-97.418
G1 X28.89 Y-97.256
G1 X28.806 Y-97.1
G1 X28.71 Y-96.952
G1 X28.602 Y-96.813
G1 X28.482 Y-96.684
G1 X28.351 Y-96.565
G1 X28.211 Y-96.458

(Linearized circular move - low memory mode enabled)
G1 X28.052 Y-96.366
G1 X27.885 Y-96.288
G1 X27.713 Y-96.224
G1 X27.537 Y-96.174
G1 X27.356 Y-96.139
G1 X27.174 Y-96.119
G1 X26.99 Y-96.114
G1 X26.807 Y-96.124
G1 X26.625 Y-96.15
G1 X26.446 Y-96.19
G1 X26.271 Y-96.245
G1 X26.101 Y-96.314
G1 X25.937 Y-96.397
G1 X25.781 Y-96.493
G1 X25.633 Y-96.602
G1 X25.494 Y-96.723
G1 X25.366 Y-96.855
G1 X25.25 Y-96.996
G1 X25.145 Y-97.147

(Linearized circular move - low memory mode enabled)
G1 X25.054 Y-97.307
G1 X24.975 Y-97.473
G1 X24.911 Y-97.645
G1 X24.861 Y-97.821
G1 X24.826 Y-98.002
G1 X24.806 Y-98.184
G1 X24.801 Y-98.368
G1 X24.811 Y-98.551
G1 X24.837 Y-98.733
G1 X24.877 Y-98.912
G1 X24.932 Y-99.087
G1 X25.001 Y-99.257
G1 X25.084 Y-99.421
G1 X25.18 Y-99.578
G1 X25.289 Y-99.725
G1 X25.41 Y-99.864
G1 X25.542 Y-99.992
G1 X25.684 Y-100.108
G1 X25.835 Y-100.213

(Linearized circular move - low memory mode enabled)
G1 X25.974 Y-100.294
G1 X26.119 Y-100.365
G1 X26.268 Y-100.425
G1 X26.422 Y-100.474
G1 X26.578 Y-100.512
G1 X26.737 Y-100.539

(Linearized circular move - low memory mode enabled)
G1 X26.924 Y-100.558
G1 X27.111 Y-100.562
G1 X27.298 Y-100.55
G1 X27.483 Y-100.522
G1 X27.666 Y-100.478
G1 X27.844 Y-100.42
G1 X28.016 Y-100.347
G1 X28.182 Y-100.259
G1 X28.34 Y-100.158
G1 X28.488 Y-100.045
G1 X28.627 Y-99.919
G1 X28.755 Y-99.781
G1 X28.87 Y-99.634
G1 X28.973 Y-99.477
G1 X29.063 Y-99.313
G1 X29.138 Y-99.141

(Linearized circular move - low memory mode enabled)
G1 X29.213 Y-98.965
G1 X29.273 Y-98.784
G1 X29.318 Y-98.598
G1 X29.348 Y-98.41
G1 X29.362 Y-98.219
G1 X29.36 Y-98.028
G1 X29.342 Y-97.838
G1 X29.309 Y-97.65
G1 X29.26 Y-97.465
G1 X29.197 Y-97.285
G1 X29.118 Y-97.11
G1 X29.026 Y-96.943
G1 X28.92 Y-96.784
G1 X28.801 Y-96.635
G1 X28.67 Y-96.495
G1 X28.528 Y-96.367
G1 X28.376 Y-96.251
G1 X28.215 Y-96.148
G1 X28.046 Y-96.059

(Linearized circular move - low memory mode enabled)
G1 X27.878 Y-95.987
G1 X27.704 Y-95.928
G1 X27.526 Y-95.883
G1 X27.345 Y-95.853
G1 X27.162 Y-95.837
G1 X26.979 Y-95.835
G1 X26.795 Y-95.848
G1 X26.614 Y-95.876

(Linearized circular move - low memory mode enabled)
G1 X26.422 Y-95.916
G1 X26.234 Y-95.971
G1 X26.05 Y-96.04
G1 X25.873 Y-96.124
G1 X25.702 Y-96.221
G1 X25.54 Y-96.332
G1 X25.387 Y-96.454
G1 X25.244 Y-96.589
G1 X25.112 Y-96.734
G1 X24.992 Y-96.889
G1 X24.885 Y-97.053
G1 X24.79 Y-97.225
G1 X24.71 Y-97.404
G1 X24.644 Y-97.589
G1 X24.592 Y-97.778
G1 X24.556 Y-97.971
G1 X24.534 Y-98.166
G1 X24.528 Y-98.362
G1 X24.538 Y-98.558
G1 X24.562 Y-98.753

(Linearized circular move - low memory mode enabled)
G1 X24.601 Y-98.939
G1 X24.654 Y-99.121
G1 X24.72 Y-99.3
G1 X24.799 Y-99.472
G1 X24.892 Y-99.638
G1 X24.997 Y-99.797
G1 X25.113 Y-99.947
G1 X25.241 Y-100.087
G1 X25.379 Y-100.218
G1 X25.526 Y-100.338
G1 X25.683 Y-100.446
G1 X25.847 Y-100.542
G1 X26.017 Y-100.626
G1 X26.194 Y-100.696
G1 X26.376 Y-100.752
G1 X26.561 Y-100.795
G1 X26.749 Y-100.823
G1 X26.938 Y-100.837

(Linearized circular move - low memory mode enabled)
G1 X27.126 Y-100.84
G1 X27.313 Y-100.829
G1 X27.498 Y-100.804
G1 X27.682 Y-100.764
G1 X27.861 Y-100.71
G1 X28.036 Y-100.642
G1 X28.205 Y-100.561
G1 X28.367 Y-100.467

(Linearized circular move - low memory mode enabled)
G1 X28.536 Y-100.352
G1 X28.696 Y-100.225
G1 X28.846 Y-100.086
G1 X28.986 Y-99.936
G1 X29.113 Y-99.776
G1 X29.228 Y-99.607
G1 X29.33 Y-99.43
G1 X29.418 Y-99.245
G1 X29.492 Y-99.055
G1 X29.552 Y-98.859
G1 X29.596 Y-98.659

(Linearized circular move - low memory mode enabled)
G1 X29.626 Y-98.457
G1 X29.64 Y-98.253
G1 X29.639 Y-98.048
G1 X29.622 Y-97.844
G1 X29.59 Y-97.642
G1 X29.542 Y-97.443
G1 X29.48 Y-97.249
G1 X29.403 Y-97.059
G1 X29.313 Y-96.876
G1 X29.208 Y-96.7
G1 X29.091 Y-96.532

(Linearized circular move - low memory mode enabled)
G1 X28.954 Y-96.381
G1 X28.805 Y-96.24
G1 X28.647 Y-96.11
G1 X28.479 Y-95.993
G1 X28.303 Y-95.888
G1 X28.12 Y-95.797
G1 X27.93 Y-95.72
G1 X27.735 Y-95.657
G1 X27.536 Y-95.609
G1 X27.334 Y-95.576
G1 X27.13 Y-95.559
G1 X26.925 Y-95.556
G1 X26.721 Y-95.569
G1 X26.518 Y-95.597
G1 X26.318 Y-95.64
G1 X26.122 Y-95.698
G1 X25.931 Y-95.771
G1 X25.745 Y-95.857
G1 X25.567 Y-95.957
G1 X25.396 Y-96.071
G1 X25.235 Y-96.196

(Linearized circular move - low memory mode enabled)
G1 X25.08 Y-96.337
G1 X24.936 Y-96.489
G1 X24.804 Y-96.651
G1 X24.685 Y-96.823
G1 X24.58 Y-97.004
G1 X24.489 Y-97.192
G1 X24.412 Y-97.386
G1 X24.35 Y-97.586
G1 X24.304 Y-97.79
G1 X24.274 Y-97.997
G1 X24.259 Y-98.206

(Linearized circular move - low memory mode enabled)
G1 X24.256 Y-98.414
G1 X24.27 Y-98.622
G1 X24.298 Y-98.829
G1 X24.342 Y-99.033
G1 X24.401 Y-99.233
G1 X24.475 Y-99.428
G1 X24.563 Y-99.617
G1 X24.665 Y-99.798
G1 X24.78 Y-99.972
G1 X24.908 Y-100.137
G1 X25.048 Y-100.291
G1 X25.199 Y-100.435
G1 X25.361 Y-100.567
G1 X25.532 Y-100.687
G1 X25.711 Y-100.793
G1 X25.898 Y-100.886
G1 X26.091 Y-100.965
G1 X26.289 Y-101.028
G1 X26.492 Y-101.077
G1 X26.698 Y-101.111
G1 X26.906 Y-101.129

(Linearized circular move - low memory mode enabled)
G1 X27.111 Y-101.132
G1 X27.316 Y-101.119
G1 X27.519 Y-101.092
G1 X27.72 Y-101.049
G1 X27.917 Y-100.992
G1 X28.109 Y-100.921
G1 X28.296 Y-100.836
G1 X28.475 Y-100.737
G1 X28.647 Y-100.625
G1 X28.811 Y-100.501
G1 X28.965 Y-100.366
G1 X29.108 Y-100.219
G1 X29.24 Y-100.062

(Linearized circular move - low memory mode enabled)
G1 X29.369 Y-99.899
G1 X29.487 Y-99.727
G1 X29.591 Y-99.547
G1 X29.683 Y-99.36
G1 X29.761 Y-99.167
G1 X29.824 Y-98.969
G1 X29.874 Y-98.767
G1 X29.909 Y-98.562
G1 X29.93 Y-98.355
G1 X29.935 Y-98.147
G1 X29.926 Y-97.939
G1 X29.902 Y-97.733
G1 X29.863 Y-97.528
G1 X29.81 Y-97.327
G1 X29.742 Y-97.13
G1 X29.661 Y-96.939
G1 X29.566 Y-96.754
G1 X29.458 Y-96.576
G1 X29.338 Y-96.406
G1 X29.206 Y-96.245
G1 X29.063 Y-96.094
G1 X28.91 Y-95.953

(Linearized circular move - low memory mode enabled)
G1 X28.741 Y-95.82
G1 X28.563 Y-95.7
G1 X28.377 Y-95.593
G1 X28.183 Y-95.5
G1 X27.983 Y-95.422
G1 X27.778 Y-95.359
G1 X27.569 Y-95.31
G1 X27.357 Y-95.278
G1 X27.142 Y-95.261
G1 X26.928 Y-95.26
G1 X26.713 Y-95.275
G1 X26.501 Y-95.305
G1 X26.291 Y-95.351
G1 X26.085 Y-95.412
G1 X25.885 Y-95.489

(Linearized circular move - low memory mode enabled)
G1 X25.684 Y-95.578
G1 X25.49 Y-95.681
G1 X25.304 Y-95.798
G1 X25.127 Y-95.927
G1 X24.959 Y-96.069
G1 X24.802 Y-96.223
G1 X24.657 Y-96.387
G1 X24.523 Y-96.562
G1 X24.402 Y-96.745
G1 X24.295 Y-96.937
G1 X24.202 Y-97.135
G1 X24.123 Y-97.34
G1 X24.058 Y-97.55
G1 X24.009 Y-97.764
G1 X23.976 Y-97.981
G1 X23.958 Y-98.2
G1 X23.955 Y-98.42
G1 X23.969 Y-98.639
G1 X23.997 Y-98.857
G1 X24.042 Y-99.072
G1 X24.101 Y-99.283
G1 X24.176 Y-99.49

(Linearized circular move - low memory mode enabled)
G1 X24.263 Y-99.688
G1 X24.365 Y-99.879
G1 X24.479 Y-100.063
G1 X24.607 Y-100.238
G1 X24.746 Y-100.404
G1 X24.896 Y-100.559
G1 X25.057 Y-100.704
G1 X25.228 Y-100.837
G1 X25.408 Y-100.957
G1 X25.596 Y-101.065
G1 X25.791 Y-101.159
G1 X25.992 Y-101.239
G1 X26.198 Y-101.305
G1 X26.408 Y-101.356
G1 X26.622 Y-101.393
G1 X26.837 Y-101.414
G1 X27.053 Y-101.42
G1 X27.27 Y-101.411
G1 X27.485 Y-101.386
G1 X27.698 Y-101.347
G1 X27.907 Y-101.293
G1 X28.112 Y-101.224

(Linearized circular move - low memory mode enabled)
G1 X28.317 Y-101.141
G1 X28.515 Y-101.044
G1 X28.706 Y-100.934
G1 X28.89 Y-100.811
G1 X29.064 Y-100.676
G1 X29.229 Y-100.53
G1 X29.383 Y-100.372
G1 X29.527 Y-100.204
G1 X29.658 Y-100.027
G1 X29.778 Y-99.842
G1 X29.884 Y-99.648
G1 X29.977 Y-99.448
G1 X30.056 Y-99.242
G1 X30.12 Y-99.031
G1 X30.17 Y-98.816
G1 X30.206 Y-98.599
G1 X30.226 Y-98.379
G1 X30.232 Y-98.158
G1 X30.222 Y-97.938
G1 X30.197 Y-97.719
G1 X30.158 Y-97.502
G1 X30.103 Y-97.288
G1 X30.035 Y-97.078

(Linearized circular move - low memory mode enabled)
G1 X29.949 Y-96.869
G1 X29.849 Y-96.665
G1 X29.736 Y-96.469
G1 X29.608 Y-96.282
G1 X29.468 Y-96.104
G1 X29.316 Y-95.936
G1 X29.153 Y-95.78
G1 X28.979 Y-95.635
G1 X28.795 Y-95.503
G1 X28.602 Y-95.384
G1 X28.402 Y-95.278
G1 X28.194 Y-95.187
G1 X27.981 Y-95.111
G1 X27.763 Y-95.05
G1 X27.541 Y-95.004
G1 X27.317 Y-94.974
G1 X27.091 Y-94.96
G1 X26.864 Y-94.962

(Linearized circular move - low memory mode enabled)
G1 X26.64 Y-94.98
G1 X26.417 Y-95.014
G1 X26.198 Y-95.062
G1 X25.982 Y-95.125
G1 X25.77 Y-95.202
G1 X25.564 Y-95.293
G1 X25.365 Y-95.398
G1 X25.173 Y-95.515
G1 X24.99 Y-95.645
G1 X24.815 Y-95.787
G1 X24.65 Y-95.941
G1 X24.496 Y-96.104

(Linearized circular move - low memory mode enabled)
G1 X24.353 Y-96.278
G1 X24.222 Y-96.461
G1 X24.104 Y-96.653
G1 X23.998 Y-96.851
G1 X23.906 Y-97.057
G1 X23.828 Y-97.268
G1 X23.764 Y-97.483
G1 X23.715 Y-97.703
G1 X23.68 Y-97.925
G1 X23.66 Y-98.149
G1 X23.655 Y-98.374
G1 X23.665 Y-98.599

(Linearized circular move - low memory mode enabled)
G1 X23.692 Y-98.829
G1 X23.734 Y-99.057
G1 X23.792 Y-99.282
G1 X23.864 Y-99.502
G1 X23.952 Y-99.717
G1 X24.054 Y-99.925
G1 X24.17 Y-100.126
G1 X24.3 Y-100.318
G1 X24.442 Y-100.501
G1 X24.596 Y-100.674
G1 X24.762 Y-100.836
G1 X24.939 Y-100.986
G1 X25.125 Y-101.124
G1 X25.321 Y-101.249
G1 X25.524 Y-101.36
G1 X25.735 Y-101.457
G1 X25.951 Y-101.539
G1 X26.173 Y-101.607
G1 X26.399 Y-101.659
G1 X26.628 Y-101.696
G1 X26.859 Y-101.717
G1 X27.091 Y-101.722
G1 X27.322 Y-101.711

(Linearized circular move - low memory mode enabled)
G1 X27.545 Y-101.686
G1 X27.766 Y-101.646
G1 X27.983 Y-101.591
G1 X28.197 Y-101.522
G1 X28.405 Y-101.44
G1 X28.608 Y-101.343
G1 X28.804 Y-101.234
G1 X28.992 Y-101.112
G1 X29.172 Y-100.977
G1 X29.342 Y-100.831
G1 X29.502 Y-100.675
G1 X29.652 Y-100.508
G1 X29.79 Y-100.331

(Linearized circular move - low memory mode enabled)
G1 X29.926 Y-100.145
G1 X30.048 Y-99.951
G1 X30.158 Y-99.749
G1 X30.255 Y-99.541
G1 X30.337 Y-99.327
G1 X30.406 Y-99.107
G1 X30.46 Y-98.884
G1 X30.499 Y-98.658
G1 X30.523 Y-98.429
G1 X30.532 Y-98.2
G1 X30.527 Y-97.97
G1 X30.506 Y-97.741
G1 X30.47 Y-97.514
G1 X30.42 Y-97.29
G1 X30.355 Y-97.07
G1 X30.276 Y-96.854
G1 X30.182 Y-96.644
G1 X30.076 Y-96.441
G1 X29.956 Y-96.245
G1 X29.824 Y-96.057
G1 X29.679 Y-95.878
G1 X29.523 Y-95.709
G1 X29.357 Y-95.551
G1 X29.181 Y-95.404

(Linearized circular move - low memory mode enabled)
G1 X29.001 Y-95.273
G1 X28.814 Y-95.153
G1 X28.619 Y-95.046
G1 X28.418 Y-94.951
G1 X28.211 Y-94.869
G1 X28.0 Y-94.801
G1 X27.785 Y-94.745
G1 X27.566 Y-94.704
G1 X27.346 Y-94.676
G1 X27.124 Y-94.663
G1 X26.902 Y-94.664
G1 X26.68 Y-94.678
G1 X26.459 Y-94.707
G1 X26.241 Y-94.749
G1 X26.026 Y-94.806

(Linearized circular move - low memory mode enabled)
G1 X25.797 Y-94.879
G1 X25.573 Y-94.966
G1 X25.355 Y-95.069
G1 X25.144 Y-95.185
G1 X24.942 Y-95.315
G1 X24.748 Y-95.458
G1 X24.564 Y-95.613
G1 X24.391 Y-95.78
G1 X24.229 Y-95.958
G1 X24.079 Y-96.146
G1 X23.941 Y-96.343
G1 X23.817 Y-96.549
G1 X23.706 Y-96.763
G1 X23.61 Y-96.984
G1 X23.529 Y-97.21
G1 X23.462 Y-97.441
G1 X23.41 Y-97.676
G1 X23.374 Y-97.914
G1 X23.354 Y-98.154
G1 X23.35 Y-98.395
G1 X23.361 Y-98.635
G1 X23.388 Y-98.874
G1 X23.43 Y-99.111
G1 X23.488 Y-99.345

(Linearized circular move - low memory mode enabled)
G1 X23.56 Y-99.571
G1 X23.646 Y-99.793
G1 X23.747 Y-100.008
G1 X23.861 Y-100.216
G1 X23.989 Y-100.417
G1 X24.129 Y-100.609
G1 X24.282 Y-100.791
G1 X24.446 Y-100.964
G1 X24.62 Y-101.125
G1 X24.805 Y-101.275
G1 X24.999 Y-101.412
G1 X25.201 Y-101.537
G1 X25.411 Y-101.648
G1 X25.628 Y-101.745
G1 X25.851 Y-101.829
G1 X26.079 Y-101.897
G1 X26.31 Y-101.951
G1 X26.545 Y-101.99
G1 X26.781 Y-102.013
G1 X27.019 Y-102.022
G1 X27.257 Y-102.014
G1 X27.493 Y-101.992
G1 X27.728 Y-101.954

(Linearized circular move - low memory mode enabled)
G1 X27.963 Y-101.901
G1 X28.194 Y-101.834
G1 X28.421 Y-101.752
G1 X28.642 Y-101.656
G1 X28.857 Y-101.546
G1 X29.064 Y-101.423
G1 X29.263 Y-101.288
G1 X29.454 Y-101.14
G1 X29.634 Y-100.98
G1 X29.805 Y-100.81
G1 X29.964 Y-100.629
G1 X30.111 Y-100.438
G1 X30.247 Y-100.239
G1 X30.369 Y-100.031
G1 X30.478 Y-99.816
G1 X30.574 Y-99.595
G1 X30.655 Y-99.368
G1 X30.722 Y-99.136
G1 X30.775 Y-98.901
G1 X30.812 Y-98.663
G1 X30.835 Y-98.423
G1 X30.842 Y-98.182
G1 X30.834 Y-97.941
G1 X30.811 Y-97.701
G1 X30.773 Y-97.463

(Linearized circular move - low memory mode enabled)
G1 X30.72 Y-97.228
G1 X30.653 Y-96.997
G1 X30.571 Y-96.77
G1 X30.475 Y-96.549
G1 X30.365 Y-96.334
G1 X30.242 Y-96.127
G1 X30.107 Y-95.927
G1 X29.959 Y-95.737
G1 X29.799 Y-95.557
G1 X29.629 Y-95.386
G1 X29.448 Y-95.227
G1 X29.257 Y-95.08
G1 X29.057 Y-94.944
G1 X28.85 Y-94.822
G1 X28.635 Y-94.713
G1 X28.414 Y-94.617
G1 X28.187 Y-94.536
G1 X27.955 Y-94.469
G1 X27.72 Y-94.416
G1 X27.482 Y-94.379
G1 X27.242 Y-94.356
G1 X27.001 Y-94.349
G1 X26.76 Y-94.357
G1 X26.52 Y-94.38
G1 X26.282 Y-94.418

(Linearized circular move - low memory mode enabled)
G1 X26.059 Y-94.468
G1 X25.838 Y-94.531
G1 X25.623 Y-94.607
G1 X25.412 Y-94.696
G1 X25.206 Y-94.797

(Linearized circular move - low memory mode enabled)
G1 X24.987 Y-94.918
G1 X24.776 Y-95.052
G1 X24.574 Y-95.199
G1 X24.382 Y-95.358
G1 X24.2 Y-95.53
G1 X24.029 Y-95.712
G1 X23.87 Y-95.905
G1 X23.723 Y-96.107
G1 X23.589 Y-96.318
G1 X23.469 Y-96.538
G1 X23.363 Y-96.764
G1 X23.271 Y-96.996
G1 X23.194 Y-97.234
G1 X23.132 Y-97.476
G1 X23.086 Y-97.722
G1 X23.055 Y-97.97
G1 X23.039 Y-98.22
G1 Y-98.47
G1 X23.055 Y-98.719
G1 X23.087 Y-98.967
G1 X23.134 Y-99.212
G1 X23.196 Y-99.455
G1 X23.274 Y-99.692
G1 X23.366 Y-99.925
G1 X23.473 Y-100.151

(Linearized circular move - low memory mode enabled)
G1 X23.583 Y-100.352
G1 X23.705 Y-100.547
G1 X23.838 Y-100.735
G1 X23.982 Y-100.914
G1 X24.135 Y-101.085
G1 X24.299 Y-101.247
G1 X24.471 Y-101.399
G1 X24.652 Y-101.541
G1 X24.841 Y-101.672
G1 X25.037 Y-101.792

(Linearized circular move - low memory mode enabled)
G1 X25.253 Y-101.909
G1 X25.477 Y-102.012
G1 X25.707 Y-102.101
G1 X25.941 Y-102.177
G1 X26.18 Y-102.237
G1 X26.422 Y-102.284
G1 X26.666 Y-102.315
G1 X26.912 Y-102.331
G1 X27.158 Y-102.333
G1 X27.404 Y-102.319
G1 X27.649 Y-102.29
G1 X27.891 Y-102.247
G1 X28.13 Y-102.189

(Linearized circular move - low memory mode enabled)
G1 X28.366 Y-102.116
G1 X28.596 Y-102.029
G1 X28.821 Y-101.928
G1 X29.039 Y-101.814
G1 X29.25 Y-101.687
G1 X29.453 Y-101.547
G1 X29.646 Y-101.395
G1 X29.83 Y-101.231
G1 X30.004 Y-101.057
G1 X30.167 Y-100.872
G1 X30.318 Y-100.678
G1 X30.457 Y-100.474
G1 X30.584 Y-100.263

(Linearized circular move - low memory mode enabled)
G1 X30.704 Y-100.044
G1 X30.81 Y-99.818
G1 X30.903 Y-99.586
G1 X30.981 Y-99.349
G1 X31.045 Y-99.108
G1 X31.094 Y-98.863
G1 X31.129 Y-98.616
G1 X31.148 Y-98.367
G1 X31.152 Y-98.118
G1 X31.142 Y-97.868
G1 X31.116 Y-97.62
G1 X31.075 Y-97.374
G1 X31.02 Y-97.13
G1 X30.95 Y-96.891
G1 X30.865 Y-96.656
G1 X30.767 Y-96.427
G1 X30.654 Y-96.204
G1 X30.529 Y-95.988
G1 X30.391 Y-95.78
G1 X30.24 Y-95.581
G1 X30.078 Y-95.391
G1 X29.905 Y-95.212
G1 X29.721 Y-95.043
G1 X29.527 Y-94.886
G1 X29.324 Y-94.74
G1 X29.113 Y-94.607

(Linearized circular move - low memory mode enabled)
G1 X28.889 Y-94.485
G1 X28.658 Y-94.377
G1 X28.421 Y-94.283
G1 X28.178 Y-94.204
G1 X27.931 Y-94.14
G1 X27.681 Y-94.092
G1 X27.428 Y-94.059
G1 X27.173 Y-94.041
G1 X26.918 Y-94.04
G1 X26.663 Y-94.054
G1 X26.41 Y-94.084
G1 X26.159 Y-94.129
G1 X25.911 Y-94.19

(Linearized circular move - low memory mode enabled)
G1 X25.662 Y-94.265
G1 X25.419 Y-94.354
G1 X25.181 Y-94.457
G1 X24.95 Y-94.575
G1 X24.726 Y-94.707
G1 X24.511 Y-94.852
G1 X24.304 Y-95.01
G1 X24.108 Y-95.179
G1 X23.922 Y-95.361
G1 X23.748 Y-95.553
G1 X23.586 Y-95.755
G1 X23.436 Y-95.967
G1 X23.299 Y-96.188
G1 X23.176 Y-96.416
G1 X23.066 Y-96.651
G1 X22.971 Y-96.893
G1 X22.891 Y-97.14
G1 X22.826 Y-97.391
G1 X22.776 Y-97.646
G1 X22.742 Y-97.903
G1 X22.723 Y-98.162
G1 X22.72 Y-98.421
G1 X22.733 Y-98.681
G1 X22.761 Y-98.939
G1 X22.805 Y-99.194
G1 X22.864 Y-99.447

(Linearized circular move - low memory mode enabled)
G1 X22.939 Y-99.698
G1 X23.029 Y-99.944
G1 X23.134 Y-100.184
G1 X23.254 Y-100.417
G1 X23.387 Y-100.642
G1 X23.534 Y-100.859
G1 X23.694 Y-101.067
G1 X23.866 Y-101.264
G1 X24.05 Y-101.451
G1 X24.245 Y-101.625
G1 X24.45 Y-101.788
G1 X24.665 Y-101.938
G1 X24.888 Y-102.075
G1 X25.12 Y-102.197
G1 X25.358 Y-102.306
G1 X25.603 Y-102.399

(Linearized circular move - low memory mode enabled)
G1 X25.838 Y-102.475
G1 X26.078 Y-102.537
G1 X26.321 Y-102.586
G1 X26.566 Y-102.62
G1 X26.812 Y-102.641
G1 X27.06 Y-102.648
G1 X27.307 Y-102.641
G1 X27.554 Y-102.62
G1 X27.799 Y-102.585
G1 X28.041 Y-102.536
G1 X28.281 Y-102.474
G1 X28.516 Y-102.398
G1 X28.747 Y-102.309
G1 X28.973 Y-102.207

(Linearized circular move - low memory mode enabled)
G1 X29.192 Y-102.092
G1 X29.405 Y-101.966
G1 X29.61 Y-101.827
G1 X29.807 Y-101.678
G1 X29.996 Y-101.517
G1 X30.174 Y-101.346
G1 X30.343 Y-101.165
G1 X30.502 Y-100.975
G1 X30.649 Y-100.776
G1 X30.785 Y-100.569
G1 X30.909 Y-100.355
G1 X31.021 Y-100.135
G1 X31.121 Y-99.908
G1 X31.207 Y-99.676

(Linearized circular move - low memory mode enabled)
G1 X31.287 Y-99.43
G1 X31.354 Y-99.18
G1 X31.405 Y-98.926
G1 X31.442 Y-98.67
G1 X31.464 Y-98.412
G1 X31.47 Y-98.154
G1 X31.462 Y-97.895
G1 X31.439 Y-97.637
G1 X31.401 Y-97.381
G1 X31.347 Y-97.128
G1 X31.28 Y-96.878
G1 X31.198 Y-96.633
G1 X31.101 Y-96.393
G1 X30.991 Y-96.159
G1 X30.868 Y-95.931
G1 X30.731 Y-95.711
G1 X30.582 Y-95.5
G1 X30.421 Y-95.297
G1 X30.249 Y-95.105
G1 X30.065 Y-94.922
G1 X29.871 Y-94.751
G1 X29.668 Y-94.591
G1 X29.455 Y-94.443
G1 X29.235 Y-94.308
G1 X29.007 Y-94.186
G1 X28.772 Y-94.077
G1 X28.531 Y-93.982

(Linearized circular move - low memory mode enabled)
G1 X28.288 Y-93.902
G1 X28.04 Y-93.837
G1 X27.789 Y-93.785
G1 X27.536 Y-93.748
G1 X27.28 Y-93.726
G1 X27.024 Y-93.719
G1 X26.768 Y-93.726
G1 X26.513 Y-93.748
G1 X26.259 Y-93.784
G1 X26.008 Y-93.836

(Linearized circular move - low memory mode enabled)
G1 X25.75 Y-93.9
G1 X25.497 Y-93.979
G1 X25.248 Y-94.072
G1 X25.005 Y-94.18
G1 X24.769 Y-94.302
G1 X24.54 Y-94.437
G1 X24.319 Y-94.585
G1 X24.108 Y-94.746
G1 X23.906 Y-94.919
G1 X23.714 Y-95.103
G1 X23.534 Y-95.298
G1 X23.365 Y-95.503
G1 X23.208 Y-95.718
G1 X23.064 Y-95.941
G1 X22.933 Y-96.172
G1 X22.816 Y-96.411
G1 X22.713 Y-96.656
G1 X22.624 Y-96.906
G1 X22.55 Y-97.162
G1 X22.491 Y-97.421
G1 X22.447 Y-97.683
G1 X22.418 Y-97.947
G1 X22.405 Y-98.212
G1 X22.407 Y-98.478

(Linearized circular move - low memory mode enabled)
G1 X22.423 Y-98.748
G1 X22.455 Y-99.016
G1 X22.502 Y-99.282
G1 X22.565 Y-99.545
G1 X22.643 Y-99.804
G1 X22.736 Y-100.058
G1 X22.843 Y-100.306
G1 X22.965 Y-100.547
G1 X23.101 Y-100.781
G1 X23.25 Y-101.006
G1 X23.411 Y-101.223
G1 X23.585 Y-101.429
G1 X23.771 Y-101.626
G1 X23.968 Y-101.811
G1 X24.176 Y-101.984
G1 X24.393 Y-102.145
G1 X24.619 Y-102.293
G1 X24.853 Y-102.428
G1 X25.095 Y-102.549
G1 X25.343 Y-102.655
G1 X25.597 Y-102.747
G1 X25.856 Y-102.824
G1 X26.119 Y-102.886
G1 X26.386 Y-102.933
G1 X26.654 Y-102.963
G1 X26.924 Y-102.979
G1 X27.194 Y-102.978

(Linearized circular move - low memory mode enabled)
G1 X27.451 Y-102.963
G1 X27.707 Y-102.934
G1 X27.961 Y-102.89
G1 X28.213 Y-102.833
G1 X28.46 Y-102.761
G1 X28.703 Y-102.676
G1 X28.942 Y-102.578
G1 X29.174 Y-102.467
G1 X29.4 Y-102.343
G1 X29.618 Y-102.206
G1 X29.829 Y-102.058
G1 X30.031 Y-101.898
G1 X30.224 Y-101.727

(Linearized circular move - low memory mode enabled)
G1 X30.418 Y-101.542
G1 X30.601 Y-101.345
G1 X30.773 Y-101.139
G1 X30.934 Y-100.924
G1 X31.081 Y-100.7
G1 X31.216 Y-100.468
G1 X31.338 Y-100.228
G1 X31.446 Y-99.983
G1 X31.54 Y-99.731
G1 X31.62 Y-99.475
G1 X31.686 Y-99.215
G1 X31.737 Y-98.951
G1 X31.773 Y-98.685
G1 X31.794 Y-98.417
G1 X31.799 Y-98.149
G1 X31.79 Y-97.88
G1 X31.766 Y-97.613
G1 X31.727 Y-97.347
G1 X31.673 Y-97.084
G1 X31.604 Y-96.825
G1 X31.521 Y-96.57
G1 X31.424 Y-96.319
G1 X31.313 Y-96.075
G1 X31.188 Y-95.837
G1 X31.05 Y-95.607
G1 X30.9 Y-95.385
G1 X30.737 Y-95.171
G1 X30.563 Y-94.967

(Linearized circular move - low memory mode enabled)
G1 X30.375 Y-94.771
G1 X30.176 Y-94.586
G1 X29.967 Y-94.412
G1 X29.749 Y-94.25
G1 X29.521 Y-94.101
G1 X29.286 Y-93.966
G1 X29.043 Y-93.844
G1 X28.794 Y-93.735
G1 X28.539 Y-93.641
G1 X28.279 Y-93.562
G1 X28.016 Y-93.498
G1 X27.748 Y-93.448
G1 X27.479 Y-93.414
G1 X27.208 Y-93.396
G1 X26.936 Y-93.392
G1 X26.665 Y-93.404
G1 X26.395 Y-93.432
G1 X26.126 Y-93.475
G1 X25.861 Y-93.533
G1 X25.599 Y-93.605

(Linearized circular move - low memory mode enabled)
G1 X25.336 Y-93.692
G1 X25.077 Y-93.794
G1 X24.825 Y-93.91
G1 X24.58 Y-94.04
G1 X24.343 Y-94.183
G1 X24.114 Y-94.34
G1 X23.894 Y-94.509
G1 X23.683 Y-94.69
G1 X23.484 Y-94.882
G1 X23.295 Y-95.086
G1 X23.118 Y-95.3
G1 X22.954 Y-95.523
G1 X22.802 Y-95.755
G1 X22.663 Y-95.996
G1 X22.539 Y-96.244
G1 X22.428 Y-96.498
G1 X22.332 Y-96.758
G1 X22.25 Y-97.024
G1 X22.183 Y-97.293
G1 X22.132 Y-97.566
G1 X22.096 Y-97.841
G1 X22.076 Y-98.117
G1 X22.071 Y-98.395
G1 X22.081 Y-98.672
G1 X22.107 Y-98.948
G1 X22.149 Y-99.223
G1 X22.206 Y-99.494
G1 X22.278 Y-99.762

(Linearized circular move - low memory mode enabled)
G1 X22.363 Y-100.022
G1 X22.463 Y-100.276
G1 X22.577 Y-100.524
G1 X22.704 Y-100.766
G1 X22.844 Y-101.001
G1 X22.997 Y-101.227
G1 X23.162 Y-101.445
G1 X23.34 Y-101.653
G1 X23.528 Y-101.851
G1 X23.727 Y-102.038
G1 X23.936 Y-102.214
G1 X24.154 Y-102.378
G1 X24.381 Y-102.53
G1 X24.616 Y-102.669
G1 X24.859 Y-102.795
G1 X25.108 Y-102.908
G1 X25.363 Y-103.006
G1 X25.622 Y-103.09
G1 X25.887 Y-103.16

(Linearized circular move - low memory mode enabled)
G1 X26.165 Y-103.217
G1 X26.446 Y-103.259
G1 X26.73 Y-103.285
G1 X27.014 Y-103.295
G1 X27.298 Y-103.289
G1 X27.582 Y-103.267
G1 X27.864 Y-103.229
G1 X28.143 Y-103.176
G1 X28.419 Y-103.107
G1 X28.69 Y-103.022
G1 X28.957 Y-102.923
G1 X29.217 Y-102.809
G1 X29.471 Y-102.68
G1 X29.717 Y-102.537
G1 X29.954 Y-102.381
G1 X30.183 Y-102.211
G1 X30.401 Y-102.03
G1 X30.609 Y-101.836
G1 X30.806 Y-101.63
G1 X30.991 Y-101.414
G1 X31.164 Y-101.188
G1 X31.324 Y-100.953
G1 X31.47 Y-100.709
G1 X31.602 Y-100.457
G1 X31.72 Y-100.199
G1 X31.823 Y-99.934
G1 X31.912 Y-99.663
G1 X31.985 Y-99.388

(Linearized circular move - low memory mode enabled)
G1 X32.033 Y-99.161
G1 X32.07 Y-98.933
G1 X32.097 Y-98.702
G1 X32.114 Y-98.471
G1 X32.12 Y-98.239

(Linearized circular move - low memory mode enabled)
G1 X32.118 Y-97.964
G1 X32.101 Y-97.69
G1 X32.07 Y-97.418
G1 X32.023 Y-97.147
G1 X31.962 Y-96.88
G1 X31.886 Y-96.616
G1 X31.796 Y-96.357
G1 X31.692 Y-96.103
G1 X31.574 Y-95.855
G1 X31.443 Y-95.614
G1 X31.299 Y-95.381
G1 X31.142 Y-95.156
G1 X30.974 Y-94.939
G1 X30.793 Y-94.732
G1 X30.602 Y-94.536
G1 X30.4 Y-94.35

(Linearized circular move - low memory mode enabled)
G1 X30.183 Y-94.17
G1 X29.956 Y-94.002
G1 X29.72 Y-93.847
G1 X29.476 Y-93.705
G1 X29.225 Y-93.576
G1 X28.967 Y-93.461
G1 X28.704 Y-93.36
G1 X28.435 Y-93.274
G1 X28.162 Y-93.202
G1 X27.886 Y-93.145
G1 X27.607 Y-93.103
G1 X27.326 Y-93.077
G1 X27.044 Y-93.065
G1 X26.762 Y-93.069
G1 X26.48 Y-93.088
G1 X26.2 Y-93.122
G1 X25.922 Y-93.172
G1 X25.648 Y-93.236
G1 X25.377 Y-93.315
G1 X25.111 Y-93.409
G1 X24.85 Y-93.517
G1 X24.596 Y-93.639
G1 X24.348 Y-93.775
G1 X24.108 Y-93.923
G1 X23.877 Y-94.085
G1 X23.655 Y-94.259
G1 X23.442 Y-94.444
G1 X23.24 Y-94.641
G1 X23.049 Y-94.849

(Linearized circular move - low memory mode enabled)
G1 X22.868 Y-95.067
G1 X22.7 Y-95.294
G1 X22.545 Y-95.53
G1 X22.403 Y-95.775
G1 X22.274 Y-96.026
G1 X22.159 Y-96.285
G1 X22.058 Y-96.549
G1 X21.971 Y-96.818
G1 X21.9 Y-97.091

(Linearized circular move - low memory mode enabled)
G1 X21.839 Y-97.372
G1 X21.794 Y-97.655
G1 X21.764 Y-97.94
G1 X21.75 Y-98.226
G1 X21.751 Y-98.513
G1 X21.768 Y-98.799
G1 X21.8 Y-99.084
G1 X21.848 Y-99.367
G1 X21.911 Y-99.647
G1 X21.989 Y-99.923
G1 X22.082 Y-100.194
G1 X22.189 Y-100.46
G1 X22.31 Y-100.719
G1 X22.446 Y-100.972
G1 X22.595 Y-101.217
G1 X22.757 Y-101.454
G1 X22.931 Y-101.681
G1 X23.118 Y-101.899
G1 X23.316 Y-102.106
G1 X23.525 Y-102.302
G1 X23.744 Y-102.487
G1 X23.973 Y-102.66
G1 X24.211 Y-102.819
G1 X24.457 Y-102.966
G1 X24.711 Y-103.099
G1 X24.972 Y-103.219
G1 X25.239 Y-103.324
G1 X25.511 Y-103.414
G1 X25.788 Y-103.489

(Linearized circular move - low memory mode enabled)
G1 X26.062 Y-103.549
G1 X26.339 Y-103.593
G1 X26.617 Y-103.623
G1 X26.898 Y-103.638
G1 X27.178 Y-103.639
G1 X27.458 Y-103.624
G1 X27.737 Y-103.595
G1 X28.014 Y-103.55
G1 X28.288 Y-103.492
G1 X28.559 Y-103.419
G1 X28.825 Y-103.331
G1 X29.087 Y-103.23
G1 X29.343 Y-103.115
G1 X29.592 Y-102.986
G1 X29.834 Y-102.845

(Linearized circular move - low memory mode enabled)
G1 X30.082 Y-102.685
G1 X30.322 Y-102.512
G1 X30.552 Y-102.327
G1 X30.771 Y-102.129
G1 X30.979 Y-101.92
G1 X31.176 Y-101.699
G1 X31.36 Y-101.469
G1 X31.532 Y-101.228
G1 X31.69 Y-100.979
G1 X31.835 Y-100.722
G1 X31.966 Y-100.457
G1 X32.082 Y-100.186
G1 X32.183 Y-99.908
G1 X32.269 Y-99.626
G1 X32.34 Y-99.339
G1 X32.394 Y-99.049
G1 X32.434 Y-98.756
G1 X32.457 Y-98.462
G1 X32.464 Y-98.167
G1 X32.456 Y-97.871
G1 X32.431 Y-97.577
G1 X32.39 Y-97.285
G1 X32.334 Y-96.995
G1 X32.262 Y-96.708
G1 X32.175 Y-96.426
G1 X32.072 Y-96.149
G1 X31.955 Y-95.878
G1 X31.823 Y-95.614
G1 X31.677 Y-95.358

(Linearized circular move - low memory mode enabled)
G1 X31.52 Y-95.113
G1 X31.35 Y-94.877
G1 X31.168 Y-94.65
G1 X30.973 Y-94.433
G1 X30.768 Y-94.227
G1 X30.552 Y-94.033
G1 X30.326 Y-93.85
G1 X30.09 Y-93.679
G1 X29.846 Y-93.521
G1 X29.593 Y-93.377
G1 X29.333 Y-93.246
G1 X29.067 Y-93.129
G1 X28.795 Y-93.027
G1 X28.517 Y-92.939
G1 X28.236 Y-92.866
G1 X27.951 Y-92.809
G1 X27.663 Y-92.766
G1 X27.373 Y-92.739
G1 X27.083 Y-92.728
G1 X26.792 Y-92.732
G1 X26.501 Y-92.751
G1 X26.213 Y-92.786
G1 X25.926 Y-92.837
G1 X25.643 Y-92.902

(Linearized circular move - low memory mode enabled)
G1 X25.36 Y-92.981
G1 X25.082 Y-93.074
G1 X24.809 Y-93.181
G1 X24.543 Y-93.303
G1 X24.283 Y-93.439
G1 X24.03 Y-93.588
G1 X23.786 Y-93.75
G1 X23.55 Y-93.924
G1 X23.324 Y-94.111
G1 X23.108 Y-94.309
G1 X22.903 Y-94.518
G1 X22.709 Y-94.738
G1 X22.526 Y-94.968
G1 X22.356 Y-95.207
G1 X22.199 Y-95.454
G1 X22.055 Y-95.709
G1 X21.924 Y-95.972
G1 X21.807 Y-96.241
G1 X21.705 Y-96.515
G1 X21.617 Y-96.795
G1 X21.543 Y-97.079
G1 X21.485 Y-97.366
G1 X21.442 Y-97.656
G1 X21.414 Y-97.948
G1 X21.402 Y-98.241
G1 X21.404 Y-98.534
G1 X21.422 Y-98.827
G1 X21.456 Y-99.118
G1 X21.504 Y-99.407
G1 X21.568 Y-99.694

(Linearized circular move - low memory mode enabled)
G1 X21.643 Y-99.965
G1 X21.732 Y-100.232
G1 X21.834 Y-100.495
G1 X21.949 Y-100.752
G1 X22.077 Y-101.003
G1 X22.217 Y-101.247
G1 X22.37 Y-101.484
G1 X22.534 Y-101.713
G1 X22.71 Y-101.934

(Linearized circular move - low memory mode enabled)
G1 X22.905 Y-102.156
G1 X23.112 Y-102.369
G1 X23.329 Y-102.57
G1 X23.557 Y-102.76
G1 X23.795 Y-102.937
G1 X24.041 Y-103.102
G1 X24.296 Y-103.253
G1 X24.558 Y-103.391
G1 X24.827 Y-103.515
G1 X25.103 Y-103.625
G1 X25.383 Y-103.721
G1 X25.669 Y-103.801
G1 X25.958 Y-103.867
G1 X26.25 Y-103.917
G1 X26.544 Y-103.952
G1 X26.84 Y-103.971
G1 X27.136 Y-103.975
G1 X27.432 Y-103.964
G1 X27.728 Y-103.937
G1 X28.021 Y-103.894
G1 X28.312 Y-103.836
G1 X28.599 Y-103.763
G1 X28.882 Y-103.676
G1 X29.16 Y-103.573
G1 X29.432 Y-103.456
G1 X29.698 Y-103.325
G1 X29.957 Y-103.18
G1 X30.208 Y-103.022
G1 X30.45 Y-102.851
G1 X30.682 Y-102.668

(Linearized circular move - low memory mode enabled)
G1 X30.891 Y-102.485
G1 X31.091 Y-102.293
G1 X31.281 Y-102.091
G1 X31.461 Y-101.88

(Linearized circular move - low memory mode enabled)
G1 X31.648 Y-101.642
G1 X31.823 Y-101.395
G1 X31.985 Y-101.139
G1 X32.134 Y-100.875
G1 X32.268 Y-100.603
G1 X32.388 Y-100.325
G1 X32.493 Y-100.041
G1 X32.583 Y-99.751
G1 X32.658 Y-99.458
G1 X32.717 Y-99.161
G1 X32.761 Y-98.861
G1 X32.789 Y-98.559
G1 X32.801 Y-98.256
G1 X32.797 Y-97.953
G1 X32.778 Y-97.651
G1 X32.742 Y-97.35
G1 X32.691 Y-97.052
G1 X32.625 Y-96.756
G1 X32.543 Y-96.464
G1 X32.446 Y-96.177
G1 X32.334 Y-95.896
G1 X32.207 Y-95.621
G1 X32.066 Y-95.352
G1 X31.911 Y-95.092
G1 X31.743 Y-94.84
G1 X31.562 Y-94.597
G1 X31.369 Y-94.364
G1 X31.163 Y-94.141
G1 X30.946 Y-93.929
G1 X30.719 Y-93.729

(Linearized circular move - low memory mode enabled)
G1 X30.49 Y-93.549
G1 X30.253 Y-93.379
G1 X30.007 Y-93.222
G1 X29.753 Y-93.078
G1 X29.493 Y-92.946
G1 X29.227 Y-92.828
G1 X28.954 Y-92.723
G1 X28.677 Y-92.633
G1 X28.396 Y-92.556
G1 X28.111 Y-92.493
G1 X27.823 Y-92.445
G1 X27.534 Y-92.412
G1 X27.243 Y-92.393
G1 X26.951 Y-92.389
G1 X26.659 Y-92.399
G1 X26.369 Y-92.425
G1 X26.08 Y-92.464
G1 X25.793 Y-92.519

(Linearized circular move - low memory mode enabled)
G1 X25.5 Y-92.588
G1 X25.21 Y-92.672
G1 X24.925 Y-92.77
G1 X24.646 Y-92.883
G1 X24.372 Y-93.01
G1 X24.105 Y-93.15
G1 X23.846 Y-93.304
G1 X23.595 Y-93.471
G1 X23.353 Y-93.65
G1 X23.119 Y-93.842
G1 X22.896 Y-94.045
G1 X22.684 Y-94.259
G1 X22.483 Y-94.483
G1 X22.293 Y-94.717
G1 X22.115 Y-94.961
G1 X21.95 Y-95.213
G1 X21.798 Y-95.474
G1 X21.659 Y-95.741
G1 X21.534 Y-96.016
G1 X21.423 Y-96.296
G1 X21.327 Y-96.582
G1 X21.245 Y-96.872
G1 X21.178 Y-97.166
G1 X21.126 Y-97.463
G1 X21.089 Y-97.762
G1 X21.067 Y-98.063
G1 X21.06 Y-98.364
G1 X21.069 Y-98.666
G1 X21.093 Y-98.966
G1 X21.132 Y-99.265
G1 X21.186 Y-99.562

(Linearized circular move - low memory mode enabled)
G1 X21.255 Y-99.855
G1 X21.339 Y-100.144
G1 X21.437 Y-100.428
G1 X21.549 Y-100.707
G1 X21.676 Y-100.98
G1 X21.816 Y-101.247
G1 X21.969 Y-101.506
G1 X22.136 Y-101.756
G1 X22.314 Y-101.999
G1 X22.505 Y-102.231
G1 X22.707 Y-102.454
G1 X22.921 Y-102.666
G1 X23.144 Y-102.868
G1 X23.378 Y-103.057
G1 X23.621 Y-103.235
G1 X23.872 Y-103.4
G1 X24.132 Y-103.552
G1 X24.399 Y-103.691
G1 X24.673 Y-103.816
G1 X24.952 Y-103.928

(Linearized circular move - low memory mode enabled)
G1 X25.237 Y-104.026
G1 X25.526 Y-104.109
G1 X25.819 Y-104.178
G1 X26.115 Y-104.233
G1 X26.413 Y-104.273
G1 X26.713 Y-104.298
G1 X27.013 Y-104.308
G1 X27.314 Y-104.303
G1 X27.615 Y-104.283
G1 X27.913 Y-104.248
G1 X28.21 Y-104.199
G1 X28.504 Y-104.134
G1 X28.795 Y-104.056
G1 X29.081 Y-103.963
G1 X29.362 Y-103.855
G1 X29.637 Y-103.734
G1 X29.906 Y-103.6

(Linearized circular move - low memory mode enabled)
G1 X30.169 Y-103.452
G1 X30.423 Y-103.292
G1 X30.669 Y-103.119
G1 X30.906 Y-102.933
G1 X31.134 Y-102.737
G1 X31.352 Y-102.529
G1 X31.559 Y-102.311
G1 X31.755 Y-102.083
G1 X31.94 Y-101.845
G1 X32.112 Y-101.599
G1 X32.272 Y-101.344
G1 X32.419 Y-101.081
G1 X32.553 Y-100.812
G1 X32.673 Y-100.536
G1 X32.78 Y-100.255
G1 X32.872 Y-99.968
G1 X32.95 Y-99.678
G1 X33.012 Y-99.456

(Linearized circular move - low memory mode enabled)
G1 X33.068 Y-99.15
G1 X33.109 Y-98.841
G1 X33.134 Y-98.53
G1 X33.144 Y-98.219
G1 X33.137 Y-97.908
G1 X33.115 Y-97.597
G1 X33.077 Y-97.288
G1 X33.024 Y-96.981
G1 X32.955 Y-96.677
G1 X32.871 Y-96.377
G1 X32.772 Y-96.082
G1 X32.657 Y-95.792
G1 X32.529 Y-95.508
G1 X32.386 Y-95.231
G1 X32.229 Y-94.962
G1 X32.059 Y-94.701
G1 X31.876 Y-94.449
G1 X31.68 Y-94.207
G1 X31.473 Y-93.975
G1 X31.253 Y-93.753
G1 X31.023 Y-93.543
G1 X30.783 Y-93.345
G1 X30.533 Y-93.16
G1 X30.273 Y-92.987
G1 X30.006 Y-92.828
G1 X29.73 Y-92.682
G1 X29.448 Y-92.551
G1 X29.159 Y-92.434
G1 X28.865 Y-92.332
G1 X28.566 Y-92.244
G1 X28.263 Y-92.172

(Linearized circular move - low memory mode enabled)
G1 X27.97 Y-92.118
G1 X27.674 Y-92.078
G1 X27.377 Y-92.052
G1 X27.079 Y-92.041
G1 X26.781 Y-92.044
G1 X26.484 Y-92.062
G1 X26.188 Y-92.094
G1 X25.893 Y-92.14
G1 X25.601 Y-92.201
G1 X25.313 Y-92.275
G1 X25.028 Y-92.364
G1 X24.748 Y-92.466
G1 X24.473 Y-92.582
G1 X24.205 Y-92.71

(Linearized circular move - low memory mode enabled)
G1 X23.931 Y-92.855
G1 X23.666 Y-93.014
G1 X23.408 Y-93.185
G1 X23.159 Y-93.368
G1 X22.919 Y-93.564
G1 X22.689 Y-93.771
G1 X22.47 Y-93.989
G1 X22.262 Y-94.217
G1 X22.065 Y-94.456
G1 X21.88 Y-94.704
G1 X21.707 Y-94.961
G1 X21.547 Y-95.225
G1 X21.401 Y-95.498
G1 X21.268 Y-95.777
G1 X21.148 Y-96.063
G1 X21.043 Y-96.353
G1 X20.953 Y-96.649
G1 X20.877 Y-96.949
G1 X20.815 Y-97.252
G1 X20.769 Y-97.558
G1 X20.738 Y-97.866
G1 X20.722 Y-98.175
G1 X20.721 Y-98.484
G1 X20.735 Y-98.793
G1 X20.764 Y-99.101
G1 X20.809 Y-99.407
G1 X20.868 Y-99.711
G1 X20.943 Y-100.011
G1 X21.032 Y-100.307
G1 X21.135 Y-100.599
G1 X21.252 Y-100.885
G1 X21.384 Y-101.165

(Linearized circular move - low memory mode enabled)
G1 X21.53 Y-101.44
G1 X21.689 Y-101.707
G1 X21.862 Y-101.967
G1 X22.047 Y-102.217
G1 X22.244 Y-102.458
G1 X22.453 Y-102.689
G1 X22.673 Y-102.909
G1 X22.903 Y-103.119
G1 X23.144 Y-103.316
G1 X23.394 Y-103.502
G1 X23.653 Y-103.675
G1 X23.92 Y-103.834
G1 X24.195 Y-103.981
G1 X24.477 Y-104.114
G1 X24.764 Y-104.232
G1 X25.058 Y-104.337
G1 X25.356 Y-104.426
G1 X25.658 Y-104.501
G1 X25.964 Y-104.561
G1 X26.272 Y-104.606
G1 X26.582 Y-104.635
G1 X26.893 Y-104.649

(Linearized circular move - low memory mode enabled)
G1 X27.21 Y-104.648
G1 X27.527 Y-104.632
G1 X27.842 Y-104.6
G1 X28.156 Y-104.553
G1 X28.466 Y-104.49
G1 X28.774 Y-104.412
G1 X29.077 Y-104.319
G1 X29.375 Y-104.212
G1 X29.668 Y-104.089
G1 X29.954 Y-103.953
G1 X30.233 Y-103.803
G1 X30.505 Y-103.639
G1 X30.768 Y-103.462
G1 X31.022 Y-103.273
G1 X31.267 Y-103.071
G1 X31.501 Y-102.858
G1 X31.724 Y-102.633
G1 X31.937 Y-102.397
G1 X32.137 Y-102.152
G1 X32.325 Y-101.896
G1 X32.501 Y-101.632
G1 X32.663 Y-101.36
G1 X32.812 Y-101.08
G1 X32.947 Y-100.793
G1 X33.067 Y-100.5
G1 X33.173 Y-100.201
G1 X33.264 Y-99.897
G1 X33.341 Y-99.59
G1 X33.402 Y-99.278
G1 X33.447 Y-98.965
G1 X33.478 Y-98.649
G1 X33.492 Y-98.332

(Linearized circular move - low memory mode enabled)
G1 Y-98.017
G1 X33.475 Y-97.703
G1 X33.444 Y-97.389
G1 X33.397 Y-97.077
G1 X33.335 Y-96.768
G1 X33.258 Y-96.463
G1 X33.167 Y-96.161
G1 X33.06 Y-95.864
G1 X32.94 Y-95.573
G1 X32.805 Y-95.288
G1 X32.657 Y-95.01
G1 X32.495 Y-94.74
G1 X32.32 Y-94.478

(Linearized circular move - low memory mode enabled)
G1 X32.134 Y-94.224
G1 X31.937 Y-93.979
G1 X31.728 Y-93.744
G1 X31.507 Y-93.52
G1 X31.276 Y-93.307
G1 X31.035 Y-93.105
G1 X30.784 Y-92.915
G1 X30.524 Y-92.738
G1 X30.256 Y-92.574
G1 X29.98 Y-92.423
G1 X29.697 Y-92.285
G1 X29.408 Y-92.162
G1 X29.113 Y-92.053
G1 X28.813 Y-91.958
G1 X28.509 Y-91.878
G1 X28.201 Y-91.813
G1 X27.891 Y-91.764

(Linearized circular move - low memory mode enabled)
G1 X27.567 Y-91.728
G1 X27.242 Y-91.708
G1 X26.917 Y-91.705
G1 X26.591 Y-91.717
G1 X26.267 Y-91.745
G1 X25.944 Y-91.789
G1 X25.624 Y-91.849
G1 X25.307 Y-91.924
G1 X24.994 Y-92.015
G1 X24.687 Y-92.121
G1 X24.384 Y-92.243
G1 X24.088 Y-92.378
G1 X23.799 Y-92.529
G1 X23.518 Y-92.693
G1 X23.245 Y-92.871
G1 X22.982 Y-93.062
G1 X22.727 Y-93.265
G1 X22.484 Y-93.481
G1 X22.251 Y-93.709
G1 X22.029 Y-93.948
G1 X21.82 Y-94.197
G1 X21.623 Y-94.456
G1 X21.439 Y-94.725
G1 X21.268 Y-95.002
G1 X21.111 Y-95.288
G1 X20.969 Y-95.581
G1 X20.841 Y-95.88
G1 X20.728 Y-96.185
G1 X20.629 Y-96.496
G1 X20.547 Y-96.811
G1 X20.479 Y-97.129
G1 X20.428 Y-97.451

(Linearized circular move - low memory mode enabled)
G1 X20.393 Y-97.772
G1 X20.373 Y-98.095
G1 X20.369 Y-98.418
G1 X20.381 Y-98.741
G1 X20.408 Y-99.063
G1 X20.451 Y-99.384
G1 X20.51 Y-99.701
G1 X20.584 Y-100.016
G1 X20.673 Y-100.327
G1 X20.777 Y-100.633
G1 X20.897 Y-100.933
G1 X21.03 Y-101.227
G1 X21.178 Y-101.515
G1 X21.34 Y-101.795
G1 X21.515 Y-102.067
G1 X21.703 Y-102.33
G1 X21.903 Y-102.583
G1 X22.116 Y-102.826
G1 X22.341 Y-103.059
G1 X22.576 Y-103.28
G1 X22.822 Y-103.49
G1 X23.078 Y-103.688
G1 X23.343 Y-103.873

(Linearized circular move - low memory mode enabled)
G1 X23.619 Y-104.047
G1 X23.903 Y-104.208
G1 X24.194 Y-104.356
G1 X24.492 Y-104.488
G1 X24.797 Y-104.606
G1 X25.106 Y-104.71
G1 X25.421 Y-104.798
G1 X25.739 Y-104.87
G1 X26.06 Y-104.927
G1 X26.384 Y-104.969
G1 X26.709 Y-104.994
G1 X27.036 Y-105.004
G1 X27.362 Y-104.998
G1 X27.688 Y-104.976
G1 X28.012 Y-104.938
G1 X28.334 Y-104.884
G1 X28.653 Y-104.815

(Linearized circular move - low memory mode enabled)
G1 X28.968 Y-104.73
G1 X29.279 Y-104.631
G1 X29.585 Y-104.516
G1 X29.884 Y-104.386
G1 X30.177 Y-104.242
G1 X30.463 Y-104.084
G1 X30.741 Y-103.913
G1 X31.01 Y-103.728
G1 X31.269 Y-103.53
G1 X31.519 Y-103.32
G1 X31.758 Y-103.097
G1 X31.986 Y-102.864
G1 X32.203 Y-102.62
G1 X32.407 Y-102.365
G1 X32.599 Y-102.101
G1 X32.777 Y-101.828
G1 X32.943 Y-101.546

(Linearized circular move - low memory mode enabled)
G1 X33.096 Y-101.26
G1 X33.236 Y-100.967
G1 X33.362 Y-100.668
G1 X33.474 Y-100.363
G1 X33.571 Y-100.053
G1 X33.653 Y-99.739
G1 X33.72 Y-99.422
G1 X33.772 Y-99.101
G1 X33.808 Y-98.778
G1 X33.829 Y-98.455
G1 X33.835 Y-98.13
G1 X33.825 Y-97.805
G1 X33.8 Y-97.482
G1 X33.759 Y-97.16
G1 X33.703 Y-96.84
G1 X33.632 Y-96.523
G1 X33.546 Y-96.21
G1 X33.446 Y-95.901
G1 X33.33 Y-95.598
G1 X33.201 Y-95.3
G1 X33.057 Y-95.009
G1 X32.9 Y-94.725
G1 X32.729 Y-94.449
G1 X32.545 Y-94.181
G1 X32.349 Y-93.923
G1 X32.141 Y-93.674
G1 X31.921 Y-93.435
G1 X31.69 Y-93.207
G1 X31.448 Y-92.99
G1 X31.197 Y-92.785
G1 X30.935 Y-92.592
G1 X30.665 Y-92.411
G1 X30.387 Y-92.244

(Linearized circular move - low memory mode enabled)
G1 X30.099 Y-92.089
G1 X29.804 Y-91.949
G1 X29.503 Y-91.822
G1 X29.196 Y-91.71
G1 X28.884 Y-91.613
G1 X28.568 Y-91.531
G1 X28.248 Y-91.464
G1 X27.926 Y-91.413
G1 X27.601 Y-91.377
G1 X27.275 Y-91.357
G1 X26.948 Y-91.352
G1 X26.621 Y-91.363
G1 X26.296 Y-91.39
G1 X25.972 Y-91.432
G1 X25.65 Y-91.489

(Linearized circular move - low memory mode enabled)
G1 X25.336 Y-91.557
G1 X25.025 Y-91.639
G1 X24.718 Y-91.736
G1 X24.416 Y-91.846
G1 X24.12 Y-91.971
G1 X23.83 Y-92.11
G1 X23.547 Y-92.262
G1 X23.271 Y-92.427
G1 X23.003 Y-92.605
G1 X22.744 Y-92.795
G1 X22.494 Y-92.997
G1 X22.253 Y-93.211
G1 X22.023 Y-93.435
G1 X21.804 Y-93.671
G1 X21.596 Y-93.916
G1 X21.4 Y-94.17
G1 X21.216 Y-94.434

(Linearized circular move - low memory mode enabled)
G1 X21.036 Y-94.716
G1 X20.871 Y-95.006
G1 X20.719 Y-95.303
G1 X20.581 Y-95.608
G1 X20.459 Y-95.918
G1 X20.351 Y-96.235
G1 X20.258 Y-96.555
G1 X20.181 Y-96.88
G1 X20.119 Y-97.209
G1 X20.073 Y-97.539
G1 X20.043 Y-97.872
G1 X20.028 Y-98.206
G1 X20.03 Y-98.54
G1 X20.047 Y-98.873
G1 X20.081 Y-99.206
G1 X20.13 Y-99.536
G1 X20.194 Y-99.864
G1 X20.275 Y-100.188
G1 X20.37 Y-100.508
G1 X20.481 Y-100.823
G1 X20.606 Y-101.133
G1 X20.746 Y-101.436
G1 X20.901 Y-101.732
G1 X21.069 Y-102.021
G1 X21.251 Y-102.301
G1 X21.446 Y-102.572
G1 X21.654 Y-102.834
G1 X21.873 Y-103.085
G1 X22.105 Y-103.326
G1 X22.348 Y-103.555
G1 X22.601 Y-103.773
G1 X22.865 Y-103.978
G1 X23.138 Y-104.171

(Linearized circular move - low memory mode enabled)
G1 X23.418 Y-104.349
G1 X23.706 Y-104.514
G1 X24.002 Y-104.665
G1 X24.304 Y-104.802
G1 X24.613 Y-104.924
G1 X24.927 Y-105.032
G1 X25.246 Y-105.124
G1 X25.569 Y-105.202
G1 X25.895 Y-105.264
G1 X26.224 Y-105.311
G1 X26.555 Y-105.342
G1 X26.887 Y-105.357
G1 X27.219
G1 X27.551 Y-105.341
G1 X27.881 Y-105.309
G1 X28.21 Y-105.261
G1 X28.536 Y-105.199
G1 X28.859 Y-105.12
G1 X29.178 Y-105.027
G1 X29.492 Y-104.919
G1 X29.8 Y-104.795
G1 X30.102 Y-104.658
G1 X30.398 Y-104.506

(Linearized circular move - low memory mode enabled)
G1 X30.686 Y-104.343
G1 X30.967 Y-104.167
G1 X31.24 Y-103.978
G1 X31.503 Y-103.777
G1 X31.757 Y-103.563
G1 X32.0 Y-103.339
G1 X32.233 Y-103.103
G1 X32.455 Y-102.856
G1 X32.665 Y-102.6
G1 X32.864 Y-102.334
G1 X33.049 Y-102.06
G1 X33.222 Y-101.777
G1 X33.382 Y-101.486
G1 X33.528 Y-101.189
G1 X33.66 Y-100.885
G1 X33.778 Y-100.575
G1 X33.881 Y-100.26
G1 X33.97 Y-99.941
G1 X34.044 Y-99.617
G1 X34.103 Y-99.291
G1 X34.147 Y-98.963
G1 X34.176 Y-98.632
G1 X34.189 Y-98.301
G1 X34.187 Y-97.97
G1 X34.17 Y-97.639
G1 X34.137 Y-97.309
G1 X34.089 Y-96.981
G1 X34.027 Y-96.655
G1 X33.949 Y-96.333
G1 X33.856 Y-96.015
G1 X33.749 Y-95.701
G1 X33.627 Y-95.393
G1 X33.492 Y-95.09
G1 X33.342 Y-94.794

(Linearized circular move - low memory mode enabled)
G1 X33.18 Y-94.507
G1 X33.005 Y-94.228
G1 X32.818 Y-93.958
G1 X32.618 Y-93.696
G1 X32.407 Y-93.443
G1 X32.184 Y-93.201
G1 X31.95 Y-92.969
G1 X31.706 Y-92.748
G1 X31.451 Y-92.538
G1 X31.188 Y-92.341
G1 X30.916 Y-92.155
G1 X30.635 Y-91.983
G1 X30.347 Y-91.823
G1 X30.052 Y-91.677
G1 X29.751 Y-91.544
G1 X29.443 Y-91.426
G1 X29.131 Y-91.321
G1 X28.814 Y-91.231
G1 X28.493 Y-91.156
G1 X28.17 Y-91.095
G1 X27.843 Y-91.05
G1 X27.515 Y-91.019
G1 X27.186 Y-91.004
G1 X26.857
G1 X26.528 Y-91.018
G1 X26.2 Y-91.048
G1 X25.874 Y-91.093

(Linearized circular move - low memory mode enabled)
G1 X25.538 Y-91.152
G1 X25.206 Y-91.227
G1 X24.878 Y-91.317
G1 X24.554 Y-91.423
G1 X24.236 Y-91.543
G1 X23.923 Y-91.678
G1 X23.617 Y-91.828
G1 X23.319 Y-91.992
G1 X23.028 Y-92.169
G1 X22.746 Y-92.36
G1 X22.473 Y-92.563
G1 X22.21 Y-92.78
G1 X21.957 Y-93.008
G1 X21.715 Y-93.247
G1 X21.485 Y-93.498
G1 X21.266 Y-93.759
G1 X21.06 Y-94.031
G1 X20.867 Y-94.311
G1 X20.687 Y-94.6
G1 X20.521 Y-94.897
G1 X20.369 Y-95.202
G1 X20.231 Y-95.513
G1 X20.108 Y-95.831
G1 X20.0 Y-96.153
G1 X19.907 Y-96.481
G1 X19.829 Y-96.812
G1 X19.767 Y-97.147
G1 X19.72 Y-97.485
G1 X19.689 Y-97.824
G1 X19.674 Y-98.164
G1 X19.675 Y-98.504
G1 X19.692 Y-98.844
G1 X19.725 Y-99.183

(Linearized circular move - low memory mode enabled)
G1 X19.772 Y-99.521
G1 X19.835 Y-99.856
G1 X19.913 Y-100.188
G1 X20.006 Y-100.516
G1 X20.114 Y-100.839
G1 X20.237 Y-101.157
G1 X20.375 Y-101.469
G1 X20.527 Y-101.775
G1 X20.693 Y-102.072
G1 X20.873 Y-102.362
G1 X21.065 Y-102.644
G1 X21.271 Y-102.916
G1 X21.489 Y-103.178
G1 X21.718 Y-103.43
G1 X21.96 Y-103.671
G1 X22.212 Y-103.901
G1 X22.474 Y-104.119
G1 X22.746 Y-104.324
G1 X23.027 Y-104.517
G1 X23.317 Y-104.696
G1 X23.615 Y-104.862
G1 X23.92 Y-105.014
G1 X24.232 Y-105.152
G1 X24.55 Y-105.275
G1 X24.874 Y-105.383
G1 X25.202 Y-105.477
G1 X25.534 Y-105.555
G1 X25.869 Y-105.617
G1 X26.206 Y-105.665
G1 X26.546 Y-105.696
G1 X26.887 Y-105.712
G1 X27.228
G1 X27.568 Y-105.696
G1 X27.908 Y-105.664

(Linearized circular move - low memory mode enabled)
G1 X28.226 Y-105.62
G1 X28.543 Y-105.562
G1 X28.857 Y-105.491
G1 X29.167 Y-105.406
G1 X29.473 Y-105.307
G1 X29.775 Y-105.195
G1 X30.072 Y-105.07
G1 X30.362 Y-104.932
G1 X30.647 Y-104.782
G1 X30.925 Y-104.619
G1 X31.195 Y-104.445
G1 X31.457 Y-104.258
G1 X31.712 Y-104.061

(Linearized circular move - low memory mode enabled)
G1 X31.971 Y-103.844
G1 X32.22 Y-103.616
G1 X32.459 Y-103.377
G1 X32.687 Y-103.128
G1 X32.903 Y-102.868
G1 X33.108 Y-102.599
G1 X33.3 Y-102.321
G1 X33.48 Y-102.035
G1 X33.646 Y-101.741
G1 X33.8 Y-101.44
G1 X33.939 Y-101.132
G1 X34.065 Y-100.819
G1 X34.176 Y-100.5
G1 X34.273 Y-100.176
G1 X34.356 Y-99.848
G1 X34.423 Y-99.517
G1 X34.476 Y-99.183
G1 X34.514 Y-98.848
G1 X34.536 Y-98.51
G1 X34.544 Y-98.173
G1 X34.536 Y-97.835
G1 X34.513 Y-97.498
G1 X34.475 Y-97.162
G1 X34.422 Y-96.828
G1 X34.354 Y-96.497
G1 X34.271 Y-96.17
G1 X34.174 Y-95.846
G1 X34.062 Y-95.527
G1 X33.936 Y-95.214
G1 X33.796 Y-94.906
G1 X33.643 Y-94.605
G1 X33.476 Y-94.311
G1 X33.296 Y-94.025
G1 X33.103 Y-93.748
G1 X32.898 Y-93.479

(Linearized circular move - low memory mode enabled)
G1 X32.677 Y-93.214
G1 X32.444 Y-92.96
G1 X32.2 Y-92.717
G1 X31.945 Y-92.486
G1 X31.679 Y-92.266
G1 X31.404 Y-92.059
G1 X31.119 Y-91.864
G1 X30.826 Y-91.683
G1 X30.525 Y-91.515
G1 X30.216 Y-91.362
G1 X29.901 Y-91.222
G1 X29.579 Y-91.097
G1 X29.253 Y-90.987
G1 X28.921 Y-90.892
G1 X28.586 Y-90.813
G1 X28.247 Y-90.748
G1 X27.906 Y-90.7
G1 X27.563 Y-90.667
G1 X27.219 Y-90.65
G1 X26.874 Y-90.648
G1 X26.53 Y-90.662
G1 X26.186 Y-90.692
G1 X25.844 Y-90.738

(Linearized circular move - low memory mode enabled)
G1 X25.505 Y-90.797
G1 X25.169 Y-90.871
G1 X24.836 Y-90.96
G1 X24.507 Y-91.064
G1 X24.184 Y-91.183
G1 X23.866 Y-91.316
G1 X23.555 Y-91.463
G1 X23.25 Y-91.624
G1 X22.953 Y-91.799
G1 X22.664 Y-91.986
G1 X22.384 Y-92.187
G1 X22.113 Y-92.4
G1 X21.852 Y-92.624
G1 X21.601 Y-92.861
G1 X21.361 Y-93.108
G1 X21.133 Y-93.365
G1 X20.916 Y-93.633
G1 X20.711 Y-93.91
G1 X20.519 Y-94.197
G1 X20.34 Y-94.491
G1 X20.175 Y-94.793
G1 X20.023 Y-95.102
G1 X19.885 Y-95.418
G1 X19.762 Y-95.74
G1 X19.653 Y-96.067
G1 X19.559 Y-96.398
G1 X19.48 Y-96.733
G1 X19.416 Y-97.072
G1 X19.367 Y-97.413
G1 X19.333 Y-97.756
G1 X19.315 Y-98.1
G1 X19.313 Y-98.444
G1 X19.326 Y-98.789
G1 X19.354 Y-99.132
G1 X19.398 Y-99.474

(Linearized circular move - low memory mode enabled)
G1 X19.453 Y-99.795
G1 X19.522 Y-100.114
G1 X19.605 Y-100.429
G1 X19.7 Y-100.741
G1 X19.809 Y-101.048
G1 X19.931 Y-101.351
G1 X20.066 Y-101.648
G1 X20.213 Y-101.939

(Linearized circular move - low memory mode enabled)
G1 X20.382 Y-102.242
G1 X20.565 Y-102.538
G1 X20.761 Y-102.826
G1 X20.969 Y-103.104
G1 X21.19 Y-103.373
G1 X21.422 Y-103.631
G1 X21.666 Y-103.879
G1 X21.921 Y-104.115
G1 X22.186 Y-104.34
G1 X22.461 Y-104.553
G1 X22.745 Y-104.753
G1 X23.038 Y-104.941
G1 X23.339 Y-105.115
G1 X23.648 Y-105.275
G1 X23.963 Y-105.421
G1 X24.285 Y-105.553
G1 X24.612 Y-105.671
G1 X24.944 Y-105.773
G1 X25.281 Y-105.861
G1 X25.621 Y-105.933
G1 X25.964 Y-105.991
G1 X26.309 Y-106.032
G1 X26.656 Y-106.058
G1 X27.003 Y-106.069
G1 X27.351 Y-106.064
G1 X27.698 Y-106.043
G1 X28.044 Y-106.007
G1 X28.387 Y-105.955
G1 X28.729 Y-105.888
G1 X29.066 Y-105.806
G1 X29.4 Y-105.709
G1 X29.729 Y-105.597
G1 X30.053 Y-105.47
G1 X30.371 Y-105.328
G1 X30.682 Y-105.173

(Linearized circular move - low memory mode enabled)
G1 X30.977 Y-105.009
G1 X31.265 Y-104.832
G1 X31.544 Y-104.643
G1 X31.816 Y-104.442
G1 X32.078 Y-104.229
G1 X32.33 Y-104.005
G1 X32.573 Y-103.77

(Linearized circular move - low memory mode enabled)
G1 X32.818 Y-103.515
G1 X33.052 Y-103.249
G1 X33.274 Y-102.973
G1 X33.483 Y-102.687
G1 X33.678 Y-102.392
G1 X33.861 Y-102.089
G1 X34.03 Y-101.778
G1 X34.184 Y-101.46
G1 X34.324 Y-101.135
G1 X34.45 Y-100.804
G1 X34.56 Y-100.467
G1 X34.655 Y-100.126
G1 X34.735 Y-99.782
G1 X34.799 Y-99.433
G1 X34.848 Y-99.083
G1 X34.881 Y-98.73
G1 X34.898 Y-98.377
G1 X34.899 Y-98.023
G1 X34.884 Y-97.669
G1 X34.853 Y-97.317
G1 X34.807 Y-96.966
G1 X34.745 Y-96.617
G1 X34.667 Y-96.272
G1 X34.574 Y-95.931
G1 X34.466 Y-95.594
G1 X34.342 Y-95.262
G1 X34.204 Y-94.936
G1 X34.052 Y-94.617
G1 X33.885 Y-94.305
G1 X33.704 Y-94.0
G1 X33.51 Y-93.704
G1 X33.303 Y-93.417
G1 X33.083 Y-93.14
G1 X32.851 Y-92.873
G1 X32.607 Y-92.616

(Linearized circular move - low memory mode enabled)
G1 X32.353 Y-92.371
G1 X32.088 Y-92.138
G1 X31.812 Y-91.917
G1 X31.527 Y-91.708
G1 X31.233 Y-91.512
G1 X30.93 Y-91.33
G1 X30.62 Y-91.162
G1 X30.302 Y-91.007
G1 X29.978 Y-90.867
G1 X29.648 Y-90.742
G1 X29.312 Y-90.631
G1 X28.972 Y-90.536
G1 X28.628 Y-90.456
G1 X28.28 Y-90.391
G1 X27.931 Y-90.342
G1 X27.579 Y-90.309
G1 X27.226 Y-90.292
G1 X26.873 Y-90.29
G1 X26.52 Y-90.304
G1 X26.168 Y-90.334
G1 X25.818 Y-90.38

(Linearized circular move - low memory mode enabled)
G1 X25.472 Y-90.439
G1 X25.129 Y-90.512
G1 X24.79 Y-90.601
G1 X24.455 Y-90.704
G1 X24.124 Y-90.822
G1 X23.8 Y-90.954
G1 X23.481 Y-91.1
G1 X23.169 Y-91.26
G1 X22.864 Y-91.434
G1 X22.567 Y-91.62
G1 X22.279 Y-91.819
G1 X21.999 Y-92.031
G1 X21.729 Y-92.255
G1 X21.469 Y-92.49
G1 X21.22 Y-92.736
G1 X20.981 Y-92.993
G1 X20.754 Y-93.26
G1 X20.539 Y-93.537
G1 X20.336 Y-93.823
G1 X20.146 Y-94.118
G1 X19.968 Y-94.42
G1 X19.804 Y-94.73
G1 X19.654 Y-95.047
G1 X19.518 Y-95.37
G1 X19.396 Y-95.699
G1 X19.288 Y-96.033
G1 X19.196 Y-96.371
G1 X19.117 Y-96.713
G1 X19.054 Y-97.057
G1 X19.006 Y-97.405
G1 X18.974 Y-97.754
G1 X18.956 Y-98.104
G1 X18.954 Y-98.455
G1 X18.967 Y-98.805
G1 X18.995 Y-99.155
G1 X19.039 Y-99.503

(Linearized circular move - low memory mode enabled)
G1 X19.095 Y-99.834
G1 X19.165 Y-100.164
G1 X19.248 Y-100.489
G1 X19.345 Y-100.812
G1 X19.456 Y-101.129
G1 X19.58 Y-101.442
G1 X19.717 Y-101.75
G1 X19.866 Y-102.051
G1 X20.028 Y-102.346

(Linearized circular move - low memory mode enabled)
G1 X20.21 Y-102.649
G1 X20.406 Y-102.944
G1 X20.614 Y-103.23
G1 X20.834 Y-103.507
G1 X21.067 Y-103.774
G1 X21.31 Y-104.031
G1 X21.565 Y-104.276
G1 X21.83 Y-104.511
G1 X22.105 Y-104.733
G1 X22.39 Y-104.944
G1 X22.683 Y-105.141
G1 X22.985 Y-105.326
G1 X23.294 Y-105.497
G1 X23.611 Y-105.655
G1 X23.935 Y-105.799
G1 X24.264 Y-105.928
G1 X24.598 Y-106.043
G1 X24.938 Y-106.144
G1 X25.281 Y-106.229
G1 X25.628 Y-106.299
G1 X25.977 Y-106.355
G1 X26.329 Y-106.394
G1 X26.682 Y-106.419
G1 X27.036 Y-106.428
G1 X27.389 Y-106.422
G1 X27.742 Y-106.4
G1 X28.094 Y-106.363
G1 X28.444 Y-106.31
G1 X28.791 Y-106.242
G1 X29.135 Y-106.16
G1 X29.476 Y-106.062
G1 X29.811 Y-105.95
G1 X30.141 Y-105.823
G1 X30.466 Y-105.681
G1 X30.784 Y-105.526
G1 X31.094 Y-105.357

(Linearized circular move - low memory mode enabled)
G1 X31.397 Y-105.175
G1 X31.692 Y-104.98
G1 X31.977 Y-104.772
G1 X32.254 Y-104.552
G1 X32.52 Y-104.321
G1 X32.776 Y-104.077
G1 X33.022 Y-103.823

(Linearized circular move - low memory mode enabled)
G1 X33.262 Y-103.556
G1 X33.491 Y-103.278
G1 X33.707 Y-102.991
G1 X33.911 Y-102.694
G1 X34.102 Y-102.389
G1 X34.279 Y-102.076
G1 X34.442 Y-101.755
G1 X34.591 Y-101.428
G1 X34.726 Y-101.094
G1 X34.846 Y-100.755
G1 X34.951 Y-100.411
G1 X35.041 Y-100.063
G1 X35.115 Y-99.711
G1 X35.175 Y-99.356
G1 X35.218 Y-98.999
G1 X35.247 Y-98.64
G1 X35.259 Y-98.281
G1 X35.256 Y-97.921
G1 X35.237 Y-97.561
G1 X35.202 Y-97.203
G1 X35.152 Y-96.847
G1 X35.086 Y-96.493
G1 X35.005 Y-96.143
G1 X34.909 Y-95.796
G1 X34.798 Y-95.454
G1 X34.672 Y-95.117
G1 X34.531 Y-94.786
G1 X34.376 Y-94.461
G1 X34.207 Y-94.144
G1 X34.024 Y-93.834
G1 X33.828 Y-93.532
G1 X33.619 Y-93.239
G1 X33.398 Y-92.956
G1 X33.164 Y-92.682
G1 X32.919 Y-92.419
G1 X32.662 Y-92.167

(Linearized circular move - low memory mode enabled)
G1 X32.398 Y-91.93
G1 X32.125 Y-91.704
G1 X31.842 Y-91.49
G1 X31.55 Y-91.289
G1 X31.25 Y-91.1
G1 X30.942 Y-90.925
G1 X30.626 Y-90.762
G1 X30.304 Y-90.614
G1 X29.976 Y-90.479
G1 X29.643 Y-90.359
G1 X29.304 Y-90.253
G1 X28.962 Y-90.162
G1 X28.615 Y-90.086
G1 X28.266 Y-90.024
G1 X27.914 Y-89.978
G1 X27.561 Y-89.947
G1 X27.207 Y-89.931
G1 X26.852 Y-89.93
G1 X26.498 Y-89.945
G1 X26.145 Y-89.975
G1 X25.793 Y-90.02

(Linearized circular move - low memory mode enabled)
G1 X25.436 Y-90.079
G1 X25.083 Y-90.153
G1 X24.732 Y-90.242
G1 X24.386 Y-90.347
G1 X24.045 Y-90.466
G1 X23.709 Y-90.6
G1 X23.38 Y-90.748
G1 X23.057 Y-90.91
G1 X22.741 Y-91.086
G1 X22.433 Y-91.276
G1 X22.134 Y-91.478
G1 X21.843 Y-91.693
G1 X21.563 Y-91.921
G1 X21.292 Y-92.16
G1 X21.032 Y-92.411
G1 X20.783 Y-92.673
G1 X20.545 Y-92.945
G1 X20.319 Y-93.228
G1 X20.106 Y-93.52
G1 X19.905 Y-93.82

(Linearized circular move - low memory mode enabled)
G1 X19.718 Y-94.129
G1 X19.544 Y-94.446
G1 X19.384 Y-94.77
G1 X19.238 Y-95.101
G1 X19.106 Y-95.438
G1 X18.989 Y-95.78
G1 X18.887 Y-96.126
G1 X18.8 Y-96.477
G1 X18.728 Y-96.831
G1 X18.672 Y-97.188
G1 X18.631 Y-97.547
G1 X18.605 Y-97.908
G1 X18.595 Y-98.269
G1 X18.6 Y-98.631
G1 X18.621 Y-98.991
G1 X18.658 Y-99.351
G1 X18.71 Y-99.709
G1 X18.777 Y-100.064
G1 X18.86 Y-100.416
G1 X18.958 Y-100.764

(Linearized circular move - low memory mode enabled)
G1 X19.068 Y-101.105
G1 X19.194 Y-101.442
G1 X19.333 Y-101.773
G1 X19.486 Y-102.098
G1 X19.653 Y-102.416
G1 X19.833 Y-102.727
G1 X20.026 Y-103.03
G1 X20.232 Y-103.324
G1 X20.45 Y-103.609
G1 X20.68 Y-103.885
G1 X20.922 Y-104.151
G1 X21.175 Y-104.406
G1 X21.438 Y-104.651
G1 X21.711 Y-104.884
G1 X21.994 Y-105.105
G1 X22.286 Y-105.314
G1 X22.587 Y-105.51
G1 X22.896 Y-105.694
G1 X23.212 Y-105.864
G1 X23.536 Y-106.02
G1 X23.865 Y-106.163
G1 X24.201 Y-106.291
G1 X24.541 Y-106.406
G1 X24.886 Y-106.505
G1 X25.235 Y-106.59
G1 X25.588 Y-106.66
G1 X25.943 Y-106.715
G1 X26.3 Y-106.755
G1 X26.658 Y-106.78
G1 X27.017 Y-106.79
G1 X27.376 Y-106.784
G1 X27.735 Y-106.763
G1 X28.092 Y-106.727
G1 X28.448 Y-106.675
G1 X28.801 Y-106.609
G1 X29.15 Y-106.528
G1 X29.497 Y-106.432

(Linearized circular move - low memory mode enabled)
G1 X29.832 Y-106.323
G1 X30.162 Y-106.201
G1 X30.487 Y-106.065
G1 X30.806 Y-105.916
G1 X31.119 Y-105.753
G1 X31.425 Y-105.578
G1 X31.723 Y-105.39
G1 X32.012 Y-105.19
G1 X32.294 Y-104.978
G1 X32.566 Y-104.754

(Linearized circular move - low memory mode enabled)
G1 X32.84 Y-104.512
G1 X33.104 Y-104.259
G1 X33.357 Y-103.995
G1 X33.598 Y-103.721
G1 X33.827 Y-103.437
G1 X34.044 Y-103.143
G1 X34.249 Y-102.84
G1 X34.44 Y-102.528
G1 X34.618 Y-102.209
G1 X34.783 Y-101.883
G1 X34.933 Y-101.55
G1 X35.069 Y-101.211
G1 X35.191 Y-100.866
G1 X35.297 Y-100.517
G1 X35.389 Y-100.163
G1 X35.466 Y-99.806
G1 X35.528 Y-99.445
G1 X35.574 Y-99.083
G1 X35.605 Y-98.719
G1 X35.62 Y-98.354
G1 Y-97.988
G1 X35.604 Y-97.623
G1 X35.573 Y-97.259
G1 X35.526 Y-96.896
G1 X35.464 Y-96.536
G1 X35.387 Y-96.179
G1 X35.295 Y-95.826
G1 X35.187 Y-95.476
G1 X35.065 Y-95.132
G1 X34.929 Y-94.793
G1 X34.778 Y-94.46
G1 X34.614 Y-94.134
G1 X34.435 Y-93.815
G1 X34.243 Y-93.504
G1 X34.039 Y-93.201
G1 X33.821 Y-92.907
G1 X33.591 Y-92.623

(Linearized circular move - low memory mode enabled)
G1 X33.351 Y-92.35
G1 X33.099 Y-92.087
G1 X32.836 Y-91.836
G1 X32.563 Y-91.595
G1 X32.28 Y-91.367
G1 X31.988 Y-91.15
G1 X31.686 Y-90.946
G1 X31.376 Y-90.755
G1 X31.059 Y-90.577
G1 X30.734 Y-90.413
G1 X30.403 Y-90.263
G1 X30.065 Y-90.127
G1 X29.723 Y-90.005
G1 X29.375 Y-89.898
G1 X29.023 Y-89.805
G1 X28.667 Y-89.728
G1 X28.309 Y-89.666
G1 X27.948 Y-89.618
G1 X27.586 Y-89.587
G1 X27.222 Y-89.57
G1 X26.858 Y-89.569
G1 X26.495 Y-89.583
G1 X26.132 Y-89.613
G1 X25.771 Y-89.658

(Linearized circular move - low memory mode enabled)
G1 X25.404 Y-89.716
G1 X25.041 Y-89.79
G1 X24.681 Y-89.88
G1 X24.325 Y-89.985
G1 X23.973 Y-90.104
G1 X23.627 Y-90.239
G1 X23.288 Y-90.388
G1 X22.955 Y-90.552
G1 X22.629 Y-90.73
G1 X22.311 Y-90.921
G1 X22.001 Y-91.125
G1 X21.701 Y-91.343
G1 X21.41 Y-91.573
G1 X21.129 Y-91.816
G1 X20.858 Y-92.07
G1 X20.599 Y-92.335
G1 X20.351 Y-92.612
G1 X20.115 Y-92.898
G1 X19.892 Y-93.194
G1 X19.681 Y-93.5
G1 X19.483 Y-93.814
G1 X19.3 Y-94.136
G1 X19.13 Y-94.466
G1 X18.974 Y-94.803
G1 X18.832 Y-95.146
G1 X18.706 Y-95.495
G1 X18.594 Y-95.849
G1 X18.497 Y-96.207
G1 X18.416 Y-96.569
G1 X18.351 Y-96.935

(Linearized circular move - low memory mode enabled)
G1 X18.299 Y-97.305
G1 X18.263 Y-97.678
G1 X18.243 Y-98.051
G1 X18.239 Y-98.425
G1 X18.25 Y-98.799
G1 X18.278 Y-99.173
G1 X18.321 Y-99.544
G1 X18.38 Y-99.914
G1 X18.455 Y-100.28
G1 X18.545 Y-100.643
G1 X18.651 Y-101.002
G1 X18.771 Y-101.357
G1 X18.907 Y-101.705
G1 X19.057 Y-102.048
G1 X19.222 Y-102.384
G1 X19.401 Y-102.713
G1 X19.593 Y-103.033
G1 X19.799 Y-103.346
G1 X20.018 Y-103.649
G1 X20.25 Y-103.943
G1 X20.494 Y-104.226
G1 X20.75 Y-104.499
G1 X21.017 Y-104.761
G1 X21.295 Y-105.012
G1 X21.584 Y-105.25
G1 X21.882 Y-105.476
G1 X22.19 Y-105.689
G1 X22.506 Y-105.889
G1 X22.83 Y-106.075
G1 X23.163 Y-106.247
G1 X23.502 Y-106.405
G1 X23.847 Y-106.548
G1 X24.199 Y-106.677
G1 X24.555 Y-106.791
G1 X24.916 Y-106.889
G1 X25.281 Y-106.972
G1 X25.649 Y-107.039

(Linearized circular move - low memory mode enabled)
G1 X26.016 Y-107.091
G1 X26.385 Y-107.126
G1 X26.755 Y-107.147
G1 X27.125 Y-107.151
G1 X27.495 Y-107.141
G1 X27.865 Y-107.114
G1 X28.233 Y-107.072
G1 X28.599 Y-107.015
G1 X28.962 Y-106.942
G1 X29.322 Y-106.855
G1 X29.677 Y-106.752
G1 X30.029 Y-106.634
G1 X30.375 Y-106.502
G1 X30.715 Y-106.355
G1 X31.048 Y-106.194
G1 X31.375 Y-106.019
G1 X31.694 Y-105.831

(Linearized circular move - low memory mode enabled)
G1 X32.006 Y-105.631
G1 X32.31 Y-105.418
G1 X32.604 Y-105.193
G1 X32.89 Y-104.956
G1 X33.165 Y-104.708
G1 X33.429 Y-104.448
G1 X33.683 Y-104.177
G1 X33.925 Y-103.897
G1 X34.156 Y-103.606
G1 X34.374 Y-103.306
G1 X34.58 Y-102.998
G1 X34.773 Y-102.681
G1 X34.952 Y-102.357
G1 X35.118 Y-102.025
G1 X35.27 Y-101.687
G1 X35.408 Y-101.343
G1 X35.532 Y-100.993
G1 X35.641 Y-100.639
G1 X35.735 Y-100.28
G1 X35.815 Y-99.918
G1 X35.879 Y-99.553
G1 X35.929 Y-99.186
G1 X35.963 Y-98.816
G1 X35.982 Y-98.446
G1 X35.985 Y-98.075
G1 X35.973 Y-97.705
G1 X35.946 Y-97.335
G1 X35.904 Y-96.966
G1 X35.846 Y-96.6
G1 X35.773 Y-96.237
G1 X35.686 Y-95.876
G1 X35.583 Y-95.52
G1 X35.466 Y-95.168
G1 X35.335 Y-94.821
G1 X35.189 Y-94.48
G1 X35.029 Y-94.146
G1 X34.856 Y-93.818
G1 X34.669 Y-93.498

(Linearized circular move - low memory mode enabled)
G1 X34.467 Y-93.183
G1 X34.253 Y-92.876
G1 X34.025 Y-92.579
G1 X33.786 Y-92.292
G1 X33.535 Y-92.015
G1 X33.272 Y-91.749
G1 X32.999 Y-91.493
G1 X32.715 Y-91.25
G1 X32.422 Y-91.018
G1 X32.118 Y-90.799
G1 X31.806 Y-90.593
G1 X31.486 Y-90.4
G1 X31.158 Y-90.22
G1 X30.823 Y-90.054
G1 X30.481 Y-89.903
G1 X30.133 Y-89.765
G1 X29.78 Y-89.643
G1 X29.422 Y-89.535
G1 X29.06 Y-89.442
G1 X28.694 Y-89.364
G1 X28.325 Y-89.302
G1 X27.954 Y-89.255
G1 X27.581 Y-89.224
G1 X27.208 Y-89.208
G1 X26.834
G1 X26.46 Y-89.223
G1 X26.087 Y-89.254

(Linearized circular move - low memory mode enabled)
G1 X25.728 Y-89.295
G1 X25.371 Y-89.35
G1 X25.016 Y-89.419
G1 X24.665 Y-89.503
G1 X24.317 Y-89.601
G1 X23.973 Y-89.713
G1 X23.634 Y-89.838
G1 X23.301 Y-89.977
G1 X22.973 Y-90.13
G1 X22.652 Y-90.296
G1 X22.338 Y-90.474
G1 X22.031 Y-90.665
G1 X21.732 Y-90.868
G1 X21.442 Y-91.084
G1 X21.161 Y-91.31
G1 X20.889 Y-91.548

(Linearized circular move - low memory mode enabled)
G1 X20.613 Y-91.808
G1 X20.348 Y-92.079
G1 X20.095 Y-92.361
G1 X19.854 Y-92.653
G1 X19.625 Y-92.955
G1 X19.408 Y-93.266
G1 X19.205 Y-93.586
G1 X19.015 Y-93.914
G1 X18.839 Y-94.249
G1 X18.676 Y-94.591
G1 X18.528 Y-94.94
G1 X18.395 Y-95.295
G1 X18.276 Y-95.655
G1 X18.173 Y-96.019
G1 X18.084 Y-96.387
G1 X18.011 Y-96.759
G1 X17.953 Y-97.134
G1 X17.911 Y-97.51
G1 X17.884 Y-97.888
G1 X17.873 Y-98.267
G1 X17.877 Y-98.646
G1 X17.898 Y-99.024
G1 X17.933 Y-99.401
G1 X17.985 Y-99.777
G1 X18.052 Y-100.15
G1 X18.134 Y-100.519
G1 X18.231 Y-100.886
G1 X18.344 Y-101.247
G1 X18.471 Y-101.604
G1 X18.613 Y-101.956
G1 X18.77 Y-102.301
G1 X18.94 Y-102.639
G1 X19.125 Y-102.97
G1 X19.322 Y-103.293
G1 X19.534 Y-103.608
G1 X19.757 Y-103.913
G1 X19.994 Y-104.21
G1 X20.242 Y-104.496

(Linearized circular move - low memory mode enabled)
G1 X20.503 Y-104.772
G1 X20.775 Y-105.038
G1 X21.058 Y-105.292
G1 X21.351 Y-105.534
G1 X21.654 Y-105.763
G1 X21.966 Y-105.98
G1 X22.287 Y-106.184
G1 X22.616 Y-106.374
G1 X22.952 Y-106.551
G1 X23.296 Y-106.713
G1 X23.646 Y-106.861
G1 X24.002 Y-106.994
G1 X24.363 Y-107.113
G1 X24.729 Y-107.216
G1 X25.099 Y-107.304
G1 X25.472 Y-107.377
G1 X25.848 Y-107.434
G1 X26.226 Y-107.476
G1 X26.605 Y-107.502
G1 X26.985 Y-107.512
G1 X27.365 Y-107.506
G1 X27.744 Y-107.485
G1 X28.123 Y-107.448
G1 X28.499 Y-107.395
G1 X28.873 Y-107.327
G1 X29.244 Y-107.243
G1 X29.611 Y-107.144
G1 X29.973 Y-107.03

(Linearized circular move - low memory mode enabled)
G1 X30.336 Y-106.9
G1 X30.694 Y-106.756
G1 X31.045 Y-106.597
G1 X31.39 Y-106.423
G1 X31.727 Y-106.236
G1 X32.056 Y-106.034
G1 X32.376 Y-105.82
G1 X32.687 Y-105.592
G1 X32.989 Y-105.351
G1 X33.28 Y-105.099
G1 X33.561 Y-104.834
G1 X33.83 Y-104.558
G1 X34.088 Y-104.271
G1 X34.334 Y-103.974
G1 X34.567 Y-103.667
G1 X34.788 Y-103.351
G1 X34.995 Y-103.025
G1 X35.188 Y-102.692
G1 X35.368 Y-102.35
G1 X35.533 Y-102.002
G1 X35.684 Y-101.647
G1 X35.82 Y-101.286
G1 X35.941 Y-100.92
G1 X36.046 Y-100.549
G1 X36.137 Y-100.174
G1 X36.211 Y-99.796
G1 X36.27 Y-99.414
G1 X36.313 Y-99.031
G1 X36.341 Y-98.646
G1 X36.352 Y-98.261
G1 X36.348 Y-97.875
G1 X36.327 Y-97.49
G1 X36.291 Y-97.106
G1 X36.238 Y-96.724
G1 X36.171 Y-96.345
G1 X36.087 Y-95.968
G1 X35.988 Y-95.595
G1 X35.873 Y-95.227

(Linearized circular move - low memory mode enabled)
G1 X35.748 Y-94.874
G1 X35.608 Y-94.527
G1 X35.455 Y-94.185
G1 X35.288 Y-93.85
G1 X35.108 Y-93.522
G1 X34.914 Y-93.202
G1 X34.709 Y-92.889
G1 X34.49 Y-92.585
G1 X34.26 Y-92.29
G1 X34.018 Y-92.004
G1 X33.764 Y-91.729
G1 X33.5 Y-91.463
G1 X33.226 Y-91.209
G1 X32.941 Y-90.966
G1 X32.647 Y-90.734
G1 X32.344 Y-90.514
G1 X32.032 Y-90.307
G1 X31.712 Y-90.112
G1 X31.385 Y-89.931
G1 X31.051 Y-89.762
G1 X30.71 Y-89.607
G1 X30.363 Y-89.466
G1 X30.011 Y-89.339
G1 X29.654 Y-89.227
G1 X29.293 Y-89.128
G1 X28.928 Y-89.044
G1 X28.56 Y-88.975
G1 X28.19 Y-88.921

(Linearized circular move - low memory mode enabled)
G1 X27.81 Y-88.88
G1 X27.429 Y-88.854
G1 X27.047 Y-88.844
G1 X26.665 Y-88.849
G1 X26.283 Y-88.87
G1 X25.903 Y-88.906
G1 X25.525 Y-88.957
G1 X25.148 Y-89.024
G1 X24.775 Y-89.105
G1 X24.406 Y-89.201
G1 X24.04 Y-89.313
G1 X23.68 Y-89.439
G1 X23.324 Y-89.579
G1 X22.975 Y-89.733
G1 X22.632 Y-89.902
G1 X22.296 Y-90.084
G1 X21.968 Y-90.279
G1 X21.648 Y-90.488
G1 X21.337 Y-90.709
G1 X21.034 Y-90.943
G1 X20.742 Y-91.188
G1 X20.459 Y-91.445
G1 X20.188 Y-91.714
G1 X19.927 Y-91.993
G1 X19.677 Y-92.282
G1 X19.44 Y-92.581
G1 X19.214 Y-92.89
G1 X19.002 Y-93.207
G1 X18.802 Y-93.533
G1 X18.616 Y-93.866
G1 X18.443 Y-94.207
G1 X18.284 Y-94.554
G1 X18.139 Y-94.907
G1 X18.008 Y-95.266
G1 X17.892 Y-95.63
G1 X17.791 Y-95.999
G1 X17.704 Y-96.371
G1 X17.633 Y-96.746
G1 X17.577 Y-97.124

(Linearized circular move - low memory mode enabled)
G1 X17.537 Y-97.497
G1 X17.511 Y-97.871
G1 X17.5 Y-98.245
G1 X17.505 Y-98.62
G1 X17.524 Y-98.995
G1 X17.557 Y-99.368
G1 X17.606 Y-99.74
G1 X17.669 Y-100.11
G1 X17.747 Y-100.477
G1 X17.839 Y-100.84
G1 X17.945 Y-101.2
G1 X18.066 Y-101.555
G1 X18.2 Y-101.905
G1 X18.348 Y-102.249
G1 X18.51 Y-102.588
G1 X18.685 Y-102.919
G1 X18.873 Y-103.244
G1 X19.074 Y-103.561

(Linearized circular move - low memory mode enabled)
G1 X19.291 Y-103.878
G1 X19.521 Y-104.187
G1 X19.763 Y-104.486
G1 X20.017 Y-104.775
G1 X20.282 Y-105.054
G1 X20.559 Y-105.322
G1 X20.846 Y-105.578
G1 X21.143 Y-105.823
G1 X21.45 Y-106.055
G1 X21.765 Y-106.275
G1 X22.09 Y-106.482
G1 X22.422 Y-106.676
G1 X22.762 Y-106.856
G1 X23.109 Y-107.023
G1 X23.463 Y-107.175
G1 X23.822 Y-107.313
G1 X24.187 Y-107.436
G1 X24.556 Y-107.545
G1 X24.929 Y-107.639
G1 X25.306 Y-107.717
G1 X25.686 Y-107.781
G1 X26.067 Y-107.829
G1 X26.451 Y-107.861
G1 X26.835 Y-107.878
G1 X27.22 Y-107.88
G1 X27.605 Y-107.866
G1 X27.989 Y-107.837
G1 X28.371 Y-107.792
G1 X28.751 Y-107.732
G1 X29.128 Y-107.656
G1 X29.502 Y-107.566
G1 X29.873 Y-107.46
G1 X30.238 Y-107.34
G1 X30.599 Y-107.205
G1 X30.953 Y-107.056
G1 X31.302 Y-106.892
G1 X31.643 Y-106.715
G1 X31.977 Y-106.524
G1 X32.303 Y-106.319

(Linearized circular move - low memory mode enabled)
G1 X32.607 Y-106.112
G1 X32.903 Y-105.893
G1 X33.19 Y-105.663
G1 X33.468 Y-105.422
G1 X33.736 Y-105.17

(Linearized circular move - low memory mode enabled)
G1 X34.01 Y-104.897
G1 X34.273 Y-104.613
G1 X34.525 Y-104.319
G1 X34.764 Y-104.015
G1 X34.991 Y-103.702
G1 X35.205 Y-103.38
G1 X35.407 Y-103.049
G1 X35.595 Y-102.711
G1 X35.769 Y-102.366
G1 X35.929 Y-102.013
G1 X36.075 Y-101.655
G1 X36.207 Y-101.291
G1 X36.324 Y-100.922
G1 X36.426 Y-100.549
G1 X36.513 Y-100.172
G1 X36.585 Y-99.792
G1 X36.641 Y-99.409
G1 X36.683 Y-99.024
G1 X36.708 Y-98.638
G1 X36.719 Y-98.251
G1 X36.714 Y-97.864
G1 X36.693 Y-97.478
G1 X36.657 Y-97.093
G1 X36.605 Y-96.709
G1 X36.539 Y-96.328
G1 X36.457 Y-95.95
G1 X36.359 Y-95.575
G1 X36.247 Y-95.205
G1 X36.121 Y-94.839
G1 X35.98 Y-94.479

(Linearized circular move - low memory mode enabled)
G1 X35.822 Y-94.119
G1 X35.649 Y-93.766
G1 X35.463 Y-93.42
G1 X35.263 Y-93.082
G1 X35.049 Y-92.752
G1 X34.822 Y-92.432
G1 X34.583 Y-92.12
G1 X34.331 Y-91.818
G1 X34.067 Y-91.527
G1 X33.792 Y-91.247
G1 X33.505 Y-90.978
G1 X33.209 Y-90.72
G1 X32.901 Y-90.475
G1 X32.585 Y-90.243
G1 X32.259 Y-90.023
G1 X31.925 Y-89.817
G1 X31.582 Y-89.624
G1 X31.232 Y-89.445
G1 X30.875 Y-89.281
G1 X30.512 Y-89.131
G1 X30.143 Y-88.995
G1 X29.769 Y-88.875
G1 X29.391 Y-88.77
G1 X29.008 Y-88.68
G1 X28.622 Y-88.606
G1 X28.234 Y-88.547
G1 X27.843 Y-88.504
G1 X27.451 Y-88.477
G1 X27.059 Y-88.465
G1 X26.666 Y-88.469
G1 X26.273 Y-88.49
G1 X25.882 Y-88.525
G1 X25.493 Y-88.577
G1 X25.105 Y-88.644
G1 X24.721 Y-88.727
G1 X24.341 Y-88.825
G1 X23.965 Y-88.939
G1 X23.593 Y-89.067
G1 X23.228 Y-89.211

(Linearized circular move - low memory mode enabled)
G1 X22.891 Y-89.358
G1 X22.561 Y-89.517
G1 X22.237 Y-89.689
G1 X21.92 Y-89.873
G1 X21.609 Y-90.069
G1 X21.307 Y-90.276
G1 X21.012 Y-90.495
G1 X20.726 Y-90.724
G1 X20.448 Y-90.964
G1 X20.18 Y-91.214
G1 X19.922 Y-91.474

(Linearized circular move - low memory mode enabled)
G1 X19.657 Y-91.759
G1 X19.404 Y-92.054
G1 X19.163 Y-92.358
G1 X18.933 Y-92.671
G1 X18.717 Y-92.994
G1 X18.513 Y-93.324
G1 X18.322 Y-93.663
G1 X18.145 Y-94.008
G1 X17.981 Y-94.36
G1 X17.831 Y-94.719
G1 X17.696 Y-95.083
G1 X17.575 Y-95.452
G1 X17.468 Y-95.825
G1 X17.376 Y-96.203
G1 X17.3 Y-96.583
G1 X17.238 Y-96.967
G1 X17.191 Y-97.352
G1 X17.159 Y-97.74
G1 X17.143 Y-98.128
G1 X17.142 Y-98.516
G1 X17.156 Y-98.904
G1 X17.185 Y-99.291
G1 X17.23 Y-99.677
G1 X17.289 Y-100.061
G1 X17.364 Y-100.442
G1 X17.454 Y-100.82
G1 X17.558 Y-101.194
G1 X17.677 Y-101.564
G1 X17.81 Y-101.929
G1 X17.958 Y-102.288
G1 X18.119 Y-102.641
G1 X18.295 Y-102.988
G1 X18.483 Y-103.327
G1 X18.685 Y-103.659
G1 X18.9 Y-103.983
G1 X19.128 Y-104.298
G1 X19.367 Y-104.603
G1 X19.618 Y-104.899
G1 X19.881 Y-105.185
G1 X20.155 Y-105.461

(Linearized circular move - low memory mode enabled)
G1 X20.438 Y-105.724
G1 X20.73 Y-105.975
G1 X21.032 Y-106.215
G1 X21.343 Y-106.443
G1 X21.663 Y-106.659
G1 X21.991 Y-106.862
G1 X22.327 Y-107.052
G1 X22.67 Y-107.228
G1 X23.019 Y-107.392
G1 X23.375 Y-107.541
G1 X23.736 Y-107.677
G1 X24.102 Y-107.798
G1 X24.473 Y-107.905
G1 X24.847 Y-107.998
G1 X25.225 Y-108.076
G1 X25.606 Y-108.139
G1 X25.988 Y-108.187
G1 X26.373 Y-108.22
G1 X26.758 Y-108.238
G1 X27.144 Y-108.241
G1 X27.529 Y-108.23
G1 X27.914 Y-108.203
G1 X28.298 Y-108.161
G1 X28.679 Y-108.104
G1 X29.058 Y-108.032

(Linearized circular move - low memory mode enabled)
G1 X29.424 Y-107.952
G1 X29.787 Y-107.858
G1 X30.146 Y-107.751
G1 X30.5 Y-107.63
G1 X30.85 Y-107.495
G1 X31.194 Y-107.347
G1 X31.533 Y-107.187
G1 X31.865 Y-107.013
G1 X32.19 Y-106.827
G1 X32.508 Y-106.629
G1 X32.818 Y-106.418
G1 X33.119 Y-106.196
G1 X33.413 Y-105.963
G1 X33.697 Y-105.719
G1 X33.971 Y-105.464

(Linearized circular move - low memory mode enabled)
G1 X34.252 Y-105.185
G1 X34.521 Y-104.895
G1 X34.779 Y-104.594
G1 X35.025 Y-104.284
G1 X35.258 Y-103.965
G1 X35.479 Y-103.636
G1 X35.686 Y-103.299
G1 X35.881 Y-102.954
G1 X36.061 Y-102.602
G1 X36.228 Y-102.243
G1 X36.38 Y-101.878
G1 X36.518 Y-101.507
G1 X36.641 Y-101.131
G1 X36.75 Y-100.75
G1 X36.843 Y-100.365
G1 X36.921 Y-99.977
G1 X36.984 Y-99.587
G1 X37.032 Y-99.194
G1 X37.064 Y-98.799
G1 X37.08 Y-98.404
G1 X37.081 Y-98.008
G1 X37.067 Y-97.612
G1 X37.037 Y-97.218
G1 X36.991 Y-96.825
G1 X36.93 Y-96.434
G1 X36.854 Y-96.045
G1 X36.762 Y-95.66
G1 X36.656 Y-95.279
G1 X36.535 Y-94.902
G1 X36.399 Y-94.53
G1 X36.248 Y-94.164
G1 X36.083 Y-93.805
G1 X35.904 Y-93.451
G1 X35.712 Y-93.106
G1 X35.506 Y-92.768
G1 X35.287 Y-92.438
G1 X35.055 Y-92.117
G1 X34.811 Y-91.806
G1 X34.554 Y-91.504
G1 X34.286 Y-91.213

(Linearized circular move - low memory mode enabled)
G1 X34.01 Y-90.935
G1 X33.723 Y-90.668
G1 X33.426 Y-90.413
G1 X33.119 Y-90.169
G1 X32.803 Y-89.937
G1 X32.478 Y-89.718
G1 X32.145 Y-89.511
G1 X31.804 Y-89.318
G1 X31.456 Y-89.138
G1 X31.101 Y-88.972
G1 X30.74 Y-88.819
G1 X30.373 Y-88.681
G1 X30.001 Y-88.557
G1 X29.625 Y-88.448
G1 X29.244 Y-88.354
G1 X28.861 Y-88.274
G1 X28.474 Y-88.209
G1 X28.085 Y-88.16
G1 X27.695 Y-88.125
G1 X27.303 Y-88.106
G1 X26.911 Y-88.102
G1 X26.52 Y-88.113
G1 X26.129 Y-88.139
G1 X25.739 Y-88.181
G1 X25.351 Y-88.238
G1 X24.966 Y-88.309
G1 X24.584 Y-88.396

(Linearized circular move - low memory mode enabled)
G1 X24.195 Y-88.499
G1 X23.811 Y-88.616
G1 X23.431 Y-88.749
G1 X23.057 Y-88.897
G1 X22.689 Y-89.059
G1 X22.328 Y-89.235
G1 X21.974 Y-89.426
G1 X21.628 Y-89.63
G1 X21.29 Y-89.848
G1 X20.961 Y-90.079
G1 X20.641 Y-90.322
G1 X20.331 Y-90.578
G1 X20.031 Y-90.846
G1 X19.742 Y-91.125
G1 X19.465 Y-91.416
G1 X19.198 Y-91.717
G1 X18.944 Y-92.029
G1 X18.702 Y-92.35
G1 X18.474 Y-92.68
G1 X18.258 Y-93.019
G1 X18.056 Y-93.367
G1 X17.867 Y-93.722
G1 X17.693 Y-94.084
G1 X17.533 Y-94.453
G1 X17.387 Y-94.828
G1 X17.256 Y-95.208
G1 X17.141 Y-95.593
G1 X17.04 Y-95.982
G1 X16.955 Y-96.375
G1 X16.886 Y-96.771
G1 X16.832 Y-97.169
G1 X16.794 Y-97.569
G1 X16.771 Y-97.971
G1 X16.764 Y-98.373
G1 X16.773 Y-98.774
G1 X16.798 Y-99.176
G1 X16.839 Y-99.576
G1 X16.895 Y-99.974
G1 X16.967 Y-100.369
G1 X17.054 Y-100.762

(Linearized circular move - low memory mode enabled)
G1 X17.157 Y-101.152
G1 X17.275 Y-101.538
G1 X17.409 Y-101.919
G1 X17.557 Y-102.294
G1 X17.72 Y-102.663
G1 X17.898 Y-103.026
G1 X18.089 Y-103.381
G1 X18.295 Y-103.728
G1 X18.514 Y-104.067
G1 X18.746 Y-104.398
G1 X18.991 Y-104.718
G1 X19.249 Y-105.029
G1 X19.518 Y-105.329
G1 X19.8 Y-105.619
G1 X20.092 Y-105.897
G1 X20.395 Y-106.164
G1 X20.708 Y-106.418
G1 X21.032 Y-106.66
G1 X21.364 Y-106.889

(Linearized circular move - low memory mode enabled)
G1 X21.707 Y-107.106
G1 X22.058 Y-107.31
G1 X22.416 Y-107.501
G1 X22.782 Y-107.676
G1 X23.155 Y-107.838
G1 X23.533 Y-107.984
G1 X23.917 Y-108.116
G1 X24.306 Y-108.233
G1 X24.699 Y-108.334
G1 X25.096 Y-108.419
G1 X25.496 Y-108.489
G1 X25.898 Y-108.543
G1 X26.303 Y-108.582
G1 X26.708 Y-108.604
G1 X27.114 Y-108.611
G1 X27.52 Y-108.601
G1 X27.925 Y-108.576
G1 X28.329 Y-108.535
G1 X28.731 Y-108.478
G1 X29.13 Y-108.405
G1 X29.526 Y-108.317
G1 X29.919 Y-108.213
G1 X30.307 Y-108.093
G1 X30.69 Y-107.959
G1 X31.067 Y-107.81
G1 X31.439 Y-107.646
G1 X31.803 Y-107.467
G1 X32.161 Y-107.275
G1 X32.51 Y-107.068
G1 X32.851 Y-106.848
G1 X33.183 Y-106.615
G1 X33.506 Y-106.369
G1 X33.819 Y-106.11
G1 X34.122 Y-105.839
G1 X34.413 Y-105.557
G1 X34.694 Y-105.263
G1 X34.962 Y-104.959
G1 X35.219 Y-104.644
G1 X35.462 Y-104.32
G1 X35.693 Y-103.986

(Linearized circular move - low memory mode enabled)
G1 X35.905 Y-103.653
G1 X36.104 Y-103.313
G1 X36.289 Y-102.965
G1 X36.461 Y-102.61
G1 X36.62 Y-102.249

(Linearized circular move - low memory mode enabled)
G1 X36.769 Y-101.878
G1 X36.904 Y-101.501
G1 X37.024 Y-101.12
G1 X37.13 Y-100.734
G1 X37.22 Y-100.345
G1 X37.296 Y-99.952
G1 X37.357 Y-99.556
G1 X37.402 Y-99.159
G1 X37.432 Y-98.76
G1 X37.447 Y-98.361
G1 Y-97.961
G1 X37.431 Y-97.561
G1 X37.4 Y-97.162
G1 X37.353 Y-96.765
G1 X37.292 Y-96.37
G1 X37.215 Y-95.977
G1 X37.124 Y-95.588
G1 X37.017 Y-95.202
G1 X36.896 Y-94.821
G1 X36.76 Y-94.445
G1 X36.61 Y-94.074
G1 X36.446 Y-93.709
G1 X36.268 Y-93.351
G1 X36.077 Y-93.0
G1 X35.872 Y-92.656
G1 X35.654 Y-92.321
G1 X35.424 Y-91.994
G1 X35.181 Y-91.676
G1 X34.926 Y-91.368
G1 X34.66 Y-91.069
G1 X34.382 Y-90.782
G1 X34.094 Y-90.505
G1 X33.795 Y-90.239
G1 X33.486 Y-89.985
G1 X33.167 Y-89.743
G1 X32.84 Y-89.513
G1 X32.504 Y-89.296
G1 X32.16 Y-89.092
G1 X31.808 Y-88.901
G1 X31.45 Y-88.724
G1 X31.085 Y-88.561

(Linearized circular move - low memory mode enabled)
G1 X30.72 Y-88.414
G1 X30.349 Y-88.281
G1 X29.974 Y-88.162
G1 X29.595 Y-88.058
G1 X29.212 Y-87.968
G1 X28.826 Y-87.892
G1 X28.438 Y-87.831
G1 X28.047 Y-87.784
G1 X27.655 Y-87.752
G1 X27.262 Y-87.735
G1 X26.869 Y-87.733
G1 X26.476 Y-87.746
G1 X26.083 Y-87.773
G1 X25.692 Y-87.816
G1 X25.303 Y-87.872
G1 X24.916 Y-87.944

(Linearized circular move - low memory mode enabled)
G1 X24.53 Y-88.027
G1 X24.146 Y-88.125
G1 X23.767 Y-88.237
G1 X23.392 Y-88.364
G1 X23.022 Y-88.504
G1 X22.658 Y-88.659
G1 X22.299 Y-88.826
G1 X21.948 Y-89.008
G1 X21.603 Y-89.202
G1 X21.266 Y-89.409
G1 X20.937 Y-89.629
G1 X20.617 Y-89.861
G1 X20.305 Y-90.105
G1 X20.003 Y-90.361
G1 X19.711 Y-90.627
G1 X19.429 Y-90.905
G1 X19.157 Y-91.193
G1 X18.897 Y-91.491
G1 X18.648 Y-91.798
G1 X18.411 Y-92.115
G1 X18.186 Y-92.441
G1 X17.973 Y-92.774
G1 X17.774 Y-93.116

(Linearized circular move - low memory mode enabled)
G1 X17.58 Y-93.475
G1 X17.401 Y-93.841
G1 X17.235 Y-94.214
G1 X17.084 Y-94.593
G1 X16.948 Y-94.977
G1 X16.827 Y-95.366
G1 X16.72 Y-95.76
G1 X16.629 Y-96.157
G1 X16.553 Y-96.558
G1 X16.492 Y-96.961
G1 X16.447 Y-97.366
G1 X16.417 Y-97.773
G1 X16.403 Y-98.181
G1 X16.405 Y-98.588
G1 X16.422 Y-98.996
G1 X16.455 Y-99.402
G1 X16.503 Y-99.807
G1 X16.567 Y-100.21
G1 X16.646 Y-100.61
G1 X16.741 Y-101.007
G1 X16.851 Y-101.4
G1 X16.975 Y-101.788
G1 X17.115 Y-102.171
G1 X17.268 Y-102.549
G1 X17.437 Y-102.92
G1 X17.619 Y-103.285
G1 X17.815 Y-103.642
G1 X18.025 Y-103.992
G1 X18.248 Y-104.333
G1 X18.484 Y-104.666
G1 X18.733 Y-104.989
G1 X18.993 Y-105.303
G1 X19.266 Y-105.606
G1 X19.55 Y-105.899
G1 X19.845 Y-106.181
G1 X20.15 Y-106.451
G1 X20.466 Y-106.709
G1 X20.791 Y-106.955
G1 X21.126 Y-107.188
G1 X21.469 Y-107.408
G1 X21.82 Y-107.615

(Linearized circular move - low memory mode enabled)
G1 X22.171 Y-107.805
G1 X22.529 Y-107.981
G1 X22.894 Y-108.143
G1 X23.264 Y-108.292
G1 X23.639 Y-108.427
G1 X24.02 Y-108.547
G1 X24.404 Y-108.654
G1 X24.793 Y-108.745
G1 X25.184 Y-108.823
G1 X25.578 Y-108.885
G1 X25.974 Y-108.933
G1 X26.372 Y-108.965
G1 X26.77 Y-108.983
G1 X27.169 Y-108.986
G1 X27.568 Y-108.974
G1 X27.966 Y-108.947
G1 X28.363 Y-108.905
G1 X28.758 Y-108.848
G1 X29.151 Y-108.777
G1 X29.54 Y-108.691
G1 X29.926 Y-108.59
G1 X30.308 Y-108.475
G1 X30.686 Y-108.345
G1 X31.058 Y-108.202
G1 X31.425 Y-108.045

(Linearized circular move - low memory mode enabled)
G1 X31.799 Y-107.869
G1 X32.167 Y-107.679
G1 X32.527 Y-107.476
G1 X32.879 Y-107.258
G1 X33.222 Y-107.027
G1 X33.557 Y-106.784
G1 X33.881 Y-106.527
G1 X34.196 Y-106.259
G1 X34.5 Y-105.978
G1 X34.793 Y-105.687
G1 X35.075 Y-105.384
G1 X35.345 Y-105.07
G1 X35.603 Y-104.747
G1 X35.848 Y-104.413
G1 X36.08 Y-104.071
G1 X36.299 Y-103.72
G1 X36.505 Y-103.361
G1 X36.696 Y-102.994
G1 X36.873 Y-102.62
G1 X37.036 Y-102.24
G1 X37.184 Y-101.854
G1 X37.318 Y-101.462
G1 X37.436 Y-101.066
G1 X37.539 Y-100.665
G1 X37.626 Y-100.26
G1 X37.698 Y-99.853
G1 X37.754 Y-99.443
G1 X37.795 Y-99.031
G1 X37.819 Y-98.618
G1 X37.828 Y-98.205
G1 X37.821 Y-97.791
G1 X37.798 Y-97.378
G1 X37.76 Y-96.966
G1 X37.705 Y-96.556
G1 X37.635 Y-96.148
G1 X37.549 Y-95.744
G1 X37.448 Y-95.342
G1 X37.332 Y-94.945
G1 X37.201 Y-94.553
G1 X37.054 Y-94.166
G1 X36.893 Y-93.785

(Linearized circular move - low memory mode enabled)
G1 X36.721 Y-93.419
G1 X36.536 Y-93.059
G1 X36.338 Y-92.707
G1 X36.126 Y-92.362
G1 X35.902 Y-92.025
G1 X35.665 Y-91.697
G1 X35.416 Y-91.378
G1 X35.156 Y-91.069
G1 X34.884 Y-90.77
G1 X34.601 Y-90.481
G1 X34.307 Y-90.203
G1 X34.003 Y-89.936
G1 X33.689 Y-89.68
G1 X33.366 Y-89.437
G1 X33.034 Y-89.206
G1 X32.694 Y-88.987

(Linearized circular move - low memory mode enabled)
G1 X32.342 Y-88.779
G1 X31.983 Y-88.585
G1 X31.616 Y-88.404
G1 X31.243 Y-88.237
G1 X30.864 Y-88.084
G1 X30.48 Y-87.945
G1 X30.091 Y-87.821
G1 X29.697 Y-87.711
G1 X29.299 Y-87.617
G1 X28.899 Y-87.537
G1 X28.495 Y-87.472
G1 X28.09 Y-87.422
G1 X27.682 Y-87.388
G1 X27.274 Y-87.369
G1 X26.866 Y-87.365
G1 X26.457 Y-87.376
G1 X26.049 Y-87.403
G1 X25.643 Y-87.445
G1 X25.238 Y-87.502
G1 X24.836 Y-87.574
G1 X24.437 Y-87.661
G1 X24.041 Y-87.763
G1 X23.649 Y-87.879
G1 X23.262 Y-88.011
G1 X22.88 Y-88.156
G1 X22.504 Y-88.316
G1 X22.134 Y-88.49
G1 X21.771 Y-88.677
G1 X21.415 Y-88.878
G1 X21.067 Y-89.092
G1 X20.728 Y-89.319
G1 X20.396 Y-89.559
G1 X20.074 Y-89.81
G1 X19.762 Y-90.074
G1 X19.46 Y-90.349
G1 X19.168 Y-90.635
G1 X18.887 Y-90.932
G1 X18.618 Y-91.239
G1 X18.36 Y-91.556
G1 X18.114 Y-91.883
G1 X17.881 Y-92.218
G1 X17.66 Y-92.562

(Linearized circular move - low memory mode enabled)
G1 X17.452 Y-92.915
G1 X17.257 Y-93.275
G1 X17.076 Y-93.642
G1 X16.909 Y-94.015
G1 X16.756 Y-94.395
G1 X16.617 Y-94.78
G1 X16.493 Y-95.17
G1 X16.383 Y-95.565
G1 X16.288 Y-95.963
G1 X16.209 Y-96.365
G1 X16.144 Y-96.769
G1 X16.094 Y-97.175
G1 X16.06 Y-97.583
G1 X16.041 Y-97.992

(Linearized circular move - low memory mode enabled)
G1 X16.036 Y-98.404
G1 X16.046 Y-98.816
G1 X16.071 Y-99.227
G1 X16.112 Y-99.637
G1 X16.168 Y-100.045
G1 X16.239 Y-100.451
G1 X16.325 Y-100.854
G1 X16.426 Y-101.253
G1 X16.542 Y-101.648
G1 X16.673 Y-102.039
G1 X16.819 Y-102.424
G1 X16.978 Y-102.804
G1 X17.152 Y-103.178
G1 X17.34 Y-103.544
G1 X17.541 Y-103.904
G1 X17.755 Y-104.255
G1 X17.983 Y-104.599
G1 X18.223 Y-104.934
G1 X18.475 Y-105.259
G1 X18.74 Y-105.575
G1 X19.016 Y-105.881
G1 X19.303 Y-106.176
G1 X19.601 Y-106.46
G1 X19.91 Y-106.733
G1 X20.229 Y-106.994
G1 X20.557 Y-107.243
G1 X20.894 Y-107.48
G1 X21.24 Y-107.703
G1 X21.594 Y-107.914
G1 X21.955 Y-108.112
G1 X22.324 Y-108.295
G1 X22.699 Y-108.465
G1 X23.081 Y-108.621
G1 X23.468 Y-108.762
G1 X23.86 Y-108.889
G1 X24.256 Y-109.0
G1 X24.657 Y-109.098
G1 X25.06 Y-109.18
G1 X25.467 Y-109.246
G1 X25.875 Y-109.298
G1 X26.286 Y-109.334
G1 X26.697 Y-109.355

(Linearized circular move - low memory mode enabled)
G1 X27.112 Y-109.361
G1 X27.527 Y-109.351
G1 X27.942 Y-109.325
G1 X28.355 Y-109.283
G1 X28.766 Y-109.227
G1 X29.175 Y-109.154
G1 X29.581 Y-109.066
G1 X29.983 Y-108.963
G1 X30.381 Y-108.845
G1 X30.774 Y-108.713
G1 X31.162 Y-108.565
G1 X31.544 Y-108.403
G1 X31.92 Y-108.226
G1 X32.289 Y-108.036

(Linearized circular move - low memory mode enabled)
G1 X32.622 Y-107.854
G1 X32.948 Y-107.661
G1 X33.267 Y-107.456
G1 X33.579 Y-107.24
G1 X33.883 Y-107.014
G1 X34.179 Y-106.776
G1 X34.466 Y-106.529
G1 X34.744 Y-106.271
G1 X35.013 Y-106.004
G1 X35.273 Y-105.727
G1 X35.522 Y-105.442

(Linearized circular move - low memory mode enabled)
G1 X35.788 Y-105.118
G1 X36.041 Y-104.785
G1 X36.282 Y-104.442
G1 X36.509 Y-104.091
G1 X36.723 Y-103.731
G1 X36.924 Y-103.364
G1 X37.111 Y-102.989
G1 X37.284 Y-102.608
G1 X37.442 Y-102.22
G1 X37.586 Y-101.827
G1 X37.715 Y-101.428
G1 X37.829 Y-101.025
G1 X37.927 Y-100.619
G1 X38.011 Y-100.208
G1 X38.079 Y-99.795
G1 X38.132 Y-99.38
G1 X38.169 Y-98.963
G1 X38.19 Y-98.545
G1 X38.196 Y-98.126
G1 X38.186 Y-97.707
G1 X38.16 Y-97.29
G1 X38.119 Y-96.873
G1 X38.062 Y-96.458
G1 X37.99 Y-96.046
G1 X37.903 Y-95.636
G1 X37.8 Y-95.23
G1 X37.682 Y-94.829
G1 X37.549 Y-94.432
G1 X37.402 Y-94.04
G1 X37.24 Y-93.654
G1 X37.063 Y-93.274
G1 X36.873 Y-92.901
G1 X36.668 Y-92.536
G1 X36.45 Y-92.178
G1 X36.219 Y-91.829
G1 X35.975 Y-91.489
G1 X35.719 Y-91.158
G1 X35.45 Y-90.837
G1 X35.17 Y-90.526
G1 X34.878 Y-90.226
G1 X34.575 Y-89.937
G1 X34.261 Y-89.66

(Linearized circular move - low memory mode enabled)
G1 X33.937 Y-89.393
G1 X33.602 Y-89.14
G1 X33.259 Y-88.898
G1 X32.907 Y-88.67
G1 X32.546 Y-88.456
G1 X32.177 Y-88.255
G1 X31.802 Y-88.068
G1 X31.419 Y-87.895
G1 X31.031 Y-87.736
G1 X30.636 Y-87.593
G1 X30.237 Y-87.464
G1 X29.833 Y-87.35
G1 X29.425 Y-87.251
G1 X29.014 Y-87.168
G1 X28.599 Y-87.101
G1 X28.183 Y-87.048
G1 X27.765 Y-87.012
G1 X27.346 Y-86.991
G1 X26.926 Y-86.986
G1 X26.506 Y-86.997
G1 X26.088 Y-87.023
G1 X25.67 Y-87.065
G1 X25.254 Y-87.123
G1 X24.841 Y-87.196
G1 X24.431 Y-87.285
G1 X24.024 Y-87.389

(Linearized circular move - low memory mode enabled)
G1 X23.617 Y-87.507
G1 X23.214 Y-87.641
G1 X22.817 Y-87.79
G1 X22.426 Y-87.953
G1 X22.041 Y-88.131
G1 X21.663 Y-88.323
G1 X21.292 Y-88.529
G1 X20.929 Y-88.749
G1 X20.575 Y-88.983
G1 X20.23 Y-89.229
G1 X19.894 Y-89.488
G1 X19.568 Y-89.76
G1 X19.253 Y-90.043
G1 X18.948 Y-90.338
G1 X18.655 Y-90.645
G1 X18.373 Y-90.962
G1 X18.103 Y-91.289
G1 X17.846 Y-91.626
G1 X17.602 Y-91.973
G1 X17.37 Y-92.328
G1 X17.152 Y-92.692
G1 X16.948 Y-93.064
G1 X16.758 Y-93.443
G1 X16.582 Y-93.829
G1 X16.421 Y-94.222
G1 X16.275 Y-94.62
G1 X16.143 Y-95.023
G1 X16.027 Y-95.431
G1 X15.926 Y-95.843
G1 X15.841 Y-96.258
G1 X15.771 Y-96.677
G1 X15.716 Y-97.097
G1 X15.678 Y-97.52
G1 X15.655 Y-97.943
G1 X15.649 Y-98.367
G1 X15.658 Y-98.792
G1 X15.683 Y-99.215
G1 X15.724 Y-99.637
G1 X15.78 Y-100.058
G1 X15.852 Y-100.475
G1 X15.94 Y-100.89
G1 X16.043 Y-101.302

(Linearized circular move - low memory mode enabled)
G1 X16.15 Y-101.671
G1 X16.269 Y-102.035
G1 X16.401 Y-102.396
G1 X16.544 Y-102.752
G1 X16.699 Y-103.103
G1 X16.867 Y-103.449
G1 X17.045 Y-103.788
G1 X17.236 Y-104.122
G1 X17.437 Y-104.448
G1 X17.649 Y-104.768

(Linearized circular move - low memory mode enabled)
G1 X17.897 Y-105.116
G1 X18.158 Y-105.454
G1 X18.432 Y-105.783
G1 X18.717 Y-106.101
G1 X19.015 Y-106.408
G1 X19.323 Y-106.703
G1 X19.642 Y-106.987
G1 X19.972 Y-107.259
G1 X20.312 Y-107.518
G1 X20.661 Y-107.765
G1 X21.019 Y-107.998
G1 X21.385 Y-108.217
G1 X21.76 Y-108.423
G1 X22.142 Y-108.615
G1 X22.53 Y-108.792
G1 X22.926 Y-108.954
G1 X23.327 Y-109.102
G1 X23.733 Y-109.234
G1 X24.144 Y-109.352
G1 X24.559 Y-109.453
G1 X24.977 Y-109.54
G1 X25.398 Y-109.61
G1 X25.822 Y-109.665
G1 X26.248 Y-109.704
G1 X26.674 Y-109.727
G1 X27.102 Y-109.733
G1 X27.529 Y-109.724
G1 X27.955 Y-109.699
G1 X28.381 Y-109.658
G1 X28.804 Y-109.602
G1 X29.225 Y-109.529
G1 X29.643 Y-109.441
G1 X30.058 Y-109.337
G1 X30.468 Y-109.217
G1 X30.873 Y-109.083
G1 X31.274 Y-108.933
G1 X31.668 Y-108.769
G1 X32.056 Y-108.59
G1 X32.437 Y-108.396
G1 X32.81 Y-108.188
G1 X33.176 Y-107.967
G1 X33.532 Y-107.732

(Linearized circular move - low memory mode enabled)
G1 X33.841 Y-107.513
G1 X34.142 Y-107.284
G1 X34.436 Y-107.044
G1 X34.721 Y-106.796
G1 X34.998 Y-106.538
G1 X35.267 Y-106.27
G1 X35.526 Y-105.995
G1 X35.776 Y-105.71

(Linearized circular move - low memory mode enabled)
G1 X36.045 Y-105.385
G1 X36.303 Y-105.05
G1 X36.548 Y-104.707
G1 X36.78 Y-104.354
G1 X36.999 Y-103.993
G1 X37.205 Y-103.624
G1 X37.397 Y-103.249
G1 X37.575 Y-102.866
G1 X37.74 Y-102.477
G1 X37.89 Y-102.082
G1 X38.025 Y-101.682
G1 X38.146 Y-101.278
G1 X38.252 Y-100.869
G1 X38.343 Y-100.457
G1 X38.419 Y-100.041
G1 X38.479 Y-99.624
G1 X38.525 Y-99.204
G1 X38.554 Y-98.783
G1 X38.569 Y-98.361
G1 X38.568 Y-97.938
G1 X38.552 Y-97.517
G1 X38.52 Y-97.096
G1 X38.473 Y-96.676
G1 X38.411 Y-96.258
G1 X38.333 Y-95.843
G1 X38.24 Y-95.431
G1 X38.133 Y-95.023
G1 X38.01 Y-94.619
G1 X37.873 Y-94.22
G1 X37.721 Y-93.826
G1 X37.555 Y-93.438
G1 X37.375 Y-93.056
G1 X37.182 Y-92.681
G1 X36.974 Y-92.313
G1 X36.753 Y-91.953
G1 X36.52 Y-91.601
G1 X36.273 Y-91.258
G1 X36.015 Y-90.925
G1 X35.744 Y-90.601
G1 X35.461 Y-90.287
G1 X35.168 Y-89.984
G1 X34.863 Y-89.691
G1 X34.548 Y-89.41

(Linearized circular move - low memory mode enabled)
G1 X34.228 Y-89.145
G1 X33.9 Y-88.892
G1 X33.562 Y-88.651
G1 X33.216 Y-88.422
G1 X32.861 Y-88.205
G1 X32.5 Y-88.001
G1 X32.131 Y-87.811
G1 X31.755 Y-87.634
G1 X31.374 Y-87.47
G1 X30.987 Y-87.321
G1 X30.594 Y-87.185
G1 X30.197 Y-87.063
G1 X29.796 Y-86.956
G1 X29.392 Y-86.863
G1 X28.984 Y-86.785
G1 X28.574 Y-86.722
G1 X28.162 Y-86.673
G1 X27.748 Y-86.639
G1 X27.333 Y-86.62
G1 X26.918 Y-86.616
G1 X26.503 Y-86.627
G1 X26.089 Y-86.653
G1 X25.676 Y-86.693

(Linearized circular move - low memory mode enabled)
G1 X25.329 Y-86.732
G1 X24.984 Y-86.782
G1 X24.641 Y-86.845
G1 X24.3 Y-86.921
G1 X23.962 Y-87.008

(Linearized circular move - low memory mode enabled)
G1 X23.55 Y-87.127
G1 X23.142 Y-87.262
G1 X22.74 Y-87.411
G1 X22.343 Y-87.574
G1 X21.953 Y-87.752
G1 X21.569 Y-87.945
G1 X21.193 Y-88.151
G1 X20.825 Y-88.37
G1 X20.464 Y-88.604
G1 X20.113 Y-88.85
G1 X19.77 Y-89.108
G1 X19.438 Y-89.379
G1 X19.115 Y-89.662
G1 X18.803 Y-89.957
G1 X18.502 Y-90.263
G1 X18.213 Y-90.579
G1 X17.935 Y-90.906
G1 X17.669 Y-91.243
G1 X17.416 Y-91.589
G1 X17.175 Y-91.945
G1 X16.948 Y-92.309
G1 X16.734 Y-92.681
G1 X16.534 Y-93.06
G1 X16.348 Y-93.447
G1 X16.176 Y-93.84
G1 X16.018 Y-94.239
G1 X15.875 Y-94.644
G1 X15.748 Y-95.053
G1 X15.635 Y-95.467
G1 X15.537 Y-95.885
G1 X15.455 Y-96.306
G1 X15.388 Y-96.73
G1 X15.337 Y-97.156
G1 X15.301 Y-97.584
G1 X15.281 Y-98.012
G1 X15.276 Y-98.442
G1 X15.287 Y-98.87
G1 X15.314 Y-99.299
G1 X15.357 Y-99.726
G1 X15.415 Y-100.151
G1 X15.488 Y-100.574
G1 X15.577 Y-100.993
G1 X15.682 Y-101.41

(Linearized circular move - low memory mode enabled)
G1 X15.8 Y-101.819
G1 X15.934 Y-102.224
G1 X16.082 Y-102.624
G1 X16.244 Y-103.019
G1 X16.421 Y-103.407
G1 X16.611 Y-103.789
G1 X16.815 Y-104.163
G1 X17.033 Y-104.53
G1 X17.264 Y-104.888
G1 X17.508 Y-105.238
G1 X17.764 Y-105.579
G1 X18.033 Y-105.91
G1 X18.313 Y-106.232
G1 X18.605 Y-106.543
G1 X18.908 Y-106.843
G1 X19.222 Y-107.132
G1 X19.546 Y-107.409
G1 X19.88 Y-107.675
G1 X20.223 Y-107.928
G1 X20.575 Y-108.168
G1 X20.936 Y-108.396
G1 X21.305 Y-108.61
G1 X21.681 Y-108.811
G1 X22.064 Y-108.998
G1 X22.454 Y-109.171
G1 X22.85 Y-109.33
G1 X23.251 Y-109.474
G1 X23.658 Y-109.604
G1 X24.069 Y-109.718
G1 X24.483 Y-109.818
G1 X24.901 Y-109.903
G1 X25.322 Y-109.972
G1 X25.745 Y-110.026
G1 X26.17 Y-110.065
G1 X26.596 Y-110.088

(Linearized circular move - low memory mode enabled)
G1 X27.029 Y-110.098
G1 X27.462 Y-110.091
G1 X27.895 Y-110.069
G1 X28.326 Y-110.031
G1 X28.756 Y-109.977
G1 X29.184 Y-109.908
G1 X29.609 Y-109.823
G1 X30.03 Y-109.723
G1 X30.448 Y-109.607
G1 X30.861 Y-109.476
G1 X31.269 Y-109.33
G1 X31.671 Y-109.169
G1 X32.068 Y-108.994
G1 X32.457 Y-108.805
G1 X32.84 Y-108.601
G1 X33.214 Y-108.383
G1 X33.581 Y-108.152
G1 X33.939 Y-107.908
G1 X34.287 Y-107.651
G1 X34.626 Y-107.381
G1 X34.955 Y-107.099
G1 X35.274 Y-106.805
G1 X35.581 Y-106.5
G1 X35.877 Y-106.184
G1 X36.162 Y-105.857
G1 X36.434 Y-105.52
G1 X36.694 Y-105.173
G1 X36.941 Y-104.817
G1 X37.175 Y-104.452
G1 X37.395 Y-104.079
G1 X37.602 Y-103.698
G1 X37.794 Y-103.31
G1 X37.973 Y-102.915
G1 X38.136 Y-102.514
G1 X38.285 Y-102.107
G1 X38.419 Y-101.695
G1 X38.538 Y-101.279
G1 X38.642 Y-100.858
G1 X38.73 Y-100.434

(Linearized circular move - low memory mode enabled)
G1 X38.804 Y-100.004
G1 X38.863 Y-99.571
G1 X38.906 Y-99.137
G1 X38.932 Y-98.701
G1 X38.943 Y-98.264
G1 X38.938 Y-97.828
G1 X38.918 Y-97.392
G1 X38.881 Y-96.957
G1 X38.828 Y-96.524
G1 X38.76 Y-96.092
G1 X38.675 Y-95.664
G1 X38.576 Y-95.239
G1 X38.46 Y-94.818
G1 X38.33 Y-94.401
G1 X38.184 Y-93.99
G1 X38.024 Y-93.584
G1 X37.848 Y-93.184
G1 X37.659 Y-92.791
G1 X37.455 Y-92.405
G1 X37.237 Y-92.027
G1 X37.005 Y-91.657
G1 X36.76 Y-91.296
G1 X36.502 Y-90.943
G1 X36.231 Y-90.601
G1 X35.948 Y-90.269
G1 X35.653 Y-89.947
G1 X35.346 Y-89.636
G1 X35.029 Y-89.337
G1 X34.7 Y-89.049
G1 X34.362 Y-88.774
G1 X34.013 Y-88.511
G1 X33.655 Y-88.261
G1 X33.288 Y-88.024
G1 X32.913 Y-87.801
G1 X32.53 Y-87.592
G1 X32.14 Y-87.396
G1 X31.742 Y-87.216
G1 X31.339 Y-87.05
G1 X30.929 Y-86.898
G1 X30.514 Y-86.762
G1 X30.095 Y-86.641
G1 X29.671 Y-86.535
G1 X29.244 Y-86.445

(Linearized circular move - low memory mode enabled)
G1 X28.829 Y-86.373
G1 X28.412 Y-86.316
G1 X27.993 Y-86.273
G1 X27.573 Y-86.246
G1 X27.152 Y-86.232
G1 X26.731 Y-86.234
G1 X26.31 Y-86.251
G1 X25.89 Y-86.282
G1 X25.471 Y-86.329
G1 X25.055 Y-86.39
G1 X24.64 Y-86.465
G1 X24.229 Y-86.555
G1 X23.821 Y-86.66
G1 X23.417 Y-86.779
G1 X23.017 Y-86.912
G1 X22.623 Y-87.059

(Linearized circular move - low memory mode enabled)
G1 X22.223 Y-87.222
G1 X21.829 Y-87.4
G1 X21.442 Y-87.592
G1 X21.061 Y-87.797
G1 X20.689 Y-88.015
G1 X20.324 Y-88.247
G1 X19.968 Y-88.492
G1 X19.621 Y-88.749
G1 X19.283 Y-89.019
G1 X18.955 Y-89.3
G1 X18.638 Y-89.593
G1 X18.331 Y-89.897
G1 X18.035 Y-90.212
G1 X17.75 Y-90.537
G1 X17.477 Y-90.872
G1 X17.217 Y-91.216
G1 X16.969 Y-91.57
G1 X16.733 Y-91.932
G1 X16.511 Y-92.303
G1 X16.302 Y-92.681
G1 X16.107 Y-93.066
G1 X15.925 Y-93.459
G1 X15.758 Y-93.857
G1 X15.605 Y-94.261
G1 X15.467 Y-94.67
G1 X15.343 Y-95.084
G1 X15.234 Y-95.502
G1 X15.14 Y-95.924
G1 X15.061 Y-96.349
G1 X14.998 Y-96.776
G1 X14.95 Y-97.206
G1 X14.917 Y-97.636
G1 X14.899 Y-98.068
G1 X14.897 Y-98.5
G1 X14.91 Y-98.932
G1 X14.939 Y-99.363
G1 X14.983 Y-99.793
G1 X15.043 Y-100.221
G1 X15.117 Y-100.646
G1 X15.207 Y-101.069
G1 X15.312 Y-101.488
G1 X15.431 Y-101.903
G1 X15.566 Y-102.314
G1 X15.715 Y-102.719

(Linearized circular move - low memory mode enabled)
G1 X15.874 Y-103.11
G1 X16.047 Y-103.494
G1 X16.233 Y-103.872
G1 X16.432 Y-104.244
G1 X16.644 Y-104.608
G1 X16.868 Y-104.965
G1 X17.105 Y-105.314
G1 X17.354 Y-105.654
G1 X17.614 Y-105.986
G1 X17.886 Y-106.308
G1 X18.169 Y-106.62
G1 X18.462 Y-106.923
G1 X18.766 Y-107.215
G1 X19.08 Y-107.496

(Linearized circular move - low memory mode enabled)
G1 X19.414 Y-107.776
G1 X19.758 Y-108.043
G1 X20.111 Y-108.298
G1 X20.473 Y-108.54
G1 X20.843 Y-108.77
G1 X21.222 Y-108.986
G1 X21.607 Y-109.188
G1 X22.0 Y-109.376
G1 X22.399 Y-109.55
G1 X22.805 Y-109.71
G1 X23.215 Y-109.855
G1 X23.631 Y-109.986
G1 X24.051 Y-110.101
G1 X24.474 Y-110.202
G1 X24.902 Y-110.287
G1 X25.331 Y-110.357
G1 X25.764 Y-110.412
G1 X26.197 Y-110.451
G1 X26.632 Y-110.475
G1 X27.068 Y-110.483
G1 X27.503 Y-110.476
G1 X27.938 Y-110.453
G1 X28.372 Y-110.414
G1 X28.804 Y-110.36
G1 X29.234 Y-110.291
G1 X29.661 Y-110.207
G1 X30.085 Y-110.107
G1 X30.506 Y-109.992
G1 X30.921 Y-109.862
G1 X31.332 Y-109.718
G1 X31.738 Y-109.558
G1 X32.137 Y-109.385
G1 X32.53 Y-109.197
G1 X32.916 Y-108.996
G1 X33.295 Y-108.781
G1 X33.666 Y-108.552
G1 X34.028 Y-108.31
G1 X34.382 Y-108.056
G1 X34.726 Y-107.789
G1 X35.06 Y-107.51
G1 X35.385 Y-107.219
G1 X35.698 Y-106.917
G1 X36.001 Y-106.604
G1 X36.292 Y-106.28

(Linearized circular move - low memory mode enabled)
G1 X36.547 Y-105.977
G1 X36.792 Y-105.666
G1 X37.026 Y-105.347
G1 X37.25 Y-105.02
G1 X37.463 Y-104.687
G1 X37.666 Y-104.347
G1 X37.857 Y-104.0

(Linearized circular move - low memory mode enabled)
G1 X38.058 Y-103.609
G1 X38.245 Y-103.211
G1 X38.418 Y-102.807
G1 X38.576 Y-102.397
G1 X38.719 Y-101.981
G1 X38.847 Y-101.561
G1 X38.961 Y-101.136
G1 X39.059 Y-100.708
G1 X39.142 Y-100.276
G1 X39.209 Y-99.842
G1 X39.261 Y-99.405
G1 X39.297 Y-98.967
G1 X39.318 Y-98.528
G1 X39.322 Y-98.088
G1 X39.312 Y-97.649
G1 X39.285 Y-97.21
G1 X39.243 Y-96.773
G1 X39.185 Y-96.337
G1 X39.112 Y-95.903
G1 X39.023 Y-95.473
G1 X38.919 Y-95.046
G1 X38.8 Y-94.623
G1 X38.666 Y-94.204
G1 X38.516 Y-93.791
G1 X38.353 Y-93.383
G1 X38.174 Y-92.981
G1 X37.982 Y-92.586
G1 X37.776 Y-92.198
G1 X37.555 Y-91.817
G1 X37.322 Y-91.445
G1 X37.075 Y-91.081
G1 X36.816 Y-90.726
G1 X36.544 Y-90.381
G1 X36.259 Y-90.046
G1 X35.963 Y-89.721
G1 X35.656 Y-89.406
G1 X35.338 Y-89.103
G1 X35.009 Y-88.812
G1 X34.669 Y-88.532
G1 X34.32 Y-88.265
G1 X33.962 Y-88.01
G1 X33.595 Y-87.769
G1 X33.219 Y-87.54
G1 X32.836 Y-87.325

(Linearized circular move - low memory mode enabled)
G1 X32.447 Y-87.125
G1 X32.052 Y-86.939
G1 X31.65 Y-86.767
G1 X31.242 Y-86.61
G1 X30.829 Y-86.467
G1 X30.411 Y-86.339
G1 X29.989 Y-86.225
G1 X29.563 Y-86.127
G1 X29.133 Y-86.044
G1 X28.701 Y-85.976
G1 X28.267 Y-85.924
G1 X27.832 Y-85.887
G1 X27.395 Y-85.865
G1 X26.958 Y-85.86
G1 X26.521 Y-85.869
G1 X26.085 Y-85.894
G1 X25.649 Y-85.935
G1 X25.216 Y-85.991
G1 X24.756 Y-86.056
G1 X24.301 Y-86.147

(Linearized circular move - low memory mode enabled)
G1 X23.867 Y-86.252
G1 X23.437 Y-86.373
G1 X23.012 Y-86.508
G1 X22.592 Y-86.659
G1 X22.178 Y-86.824
G1 X21.769 Y-87.004
G1 X21.368 Y-87.199
G1 X20.973 Y-87.408
G1 X20.587 Y-87.631
G1 X20.208 Y-87.867
G1 X19.838 Y-88.117
G1 X19.478 Y-88.38
G1 X19.127 Y-88.656
G1 X18.786 Y-88.944
G1 X18.455 Y-89.244
G1 X18.136 Y-89.555
G1 X17.828 Y-89.878
G1 X17.531 Y-90.211
G1 X17.247 Y-90.555
G1 X16.975 Y-90.909
G1 X16.716 Y-91.272
G1 X16.47 Y-91.645
G1 X16.237 Y-92.026
G1 X16.018 Y-92.415
G1 X15.813 Y-92.811
G1 X15.623 Y-93.215
G1 X15.447 Y-93.625
G1 X15.286 Y-94.041
G1 X15.139 Y-94.462
G1 X15.008 Y-94.889
G1 X14.892 Y-95.32
G1 X14.792 Y-95.755
G1 X14.707 Y-96.193
G1 X14.638 Y-96.634
G1 X14.585 Y-97.077
G1 X14.547 Y-97.522
G1 X14.526 Y-97.967
G1 X14.52 Y-98.414
G1 X14.53 Y-98.86
G1 X14.557 Y-99.305
G1 X14.599 Y-99.75
G1 X14.656 Y-100.192
G1 X14.73 Y-100.632
G1 X14.819 Y-101.069

(Linearized circular move - low memory mode enabled)
G1 X14.923 Y-101.498
G1 X15.042 Y-101.923
G1 X15.175 Y-102.344
G1 X15.324 Y-102.759
G1 X15.487 Y-103.169
G1 X15.664 Y-103.573
G1 X15.855 Y-103.971
G1 X16.061 Y-104.361
G1 X16.28 Y-104.744
G1 X16.512 Y-105.119
G1 X16.757 Y-105.486
G1 X17.016 Y-105.844
G1 X17.286 Y-106.192
G1 X17.569 Y-106.531
G1 X17.864 Y-106.859
G1 X18.17 Y-107.177
G1 X18.487 Y-107.484
G1 X18.814 Y-107.779
G1 X19.152 Y-108.063
G1 X19.5 Y-108.335
G1 X19.857 Y-108.594
G1 X20.223 Y-108.84
G1 X20.597 Y-109.074
G1 X20.98 Y-109.294
G1 X21.37 Y-109.5
G1 X21.767 Y-109.692
G1 X22.17 Y-109.871
G1 X22.58 Y-110.035
G1 X22.995 Y-110.184
G1 X23.415 Y-110.319
G1 X23.84 Y-110.439
G1 X24.268 Y-110.543
G1 X24.7 Y-110.633
G1 X25.135 Y-110.707
G1 X25.572 Y-110.766
G1 X26.011 Y-110.809

(Linearized circular move - low memory mode enabled)
G1 X26.454 Y-110.839
G1 X26.898 Y-110.854
G1 X27.341 Y-110.852
G1 X27.785 Y-110.835
G1 X28.227 Y-110.803
G1 X28.669 Y-110.755
G1 X29.108 Y-110.691
G1 X29.544 Y-110.612
G1 X29.978 Y-110.517
G1 X30.408 Y-110.408
G1 X30.834 Y-110.283
G1 X31.255 Y-110.143
G1 X31.671 Y-109.989
G1 X32.082 Y-109.82
G1 X32.486 Y-109.636
G1 X32.883 Y-109.439
G1 X33.273 Y-109.228
G1 X33.656 Y-109.002
G1 X34.03 Y-108.764
G1 X34.396 Y-108.513
G1 X34.752 Y-108.249
G1 X35.099 Y-107.972
G1 X35.437 Y-107.683
G1 X35.763 Y-107.383
G1 X36.079 Y-107.071
G1 X36.384 Y-106.749
G1 X36.677 Y-106.416
G1 X36.958 Y-106.073
G1 X37.227 Y-105.72
G1 X37.484 Y-105.357
G1 X37.727 Y-104.986
G1 X37.958 Y-104.607
G1 X38.174 Y-104.22

(Linearized circular move - low memory mode enabled)
G1 X38.378 Y-103.826
G1 X38.568 Y-103.426
G1 X38.744 Y-103.019
G1 X38.906 Y-102.606
G1 X39.053 Y-102.188
G1 X39.186 Y-101.764
G1 X39.304 Y-101.337
G1 X39.406 Y-100.906
G1 X39.494 Y-100.471
G1 X39.566 Y-100.034
G1 X39.623 Y-99.594
G1 X39.665 Y-99.153
G1 X39.691 Y-98.71
G1 X39.702 Y-98.267
G1 X39.697 Y-97.823
G1 X39.677 Y-97.38
G1 X39.642 Y-96.938
G1 X39.591 Y-96.498
G1 X39.524 Y-96.06
G1 X39.443 Y-95.624
G1 X39.346 Y-95.191
G1 X39.234 Y-94.762
G1 X39.107 Y-94.337
G1 X38.966 Y-93.917
G1 X38.81 Y-93.502
G1 X38.639 Y-93.093
G1 X38.455 Y-92.69
G1 X38.256 Y-92.293
G1 X38.044 Y-91.904
G1 X37.818 Y-91.522
G1 X37.579 Y-91.149
G1 X37.327 Y-90.784
G1 X37.063 Y-90.428
G1 X36.786 Y-90.082
G1 X36.498 Y-89.745
G1 X36.198 Y-89.419
G1 X35.886 Y-89.103
G1 X35.564 Y-88.798
G1 X35.231 Y-88.505
G1 X34.889 Y-88.224
G1 X34.537 Y-87.955
G1 X34.175 Y-87.698
G1 X33.805 Y-87.454
G1 X33.426 Y-87.223
G1 X33.04 Y-87.005

(Linearized circular move - low memory mode enabled)
G1 X32.656 Y-86.806
G1 X32.264 Y-86.62
G1 X31.867 Y-86.447
G1 X31.464 Y-86.288
G1 X31.056 Y-86.142
G1 X30.643 Y-86.011
G1 X30.226 Y-85.894
G1 X29.806 Y-85.791
G1 X29.382 Y-85.702
G1 X28.955 Y-85.629
G1 X28.526 Y-85.569
G1 X28.095 Y-85.524
G1 X27.663 Y-85.494
G1 X27.23 Y-85.479
G1 X26.796
G1 X26.363 Y-85.493
G1 X25.931 Y-85.522
G1 X25.5 Y-85.566
G1 X25.071 Y-85.625
G1 X24.644 Y-85.698
G1 X24.22 Y-85.785
G1 X23.799 Y-85.887

(Linearized circular move - low memory mode enabled)
G1 X23.365 Y-86.006
G1 X22.936 Y-86.14
G1 X22.512 Y-86.289
G1 X22.094 Y-86.453
G1 X21.681 Y-86.631
G1 X21.275 Y-86.824
G1 X20.876 Y-87.03
G1 X20.485 Y-87.251
G1 X20.101 Y-87.485
G1 X19.726 Y-87.732
G1 X19.36 Y-87.993
G1 X19.003 Y-88.266
G1 X18.655 Y-88.551
G1 X18.318 Y-88.848
G1 X17.992 Y-89.157
G1 X17.677 Y-89.477
G1 X17.373 Y-89.808
G1 X17.08 Y-90.15
G1 X16.8 Y-90.501
G1 X16.533 Y-90.862
G1 X16.278 Y-91.233
G1 X16.036 Y-91.611
G1 X15.808 Y-91.999

(Linearized circular move - low memory mode enabled)
G1 X15.593 Y-92.393
G1 X15.393 Y-92.796
G1 X15.206 Y-93.205
G1 X15.034 Y-93.62
G1 X14.877 Y-94.041
G1 X14.734 Y-94.467
G1 X14.607 Y-94.898
G1 X14.494 Y-95.333
G1 X14.397 Y-95.772
G1 X14.316 Y-96.214
G1 X14.25 Y-96.658
G1 X14.199 Y-97.105
G1 X14.164 Y-97.553
G1 X14.145 Y-98.002
G1 X14.142 Y-98.451
G1 X14.154 Y-98.9
G1 X14.182 Y-99.349
G1 X14.226 Y-99.796
G1 X14.285 Y-100.242
G1 X14.36 Y-100.685
G1 X14.45 Y-101.125
G1 X14.556 Y-101.562
G1 X14.677 Y-101.995

(Linearized circular move - low memory mode enabled)
G1 X14.812 Y-102.425
G1 X14.963 Y-102.85
G1 X15.128 Y-103.27
G1 X15.308 Y-103.683
G1 X15.502 Y-104.09
G1 X15.71 Y-104.49
G1 X15.933 Y-104.883
G1 X16.168 Y-105.268
G1 X16.417 Y-105.644
G1 X16.679 Y-106.011
G1 X16.954 Y-106.369
G1 X17.241 Y-106.717
G1 X17.539 Y-107.055
G1 X17.85 Y-107.382
G1 X18.172 Y-107.698
G1 X18.504 Y-108.003
G1 X18.847 Y-108.295
G1 X19.2 Y-108.576
G1 X19.563 Y-108.844
G1 X19.934 Y-109.1
G1 X20.315 Y-109.342
G1 X20.703 Y-109.571
G1 X21.1 Y-109.786
G1 X21.503 Y-109.988
G1 X21.914 Y-110.175
G1 X22.331 Y-110.347
G1 X22.753 Y-110.505
G1 X23.181 Y-110.648
G1 X23.613 Y-110.776
G1 X24.05 Y-110.889
G1 X24.49 Y-110.987
G1 X24.934 Y-111.069
G1 X25.38 Y-111.136
G1 X25.828 Y-111.187
G1 X26.278 Y-111.222
G1 X26.728 Y-111.242
G1 X27.179 Y-111.246
G1 X27.63 Y-111.234
G1 X28.08 Y-111.206
G1 X28.529 Y-111.163
G1 X28.976 Y-111.104
G1 X29.421 Y-111.03
G1 X29.863 Y-110.94
G1 X30.302 Y-110.835
G1 X30.736 Y-110.714

(Linearized circular move - low memory mode enabled)
G1 X31.162 Y-110.58
G1 X31.582 Y-110.432
G1 X31.997 Y-110.269
G1 X32.406 Y-110.091
G1 X32.808 Y-109.9
G1 X33.204 Y-109.695
G1 X33.593 Y-109.477
G1 X33.974 Y-109.245
G1 X34.346 Y-109.0
G1 X34.71 Y-108.743
G1 X35.065 Y-108.473
G1 X35.41 Y-108.191

(Linearized circular move - low memory mode enabled)
G1 X35.754 Y-107.893
G1 X36.088 Y-107.582
G1 X36.411 Y-107.26
G1 X36.722 Y-106.928
G1 X37.021 Y-106.584
G1 X37.309 Y-106.23
G1 X37.584 Y-105.867
G1 X37.845 Y-105.494
G1 X38.094 Y-105.112
G1 X38.329 Y-104.721
G1 X38.551 Y-104.323
G1 X38.758 Y-103.917
G1 X38.951 Y-103.504
G1 X39.13 Y-103.085
G1 X39.294 Y-102.66
G1 X39.443 Y-102.229
G1 X39.577 Y-101.794
G1 X39.695 Y-101.354
G1 X39.798 Y-100.91
G1 X39.886 Y-100.462
G1 X39.958 Y-100.012
G1 X40.014 Y-99.56
G1 X40.054 Y-99.106
G1 X40.079 Y-98.651
G1 X40.087 Y-98.195
G1 X40.08 Y-97.74
G1 X40.057 Y-97.285
G1 X40.018 Y-96.831
G1 X39.963 Y-96.378
G1 X39.892 Y-95.928
G1 X39.806 Y-95.48
G1 X39.704 Y-95.036
G1 X39.586 Y-94.596
G1 X39.454 Y-94.16
G1 X39.306 Y-93.729
G1 X39.143 Y-93.303
G1 X38.965 Y-92.883
G1 X38.773 Y-92.47
G1 X38.567 Y-92.064
G1 X38.347 Y-91.665
G1 X38.112 Y-91.274
G1 X37.865 Y-90.891
G1 X37.604 Y-90.518
G1 X37.33 Y-90.153
G1 X37.044 Y-89.799

(Linearized circular move - low memory mode enabled)
G1 X36.758 Y-89.469
G1 X36.462 Y-89.149
G1 X36.155 Y-88.839
G1 X35.838 Y-88.539
G1 X35.511 Y-88.25
G1 X35.174 Y-87.972
G1 X34.829 Y-87.706
G1 X34.474 Y-87.451
G1 X34.112 Y-87.209

(Linearized circular move - low memory mode enabled)
G1 X33.729 Y-86.971
G1 X33.339 Y-86.747
G1 X32.941 Y-86.536
G1 X32.536 Y-86.338
G1 X32.125 Y-86.155
G1 X31.708 Y-85.986
G1 X31.285 Y-85.831
G1 X30.857 Y-85.691
G1 X30.425 Y-85.565
G1 X29.988 Y-85.454
G1 X29.548 Y-85.358
G1 X29.105 Y-85.278
G1 X28.66 Y-85.212
G1 X28.212 Y-85.162
G1 X27.763 Y-85.127
G1 X27.313 Y-85.107
G1 X26.863 Y-85.103
G1 X26.413 Y-85.114
G1 X25.963 Y-85.14
G1 X25.515 Y-85.182
G1 X25.068 Y-85.239
G1 X24.624 Y-85.311
G1 X24.182 Y-85.399
G1 X23.743 Y-85.501
G1 X23.309 Y-85.619
G1 X22.878 Y-85.751
G1 X22.452 Y-85.898
G1 X22.032 Y-86.059
G1 X21.617 Y-86.234
G1 X21.209 Y-86.424
G1 X20.807 Y-86.627
G1 X20.412 Y-86.844
G1 X20.025 Y-87.074
G1 X19.647 Y-87.318
G1 X19.276 Y-87.574
G1 X18.915 Y-87.842
G1 X18.563 Y-88.123
G1 X18.221 Y-88.416
G1 X17.889 Y-88.72
G1 X17.567 Y-89.036
G1 X17.257 Y-89.362
G1 X16.957 Y-89.698
G1 X16.67 Y-90.045
G1 X16.394 Y-90.401
G1 X16.131 Y-90.766
G1 X15.881 Y-91.141

(Linearized circular move - low memory mode enabled)
G1 X15.653 Y-91.507
G1 X15.436 Y-91.882
G1 X15.233 Y-92.262
G1 X15.042 Y-92.65
G1 X14.863 Y-93.043
G1 X14.698 Y-93.442
G1 X14.546 Y-93.847
G1 X14.407 Y-94.256
G1 X14.281 Y-94.669
G1 X14.17 Y-95.087
G1 X14.072 Y-95.507
G1 X13.987 Y-95.931
G1 X13.917 Y-96.357
G1 X13.861 Y-96.786
G1 X13.818 Y-97.216
G1 X13.79 Y-97.647

(Linearized circular move - low memory mode enabled)
G1 X13.774 Y-98.103
G1 X13.773 Y-98.56
G1 X13.788 Y-99.017
G1 X13.819 Y-99.473
G1 X13.865 Y-99.928
G1 X13.927 Y-100.381
G1 X14.004 Y-100.831
G1 X14.098 Y-101.279
G1 X14.206 Y-101.723
G1 X14.33 Y-102.163
G1 X14.468 Y-102.599
G1 X14.622 Y-103.029
G1 X14.79 Y-103.454
G1 X14.973 Y-103.873
G1 X15.171 Y-104.285
G1 X15.382 Y-104.69
G1 X15.607 Y-105.088
G1 X15.846 Y-105.478
G1 X16.098 Y-105.859
G1 X16.363 Y-106.232
G1 X16.641 Y-106.595
G1 X16.931 Y-106.948
G1 X17.233 Y-107.291
G1 X17.547 Y-107.623
G1 X17.872 Y-107.945

(Linearized circular move - low memory mode enabled)
G1 X18.207 Y-108.255
G1 X18.554 Y-108.553
G1 X18.91 Y-108.839
G1 X19.276 Y-109.113
G1 X19.652 Y-109.374
G1 X20.036 Y-109.622
G1 X20.428 Y-109.856
G1 X20.828 Y-110.077
G1 X21.236 Y-110.284
G1 X21.65 Y-110.477
G1 X22.071 Y-110.655
G1 X22.498 Y-110.819
G1 X22.93 Y-110.968
G1 X23.367 Y-111.102
G1 X23.808 Y-111.221
G1 X24.254 Y-111.324
G1 X24.702 Y-111.412
G1 X25.153 Y-111.485
G1 X25.607 Y-111.542
G1 X26.062 Y-111.584
G1 X26.518 Y-111.609
G1 X26.975 Y-111.619
G1 X27.432 Y-111.613
G1 X27.889 Y-111.592
G1 X28.345 Y-111.555

(Linearized circular move - low memory mode enabled)
G1 X28.801 Y-111.502
G1 X29.254 Y-111.433
G1 X29.706 Y-111.35
G1 X30.154 Y-111.251
G1 X30.598 Y-111.136
G1 X31.039 Y-111.007
G1 X31.474 Y-110.862
G1 X31.905 Y-110.703
G1 X32.329 Y-110.529
G1 X32.748 Y-110.341
G1 X33.16 Y-110.139
G1 X33.565 Y-109.923
G1 X33.962 Y-109.693
G1 X34.351 Y-109.449
G1 X34.732 Y-109.193
G1 X35.103 Y-108.923
G1 X35.465 Y-108.641
G1 X35.818 Y-108.347
G1 X36.16 Y-108.041
G1 X36.491 Y-107.724
G1 X36.811 Y-107.395
G1 X37.12 Y-107.056
G1 X37.418 Y-106.706
G1 X37.703 Y-106.346
G1 X37.975 Y-105.977
G1 X38.235 Y-105.599
G1 X38.482 Y-105.212
G1 X38.715 Y-104.817
G1 X38.935 Y-104.414
G1 X39.141 Y-104.003
G1 X39.333 Y-103.587
G1 X39.51 Y-103.163
G1 X39.673 Y-102.734
G1 X39.822 Y-102.3
G1 X39.955 Y-101.861
G1 X40.073 Y-101.417
G1 X40.176 Y-100.97
G1 X40.264 Y-100.519
G1 X40.336 Y-100.066
G1 X40.393 Y-99.611
G1 X40.434 Y-99.154
G1 X40.459 Y-98.695
G1 X40.469 Y-98.236
G1 X40.463 Y-97.778
G1 X40.442 Y-97.319
G1 X40.404 Y-96.862

(Linearized circular move - low memory mode enabled)
G1 X40.352 Y-96.404
G1 X40.283 Y-95.949
G1 X40.199 Y-95.496
G1 X40.099 Y-95.046
G1 X39.984 Y-94.6
G1 X39.854 Y-94.159
G1 X39.709 Y-93.722
G1 X39.549 Y-93.29
G1 X39.374 Y-92.864
G1 X39.185 Y-92.444
G1 X38.981 Y-92.031
G1 X38.763 Y-91.625
G1 X38.532 Y-91.227
G1 X38.287 Y-90.837
G1 X38.029 Y-90.455
G1 X37.758 Y-90.083
G1 X37.474 Y-89.72
G1 X37.178 Y-89.367
G1 X36.871 Y-89.025
G1 X36.551 Y-88.693
G1 X36.221 Y-88.372
G1 X35.879 Y-88.063
G1 X35.528 Y-87.766
G1 X35.166 Y-87.481
G1 X34.795 Y-87.208
G1 X34.414 Y-86.948
G1 X34.025 Y-86.702
G1 X33.628 Y-86.469
G1 X33.223 Y-86.25
G1 X32.811 Y-86.045
G1 X32.392 Y-85.854
G1 X31.966 Y-85.677
G1 X31.535 Y-85.515
G1 X31.099 Y-85.368
G1 X30.657 Y-85.236
G1 X30.212 Y-85.12

(Linearized circular move - low memory mode enabled)
G1 X29.771 Y-85.018
G1 X29.327 Y-84.93
G1 X28.88 Y-84.858
G1 X28.431 Y-84.801
G1 X27.98 Y-84.759
G1 X27.528 Y-84.732
G1 X27.076 Y-84.72
G1 X26.623 Y-84.723
G1 X26.171 Y-84.742
G1 X25.72 Y-84.776
G1 X25.27 Y-84.824
G1 X24.822 Y-84.888
G1 X24.376 Y-84.967
G1 X23.933 Y-85.061
G1 X23.494 Y-85.17
G1 X23.058 Y-85.293
G1 X22.627 Y-85.431
G1 X22.201 Y-85.583
G1 X21.78 Y-85.75
G1 X21.365 Y-85.93
G1 X20.956 Y-86.124
G1 X20.554 Y-86.332
G1 X20.159 Y-86.553
G1 X19.772 Y-86.788
G1 X19.393 Y-87.035
G1 X19.022 Y-87.295
G1 X18.66 Y-87.567
G1 X18.308 Y-87.851
"""

    gcs.process(gcode)

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


