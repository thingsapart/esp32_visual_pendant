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

        display_driver.set_rotation(lv.DISPLAY_ROTATION._270)

        self.input_device = indev
        self.touch_device = touch_device
        self.touch_bus = touch_bus

        # Set frequency to 240Mhz to make the UI snappier -> higher power
        # drain on battery. Default is 160Mhz.
        self.turbo()

        scrn = lv.screen_active()
        lv.refr_now(scrn.get_display())
        task_handler.TaskHandler()

    def turbo(self):
        import machine
        machine.freq(240000000)


from lv_style import *

class Interface:
    def __init__(self, machine):
        self.scr = lv.screen_active()
        self.machine = machine
        self.machine_change_callbacks = []

        self.wheel_tick = None
        self.wheel_tick_target = None

        self.init_main_tabs()

        lv.screen_load(self.scr)

    def process_wheel_tick(self, diff):
        if self.wheel_tick == None:
            return False

        self.wheel_tick(diff)

        return True

    def init_main_tabs(self):
        self.main_tabs = lv.tabview(self.scr)
        tabv = self.main_tabs
        tabv.set_tab_bar_size(40)

        tabv.get_content().remove_flag(lv.obj.FLAG.SCROLLABLE)

        self.tab_jog = TabJog(tabv, self)
        self.tab_probe = TabProbe(tabv, self)

        self.tab_machine = tabv.add_tab("Machine")
        self.tab_gcode = tabv.add_tab("GCode")
        self.tab_tool = tabv.add_tab("Tool")
        self.tab_tool = tabv.add_tab("CAM")

    def register_state_change_cb(self, cb):
        self.machine_change_callbacks.append(cb)

    def update_machine_state(self, machine):
        # TODO: upldate global displays.

        for cb in self.machine_change_callbacks:
            cb(machine)


