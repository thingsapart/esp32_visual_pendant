import lvgl as lv

def button_dialog(title, text, add_close_btn, btns, btn_cbs):
        mbox = lv.msgbox(lv.screen_active())
        mbox.add_title(title)
        mbox.add_text(text)
        if add_close_btn: mbox.add_close_button()

        def event_cb(btn_text):
            idx = btns.index(btn_text)
            cb = btn_cbs[idx]
            if cb is None:
                mbox.close()
            else:
                mbox.close()
                cb(mbox)

        print('MBOX')
        for btn in btns:
            lbtn = mbox.add_footer_button(btn)
            lbtn.add_event_cb(lambda e, s=btn: event_cb(s), lv.EVENT.CLICKED, None)
        mbox.center()

        return mbox
