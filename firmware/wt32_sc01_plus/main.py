# build command
# python3 esp32 BOARD=ESP32_GENERIC_S3 DISPLAY=st7796 INDEV=ft6x36 --flash-size=16
# LVGL MicroPython 1.23.0 on 2024-12-06
# WT32-SC01 PLUS

# Workaround for including frozen modules when running micropython with a script argument
# https://github.com/micropython/micropython/issues/6419
import usys as sys
sys.path.append('')

from micropython import const

import lvgl as lv

import task_handler

class HardwareSetupSim:
    def __init__(self):
        import display_driver
        self.scr = lv.obj()

class HardwareSetupMac:
    def __init__(self):
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

class HardwareSetupESP32:
    WIDTH = const(320)
    HEIGHT = const(480)
    FREQ = const(40000000)
    D7 = const(15)
    D6 = const(16)
    D5 = const(17)
    D4 = const(18)
    D3 = const(8)
    D2 = const(3)
    D1 = const(46)
    D0 = const(9)

    WR = const(47)
    RS = const(4)
    DC = const(0)
    CS = const(6)
    BL = const(45)
    TE = const(47)


    TP_SDA = const(6)
    TP_SDL = const(5)
    TP_RST = const(4)
    TP_HST = const(0)
    TP_INT = const(7)
    TP_FREQ = const(400000)

    SD_MOSI = const(40)
    SD_MISO = const(38)
    SD_SCK = const(39)
    SD_CS = const(41)

    EXT_IO1 = const(10)
    EXT_IO2 = const(11)
    EXT_IO3 = const(12)
    EXT_IO4 = const(13)
    EXT_IO5 = const(14)
    EXT_IO6 = const(21)

    ENC_PX = const(10) # Pin(10, Pin.IN, Pin.PULL_UP)
    ENC_PY = const(11) # Pin(11, Pin.IN, Pin.PULL_UP)

    def __init__(self):
        import lcd_bus
        import st7796
        import i2c
        import ft6x36
        import time

        display_bus = lcd_bus.I80Bus(
            wr=HardwareSetupESP32.WR,
            dc=HardwareSetupESP32.DC,
            cs=HardwareSetupESP32.CS,
            data0=HardwareSetupESP32.D0,
            data1=HardwareSetupESP32.D1,
            data2=HardwareSetupESP32.D2,
            data3=HardwareSetupESP32.D3,
            data4=HardwareSetupESP32.D4,
            data5=HardwareSetupESP32.D5,
            data6=HardwareSetupESP32.D6,
            data7=HardwareSetupESP32.D7,
            dc_data_high=True,
            dc_cmd_high=False,
            dc_idle_high=False,
            dc_dummy_high=False
        )

        # Use DMA transfers with double buffering for best performance.
        # The buffers should fit into the internal memory as this is
        # going to be the fastest memory available
        buf1 = display_bus.allocate_framebuffer(int(HardwareSetupESP32.WIDTH * HardwareSetupESP32.HEIGHT * 2 / 10), lcd_bus.MEMORY_INTERNAL | lcd_bus.MEMORY_DMA)
        buf2 = display_bus.allocate_framebuffer(int(HardwareSetupESP32.WIDTH * HardwareSetupESP32.HEIGHT * 2 / 10), lcd_bus.MEMORY_INTERNAL | lcd_bus.MEMORY_DMA)

        display_driver = st7796.ST7796(
            data_bus=display_bus,
            display_width=HardwareSetupESP32.WIDTH,
            display_height=HardwareSetupESP32.HEIGHT,
            frame_buffer1=buf1,
            frame_buffer2=buf2,
            reset_pin=HardwareSetupESP32.RS,

            # You may have to set this to STATE_LOW if you get a black screen
            reset_state=st7796.STATE_LOW, # WT32-SC01 PLUS use st7796.STATE_LOW

            backlight_pin=HardwareSetupESP32.BL,
            backlight_on_state=st7796.STATE_PWM,
            color_space=lv.COLOR_FORMAT.RGB565,
            rgb565_byte_swap=True
        )

        display_driver.set_power(True)
        display_driver.init()
        display_driver.set_params(0x34)  #display_driver.send_params(0x34)  # TEOFF
        display_driver.set_color_inversion(True)

        self.display_bus = display_bus
        self.display_driver = display_driver

        touch_bus = i2c.I2C.Bus(
            host=HardwareSetupESP32.TP_HST,
            sda=HardwareSetupESP32.TP_SDA,
            scl=HardwareSetupESP32.TP_SDL,
            freq=HardwareSetupESP32.TP_FREQ
        )

        touch_device = i2c.I2C.Device(
            bus=touch_bus,
            dev_id=ft6x36.I2C_ADDR,
            reg_bits=ft6x36.BITS
        )

        indev = ft6x36.FT6x36(device=touch_device)

        display_driver.set_backlight(100)

        if not indev.is_calibrated:
            indev.calibrate()

        display_driver.set_rotation(lv.DISPLAY_ROTATION._90)

        self.input_device = indev
        self.touch_device = touch_device
        self.touch_bus = touch_bus

        scrn = lv.screen_active()
        lv.refr_now(scrn.get_display())
        task_handler.TaskHandler()


