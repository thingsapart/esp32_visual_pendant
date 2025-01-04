import lvgl as lv

active_modal = None

def modal_active():
    return active_modal is not None

def close_modal(m):
    global active_modal
    m.close()
    active_modal = None

def home_modal(interface):
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

