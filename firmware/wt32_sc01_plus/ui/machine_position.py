import lvgl as lv
from lv_style import *
import gc

class MachinePositionWCS(lv.obj):
    FONT_WIDTH = 11
    FONT_HEIGHT = 22
    DEFAULT_COORD_SYSTEMS = ['Mach', 'WCS', 'Move']

    def __init__(self, parent, coords, interface, digits = 5,
                 coord_systems=DEFAULT_COORD_SYSTEMS, coord_sys_width = 40):
        super().__init__(parent)

        self.interface = interface
        self.parent = parent
        self.coords = coords
        self.digits = digits
        self.fmt_str = "%%%d.2f" % (digits - 2)
        self.coord_systems = coord_systems

        container = self
        style_container_blank(self)
        container.set_style_pad_column(2, lv.STATE.DEFAULT)
        container.set_style_pad_row(2, lv.STATE.DEFAULT)
        container.set_layout(lv.LAYOUT.GRID)
        non_scrollable(container)
        no_margin_pad_border(container)

        font = self.interface.font_lcd_24

        gc.collect()

        lblc_width = (digits + 1) * self.FONT_WIDTH
        lencs = len(coord_systems)
        lenc = len(coords)
        col_dsc = [coord_sys_width] + [lblc_width] * lencs + [lv.GRID_TEMPLATE_LAST]
        row_dsc = [self.FONT_HEIGHT + 2] * (lenc + 1) + [lv.GRID_TEMPLATE_LAST]

        container.set_style_grid_column_dsc_array(col_dsc, 0)
        container.set_style_grid_row_dsc_array(row_dsc, 0)
        container.set_size(coord_sys_width + lencs * 2 + lblc_width * lencs + 2,
                           lv.SIZE_CONTENT)

        self.axis_label_ids = {}
        self.axis_labels = []
        # Create Axis labels.
        for i, coord in enumerate(coords):
            lbl = lv.label(container)
            lbl.set_text(lv.SYMBOL.HOME + ' ' + coord)
            style(lbl, { 'bg_opa': 100, 'margin': 1, 'padding': 0 })
            lbl.set_grid_cell(lv.GRID_ALIGN.STRETCH, 0, 1,
                              lv.GRID_ALIGN.STRETCH, i + 1, 1)
            lbl.set_style_text_align(lv.TEXT_ALIGN.CENTER, lv.STATE.DEFAULT)
            lbl.center()
            lbl.add_event_cb(self._label_home_clicked, lv.EVENT.CLICKED, None)
            lbl.add_flag(lv.obj.FLAG.CLICKABLE)
            self.axis_label_ids[lbl.get_text()] = coord
            self.axis_labels.append(lbl)
            colr = color('BLUE') if interface.machine.axes_homed[i] else color('RED')
            lbl.set_style_bg_color(colr, lv.STATE.DEFAULT)

        # Coordinate systems.
        self.coord_sys_labels = []
        for i, cs in enumerate(coord_systems):
            print(i, cs)
            lbl = lv.label(container)
            lbl.set_text(cs)
            style(lbl, { 'bg_color': color('BLUE') , 'bg_opa': 100,
                         'margin': 1, 'padding': 0 })
            lbl.set_grid_cell(lv.GRID_ALIGN.STRETCH, i + 1, 1,
                              lv.GRID_ALIGN.STRETCH, 0, 1)
            lbl.set_style_text_align(lv.TEXT_ALIGN.CENTER, lv.STATE.DEFAULT)
            lbl.center()
            self.coord_sys_labels.append(lbl)

        self.coord_vals = {}
        self.coord_val_labels = {}
        self.coord_val_labels_by_id = []

        for i, cs in enumerate(coord_systems):
            c_labels = []
            self.coord_val_labels[cs] = c_labels
            self.coord_val_labels_by_id.append(cs)
            self.coord_vals[cs] = [None] * len(self.coords)

            for j, coord in enumerate(coords):
                lblc = lv.label(container)
                lblc.set_text('?' * (digits - 2) + '.??')
                style(lblc, { 'bg_color': color('LIME') , 'bg_opa': 60,
                             'margin': 0, 'padding': [2, 2] })

                if font is not None:
                    lblc.set_style_text_font(font, lv.STATE.DEFAULT)

                lblc.set_width(lblc_width)
                lblc.set_grid_cell(lv.GRID_ALIGN.STRETCH, i + 1, 1,
                                  lv.GRID_ALIGN.STRETCH, j + 1, 1)
                lblc.center()
                lblc.set_style_text_align(lv.TEXT_ALIGN.RIGHT, lv.STATE.DEFAULT)
                c_labels.append(lblc)

        interface.machine.add_pos_changed_cb(self._pos_updated)
        interface.machine.add_home_changed_cb(self._home_updated)
        interface.machine.add_wcs_changed_cb(self._wcs_updated)

        self._wcs_updated(interface.machine)

    def coords_undefined(self):
        for cs in self.coord_systems:
            self.coord_vals[cs] = [None] * len(self.coords)
            for lblc in self.coord_val_labels[cs]:
                lblc.set_text('?' * (self.digits - 2) + '.??')

    def set_coord(self, c, v, coord_system=None):
        if coord_system is None:
            coord_system = self.coord_systems[0]

        if isinstance(c, str):
            c = self.coords.index(c)

        if not self.interface.machine.axes_homed[c]:
            v = None

        if v != self.coord_vals[coord_system][c]:
            self.coord_vals[coord_system][c] = v
            if v is not None:
                self.coord_val_labels[coord_system][c].set_text(self.fmt_str % v)
            else:
                self.coord_val_labels[coord_system][c].set_text('?' * (self.digits - 2) + '.??')

    def _label_home_clicked(self, e):
        label = lv.label.__cast__(e.get_target())
        ax = self.axis_label_ids[label.get_text()]
        self.interface.machine.home(ax)

    def _pos_updated(self, mach):
        for i, coord in enumerate(mach.position):
            self.set_coord(i, coord, self.coord_systems[0])
        for i, coord in enumerate(mach.wcs_position):
            self.set_coord(i, coord, self.coord_systems[1])

    def _home_updated(self, mach):
        for i, homed in enumerate(mach.axes_homed):
            colr = color('BLUE') if homed else color('RED')
            self.axis_labels[i].set_style_bg_color(colr, lv.STATE.DEFAULT)
        self._pos_updated(mach)

    def _wcs_updated(self, mach):
        t = 'G%d' % (mach.wcs + 53)
        self.coord_sys_labels[1].set_text(t)