class TabProbe:
    class ProbeBtnMatrix:
        def __init__(self, parent, settings, desc, quick=True):
            style(parent, { 'width': lv.pct(100), 'height': lv.SIZE_CONTENT,
                             'margin': 0, 'padding': [0, 0, 5, 0] })
            self.container = lv.obj(parent)
            style(self.container, { 'width': lv.pct(100), 'height':
                                   lv.SIZE_CONTENT,
                                   'padding': 2, 'margin': [15, 0, 0, 0] })
            flex_col(self.container)

            self.desc = desc
            self.parent = parent
            self.settings = settings

            def make_param(param, val):
                if isinstance(val, str):
                    return param + repr(settings[val])
                elif val == None:
                    return param + repr(settings[param])
                else:
                    return param + repr(val)

            def make_gcode(gc, params):
                print(gc, params, params.items())
                print([make_param(k ,v) for k, v in params.items()])
                r = [gc]
                r.extend([make_param(k ,v) for k, v in params.items()])
                return ' '.join(r)

            def click_handler_cb(e, irow, icol):
                gcode, params, _, descr = desc[irow][icol]

                title = 'Probe ' + descr
                full_gcode = make_gcode(gcode, params)

                used_params = [(k if not isinstance(v, str) else v) for k, v in params.items()]
                print(used_params)
                print(settings)
                parms = [k + ': ' + repr(settings[v]) for k, _, _, _, v in
                         TabProbe.SETTINGS if v in used_params]
                text = 'Probing ' + descr + ' with:\n\n' + '\n'.join(parms) + '\n\nGCODE: ' + full_gcode

                mbox = lv.msgbox(lv.screen_active())

                def mbox_event_cb(e):
                    # TODO: send gcode.
                    mbox.close()

                mbox.add_title(title)
                mbox.add_text(text)
                # mbox.add_close_button()
                btn = mbox.add_footer_button('Probe')
                btn.add_event_cb(mbox_event_cb, lv.EVENT.CLICKED, None)
                btn = mbox.add_footer_button('Cancel')
                btn.add_event_cb(lambda e: mbox.close(), lv.EVENT.CLICKED, None)
                mbox.set_size(300, lv.SIZE_CONTENT)
                mbox.center()

            for j, row in enumerate(desc):
                row_container = lv.obj(self.container)
                flex_row(row_container)
                row_container.set_size(lv.pct(100), lv.SIZE_CONTENT)
                style(row_container, { 'margin': 0, 'padding': 2,
                                      'border_width': 0 })

                for i, vals in enumerate(row):
                    btn = None
                    if len(vals) == 0:
                        btn = lv.label(row_container)
                        btn.set_text('')
                    else:
                        _, _, imgp, _ = vals
                        img_dsc = load_png(imgp, 32, 32)

                        btn = lv.button(row_container)
                        btn.set_height(38)
                        btn.add_event_cb(lambda e, erow=j, ecol=i: click_handler_cb(e, erow, ecol),
                                         lv.EVENT.CLICKED, None)
                        img = lv.image(btn)
                        img.set_src(img_dsc)
                        img.center()
                        style(btn, {'border_width': 1, 'border_color':
                                   color('TEAL'), 'bg_opa': 0 })


                    #btn.set_size(32, 32)
                    btn.set_flex_grow(1)
                    style(btn, { 'margin': 0, 'padding': 0 })
            if quick:
                qcb = lv.checkbox(parent)
                qcb.set_text('Quick Mode')
                qcb.center()

    PROBE_BTNS_3D = [
        '\\', ' ', '/', '\n',
        ' ', 'O', ' ', '\n',
        '/', ' ', '\\'
    ]
    PROBE_BTNS_2D = [
        '\\', ' ', '/', '\n',
        ' ', 'O', ' ', '\n',
        '/', ' ', '\\'
    ]
    PROBE_BTNS_1D = [
        ' ', lv.SYMBOL.DOWN, ' ', '\n',
        lv.SYMBOL.RIGHT, 'O', lv.SYMBOL.LEFT, '\n',
        ' ', lv.SYMBOL.UP, ' '
    ]

    PROBE_MODES_SURF = [
        [
            [],
            # '\\' => Back
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 3, 'O': None},
             'img/arr_s.png', 'back face'],
            [],
        ],
        [
            # '/' => Left
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 0, 'O': None},
             'img/arr_e.png', 'left face'],
            # 'O' => Z
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 0, 'O': None},
             'img/center_boss.png', 'top surface'],
            # '\\' => Right
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 1, 'O': None},
             'img/arr_nw.png', 'right face'],
        ],
        [
            [],
            # '\\' => Front
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 3, 'O': None},
             'img/arr_n.png', 'front face'],
            # => Reference Surface
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 3, 'O': None},
             'img/ref_sfc.png', 'reference surface'],
        ],
    ]

    PROBE_MODES_3D = [
        [
            # '\\' => Back-Left
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 3, 'O': None},
             'img/arr_se.png', 'back-left vise corner'],
            # '/' => Back-Right
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 2, 'O': None},
             'img/arr_sw.png', 'back-right vise corner'],
        ],
        [
            # '/' => Front-Left
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 0, 'O': None},
             'img/arr_ne.png', 'front-left vise corner'],
            # '\\' => Front-Right
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 1, 'O': None},
             'img/arr_nw.png', 'front-right vise corner'],
        ],
    ]

    PROBE_MODES_2D_OUT = [
        [
            # '\\' => Back-Left
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 3, 'O': None},
             'img/arr_se.png', 'back-left corner'],
            # '/' => Back-Right
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 2, 'O': None},
             'img/arr_sw.png', 'back-right corner'],
        ],
        [
            # '/' => Front-Left
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 1, 'O': None},
             'img/arr_ne.png', 'front-left corner'],
            # '\\' => Front-Right
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 0, 'O': None},
             'img/arr_nw.png', 'front-right corner'],
        ],
        [
            # '[]' => Pocket
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 2, 'O': None},
             'img/pkt_in.png', 'outside rectangle'],
            # 'O' => Boss
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 2, 'O': None},
             'img/ctr1_boss.png', 'outside boss'],
        ]
    ]
    PROBE_MODES_2D_IN = [
        [
            # '\\' => Back-Left
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 3, 'O': None},
             'img/arr_nw.png', 'back-left inside corner'],
            # '/' => Back-Right
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 2, 'O': None},
             'img/arr_ne.png', 'back-right inside corner'],
        ],
        [
            # '/' => Front-Left
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 0, 'O': None},
             'img/arr_sw.png', 'front-left inside corner'],
            # '\\' => Front-Right
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 1, 'O': None},
             'img/arr_se.png', 'front-right inside corner'],
        ],
        [
            # '[]' => Pocket
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 2, 'O': None},
             'img/pkt_out.png', 'inside pocket'],
            # 'O' => Bore
            ['G6520.1', {'Q': None, 'W': None, 'P': 'Z', 'N': 2, 'O': None},
             'img/ctr1_bore.png', 'inside bore'],
        ]
    ]

    SETTINGS = [
        ['Width / Dia', 50.0, 1.0, 200.0, 'H'],
        ['Length', 100.0, 1.0, 200.0, 'I'],
        ['Depth', 2.0, 0.0, 10.0, 'Z'],
        ['Surf Clear', 5.0, 0.0, 20.0, 'T'],
        ['Corner Clear', 5.0, 0.0, 20.0, 'C'],
        ['Overtravel', 2.0, 0.0, 10.0, 'O'],
        ['WCS', None, None, None, 'W'],
        ['Quick Mode', None, None, None, 'Q'],
    ]

    def __init__(self, tabv, interface):
        self.tab = tabv.add_tab("Probe")
        self.interface = interface

        tab = self.tab
        # tab.set_flex_flow(lv.FLEX_FLOW.COLUMN)
        # tab.set_flex_align(lv.FLEX_ALIGN.SPACE_EVENLY, lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER)
        flex_col(tab)
        self.settings = {'W': 1, 'Q': 0}

        self.init_probe_tabv(tab)

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

    def init_probe_tabv(self, parent):
        self.main_tabs = lv.tabview(parent)

        style_pad(parent, 5)

        tabv = self.main_tabs

        self.tab_settings = tabv.add_tab("Setup")
        self.tab_wcs = tabv.add_tab("WCS")
        self.tab_probe = tabv.add_tab("3-axis")
        self.tab_probe_2d = tabv.add_tab("2-axis")
        self.tab_surf = tabv.add_tab("Surface")

        # parent.set_style_local_pad_left(lv.OBJ.PART_MAIN, lv.STATE.DEFAULT, 0)

        tabv.get_content().remove_flag(lv.obj.FLAG.SCROLLABLE)
        tabv.set_tab_bar_position(lv.DIR.LEFT)
        tabv.set_tab_bar_size(70)

        self.tab_probe.remove_flag(lv.obj.FLAG.SCROLLABLE)

        self.init_sets_tab(self.tab_settings)
        self.init_probe_tab_2d(self.tab_probe_2d)
        self.init_probe_tab_3d(self.tab_probe)
        self.init_surface_tab(self.tab_surf)
        self.init_wcs_tab(self.tab_wcs)

    def init_sets_tab(self, tab):
        style_pad(tab, 2)
        grid = lv.obj(tab)
        grid.center()
        grid.set_size(lv.pct(100), lv.pct(100))
        grid.remove_flag(lv.obj.FLAG.SCROLLABLE)

        cols = [100, 60, lv.grid_fr(1), lv.GRID_TEMPLATE_LAST]
        rows = [31] * 6 + [lv.GRID_TEMPLATE_LAST]
        # rows = [lv.GRID_CONTENT] * 6 + [lv.GRID_TEMPLATE_LAST]
        grid.set_style_grid_column_dsc_array(cols, 0)
        grid.set_style_grid_row_dsc_array(rows, 0)
        grid.set_layout(lv.LAYOUT.GRID)
        style(grid, { 'bg_opa': 0, 'border_width': 0, 'outline_width': 0 })

        self.sets_grid = grid

        def slider_event_cb(e, i):
            slider = lv.slider.__cast__(e.get_target())
            textbox = lv.textarea.__cast__(slider.get_user_data())
            value = slider.get_value() / 10.0
            textbox.set_text(f"{value:.1f}")
            self.settings[TabProbe.SETTINGS[i][-1]] = value

        def label_reset_event_cb(e):
            label = lv.label.__cast__(e.get_target())
            txt = label.get_text()
            setting = next(sets for sets in TabProbe.SETTINGS if sets[0] == txt)
            slider = lv.slider.__cast__(label.get_user_data())
            slider.set_value(int(setting[1] * 10), lv.ANIM.OFF)
            slider.send_event(lv.EVENT.VALUE_CHANGED, None)

        def text_area_inc(diff):
            slider = self.interface.wheel_tick_target
            print('slider', slider)
            v = slider.get_value()
            print('v', v)
            vv = v + diff
            print('vv', vv)
            slider.set_value(vv, lv.ANIM.OFF)
            slider.send_event(lv.EVENT.VALUE_CHANGED, None)

        def focused(slider, i):
            self.interface.wheel_tick_target = slider
            self.interface.wheel_tick = text_area_inc

        def text_area_focused(e, i):
            text = lv.textarea.__cast__(e.get_target())
            # slider = TabProbe.SETTINGS[i][-1]
            slider = lv.slider.__cast__(text.get_user_data())
            focused(slider, i)

        def text_area_defocused(e):
            self.interface.wheel_tick_target = None
            self.interface.wheel_tick = None

        for row, (key, default, mini, maxi, param) in enumerate(TabProbe.SETTINGS):
            if default is not None:
                label = lv.label(grid)
                label.set_text(key)
                label.set_grid_cell(lv.GRID_ALIGN.STRETCH, 0, 1,
                                    lv.GRID_ALIGN.CENTER, row, 1)
                label.add_flag(lv.obj.FLAG.CLICKABLE)

                textbox = lv.textarea(grid)
                textbox.set_one_line(True)
                textbox.set_text(str(default))
                textbox.set_grid_cell(lv.GRID_ALIGN.STRETCH, 1, 1,
                                      lv.GRID_ALIGN.CENTER, row, 1)
                textbox.add_event_cb(lambda e, i=row: text_area_focused(e, i),
                                     lv.EVENT.FOCUSED, None)
                textbox.add_event_cb(text_area_defocused, lv.EVENT.DEFOCUSED, None)

                slider = lv.slider(grid)
                slider.set_range(int(mini * 10), int(maxi * 10))
                slider.set_value(int(default * 10), lv.ANIM.OFF)
                slider.set_user_data(textbox)
                slider.add_event_cb(lambda e, i=row: slider_event_cb(e, i), lv.EVENT.VALUE_CHANGED, None)
                slider.set_grid_cell(lv.GRID_ALIGN.STRETCH, 2, 1,
                                     lv.GRID_ALIGN.CENTER, row, 1)
                slider.add_event_cb(lambda e, i=row: focused(lv.slider.__cast__(e.get_target()), i),
                                     lv.EVENT.FOCUSED, None)
                slider.add_event_cb(text_area_defocused, lv.EVENT.DEFOCUSED, None)
                style_pad(slider, 5)

                # TabProbe.SETTINGS[row].append(slider)
                label.set_user_data(slider)
                label.add_event_cb(label_reset_event_cb, lv.EVENT.CLICKED, None)

                textbox.set_user_data(slider)

                self.settings[param] = default

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
        style(tab, { 'padding': 5, 'margin': 0 })
        container = lv.obj(tab)
        style(container, { 'padding': 5, 'margin': 0, 'border_width': 0,
                          'bg_opa': 0, 'bg_color': color('NONE'), 'border_width': 0})
        flex_col(container)
        self.btns_2d = TabProbe.ProbeBtnMatrix(container,
                                               self.settings,
                                               TabProbe.PROBE_MODES_SURF,
                                               quick=False)

    def init_surface_tab_(self, tab):
        flex_col(tab)

        btns = lv.buttonmatrix(tab)
        btns.set_map(TabProbe.PROBE_BTNS_1D)
        self.probe_buttons_surf = btns

    def init_probe_tab_2d_(self, tab):
        flex_col(tab)

        iobtns = lv.buttonmatrix(tab)
        iobtns.set_height(70)
        iobtns.set_map(['Inside', 'Outside'])
        iobtns.set_one_checked(True)
        iobtns.set_button_ctrl_all(lv.buttonmatrix.CTRL.CHECKABLE)
        iobtns.set_button_ctrl(0, lv.buttonmatrix.CTRL.CHECKED)

        btns = lv.buttonmatrix(tab)
        btns.set_map(TabProbe.PROBE_BTNS_2D)
        self.probe_buttons_2d = btns

    def init_probe_tab_2d(self, tab2d):
        style(tab2d, { 'padding': 5, 'margin': 0 })
        tabv = lv.tabview(tab2d)
        tabv.set_tab_bar_size(40)

        tab = tabv.add_tab('Inside -> Out')
        flex_col(tab)
        self.btns_2d_in = TabProbe.ProbeBtnMatrix(tab, self.settings, TabProbe.PROBE_MODES_2D_IN)

        tab = tabv.add_tab('Outside -> In')
        flex_col(tab)
        self.btns_2d_out = TabProbe.ProbeBtnMatrix(tab, self.settings, TabProbe.PROBE_MODES_2D_OUT)

    def init_probe_tab_3d(self, tab3d):
        style(tab3d, { 'padding': 5, 'margin': 0 })
        container = lv.obj(tab3d)
        style(container, { 'padding': 5, 'margin': 0, 'border_width': 0,
                          'bg_opa': 0, 'bg_color': color('NONE'), 'border_width': 0})
        flex_col(container)
        self.btns_2d = TabProbe.ProbeBtnMatrix(container, self.settings, TabProbe.PROBE_MODES_3D)

