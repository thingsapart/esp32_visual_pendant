import lvgl as lv
from lv_style import *
import gc

class MachinePositionWCS(lv.obj):
    FONT_WIDTH = 8
    FONT_HEIGHT = 16

    def __init__(self, parent, coords, interface, digits = 5,
                 coord_systems = ['Mach', 'WCS', 'Move'], coord_sys_width = 20):
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

        gc.collect()

        lblc_width = (digits + 1) * self.FONT_WIDTH
        lencs = len(coord_systems)
        lenc = len(coords)
        col_dsc = [coord_sys_width] + [lblc_width] * lencs + [lv.GRID_TEMPLATE_LAST]
        row_dsc = [self.FONT_HEIGHT + 2] * (lenc + 1) + [lv.GRID_TEMPLATE_LAST]

        container.set_style_grid_column_dsc_array(col_dsc, 0)
        container.set_style_grid_row_dsc_array(row_dsc, 0)
        container.set_size(coord_sys_width + lencs * 0 + lblc_width * lencs + 0,
                           lv.SIZE_CONTENT)

        # Create Axis labels.
        for i, coord in enumerate(coords):
            lbl = lv.label(container)
            lbl.set_text(coord)
            style(lbl, { 'bg_color': color('BLUE') , 'bg_opa': 100,
                         'margin': 1, 'padding': 0 })
            lbl.set_grid_cell(lv.GRID_ALIGN.STRETCH, 0, 1,
                              lv.GRID_ALIGN.STRETCH, i + 1, 1)
            lbl.center()
            lbl.set_width(lv.SIZE_CONTENT)

        for i, cs in enumerate(coord_systems):
            print(i, cs)
            lbl = lv.label(container)
            lbl.set_text(cs)
            style(lbl, { 'bg_color': color('BLUE') , 'bg_opa': 100,
                         'margin': 1, 'padding': 0 })
            lbl.set_grid_cell(lv.GRID_ALIGN.STRETCH, i + 1, 1,
                              lv.GRID_ALIGN.STRETCH, 0, 1)
            lbl.center()

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
                             'margin': 0, 'padding': [2, 0] })

                if self.interface.font_lcd is not None:
                    lblc.set_style_text_font(self.interface.font_lcd, lv.STATE.DEFAULT)

                lblc.set_width(lblc_width)
                lblc.set_grid_cell(lv.GRID_ALIGN.STRETCH, i + 1, 1,
                                  lv.GRID_ALIGN.STRETCH, j + 1, 1)
                lblc.center()
                c_labels.append(lblc)

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
        if v != self.coord_vals[coord_system][c]:
            self.coord_val_labels[coord_system][c].set_text(self.fmt_str % v)
            self.coord_vals[coord_system][c] = v

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
        for coord in coords:
            lbl = lv.label(container)
            lbl.set_text(coord + ' ')
            style(lbl, { 'bg_color': color('BLUE') , 'bg_opa': 100,
                         'margin': 1, 'padding': 0 })

            lblc = lv.label(container)
            lblc.set_text('?' * (digits - 2) + '.??')
            style(lblc, { 'bg_color': color('LIME') , 'bg_opa': 100,
                         'margin': 0, 'padding': [2, 0] })

            if self.interface.font_lcd is not None:
                lblc.set_style_text_font(self.interface.font_lcd, lv.STATE.DEFAULT)
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