from lv_style import *

class Interface:
    def __init__(self):
        self.scr = lv.screen_active()

        self.init_main_tabs()

        lv.screen_load(self.scr)

    def init_main_tabs(self):
        self.main_tabs = lv.tabview(self.scr)
        tabv = self.main_tabs
        tabv.set_tab_bar_size(40)

        tabv.get_content().remove_flag(lv.obj.FLAG.SCROLLABLE)

        self.tab_jog = TabJog(tabv)
        self.tab_probe = TabProbe(tabv)

        self.tab_machine = tabv.add_tab("Machine")
        self.tab_gcode = tabv.add_tab("GCode")
        self.tab_tool = tabv.add_tab("Tool")
        self.tab_tool = tabv.add_tab("CAM")

class TabProbe:
    PROBE_BTNS = [
        '\\', '|', '/', '\n',
        '->', 'O', '<-', '\n',
        '/', '|', '\\'
    ]
    SETTINGS = [
        ['Width', 50.0, 1.0, 200.0],
        ['Length', 100.0, 1.0, 200.0],
        ['Depth', 2.0, 0.0, 10.0],
        ['Surf Clear', 5.0, 0.0, 20.0],
        ['Corner Clear', 5.0, 0.0, 20.0],
        ['Overtravel', 2.0, 0.0, 10.0]
    ]

    def __init__(self, tabv):
        self.tab = tabv.add_tab("Probe")

        tab = self.tab
        # tab.set_flex_flow(lv.FLEX_FLOW.COLUMN)
        # tab.set_flex_align(lv.FLEX_ALIGN.SPACE_EVENLY, lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER)
        flex_col(tab)

        self.init_probe_tabv(tab)

    def init_probe_tabv(self, parent):
        self.main_tabs = lv.tabview(parent)

        style_pad(parent, 5)

        tabv = self.main_tabs

        self.tab_settings = tabv.add_tab("Setup")
        self.tab_wcs = tabv.add_tab("WCS")
        self.tab_probe = tabv.add_tab("Probe")
        self.tab_surf = tabv.add_tab("Surface")

        # parent.set_style_local_pad_left(lv.OBJ.PART_MAIN, lv.STATE.DEFAULT, 0)

        tabv.get_content().remove_flag(lv.obj.FLAG.SCROLLABLE)
        tabv.set_tab_bar_position(lv.DIR.LEFT)
        tabv.set_tab_bar_size(70)

        self.tab_probe.remove_flag(lv.obj.FLAG.SCROLLABLE)

        self.init_sets_tab(self.tab_settings)
        self.init_probe_tab(self.tab_probe)
        self.init_surface_tab(self.tab_probe)
        self.init_wcs_tab(self.tab_wcs)

    def init_sets_tab(self, tab):
        style_pad(tab, 2)
        grid = lv.obj(tab)
        grid.center()
        grid.set_size(lv.pct(100), lv.pct(100))
        grid.remove_flag(lv.obj.FLAG.SCROLLABLE)

        cols = [lv.pct(1), lv.pct(30), lv.pct(50), lv.GRID_TEMPLATE_LAST]
        rows = [30] * 6 + [lv.GRID_TEMPLATE_LAST]
        # rows = [lv.GRID_CONTENT] * 6 + [lv.GRID_TEMPLATE_LAST]
        grid.set_style_grid_column_dsc_array(cols, 0)
        grid.set_style_grid_row_dsc_array(rows, 0)
        grid.set_layout(lv.LAYOUT.GRID)

        self.sets_grid = grid

        def slider_event_cb(e):
            slider = lv.slider.__cast__(e.get_target())
            textbox = lv.textarea.__cast__(slider.get_user_data())
            value = slider.get_value() / 10.0
            textbox.set_text(f"{value:.1f}")

        for row, (key, default, mini, maxi) in enumerate(TabProbe.SETTINGS):
            label = lv.label(grid)
            label.set_text(key)
            label.set_grid_cell(lv.GRID_ALIGN.STRETCH, 0, 1,
                                lv.GRID_ALIGN.CENTER, row, 1)

            textbox = lv.textarea(grid)
            textbox.set_one_line(True)
            textbox.set_text(str(default))
            textbox.set_grid_cell(lv.GRID_ALIGN.STRETCH, 1, 1,
                                  lv.GRID_ALIGN.CENTER, row, 1)

            slider = lv.slider(grid)
            slider.set_range(int(mini * 10), int(maxi * 10))
            slider.set_value(int(default * 10), lv.ANIM.OFF)
            slider.set_user_data(textbox)
            slider.add_event_cb(slider_event_cb, lv.EVENT.VALUE_CHANGED, None)
            slider.set_grid_cell(lv.GRID_ALIGN.STRETCH, 2, 1,
                                 lv.GRID_ALIGN.CENTER, row, 1)
            style_pad(slider, 5)

    def init_wcs_tab(self, tab):
        flex_col(tab)

        wcsbtns = lv.buttonmatrix(tab)
        wcsbtns.set_height(250)
        wcsbtns.set_map(['G54', 'G55', 'G56', '\n', 'G57', 'G58', 'G59', '\n', 'G59.1', 'G59.2', 'G59.3'])
        wcsbtns.set_one_checked(True)
        wcsbtns.set_button_ctrl_all(lv.buttonmatrix.CTRL.CHECKABLE)
        wcsbtns.set_button_ctrl(0, lv.buttonmatrix.CTRL.CHECKED)
        self.inside_outside_buttons = wcsbtns

        self.wcs_buttons = wcsbtns

    def init_surface_tab(self, tab):
        pass

    def init_probe_tab(self, tab):
        flex_col(tab)

        iobtns = lv.buttonmatrix(tab)
        iobtns.set_height(70)
        iobtns.set_map(['Inside', 'Outside'])
        iobtns.set_one_checked(True)
        iobtns.set_button_ctrl_all(lv.buttonmatrix.CTRL.CHECKABLE)
        iobtns.set_button_ctrl(0, lv.buttonmatrix.CTRL.CHECKED)
        self.inside_outside_buttons = iobtns

        btns = lv.buttonmatrix(tab)
        btns.set_map(TabProbe.PROBE_BTNS)
        self.probe_buttons = btns

