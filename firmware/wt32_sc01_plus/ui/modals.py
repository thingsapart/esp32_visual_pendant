import lvgl as lv

import ui.dialogs

active_modal = False

def modal_active():
    return active_modal

def close_modal(m):
    m.close_async()

    global active_modal
    active_modal = False

def home_modal(interface):
    global active_modal
    title = 'Machine not homed'
    text = '\nHome machine now?\n'
    btns = ['Home All', 'Cancel', interface.machine.home_all()]
    cbs = [lambda m: (close_modal(mbox), interface.machine.home_all()),
           lambda m: (close_modal(mbox))]

    mbox = ui.dialogs.button_dialog(title, text, False, btns, cbs)
    active_modal = True
    return mbox

def home_modal_(interface):
    global active_modal
    mbox = lv.msgbox(lv.screen_active())
    active_modal = mbox

    mbox.add_title('Machine not home')
    mbox.add_text('\nHome machine now?\n')
    btn = mbox.add_footer_button('Home All')
    btn.add_event_cb(lambda e: (interface.machine.home_all(), close_modal(mbox)),
                     lv.EVENT.CLICKED, None)
    btn = mbox.add_footer_button('Cancel')
    btn.add_event_cb(lambda e: close_modal(mbox), lv.EVENT.CLICKED, None)
    mbox.center()

    return mbox

