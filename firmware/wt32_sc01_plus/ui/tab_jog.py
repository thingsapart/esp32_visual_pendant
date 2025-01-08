import lvgl as lv
from lv_style import *
import ui.modals
from ui.machine_position import MachinePositionWCS, MachinePosition

class TabJog:
    def __init__(self, tabv, interface, tab):
        self.tabv = tabv
        self.interface = interface
        # self.tab = tabv.add_tab("Jog")
        self.tab = tab
        self.jog_dial = JogDial(self.tab, interface)

class JogDial:
    FEEDS = [
                ['100%', 1.0],
                ['25%', 0.25],
                ['10%', 0.1],
                ['1%', 0.01]
            ]

    AXES_OPTIONS = ['X', 'Y', 'Z', 'Off']
    AXES = ['X', 'Y', 'Z']
    AXIS_IDS = {
            'X': 0,
            'Y': 1,
            'Z': 2,
            'Off': None
           }

    def __init__(self, parent, interface):
        self.prev = 0
        self.parent = parent
        self.interface = interface
        self.feed = 1.0
        self.axis = self.AXES_OPTIONS[-1]
        self.axis_id = self.AXIS_IDS[self.axis]
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
        axs_delim = (list(zip(self.AXES_OPTIONS, ['\n'] *
                              (len(self.AXES_OPTIONS) - 1))) +
                     [[self.AXES_OPTIONS[-1]]])

        btns.set_map([btn for sublist in axs_delim for btn in sublist])
        btns.set_button_ctrl_all(lv.buttonmatrix.CTRL.CHECKABLE)
        btns.set_button_ctrl(0, lv.buttonmatrix.CTRL.CHECKED)
        btns.set_size(60, lv.pct(100))
        btns.set_style_pad_ver(5, lv.PART.MAIN)
        btns.set_style_pad_hor(5, lv.PART.MAIN)
        self.axis = self.AXES_OPTIONS[-1]
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


        self.position = MachinePositionWCS(arc, self.AXES, self.interface, digits=6)
        self.position.align_to(arc, lv.ALIGN.CENTER, -25, 0)

        # self.position = MachinePosition(parent, self.AXES, self.interface, digits=6)
        # self.position.container.set_width(235)
        # ignore_layout(self.position.container)
        # self.position.container.align_to(arc, lv.ALIGN.CENTER, 0 // 2, 0)

        self.set_axis_vis(self.AXES_OPTIONS[-1])

    def init_pos_labels_(self):
        self.pos_labels = {}
        for i, l in enumerate(JogDial.AXES):
            if self.AXIS_IDS[l] is not None:
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
        self.axis_id = self.AXIS_IDS[ax]
        for cb in self.axis_change_cb:
            cb(self.axis)

    def set_axis_vis(self, ax):
        self.set_axis(ax)
        i = self.AXES_OPTIONS.index(self.axis)
        self.axis_btns.set_button_ctrl(i, lv.buttonmatrix.CTRL.CHECKED)

    def next_axis(self):
        i = (self.AXES_OPTIONS.index(self.axis) + 1) % len(self.AXES_OPTIONS)
        self.axis_btns.set_button_ctrl(i, lv.buttonmatrix.CTRL.CHECKED)
        self.set_axis(self.AXES_OPTIONS[i])

        return self.axis

    def add_axis_change_db(self, cb):
        self.axis_change_cb.append(cb)

    def connection_list(self):
        pass

    def update_pos_labels(self, vals):
        for i in range(len(vals)):
            self.position.set_coord(i, vals[i])

    def update_pos_labels_(self, vals):
        for i, l in enumerate(['X', 'Y', 'Z']):
            try:
                self.pos_labels[l].set_text(l + ' ' + ('%05.2f' % float(vals[i])))
            except:
                self.pos_labels[l].set_text(l + ' ' + ('%s' % vals[i]))

    def _machine_state_updated(self, machine):
        if machine.is_connected():
            self.update_pos_labels(machine.wcs_position)
        else:
            self.position.coords_undefined()

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
        v = tgt.get_value()
        self.feed = v / 100.0
        self.feed_label.set_text(repr(v) + '%')

    def set_value(self, v):
        self.arc.set_value(v)
        self.arc.send_event(lv.EVENT.VALUE_CHANGED, None)

    def axis_selected(self):
        return self.axis_id is not None

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

class JogSlider:
    AXES = {
            'X': 0,
            'Y': 1,
            'Z': 2,
            'Off': None
           }

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

