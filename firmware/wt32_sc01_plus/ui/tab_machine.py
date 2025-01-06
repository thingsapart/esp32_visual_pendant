import lvgl as lv
from lv_style import *

class TabMachine:
    def __init__(self, tabv, interface, tab):
        self.tab = tab
        self.interface = interface

        tab = self.tab
        flex_col(tab)

        self.init_machine_tabv(tab)
        self.init_axis_float_btn()

    def init_axis_float_btn(self):
        jog_dial = self.interface.tab_jog.jog_dial

        self.float_btn = lv.button(self.tab)
        self.float_btn.set_size(45, 45)
        self.float_btn.add_flag(lv.obj.FLAG.FLOATING)
        self.float_btn.align(lv.ALIGN.BOTTOM_RIGHT, 0, 0)
        label = lv.label(self.float_btn)
        label.set_text(jog_dial.current_axis())
        label.center()
        self.float_btn.add_event_cb(lambda e: label.set_text(jog_dial.next_axis()),
                                    lv.EVENT.CLICKED, None)
        jog_dial.add_axis_change_db(lambda t: label.set_text(t))
        style(self.float_btn, { 'radius': lv.RADIUS_CIRCLE })

    def init_machine_tabv(self, parent):
        self.main_tabs = lv.tabview(parent)

        style_pad(parent, 5)

        tabv = self.main_tabs

        self.tab_status = tabv.add_tab("Status")
        self.tab_jobs = tabv.add_tab("Files")
        self.tab_macros = tabv.add_tab("Macros")

        tabv.get_content().remove_flag(lv.obj.FLAG.SCROLLABLE)
        tabv.set_tab_bar_position(lv.DIR.LEFT)
        tabv.set_tab_bar_size(70)

        non_scrollable(self.tab_status)
        non_scrollable(self.tab_jobs)
        non_scrollable(self.tab_macros)

        self.init_tab_status(self.tab_status)
        self.init_tab_jobs(self.tab_jobs)
        self.init_tab_macros(self.tab_macros)

    def init_tab_status(self, tab):
        self.mach_meter = MachineStatusMeter(tab, 16000, 6000)
        self.mach_meter.set_size(lv.pct(100), lv.pct(100))

    def init_tab_jobs(self, tab):
        pass

    def init_tab_macros(self, tab):
        pass

class MachineStatusMeter(lv.obj):
    CHIP_LOAD_RANGES = {
        'Aluminium': {
            '1/8"': [0.075, 0.1],
            '3mm': [0.075, 0.1],
            '1/4"': [0.125, 0.175],
            '6mm': [0.125, 0.175],
            '3/8"': [0.15, 0.2],
            '10mm': [0.15, 0.2],
            '1/2"': [0.15, 0.2],
            '12mm': [0.2, 0.25],
        },
        'Hard Wood': {

        },
        'MDF': {

        },
        'Soft Wood': {
        },
        'Acrylic': {

        },
        'Hard Plastic': {

        },
        'Soft Plastic': {

         }
    }

    def __init__(self, parent, spindle_max_rpm, max_feed):
        super().__init__(parent)

        style(self, { 'border_width': 1, 'padding': 0, 'margin': 0 })

        self.spindle_max_rpm = spindle_max_rpm // 100 * 100
        self.max_feed = max_feed // 100 * 100

        flex_row(self)

        self.left_side = lv.obj(self)
        self.left_side.set_width(lv.pct(33))
        self.left_side.set_flex_grow(2)
        self.left_side.set_height(lv.SIZE_CONTENT)
        flex_col(self.left_side)
        style(self.left_side, { 'bg_color': lv.color_hex(0xFFFF0000),'bg_opa': 0, 'border_width': 2, 'padding': 0,
                                'margin': 0 })

        self.right_side = lv.obj(self)
        self.right_side.set_flex_grow(3)
        self.right_side.set_height(lv.pct(100))
        flex_col(self.right_side)
        style(self.right_side, { 'bg_opa': 0, 'border_width': 2, 'margin': 0,
                                 'padding': 0 })


        scale_feed = lv.scale(self.left_side)
        scale_feed.set_mode(lv.scale.MODE.HORIZONTAL_BOTTOM)
        scale_feed.set_total_tick_count(self.spindle_max_rpm // 1000)
        scale_feed.set_major_tick_every(5)
        scale_feed.set_label_show(True)
        scale_feed.set_range(0, self.spindle_max_rpm // 1000)
        scale_feed.set_height(30)

        scale_spindle_rpm = lv.scale(self.left_side)
        scale_spindle_rpm.set_mode(lv.scale.MODE.HORIZONTAL_BOTTOM)
        scale_spindle_rpm.set_total_tick_count(self.max_feed // 1000 + 1)
        scale_spindle_rpm.set_major_tick_every(1)
        scale_spindle_rpm.set_label_show(True)
        scale_spindle_rpm.set_range(0, self.max_feed // 1000)
        #scale_spindle_rpm.set_size(30, lv.SIZE_CONTENT)
        scale_spindle_rpm.set_height(30)

        scale_spindle_chipload = lv.scale(self.left_side)
        scale_spindle_chipload.set_mode(lv.scale.MODE.HORIZONTAL_BOTTOM)
        scale_spindle_chipload.set_total_tick_count(11)
        scale_spindle_chipload.set_major_tick_every(5)
        scale_spindle_chipload.set_label_show(True)
        scale_spindle_chipload.set_range(0, 50)
        #scale_spindle_chipload.set_size(30, lv.pct(90))
        scale_spindle_chipload.set_height(30)

        self.scale_feed = scale_feed
        self.scale_spindle_rpm = scale_spindle_rpm
        self.scale_spindle_chipload = scale_spindle_chipload

        if False:
            self.update_layout()
            self.left_side.update_layout()

            w = self.left_side.get_width()
            scale_spindle_chipload.set_width(int(0.9 * w))
            scale_spindle_rpm.set_width(int(0.9 * w))
            scale_feed.set_width(int(0.9 * w))

        # Flutes
        # Diameter
        # Material
        # Chip load = feed/(rpm * n_flutes)