class TabJog:
    def __init__(self, tabv):
        self.tabv = tabv
        self.tab = tabv.add_tab("Jog")
        self.jog_dial = JogDial(self.tab)

class JogSlider:
    def __init__(self, parent):
        self.parent = parent

        parent.center()
        parent.set_flex_flow(lv.FLEX_FLOW.COLUMN)
        parent.set_flex_align(lv.FLEX_ALIGN.SPACE_EVENLY, lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER)

        btns = lv.buttonmatrix(parent)

        btns.set_map(['X', 'Y', 'Z'])
        btns.set_button_ctrl_all(lv.buttonmatrix.CTRL.CHECKABLE)
        btns.set_button_ctrl(0, lv.buttonmatrix.CTRL.CHECKED)
        self.axis = 'X'
        btns.set_one_checked(True)

        btns.align(lv.ALIGN.CENTER, 0, 0)

        btns.add_event_cb(self._axis_clicked,
                            lv.EVENT.VALUE_CHANGED,
                            None);

        self.axis_buttons = btns

        # Create a slider in the center of the display
        slider = lv.slider(parent)
        slider.center()
        slider.set_value(0, lv.ANIM.OFF)
        slider.set_range(-100, 100)
        slider.set_mode(lv.slider.MODE.SYMMETRICAL)
        #slider.add_flag(lv.obj.FLAG.ADV_HITTEST)
        slider.add_event_cb(self._slider_event_cb, lv.EVENT.ALL, None)

        self.slider = slider

        # Create a label below the slider
        slider_label = lv.label(parent)
        slider_label.set_text("0 mm/min")

        slider_label.align_to(slider, lv.ALIGN.OUT_BOTTOM_MID, 0, 10)

        self.slider_label = slider_label

    def set_value(self, v):
        self.slider.set_value(v, lv.ANIM.ON)
        self.slider.send_event(lv.EVENT.VALUE_CHANGED, None)

    def _slider_event_cb(self, e):
        print(e.get_code())
        c = e.get_code()
        print(dict((v, k) for k, v in lv.EVENT.__dict__.items())[c])
        if c == lv.EVENT.RELEASED:
            self.slider.set_value(0, lv.ANIM.ON)
            self.slider.send_event(lv.EVENT.VALUE_CHANGED, None)
            c = lv.EVENT.VALUE_CHANGED

        if c == lv.EVENT.VALUE_CHANGED:
            slider = lv.slider.__cast__(e.get_target())
            #print(slider, slider.__class__, slider.get_parent())
            self.slider_label.set_text("{:d}%".format(slider.get_value()))
            self.slider_label.add_flag(lv.obj.FLAG.IGNORE_LAYOUT)
            self.slider_label.align_to(slider, lv.ALIGN.OUT_BOTTOM_MID, 0, 10)

    def _axis_clicked(self, evt):
        tgt = lv.buttonmatrix.__cast__(evt.get_target())
        #print(tgt, tgt.__class__, tgt.get_parent())
        id = tgt.get_selected_button()
        txt = tgt.get_button_text(id)
        self.axis = txt
        self.slider.set_value(0, lv.ANIM.ON)
        self.slider.send_event(lv.EVENT.VALUE_CHANGED, None)

