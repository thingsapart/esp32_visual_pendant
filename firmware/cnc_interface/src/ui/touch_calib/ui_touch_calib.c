#include <stdlib.h>

#include "Arduino.h"

#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef ESP32_HW
#include "esp_system.h"
#endif

#include "ui/touch_calib/ui_touch_calib.h"
#include "ui/touch_calib/touch_calib.h"
#include "debug.h"

static volatile lv_point_t s_last_click = {0,0};
static float s_H[9];
static volatile bool s_recalibrate = false;
static int s_stage = 0; /* 0..4 stages for captures */
/* UI objects for state machine */
static lv_obj_t *g_cont = NULL;
static lv_obj_t *g_targets[4] = {NULL, NULL, NULL, NULL};
static lv_point_t g_targets_pos[4];
static lv_point_t g_src_points[4];
static lv_obj_t *g_btn_save = NULL;
static lv_obj_t *g_btn_recal = NULL;
static lv_obj_t *g_btn_test = NULL;
static const char *TAG = "touch_calib_ui";

static volatile bool s_test_clicked = false;
static lv_point_t s_test_point = {0,0};

/* Non-blocking test-mode state */
static lv_obj_t *s_test_scr = NULL;
static lv_obj_t *s_test_circle = NULL;
static lv_obj_t *s_test_info = NULL;
/* no timers needed: immediate event-driven show-next behaviour */
static volatile bool s_test_running = false;
static lv_obj_t *s_test_layer = NULL; /* overlay so buttons remain visible */
static lv_obj_t *s_instr = NULL; /* reference to instruction label */
static lv_obj_t *s_test_btn_label = NULL; /* reference to test button label */
static lv_obj_t *s_test_btn = NULL; /* reference to test button for toggling */
static lv_obj_t *s_start_label = NULL; /* reference to startup label to hide it */

/* Forward declarations for functions used before definition */
static lv_obj_t *draw_report_cross(lv_obj_t *parent, lv_point_t pt);
static void test_show_next_target(void);

void ui_setup();


void style_container(lv_obj_t* cnt) {
    //lv_obj_remove_style_all(cnt);
    lv_obj_set_style_pad_all(cnt, 0, 0);
    lv_obj_set_style_margin_all(cnt, 0, 0);
    lv_obj_set_style_border_width(cnt, 0, 0);
    lv_obj_set_style_outline_width(cnt, 0, 0);
    lv_obj_set_style_shadow_opa(cnt, 0, 0);
}

/* Click handler for the full-screen calibration container. */
static void container_click_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    LOGI(TAG, "container_click_cb: stage=%d x=%d y=%d", s_stage, p.x, p.y);
    s_last_click = p;

    /* Hide the startup label on first click */
    if (s_start_label && s_stage == 0) {
        lv_obj_del(s_start_label);
        s_start_label = NULL;
    }

    if (s_stage < 4) {
        /* store raw point for this stage */
        g_src_points[s_stage] = p;
        /* hide current target */
        if (g_targets[s_stage]) lv_obj_add_flag(g_targets[s_stage], LV_OBJ_FLAG_HIDDEN);
        s_stage++;
        if (s_stage < 4) {
            /* show next target */
            if (g_targets[s_stage]) lv_obj_clear_flag(g_targets[s_stage], LV_OBJ_FLAG_HIDDEN);
        } else {
            /* finished: compute homography and show buttons */
            float src[4][2];
            float dst[4][2];
            for (int i = 0; i < 4; ++i) {
                src[i][0] = (float)g_src_points[i].x;
                src[i][1] = (float)g_src_points[i].y;
                dst[i][0] = (float)g_targets_pos[i].x;
                dst[i][1] = (float)g_targets_pos[i].y;
            }
            float H[9];
            if (touch_calib_compute_homography(src, dst, H)) {
                for (int i=0;i<9;i++) s_H[i] = H[i];
                LOGI(TAG, "Computed homography and enabled Save/Test");
                if (g_btn_save) lv_obj_clear_flag(g_btn_save, LV_OBJ_FLAG_HIDDEN);
                if (g_btn_test) lv_obj_clear_flag(g_btn_test, LV_OBJ_FLAG_HIDDEN);
                if (g_btn_recal) lv_obj_clear_flag(g_btn_recal, LV_OBJ_FLAG_HIDDEN);

                // Apply but don't save yet.
                touch_calib_apply(s_H);
            } else {
                LOGW(TAG, "Failed to compute homography");
            }
        }
    }
}


