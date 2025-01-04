import lvgl as lv

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
        pass

    def init_tab_jobs(self, tab):
        pass

    def init_tab_macros(self, tab):
        pass

