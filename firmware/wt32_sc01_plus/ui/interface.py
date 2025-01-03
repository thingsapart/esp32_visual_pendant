import lvgl as lv
from lv_style import *

from ui.tab_jog import TabJog
from ui.tab_probe import TabProbe

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

        tab_jog = tabv.add_tab("Jog")
        tab_probe = tabv.add_tab("Probe")
        self.tab_machine = tabv.add_tab("Machine")
        self.tab_job_gcode = tabv.add_tab("Status")
        self.tab_tool = tabv.add_tab("Tools")
        self.tab_tool = tabv.add_tab("CAM")

        self.tab_jog = TabJog(tabv, self, tab_jog)
        self.tab_probe = TabProbe(tabv, self, tab_probe)

    def register_state_change_cb(self, cb):
        self.machine_change_callbacks.append(cb)

    def update_machine_state(self, machine):
        # TODO: upldate global displays.

        for cb in self.machine_change_callbacks:
            cb(machine)