static void test_click_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (!s_test_running) return;
    
    lv_point_t p;
    lv_indev_get_point(lv_indev_get_act(), &p);
    LOGI(TAG, "test_click_cb: x=%d y=%d", p.x, p.y);

    draw_report_cross(lv_event_get_target(e), p);
    
    test_show_next_target();
}

/* Draw a hollow blue circle (diameter ~16) centered at pt */
static lv_obj_t *draw_test_circle(lv_obj_t *parent, lv_point_t pt) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, 16, 16);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(0x22FF44), 0);
    lv_obj_set_style_border_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(c, LV_ALIGN_TOP_LEFT, pt.x - 8, pt.y - 8);
    return c;
}

/* Draw a white crosshair centered at pt */
static lv_obj_t *draw_report_cross(lv_obj_t *parent, lv_point_t pt) {
    const lv_coord_t half = 8;
    lv_obj_t *cnt = lv_obj_create(parent);
    style_container(cnt);
    lv_obj_set_size(cnt, half * 2 + 1, half * 2 + 1);
    lv_obj_set_style_bg_color(cnt, lv_color_hex(0x555555), 0);
    lv_obj_set_style_bg_opa(cnt, 0, 50);
    lv_obj_set_style_radius(cnt, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(cnt, LV_ALIGN_TOP_LEFT, pt.x - half, pt.y - half);


    /* White cross for calibration step targets */
    lv_obj_t *h = lv_obj_create(cnt);
    style_container(h);
    lv_obj_set_size(h, half * 2 + 1, 1);
    lv_obj_clear_flag(h, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(h, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(h, 0, 0);
    lv_obj_align(h, LV_ALIGN_TOP_LEFT, 0, half);
    
    
    lv_obj_t *v = lv_obj_create(cnt);
    style_container(v);
    lv_obj_set_size(v, 1, half * 2 + 1);
    lv_obj_clear_flag(v, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(v, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(v, 0, 0);
    lv_obj_align(v, LV_ALIGN_TOP_LEFT, half, 0);

    return cnt;
}

/* Forward declarations for file-scope callbacks */
static void btn_save_cb(lv_event_t *e);
static void btn_recal_cb(lv_event_t *e);
static void btn_test_cb(lv_event_t *e);

static void btn_save_cb(lv_event_t *e) {
    (void)e;
    #ifdef ESP32_HW
        touch_calib_save(s_H);
        lv_label_set_text(lv_label_create(lv_scr_act()), "Saved. Rebooting...");
        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(200));
        
        esp_restart();
    #else
        touch_calib_save(s_H);
        lv_label_set_text(lv_label_create(lv_scr_act()), "Saved. Please reset device...");
        lv_task_handler();
        while (true) { vTaskDelay(pdMS_TO_TICKS(200)); }
    #endif
}

static void btn_recal_cb(lv_event_t *e) {
    (void)e;
    /* Reset calibration state */
    s_stage = 0;
    for (int i = 0; i < 4; ++i) {
        if (g_targets[i]) {
            lv_obj_del(g_targets[i]);
            g_targets[i] = NULL;
        }
    }
    s_recalibrate = true;

    /* Clear the screen and reload the calibration UI */
    if (g_cont) {
        lv_obj_clean(g_cont);
    }
}

static void test_show_next_target(void) {
    if (!s_test_scr) {
        LOGI(TAG, "No s_test_scr, returning.");
        return;
    }
    lv_coord_t w = lv_obj_get_width(s_test_scr);
    lv_coord_t h = lv_obj_get_height(s_test_scr);
    if (w <= 40 || h <= 40) {
        LOGI(TAG, "s_test_scrc too small, returning.");
        return;
    }

    if (s_test_circle) {
        lv_obj_set_style_border_color(s_test_circle, lv_color_hex(0xBBDCFF), 0);
    }
    lv_point_t t = { (lv_coord_t)(20 + (rand() % (w-40))), (lv_coord_t)(20 + (rand() % (h-40))) };
    s_test_circle = draw_test_circle(s_test_scr, t);
    LOGI(TAG, "Next target created at: %d, %d", t.x, t.y);
}

static void btn_test_cb(lv_event_t *e) {
    (void)e;
    
    if (s_test_running) {
        /* Exit test mode */
        s_test_running = false;
        if (s_test_layer) {
            lv_obj_del(s_test_layer);
            s_test_layer = NULL;
        }
        s_test_scr = NULL;
        s_test_circle = NULL;
        
        /* Show instruction label again */
        if (s_instr) lv_obj_clear_flag(s_instr, LV_OBJ_FLAG_HIDDEN);
        
        /* Change button text back to "Test" */
        if (s_test_btn_label) lv_label_set_text(s_test_btn_label, "Test");
        
        return;
    } 
    
    /* Enter test mode */
    lv_obj_t *scr = lv_scr_act();
    
    /* Hide instruction label */
    if (s_instr) lv_obj_add_flag(s_instr, LV_OBJ_FLAG_HIDDEN);
    
    /* Change button text to "End Test" */
    if (s_test_btn_label) lv_label_set_text(s_test_btn_label, "End Test");
    
    /* create an overlay layer so existing buttons remain visible */
    if (s_test_layer) {
        lv_obj_del(s_test_layer);
        s_test_layer = NULL;
    }
    s_test_layer = lv_obj_create(scr);
    style_container(s_test_layer); /* zero padding so lv_indev_get_point screen coords align with child positions */
    lv_coord_t sh = lv_obj_get_height(scr);
    const lv_coord_t bottom_reserved = 56; /* leave bottom area for buttons */
    lv_coord_t layer_height = (sh > bottom_reserved) ? (sh - bottom_reserved) : sh;
    lv_obj_set_size(s_test_layer, lv_obj_get_width(scr), layer_height);
    lv_obj_clear_flag(s_test_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_test_layer, LV_OPA_TRANSP, 0);
    /* ensure overlay receives clicks in its area */
    lv_obj_add_flag(s_test_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_test_layer, LV_ALIGN_TOP_LEFT, 0, 0);
    s_test_scr = s_test_layer;
    s_test_running = true;
    
    /* Hide startup label during test mode */
    if (s_start_label) {
        lv_obj_del(s_start_label);
        s_start_label = NULL;
    }
    
    s_test_info = lv_label_create(s_test_layer);
    lv_label_set_text(s_test_info, "Test mode: tap targets.");
    lv_obj_set_style_text_color(s_test_info, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_test_info, LV_ALIGN_TOP_MID, 0, 8);
    /* attach click handler to the overlay */
    lv_obj_add_event_cb(s_test_layer, test_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_update_layout(lv_screen_active());

    /* Show first target immediately - after layer is fully set up */
    test_show_next_target();
}


void ui_setup() {
    lv_obj_t *scr = lv_obj_create(NULL);
    style_container(scr);
    lv_scr_load(scr);

    LOGI(TAG, "ui_run_touch_calib_blocking: start");

    /* Visible startup label so screen isn't completely black */
    s_start_label = lv_label_create(scr);
    lv_label_set_text(s_start_label, "Calibration starting...");
    lv_obj_set_style_text_color(s_start_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_start_label, LV_ALIGN_CENTER, 0, 0);

    /* Hide the label when test mode starts */
    lv_obj_add_flag(s_start_label, LV_OBJ_FLAG_HIDDEN);

    lv_coord_t w = lv_obj_get_width(scr);
    lv_coord_t h = lv_obj_get_height(scr);
    const lv_coord_t pad_x = 20; // Adjusted padding for equal inset
    const lv_coord_t pad_y = 20; // Adjusted padding for equal inset
    lv_point_t targets[4] = {
        {pad_x, pad_y}, 
        {w - pad_x, pad_y}, 
        {w - pad_x, h - pad_y}, 
        {pad_x, h - pad_y}
    };

    float src[4][2];
    float dst[4][2];

    /* Build full-screen container and drive via callbacks/state machine */
    lv_obj_t *cont = lv_obj_create(scr);
    style_container(cont);


    lv_obj_set_size(cont, lv_obj_get_width(scr), lv_obj_get_height(scr));
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(cont, container_click_cb, LV_EVENT_CLICKED, NULL);

    /* Instruction label */
    s_instr = lv_label_create(cont);
    lv_label_set_text(s_instr, "Touch each cross when shown");
    lv_obj_set_style_text_color(s_instr, lv_color_hex(0xffffff), 0);
    lv_obj_align(s_instr, LV_ALIGN_TOP_MID, 0, 8);

    /* draw targets and keep their positions */
    for (int i = 0; i < 4; ++i) {
        g_targets_pos[i].x = targets[i].x;
        g_targets_pos[i].y = targets[i].y;
        g_targets[i] = draw_report_cross(cont, g_targets_pos[i]);
        
        if (i != 0) lv_obj_add_flag(g_targets[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* create buttons and ensure readable text (hidden until computed) */
    g_btn_save = lv_btn_create(scr);
    lv_obj_align(g_btn_save, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_t *lab_save = lv_label_create(g_btn_save);
    lv_label_set_text(lab_save, "Save & Reboot");
    lv_obj_set_style_text_color(lab_save, lv_color_hex(0xffffff), 0);
    lv_obj_add_flag(g_btn_save, LV_OBJ_FLAG_HIDDEN);

    g_btn_recal = lv_btn_create(scr);
    lv_obj_align(g_btn_recal, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_t *lab_recal = lv_label_create(g_btn_recal);
    lv_label_set_text(lab_recal, "Recalibrate");
    lv_obj_set_style_text_color(lab_recal, lv_color_hex(0xffffff), 0);
    lv_obj_add_flag(g_btn_recal, LV_OBJ_FLAG_HIDDEN);

    g_btn_test = lv_btn_create(scr);
    lv_obj_align(g_btn_test, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    s_test_btn_label = lv_label_create(g_btn_test);
    lv_label_set_text(s_test_btn_label, "Test");
    lv_obj_set_style_text_color(s_test_btn_label, lv_color_hex(0xffffff), 0);
    lv_obj_add_flag(g_btn_test, LV_OBJ_FLAG_HIDDEN);
    s_test_btn = g_btn_test;

    lv_obj_add_event_cb(g_btn_save, btn_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_btn_recal, btn_recal_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_btn_test, btn_test_cb, LV_EVENT_CLICKED, NULL);

    LOGI(TAG, "UI setup done.");
}

void ui_run_touch_calib_blocking(void) {
    ui_setup();

    while (!s_recalibrate) {
        unsigned long time_start = millis();
        uint32_t sleep_time = lv_task_handler();
        vTaskDelay(sleep_time / portTICK_PERIOD_MS);
        unsigned long time_end = millis();
        lv_tick_inc(time_end - time_start);
    }
    
    /* Clean up UI state */
    s_recalibrate = false;
    s_start_label = NULL;
    s_instr = NULL;
    s_test_btn = NULL;
    s_test_btn_label = NULL;
    s_test_running = false;
    s_stage = 0;
}
