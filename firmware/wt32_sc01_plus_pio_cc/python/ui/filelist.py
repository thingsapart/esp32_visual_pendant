import lvgl as lv

class FileList:
    def __init__(self, parent, list_fn, interface):
        self.path = path
        self.interface = interface

        self.list = lv.obj(parent)
        self.list_fn = list_fn

        refresh()

    def refresh(self):
        res = self.list_fn()

