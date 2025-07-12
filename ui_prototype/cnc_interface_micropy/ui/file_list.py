import lvgl as lv
from lv_style import *

class file_list(lv.list):
    def __init__(self, parent, path, interface, on_file_click):
        super().__init__(parent)
        self.set_user_data(self)
        self.path = path

        self.interface = interface
        self.machine = interface.machine

        self.machine.add_connected_cb(self._connected)
        self.machine.add_files_changed_cb(self._updated_files, path)

        self.on_file_click = on_file_click

        self.init_emtpy()

    def init_refresh_float_btn(self):
        self.float_btn = lv.button(self.tab)
        self.float_btn.set_size(45, 45)
        self.float_btn.add_flag(lv.obj.FLAG.FLOATING)
        self.float_btn.align(lv.ALIGN.BOTTOM_RIGHT, 0, 0)
        label = lv.label(self.float_btn)
        label.set_text(lv.SYMBOL.REFRESH)
        label.center()
        self.float_btn.add_event_cb(self.refresh, lv.EVENT.CLICKED, None)
        style(self.float_btn, { 'radius': lv.RADIUS_CIRCLE })

    def _path_capitalize(self):
        s = self.path
        return s[0].upper() + s[1:]

    def _connected(self, machine):
        if machine.is_connected():
            self.refresh()

    def refresh_(self):
        self.machine.list_files(self.path)

    def refresh(self):
        files = self.machine.list_files(self.path)
        # self.show_files(files)

    def _btn_click(self, e):
        if self.on_file_click is not None:
            self.on_file_click(self.get_button_text(e.get_target_obj()))

    def init_emtpy(self):
        self.clean()

        lbl = self.add_text(self._path_capitalize())
        lbl.set_height(20)

        btn = self.add_button(lv.SYMBOL.REFRESH, "Waiting for connection...")
        btn.add_event_cb(lambda e: self.refresh(), lv.EVENT.CLICKED, None)

    def show_files(self, files):
        self.clean()

        lbl = self.add_text(self._path_capitalize())
        lbl.set_height(20)

        files.sort()
        for file in files:
            btn = self.add_button(lv.SYMBOL.FILE, file)
            btn.add_event_cb(self._btn_click, lv.EVENT.CLICKED, None)

    def _updated_files(self, machine, path, files):
        print('UPDATED')
        self.show_files(files)
