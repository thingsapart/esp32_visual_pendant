import lvgl as lv
import math
import lv_style

class HandwheelSlider(lv.slider):
    def __init__(self, parent, interface, step_size=100, ticks=50, label=True):
        super().__init__(parent)

        self.focus_cb = []
        self.defocus_cb = []
        self.val_cb = []
        self.interface = interface
        self.step_size = step_size
        self.ticks = ticks

        if label:
            self.label = lv.label(self)
            lv_style.ignore_layout(self.label)
            super().add_event_cb(self.value_changed, lv.EVENT.VALUE_CHANGED, None)
            self.label.set_style_text_color(lv.color_make(150, 150, 150),
                                            lv.STATE.DEFAULT)
            self.label.set_width(lv.SIZE_CONTENT)
            self.value_changed(None)

            style_knob = lv.style_t()
            style_knob.init()
            style_knob.set_bg_opa(lv.OPA.COVER)
            style_knob.set_bg_color(lv.palette_main(lv.PALETTE.YELLOW))
            style_knob.set_border_color(lv.palette_darken(lv.PALETTE.YELLOW, 3))
            style_knob.set_border_width(2)
            style_knob.set_radius(lv.RADIUS_CIRCLE)
            style_knob.set_pad_all(6)                   # Makes the knob larger

            self.add_style(style_knob, lv.PART.KNOB | lv.STATE.FOCUSED)


        super().add_event_cb(self.focused, lv.EVENT.FOCUSED, None)
        super().add_event_cb(self.defocused, lv.EVENT.DEFOCUSED, None)

    def set_value(self, v, anim):
        super().set_value(v, anim)
        self.value_changed(None)

    def value_changed_(self, e):
        if self.label is not None:
            label = self.label
            label.set_text(str(self.get_value()))
            df = lv.STATE.DEFAULT
            pad = self.get_style_pad_left(df) + self.get_style_pad_right(df)
            margin = self.get_style_margin_left(df) + self.get_style_margin_right(df)
            w = self.get_width() - label.get_width() - 1 # - pad - margin
            mi = self.get_min_value()
            ma = self.get_max_value()
            v = self.get_value()
            rr = ma - mi
            r = round((v - rr / 2) / rr * w)
            label.align(lv.ALIGN.CENTER, r, 0)
            label.move_foreground()
            print(v, mi, ma, r, w)
        for cb in self.val_cb:
            cb(e)

    def value_changed(self, e):
        if self.label is not None:
            label = self.label
            label.set_text(str(self.get_value()))
            label.align(lv.ALIGN.CENTER, 0, 0)
            label.move_foreground()
        for cb in self.val_cb:
            cb(e)

    def focused(self, e):
        self.interface.wheel_tick_target = self
        self.interface.wheel_tick = self.tick
        for cb in self.focus_cb:
            cb(e)

    def defocused(self, e):
        self.interface.wheel_tick_target = None
        self.interface.wheel_tick = None
        for cb in self.focus_cb:
            cb(e)

    def tick(self, v):
        mi = self.get_min_value()
        ma = self.get_max_value()
        # Divide into about 100 equal steps rounded to the next 100?
        inc = math.ceil((ma - mi) / self.ticks / self.step_size) * self.step_size
        self.set_value(self.get_value() + v * inc, lv.ANIM.OFF)
        self.send_event(lv.EVENT.VALUE_CHANGED, None)

    def add_event_cb(self, cb, evt, userdata):
        if evt == lv.EVENT.FOCUSED:
            self.focus_cb.append(cb)
        elif evt == lv.EVENT.DEFOCUSED:
            self.defocus_cb.append(cb)
        elif evt == lv.EVENT.VALUE_CHANGED:
            self.val_cb.append(cb)
        else:
            super().add_event_cb(cb, evt, userdata)