class TabJog:
    def __init__(self, tabv, interface):
        self.tabv = tabv
        self.interface = interface
        self.tab = tabv.add_tab("Jog")
        self.jog_dial = JogDial(self.tab, interface)

class JogSlider:
    AXES = ['X', 'Y', 'Z']

    def __init__(self, parent, interface):
        self.parent = parent
        self.interface = interface

        parent.center()
        parent.set_flex_flow(lv.FLEX_FLOW.COLUMN)
        parent.set_flex_align(lv.FLEX_ALIGN.SPACE_EVENLY, lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER)

        btns = lv.buttonmatrix(parent)

        btns.set_map(JogSlider.AXES)
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

    AXES = ['X', 'Y', 'Z']

    def __init__(self, parent, interface):
        self.prev = 0
        self.parent = parent
        self.interface = interface
        self.feed = 1.0
        self.axis = self.AXES[0]
        self.last_rotary_pos = 0
        self.axis_change_cb = []

        interface.register_state_change_cb(self._machine_state_updated)

        parent.center()
        # parent.set_flex_flow(lv.FLEX_FLOW.COLUMN)
        # parent.set_flex_align(lv.FLEX_ALIGN.SPACE_EVENLY, lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER)
        parent.set_flex_flow(lv.FLEX_FLOW.ROW)
        parent.set_flex_align(lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER)
        parent.remove_flag(lv.obj.FLAG.SCROLLABLE)

        btns = lv.buttonmatrix(parent)
        btns.set_map(['X', '\n', 'Y', '\n', 'Z'])
        btns.set_button_ctrl_all(lv.buttonmatrix.CTRL.CHECKABLE)
        btns.set_button_ctrl(0, lv.buttonmatrix.CTRL.CHECKED)
        btns.set_size(60, lv.pct(100))
        btns.set_style_pad_ver(5, lv.PART.MAIN)
        btns.set_style_pad_hor(5, lv.PART.MAIN)
        self.axis = self.AXES[0]
        btns.set_one_checked(True)

        btns.align(lv.ALIGN.CENTER, 0, 0)

        btns.add_event_cb(self._axis_clicked,
                            lv.EVENT.VALUE_CHANGED,
                            None);
        self.axis_btns = btns

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

        parent.update_layout()
        ignore_layout(label)
        label.align_to(slider, lv.ALIGN.OUT_LEFT_MID, -5, 115)

        self.feed_label = label
        self.feed_slider = slider

        self.pos_labels = {}
        for i, l in enumerate(JogDial.AXES):
            pos_label = lv.label(parent)
            pos_label.set_size(75, 20)
            style(pos_label, { 'margin': 0, 'padding': 0 })
            ignore_layout(pos_label)
            parent.update_layout()
            pos_label.align_to(arc, lv.ALIGN.CENTER, -95 + i * 70, 0)
            self.pos_labels[l] = pos_label
        self.update_pos_labels([0, 0, 0])

    def current_axis(self):
        return self.axis

    def set_axis(self, ax):
        self.axis = ax
        for cb in self.axis_change_cb:
            cb(self.axis)

    def next_axis(self):
        i = (self.AXES.index(self.axis) + 1) % len(self.AXES)
        self.axis_btns.set_button_ctrl(i, lv.buttonmatrix.CTRL.CHECKED)
        self.set_axis(self.AXES[i])

        return self.axis

    def add_axis_change_db(self, cb):
        self.axis_change_cb.append(cb)

    def update_pos_labels(self, vals):
        for i, l in enumerate(['X', 'Y', 'Z']):
            try:
                self.pos_labels[l].set_text(l + ' ' + ('%7.2f' % float(vals[i])))
            except:
                self.pos_labels[l].set_text(l + ' ' + ('%s' % vals[i]))

    def _machine_state_updated(self, machine):
        self.update_pos_labels(machine.wcs_position)

    def _axis_clicked(self, evt):
        tgt = lv.buttonmatrix.__cast__(evt.get_target())
        #print(tgt, tgt.__class__, tgt.get_parent())
        id = tgt.get_selected_button()
        txt = tgt.get_button_text(id)
        self.set_axis(txt)

    def _feed_clicked(self, evt):
        tgt = lv.buttonmatrix.__cast__(evt.get_target())
        #print(tgt, tgt.__class__, tgt.get_parent())
        id = tgt.get_selected_button()
        self.feed = JogDial.FEEDS[id][1]
        self.feed_label.set_text(str(self.feed) + '%')

    def _feed_changed(self, evt):
        tgt = lv.slider.__cast__(evt.get_target())
        self.feed = tgt.get_value() / 100.0
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

            # diff = val - self.prev
            # self.interface.machine.move(self.axis, self.feed, diff)

