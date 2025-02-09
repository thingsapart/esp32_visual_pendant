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

import settings

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


######################################
######################################

import sys
from rrf_machine import MachineRRF
from ui.interface import Interface
import ui.modals

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
    import time

    # Used to keep track of the last time a wheel click was captured for cont mode.
    last_tick = time.ticks_ms()
    last_dir = 0

    #@micropython.native
    def update_v(vv):
        global last_tick, last_dir

        v = vv // 4
        jog = interface.tab_jog.jog_dial
        if v != jog.last_rotary_pos:
            diff = v - jog.last_rotary_pos
            jog.last_rotary_pos = v

            # print("V: ", v, mach.position)
            if not interface.process_wheel_tick(diff) and jog.axis_selected():
                if not interface.machine.is_homed():
                    # If there's a modal already, don't show.
                    if not ui.modals.modal_active(): ui.modals.home_modal(interface)
                    return
                else:
                    lt = last_tick
                    last_tick = time.ticks_ms()
                    tick_diff = time.ticks_diff(last_tick, lt)

                    # Cheap attempt at a debounce
                    ldir = -1 if diff < 0 else 1
                    if abs(diff) == 1 and ldir != last_dir and tick_diff < settings.CONTINOUS_TICKS_DIFF_MAX_MS:
                        last_dir = ldir
                        return
                    last_dir = ldir

                    # Check for continuous mode => more than 10 ticks/second.
                    print('tick_diff', tick_diff, settings.CONTINOUS_TICKS_DIFF_MAX_MS)
                    if tick_diff < settings.CONTINOUS_TICKS_DIFF_MAX_MS:
                        mach.move_continuous(jog.axis,
                                             jog.feed * settings.CONTINUOUS_FEED_MM_PER_S,
                                             1 if diff >= 0 else 0)
                    else:
                        # Single step.
                        job = interface.tab_jog.jog_dial
                        vv = jog.arc.get_value() + diff
                        jog.set_value(vv % 100)
                        mach.move(jog.axis, jog.feed * 1000, diff)
                        mach.move_continuous_stop()
                        # print(mach.debug_print())

    print("EVT Loops:")

    try:
        evt.run(HardwareSetupESP32.ENC_PY, HardwareSetupESP32.ENC_PX, update_v)
        mach.setup_loop()
    except KeyboardInterrupt:
        print("Interrupted")
    finally:
        uasyncio.Loop.run_forever()
else:
    i = 0

    while True:
        i += 1
        if i % 10000000 == 0: (print('.'), interface.machine.task_loop_iter())
