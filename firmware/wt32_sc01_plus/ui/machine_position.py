import lvgl as lv
from lv_style import *

class MachinePosition:
    FONT_WIDTH = 7

    def __init__(self, parent, coords, interface, digits = 5):
        self.interface = interface
        self.parent = parent
        self.coords = coords
        self.fmt_str = "%d.2f" % (digits - 2)

        container = create_container(parent)
        self.container = container
        flex_row(container)
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
                         'margin': 0, 'padding': 0 })

            if self.interface.font_lcd is not None:
                lblc.set_style_text_font(self.interface.font_lcd, lv.STATE.DEFAULT)
            lblc.set_width((digits + 1) * self.FONT_WIDTH)

            self.coord_labels.append(lblc)

    def set_coord(self, c, v):
        if isinstance(c, str):
            c = self.coords.index(c)
        if v != self.coord_vals[c]:
            self.coord_labels[c].set_text(self.fmt_str % v)
            self.coord_vals[c] = v