######################################
######################################

import sys
from rrf_machine import MachineRRF

platform = sys.platform
hw = None
mach = None
evt = None

#register_png()

if platform == 'esp32':
    from encoder import EventLoop
    evt = EventLoop()
    hw = HardwareSetupESP32()
elif platform == 'darwin':
    hw = HardwareSetupMac()
else:
    hw = HardwareSetupSim()

mach = MachineRRF()
interface = Interface(mach)

def machine_update_cb(machine):
    interface.update_machine_state(mach)

mach.set_state_change_callback(machine_update_cb)

if evt:
    import uasyncio

    #@micropython.native
    def update_v(vv):
        v = vv // 4
        jog = interface.tab_jog.jog_dial
        if v != jog.last_rotary_pos:
            diff = v - jog.last_rotary_pos
            jog.last_rotary_pos = v

            # print("V: ", v, mach.position)
            if not interface.process_wheel_tick(diff):
                vv = interface.tab_jog.jog_dial.arc.get_value() + diff
                interface.tab_jog.jog_dial.set_value(vv % 100)
                mach.move(jog.axis, jog.feed * 1000, diff)
                # print(mach.debug_print())

    print("EVT Loops:")

    try:
        evt.run(HardwareSetupESP32.ENC_PY, HardwareSetupESP32.ENC_PX, update_v)
        mach.setup_loop()
    except KeyboardInterrupt:
        print("Interrupted")
    finally:
        uasyncio.Loop.run_forever()

i = 0

while True:
    i += 1