class JogDial:
    FEEDS = [
                ['100%', 1.0],
                ['25%', 0.25],
                ['10%', 0.1],
                ['1%', 0.01]
            ]

    def __init__(self, parent):
        self.prev = 0
        self.parent = parent

        parent.center()
        # parent.set_flex_flow(lv.FLEX_FLOW.COLUMN)
        # parent.set_flex_align(lv.FLEX_ALIGN.SPACE_EVENLY, lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER)
        parent.set_flex_flow(lv.FLEX_FLOW.ROW)
        parent.set_flex_align(lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER)

        btns = lv.buttonmatrix(parent)
        btns.set_map(['X', '\n', 'Y', '\n', 'Z'])
        btns.set_button_ctrl_all(lv.buttonmatrix.CTRL.CHECKABLE)
        btns.set_button_ctrl(0, lv.buttonmatrix.CTRL.CHECKED)
        btns.set_size(60, lv.pct(100))
        btns.set_style_pad_ver(5, lv.PART.MAIN)
        btns.set_style_pad_hor(5, lv.PART.MAIN)
        self.axis = 'X'
        btns.set_one_checked(True)

        btns.align(lv.ALIGN.CENTER, 0, 0)

        btns.add_event_cb(self._axis_clicked,
                            lv.EVENT.VALUE_CHANGED,
                            None);

        # if False:
        #     feedbtns = lv.buttonmatrix(parent)
        #     keys = [f[0] for f in JogDial.FEEDS]
        #     btn_map_ver = button_matrix_ver(keys)
        #     feedbtns.set_map(btn_map_ver)
        #     feedbtns.set_button_ctrl_all(lv.buttonmatrix.CTRL.CHECKABLE)
        #     feedbtns.set_button_ctrl(0, lv.buttonmatrix.CTRL.CHECKED)
        #     feedbtns.set_size(60, lv.pct(100))
        #     feedbtns.set_style_pad_ver(5, lv.PART.MAIN)
        #     feedbtns.set_style_pad_hor(5, lv.PART.MAIN)
        #     self.feed = 1.0
        #     feedbtns.set_one_checked(True)
        #     feedbtns.align(lv.ALIGN.CENTER, 0, 0)
        #     feedbtns.add_event_cb(self._feed_clicked,
        #                         lv.EVENT.VALUE_CHANGED,
        #                         None);
        #     self.feed_buttons = feedbtns

        self.arc = lv.arc(parent)
        arc = self.arc
        arc.set_size(lv.pct(100), lv.pct(100))
        arc.set_rotation(-90)
        arc.set_bg_angles(0, 360)
        arc.set_angles(0, 360)
        arc.set_style_opa(lv.OPA._0, lv.PART.INDICATOR)
        arc.set_value(0)
        arc.center()
        # arc.add_flag(lv.obj.FLAG.FLOATING)
        arc.add_flag(lv.obj.FLAG.ADV_HITTEST)
        arc.set_mode(lv.arc.MODE.SYMMETRICAL)
        arc.set_flex_grow(1)
        style(arc, { 'padding': [10, 0, 0, 40] })
        arc.add_event_cb(self._jog_dial_value_changed_event_cb,
                         lv.EVENT.VALUE_CHANGED,
                         None);

        label = lv.label(parent)
        label.set_size(38, lv.pct(100))
        style(label, { 'margin': 0, 'padding': 0 })
        label.set_text('100%')

        slider = lv.slider(parent)
        slider.set_range(0, 200)
        slider.set_value(100, lv.ANIM.OFF)
        slider.set_size(15, lv.pct(100))
        slider.add_event_cb(self._feed_changed, lv.EVENT.VALUE_CHANGED, None)

        ignore_layout(label)
        label.align_to(slider, lv.ALIGN.OUT_LEFT_MID, -5, 115)

        self.feed_label = label
        self.feed_slider = slider

    def _axis_clicked(self, evt):
        tgt = lv.buttonmatrix.__cast__(evt.get_target())
        #print(tgt, tgt.__class__, tgt.get_parent())
        id = tgt.get_selected_button()
        txt = tgt.get_button_text(id)
        self.axis = txt

    def _feed_clicked(self, evt):
        tgt = lv.buttonmatrix.__cast__(evt.get_target())
        #print(tgt, tgt.__class__, tgt.get_parent())
        id = tgt.get_selected_button()
        self.feed = JogDial.FEEDS[id][1]
        self.feed_label.set_text(str(self.feed) + '%')

    def _feed_changed(self, evt):
        tgt = lv.slider.__cast__(evt.get_target())
        self.feed = tgt.get_value()
        self.feed_label.set_text(str(self.feed) + '%')

    def set_value(self, v):
        self.arc.set_value(v)
        self.arc.send_event(lv.EVENT.VALUE_CHANGED, None)

    def inc(self):
        self.arc.set_value(self.arc.get_value() + 1)

    def dec(self):
        self.arc.set_value(self.arc.get_value() - 1)

    def _jog_dial_value_changed_event_cb(self, evt):
        # c = evt.get_code()
        # print(dict((v, k) for k, v in lv.EVENT.__dict__.items())[c])
        # if c == lv.EVENT.VALUE_CHANGED:

            tgt = lv.arc.__cast__(evt.get_target())
            val = tgt.get_value()
            prev = self.prev
            self.prev = val
            if val > 99 and prev < val:
                self.arc.set_value(0)
            if val < 1 and prev > val:
                self.arc.set_value(99)

######################################
######################################

import sys

platform = sys.platform
hw = None
evt = None

if platform == 'esp32':
    #from rotary_evt import EventLoop
    from encoder import EventLoop

    hw = HardwareSetupESP32()

    evt = EventLoop()
elif platform == 'darwin':
    hw = HardwareSetupMac()
else:
    hw = HardwareSetupSim()

interface = Interface()

if evt:
    def update_v(v):
        print("V: ", v % 100)
        interface.tab_jog.jog_dial.set_value(v % 100)

    evt.run(HardwareSetupESP32.ENC_PX, HardwareSetupESP32.ENC_PY, update_v)

i = 0
while True:
  i += 1
