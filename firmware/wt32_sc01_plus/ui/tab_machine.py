import lvgl as lv
from lv_style import *
from ucollections import OrderedDict
from ui.handwheel_slider import HandwheelSlider
from ui.tab_jog import JogDial
from ui.machine_position import MachinePositionWCS

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
        self.tab_macros = tabv.add_tab("Macros")
        self.tab_jobs = tabv.add_tab("Jobs")

        tabv.get_content().remove_flag(lv.obj.FLAG.SCROLLABLE)
        tabv.set_tab_bar_position(lv.DIR.BOTTOM)
        tabv.set_tab_bar_size(self.interface.TAB_HEIGHT)

        non_scrollable(self.tab_status)
        non_scrollable(self.tab_jobs)
        non_scrollable(self.tab_macros)

        self.init_tab_status(self.tab_status)
        self.init_tab_jobs(self.tab_jobs)
        self.init_tab_macros(self.tab_macros)

    def init_tab_status(self, tab):
        self.mach_meter = MachineStatusMeter(tab, self.interface, 16000, 6000)
        self.mach_meter.set_size(lv.pct(100), lv.pct(100))

    def init_tab_jobs(self, tab):
        pass

    def init_tab_macros(self, tab):
        pass

class MachineStatusMeter(lv.obj):
    CHIP_LOAD_RANGES = OrderedDict([
        ('Aluminium', OrderedDict([
            ('1/8"', [0.075, 0.1]),
            ('3mm', [0.075, 0.1]),
            ('1/4"', [0.125, 0.175]),
            ('6mm', [0.125, 0.175]),
            ('3/8"', [0.15, 0.2]),
            ('10mm', [0.15, 0.2]),
            ('1/2"', [0.15, 0.2]),
            ('12mm', [0.2, 0.25]),
        ])),
        ('Hard Wood', OrderedDict([
        ])),
        ('MDF', OrderedDict([
        ])),
        ('Soft Wood', {
        }),
        ('Acrylic', OrderedDict([
        ])),
        ('Hard Plastic', OrderedDict([
        ])),
        ('Soft Plastic', OrderedDict([
        ])),
    ])

    def __init__(self, parent, interface, spindle_max_rpm, max_feed):
        super().__init__(parent)

        style(self, { 'border_width': 0, 'padding': 0, 'margin': 0 })
        style(parent, { 'border_width': 0, 'padding': 0, 'margin': 0 })

        self.spindle_max_rpm = spindle_max_rpm // 100 * 100
        self.max_feed = max_feed // 100 * 100
        self.interface = interface

        flex_row(self)
        self.set_style_pad_row(0, lv.STATE.DEFAULT)
        self.set_style_pad_column(0, lv.STATE.DEFAULT)

        self.left_side = container_col(self)
        self.left_side.set_flex_grow(5)
        self.left_side.set_height(lv.SIZE_CONTENT)
        style(self.left_side, { 'bg_opa': 0, 'border_width': 0, 'padding': [5, 0] })
        self.left_side.set_style_text_font(lv.font_montserrat_12,
                                               lv.STATE.DEFAULT)
        self.right_side = lv.obj(self)
        self.right_side.set_height(lv.pct(100))
        self.right_side.set_width(lv.SIZE_CONTENT)

        flex_row(self.right_side)
        self.right_side.set_flex_align(lv.FLEX_ALIGN.SPACE_EVENLY,
                                       lv.FLEX_ALIGN.CENTER,
                                       lv.FLEX_ALIGN.START)
        style(self.right_side, { 'bg_opa': 0, 'border_width': 0,
								 'padding': [0, 10, 0, 0], 'margin': 0 })
        self.right_side.set_style_text_font(lv.font_montserrat_12,
                                               lv.STATE.DEFAULT)
        non_scrollable(self.right_side)

        label_feed = lv.label(self.left_side)
        label_feed.set_text('Feed (mm/min):')
        bar_feed = lv.bar(self.left_side)
        bar_feed.set_range(0, self.max_feed)
        no_margin_pad_border(bar_feed)
        style(bar_feed, { 'padding': [5, 18], 'bg_opa': 0 })
        bar_feed.set_height(18)
        bar_feed.set_width(lv.pct(100))
        bar_feed.set_style_margin_bottom(0, lv.STATE.DEFAULT)
        bar_feed.set_style_pad_bottom(0, lv.STATE.DEFAULT)
        bar_feed.add_flag(lv.obj.FLAG.ADV_HITTEST)
        style_indic = lv.style_t()
        style_indic.init()
        style_indic.set_bg_opa(lv.OPA.COVER)
        style_indic.set_bg_color(lv.color_make(0, 220, 0))
        style_indic.set_bg_grad_color(lv.color_make(0, 0, 220))
        style_indic.set_bg_grad_dir(lv.GRAD_DIR.HOR)
        style_indic.set_bg_main_stop(175)
        bar_feed.add_style(style_indic, lv.PART.INDICATOR)
        bar_feed.set_value(5000, lv.ANIM.ON)
        bar_feed.set_style_radius(3, lv.PART.MAIN)
        bar_feed.set_style_radius(3, lv.PART.INDICATOR)

        scale_feed = lv.scale(self.left_side)
        scale_feed.set_style_margin_top(0, lv.STATE.DEFAULT)
        no_margin_pad_border(scale_feed)
        style(scale_feed, { 'margin': [0, 18] })
        scale_feed.set_style_pad_top(0, lv.STATE.DEFAULT)
        scale_feed.set_mode(lv.scale.MODE.HORIZONTAL_BOTTOM)
        scale_feed.set_total_tick_count(self.max_feed // 1000 + 1)
        scale_feed.set_major_tick_every(1)
        scale_feed.set_label_show(True)
        scale_feed.set_range(0, self.max_feed // 1000)
        scale_feed.set_height(30)
        scale_feed.set_width(lv.pct(100))

        label_spindle_rpm = lv.label(self.left_side)
        label_spindle_rpm.set_text('Spindle (RPM):')
        label_spindle_rpm.set_style_text_font(lv.font_montserrat_12, lv.STATE.DEFAULT)
        slider_spindle_rpm = HandwheelSlider(self.left_side, self.interface)
        slider_spindle_rpm.set_range(0, self.spindle_max_rpm)
        no_margin_pad_border(slider_spindle_rpm)
        style(slider_spindle_rpm, { 'margin': [0, 18] })
        slider_spindle_rpm.set_height(18)
        slider_spindle_rpm.set_width(lv.pct(100))
        slider_spindle_rpm.set_style_margin_bottom(0, lv.STATE.DEFAULT)
        slider_spindle_rpm.set_style_pad_bottom(0, lv.STATE.DEFAULT)
        slider_spindle_rpm.add_flag(lv.obj.FLAG.ADV_HITTEST)
        slider_spindle_rpm.add_style(style_indic, lv.PART.INDICATOR)
        slider_spindle_rpm.set_style_radius(3, lv.PART.MAIN)

        scale_spindle_rpm = lv.scale(self.left_side)
        scale_spindle_rpm.set_mode(lv.scale.MODE.HORIZONTAL_BOTTOM)
        scale_spindle_rpm.set_total_tick_count(self.spindle_max_rpm // 1000 + 1)
        no_margin_pad_border(scale_spindle_rpm)
        style(scale_spindle_rpm, { 'margin': [0, 18] })
        scale_spindle_rpm.set_major_tick_every(5)
        scale_spindle_rpm.set_label_show(True)
        scale_spindle_rpm.set_range(0, self.spindle_max_rpm // 1000)
        #scale_spindle_rpm.set_size(30, lv.SIZE_CONTENT)
        scale_spindle_rpm.set_height(30)
        scale_spindle_rpm.set_width(lv.pct(100))


        # Material and End Mill Dropdowns.
        mat_end_container = container_row(self.left_side)
        mat_end_container.set_style_pad_bottom(5, lv.STATE.DEFAULT)
        material_dd = lv.dropdown(mat_end_container)
        material_dd.set_options_static('\n'.join(self.CHIP_LOAD_RANGES.keys()))
        material_dd.set_width(lv.pct(40))
        mill_dd = lv.dropdown(mat_end_container)
        self.mill_dd = mill_dd
        self.material_dd = material_dd
        material_dd.add_event_cb(self._mat_dd_change, lv.EVENT.VALUE_CHANGED,  None)
        mill_dd.add_event_cb(self._mill_dd_change, lv.EVENT.VALUE_CHANGED,  None)
        mill_dd.set_width(lv.pct(25))
        self._mat_dd_change(None)
        flute_dd = lv.dropdown(mat_end_container)
        self.flute_dd = flute_dd
        flute_dd.set_width(lv.pct(25))
        flute_dd.set_options_static('\n'.join(['1F', '2F', '3F', '4F']))

        # Chip Load.
        chip_ld_container = container_col(self.left_side)
        chip_ld_container.set_height(60)
        non_scrollable(chip_ld_container)

        # Label.
        label_spindle_chip = lv.label(chip_ld_container)
        label_spindle_chip.set_text('Chip Load (x0.1mm)')
        label_spindle_chip.set_width(lv.SIZE_CONTENT)

        # Chip Load Meter Container.
        chip_ldmc = container_col(chip_ld_container)
        # chip_ldmc.set_width(lv.pct(70))
        chip_ldmc.set_flex_grow(1)
        non_scrollable(chip_ldmc)
        style(chip_ldmc, { 'padding': [0, 18] })

        # Chip Load Bar.
        cl_min = 0.3
        cl_max = 0.5

        bar_cl = lv.bar(chip_ldmc)
        bar_cl.set_range(0, 50)
        no_margin_pad_border(bar_cl)
        style(bar_cl, { 'padding': [5, 0], 'bg_opa': 0 })
        bar_cl.set_height(18)
        bar_cl.set_width(lv.pct(100))
        bar_cl.set_style_margin_bottom(0, lv.STATE.DEFAULT)
        bar_cl.set_style_pad_bottom(0, lv.STATE.DEFAULT)
        bar_cl.add_flag(lv.obj.FLAG.ADV_HITTEST)
        style_barc = lv.style_t()
        style_barc.init()
        style_barc.set_bg_opa(lv.OPA.COVER)
        style_barc.set_bg_color(lv.color_make(0, 220, 0))
        style_barc.set_bg_grad_color(lv.color_make(0, 0, 220))
        style_barc.set_bg_grad_dir(lv.GRAD_DIR.HOR)
        style_barc.set_bg_main_stop(round(255 * cl_min))
        style_barc.set_bg_grad_stop(round(255 * cl_max))
        bar_cl.add_style(style_barc, lv.PART.INDICATOR)
        bar_cl.set_value(35, lv.ANIM.ON)
        bar_cl.set_style_radius(3, lv.PART.MAIN)
        bar_cl.set_style_radius(3, lv.PART.INDICATOR)
        bar_cl.set_style_bg_opa(0, lv.STATE.DEFAULT)

        # Chip Load Scale.
        scale_spindle_chipload = lv.scale(chip_ldmc)
        scale_spindle_chipload.set_mode(lv.scale.MODE.HORIZONTAL_BOTTOM)
        scale_spindle_chipload.set_total_tick_count(11)
        scale_spindle_chipload.set_major_tick_every(5)
        scale_spindle_chipload.set_label_show(True)
        scale_spindle_chipload.set_range(0, 50)
        #scale_spindle_chipload.set_size(30, lv.pct(90))
        scale_spindle_chipload.set_height(30)
        scale_spindle_chipload.set_width(lv.pct(100))
        no_margin_pad_border(scale_spindle_chipload)
        scale_spindle_chipload.add_style(style_barc, lv.PART.INDICATOR)

        def style_sec(bar, low, high, color):
            st = lv.style_t()
            st.init()
            st.set_line_color(color)
            st.set_bg_color(color)
            sec = bar.add_section()
            sec.set_range(low, high)
            sec.set_style(lv.PART.INDICATOR, st)
            sec.set_style(lv.PART.ITEMS, st)
            stm = lv.style_t()
            stm.init()
            stm.set_line_color(color)
            stm.set_bg_color(color)
            sec.set_style(lv.PART.MAIN, stm)

        style_sec(scale_spindle_chipload, 0, round(50 * cl_min), lv.color_make(0, 120, 120))
        style_sec(scale_spindle_chipload,
                  round(50 * cl_min),
                  round(50 * cl_max),
                  lv.color_make(0, 220, 0))
        style_sec(scale_spindle_chipload, round(50 * cl_max), 50,
                  lv.color_make(0, 0, 220))

        self.scale_feed = scale_feed
        self.bar_feed = bar_feed
        self.bar_cl = bar_cl
        self.scale_spindle_rpm = scale_spindle_rpm
        self.scale_spindle_chipload = scale_spindle_chipload

        self.update_layout()
        self.left_side.update_layout()

        # Machine Coordinates.
        coords = MachinePositionWCS.DEFAULT_COORD_SYSTEMS[:-1]
        self.position = MachinePositionWCS(self.right_side, JogDial.AXES,
                                           self.interface, digits=6,
                                           coord_systems=coords)
        self.position.align_to(self.right_side, lv.ALIGN.TOP_MID, 0, 0)
        self.position.set_style_margin_top(20, lv.STATE.DEFAULT)

        # Flutes
        # Diameter
        # Material
        # Chip load = feed/(rpm * n_flutes)

    def _mill_dd_change(self, e):
        pass

    def _mat_dd_change(self, e):
        material = list(self.CHIP_LOAD_RANGES.keys())[self.material_dd.get_selected()]
        mills = self.CHIP_LOAD_RANGES[material]
        self.mill_dd.set_options_static('\n'.join(mills.keys()))
