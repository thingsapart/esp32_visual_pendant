import lvgl as lv
from lv_style import *
import ui.modals

class TabProbe:
    class ProbeBtnMatrix:
        def __init__(self, parent, settings, desc, interface, quick=True):
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
            self.interface = interface

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
                if not interface.machine.is_homed():
                    ui.modals.home_modal(self.interface)
                    return

                gcode, params, _, descr = desc[irow][icol]

                title = 'Probe ' + descr
                full_gcode = make_gcode(gcode, params)

                used_params = [(k if not isinstance(v, str) else v) for k, v in params.items()]
                parms = [k + ': ' + repr(settings[v]) for k, _, _, _, v in
                         TabProbe.SETTINGS if v in used_params]
                text = 'Probing ' + descr + ' with:\n\n' + '\n'.join(parms) + '\n\nGCODE: ' + full_gcode

                mbox = lv.msgbox(lv.screen_active())

                def mbox_event_cb(e):
                    interface.machine.send_gcode(full_gcode, 0)
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
             'img/arr_w.png', 'right face'],
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

    def __init__(self, tabv, interface, tab):
        # self.tab = tabv.add_tab("Probe")
        self.tab = tab
        self.interface = interface

        tab = self.tab
        # tab.set_flex_flow(lv.FLEX_FLOW.COLUMN)
        # tab.set_flex_align(lv.FLEX_ALIGN.SPACE_EVENLY, lv.FLEX_ALIGN.CENTER, lv.FLEX_ALIGN.CENTER)
        flex_col(tab)
        self.settings = {'W': 1, 'Q': 0}

        self.init_probe_tabv(tab)

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
        tabv.set_tab_bar_size(self.interface.TAB_WIDTH)

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
                                               self.interface,
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
        self.btns_2d_in = TabProbe.ProbeBtnMatrix(tab,
                                                  self.settings,
                                                  TabProbe.PROBE_MODES_2D_IN,
                                                  self.interface)

        tab = tabv.add_tab('Outside -> In')
        flex_col(tab)
        self.btns_2d_out = TabProbe.ProbeBtnMatrix(tab,
                                                   self.settings,
                                                   TabProbe.PROBE_MODES_2D_OUT,
                                                   self.interface)

    def init_probe_tab_3d(self, tab3d):
        style(tab3d, { 'padding': 5, 'margin': 0 })
        container = lv.obj(tab3d)
        style(container, { 'padding': 5, 'margin': 0, 'border_width': 0,
                          'bg_opa': 0, 'bg_color': color('NONE'), 'border_width': 0})
        flex_col(container)
        self.btns_2d = TabProbe.ProbeBtnMatrix(container,
                                               self.settings,
                                               TabProbe.PROBE_MODES_3D,
                                               self.interface)