class MachinePosition:
    FONT_WIDTH = 7

    def __init__(self, parent, coords, interface, digits = 5):
        self.interface = interface
        self.parent = parent
        self.coords = coords
        self.digits = digits
        self.fmt_str = "%%%d.2f" % (digits - 2)

        container = create_container(parent)
        self.container = container
        flex_row(container)
        container.set_style_pad_column(0, lv.STATE.DEFAULT)
        non_scrollable(container)

        self.coord_vals = [None, None, None]

        self.coord_labels = []
        font = self.interface.font_lcd_18 or self.interface.font_lcd
        for coord in coords:
            lbl = lv.label(container)
            lbl.set_text(coord + ' ')
            style(lbl, { 'bg_color': color('BLUE') , 'bg_opa': 100,
                         'margin': 1, 'padding': 0 })

            lblc = lv.label(container)
            lblc.set_text('?' * (digits - 2) + '.??')
            style(lblc, { 'bg_color': color('LIME') , 'bg_opa': 100,
                         'margin': 0, 'padding': [2, 0] })


            if font is not None:
                lblc.set_style_text_font(font, lv.STATE.DEFAULT)
            lblc.set_width((digits + 1) * self.FONT_WIDTH)

            self.coord_labels.append(lblc)

    def coords_undefined(self):
        self.coord_vals = [None, None, None]
        for lblc in self.coord_labels:
            lblc.set_text('?' * (self.digits - 2) + '.??')

    def set_coord(self, c, v):
        if isinstance(c, str):
            c = self.coords.index(c)
        if v != self.coord_vals[c]:
            self.coord_labels[c].set_text(self.fmt_str % v)
            self.coord_vals[c] = v
