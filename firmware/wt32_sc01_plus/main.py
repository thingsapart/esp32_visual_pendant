# build command
# python3 esp32 BOARD=ESP32_GENERIC_S3 DISPLAY=st7796 INDEV=ft6x36 --flash-size=16
# LVGL MicroPython 1.23.0 on 2024-12-06
# WT32-SC01 PLUS

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

class Interface:
    def __init__(self):
        self.scr = lv.screen_active()

        self.init_main_tabs()

        lv.screen_load(self.scr)

    def init_probe_tab(self):
        PROBE_BTNS = [
            '\\', '|', '/', '\n',
            '->', '.', '<-', '\n',
            '/', '|', '\\'
        ]
        btns = lv.buttonmatrix(self.tab_probe)
        btns.set_map(PROBE_BTNS)
        btns.align(lv.ALIGN.CENTER, 0, 0)

    def init_main_tabs(self):
        self.main_tabs = lv.tabview(self.scr)
        tabv = self.main_tabs
        tabv.set_tab_bar_size(40)

        tabv.get_content().remove_flag(lv.obj.FLAG.SCROLLABLE)

        self.tab_probe = tabv.add_tab("Probe")
        self.tab_jog = TabJog(tabv)

        self.tab_machine = tabv.add_tab("Machine")
        self.tab_gcode = tabv.add_tab("GCode")
        self.tab_tool = tabv.add_tab("Tool")
        self.tab_tool = tabv.add_tab("CAM")

        self.init_probe_tab()

class TabJog:
    def __init__(self, tabv):
        self.tabv = tabv
        self.tab = tabv.add_tab("Jog")
        self.jog_dial = JogSlider(self.tab)

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
    def __init__(self, parent):
        self.parent = parent
        self.arc = lv.arc(parent)
        arc = self.arc
        arc.set_size(150, 150);
        arc.set_rotation(135);
        arc.set_bg_angles(0, 360);
        arc.set_value(0);
        arc.center()
        arc.add_flag(lv.obj.FLAG.FLOATING)
        arc.add_event_cb(self._jog_dial_value_changed_event_cb,
                         lv.EVENT.VALUE_CHANGED,
                         None);

    def _jog_dial_value_changed_event_cb(self, evt):
        tgt = lv.arc.__cast__(evt.get_target())
        #print(tgt)
        #print(tgt.get_value())

# https://github.com/peterhinch/micropython-async/
class Encoder:
    def __init__(
        self,
        pin_x,
        pin_y,
        v=0,
        div=1,
        vmin=None,
        vmax=None,
        mod=None,
        callback=lambda a, b: None,
        args=(),
        delay=100,
    ):
        self._pin_x = pin_x
        self._pin_y = pin_y
        self._x = pin_x()
        self._y = pin_y()
        self._v = v * div  # Initialise hardware value
        self._cv = v  # Current (divided) value
        self.delay = delay  # Pause (ms) for motion to stop/limit callback frequency
        self._trig = asyncio.Event()

        if ((vmin is not None) and v < vmin) or ((vmax is not None) and v > vmax):
            raise ValueError("Incompatible args: must have vmin <= v <= vmax")
        self._tsf = asyncio.ThreadSafeFlag()
        trig = Pin.IRQ_RISING | Pin.IRQ_FALLING
        try:
            xirq = pin_x.irq(trigger=trig, handler=self._x_cb, hard=True)
            yirq = pin_y.irq(trigger=trig, handler=self._y_cb, hard=True)
        except TypeError:  # hard arg is unsupported on some hosts
            xirq = pin_x.irq(trigger=trig, handler=self._x_cb)
            yirq = pin_y.irq(trigger=trig, handler=self._y_cb)
        asyncio.create_task(self._run(vmin, vmax, div, mod, callback, args))

    # Hardware IRQ's. Duration 36μs on Pyboard 1 ~50μs on ESP32.
    # IRQ latency: 2nd edge may have occured by the time ISR runs, in
    # which case there is no movement.
    def _x_cb(self, pin_x):
        if (x := pin_x()) != self._x:
            self._x = x
            self._v += 1 if x ^ self._pin_y() else -1
            self._tsf.set()

    def _y_cb(self, pin_y):
        if (y := pin_y()) != self._y:
            self._y = y
            self._v -= 1 if y ^ self._pin_x() else -1
            self._tsf.set()

    async def _run(self, vmin, vmax, div, mod, cb, args):
        pv = self._v  # Prior hardware value
        pcv = self._cv  # Prior divided value passed to callback
        lcv = pcv  # Current value after limits applied
        plcv = pcv  # Previous value after limits applied
        delay = self.delay
        while True:
            self._tsf.clear()
            await self._tsf.wait()  # Wait for an edge. A stopped encoder waits here.
            await asyncio.sleep_ms(delay)  # Optional rate limit for callback/trig.
            hv = self._v  # Sample hardware (atomic read).
            if hv == pv:  # A change happened but was negated before
                continue  # this got scheduled. Nothing to do.
            pv = hv
            cv = round(hv / div)  # cv is divided value.
            if not (dv := cv - pcv):  # dv is change in divided value.
                continue  # No change
            lcv += dv  # lcv: divided value with limits/mod applied
            lcv = lcv if vmax is None else min(vmax, lcv)
            lcv = lcv if vmin is None else max(vmin, lcv)
            lcv = lcv if mod is None else lcv % mod
            self._cv = lcv  # update ._cv for .value() before CB.
            if lcv != plcv:
                cb(lcv, lcv - plcv, *args)  # Run user CB in uasyncio context
                self._trig.set()  # Enable async iterator
            pcv = cv
            plcv = lcv

    def __aiter__(self):
        return self

    async def __anext__(self):
        await self._trig.wait()
        self._trig.clear()
        return self._cv

    def value(self):
        return self._cv

class EventLoop:
    def cb(self, pos, delta):
        import asyncio

        print(pos, delta)

    async def main(self):
        while True:
            await asyncio.sleep(1)

    def run(self):
        from machine import Pin
        px = Pin(HardwareSetupESP32._ENC_PX, Pin.IN, Pin.PULL_UP)
        py = Pin(HardwareSetupESP32._ENC_PY, Pin.IN, Pin.PULL_UP)
        enc = Encoder(px, py, v=0, vmin=0, vmax=100, callback=cb)
        try:
            asyncio.run(self.main())
        except KeyboardInterrupt:
            print("Interrupted")
        finally:
            asyncio.new_event_loop()

######################################
######################################

hw = HardwareSetupMac()
interface = Interface()
#evt = EventLoop()

#evt.run()
i = 0
while True:
  i += 1
