import lvgl as lv
from lv_style import *
import fs_driver

from ui.tab_jog import TabJog
from ui.tab_probe import TabProbe
from ui.tab_machine import TabMachine

class Interface:
    def __init__(self, machine):
        self.scr = lv.screen_active()
        self.machine = machine
        self.machine_change_callbacks = []

        self.fs_init()
        self.init_fonts()

        self.wheel_tick = None
        self.wheel_tick_target = None

        self.init_main_tabs()

        lv.screen_load(self.scr)

    def init_fonts(self):
        try:
            import usys as sys
            sys.path.append('') # See: https://github.com/micropython/micropython/issues/6419

            try:
                script_path = __file__[:__file__.rfind('/')] if __file__.find('/') >= 0 else '.'
            except NameError:
                script_path = ''
            self.font_lcd = lv.binfont_create('S:%s/../font/lcd_7_segment.bin' %
                                              script_path)
        except Exception as e:
            print('Failed to load font:', e)
            self.font_lcd = None

    def fs_init(self):
        self.fs_drv = lv.fs_drv_t()
        fs_driver.fs_register(self.fs_drv, 'S')

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

        tab_jog = tabv.add_tab("Jog")
        tab_probe = tabv.add_tab("Probe")
        tab_machine = tabv.add_tab("Machine")
        self.tab_job_gcode = tabv.add_tab("Status")
        self.tab_tool = tabv.add_tab("Tools")
        self.tab_tool = tabv.add_tab("CAM")

        self.tab_jog = TabJog(tabv, self, tab_jog)
        self.tab_probe = TabProbe(tabv, self, tab_probe)
        self.tab_machine = TabMachine(tabv, self, tab_machine)

    def register_state_change_cb(self, cb):
        self.machine_change_callbacks.append(cb)

    def update_machine_state(self, machine):
        # TODO: upldate global displays.

        for cb in self.machine_change_callbacks:
            cb(machine)

