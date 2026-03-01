// lv_settings.c — LVGL settings panel (lv_menu-based, LVGL 9)
//
// Layout:
//   sidebar (left)  — group list, one entry per settings group + "Reload Defaults"
//   main    (right) — per-group content pages
//
// Numeric rows  (INT32 / FLOAT):
//   [icon]  Name label (flex-grow)            value + unit  (right-aligned)
//   [slider ---- full width, wrapped to next line ---------]
//
// Bool rows:
//   [icon]  Name label (flex-grow)                   [switch]
//
// "Reload Defaults" sidebar entry navigates to a page with one clickable row
// that calls app_settings_reset_all() and refreshes all widgets.
//
// The main-panel back-bar header is hidden; navigation is sidebar-only.
// The root-back button is disabled (no "< Settings" label at top-left).

#define UI_DEBUG_LOCAL_LEVEL D_INFO
#include "debug.h"

#include "lv_settings.h"
#include "config/app_settings.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

static const char *TAG = "lv_settings";

/* =========================================================================
 * Geometry tunables
 * ========================================================================= */

#define LV_SETTINGS_MAX_SLOTS    64   /* max total setting rows across all groups */
#define LV_SETTINGS_VALUE_LBL_W 110   /* px — right-aligned value column width    */

/* =========================================================================
 * Group symbols (sidebar icons)
 * ========================================================================= */

static const char * const g_group_symbols[APP_SETTINGS_GROUP__COUNT] = {
    [APP_SETTINGS_GROUP_JOG]      = LV_SYMBOL_DRIVE,
    [APP_SETTINGS_GROUP_CAM]      = LV_SYMBOL_IMAGE,
    [APP_SETTINGS_GROUP_MACHINE]  = LV_SYMBOL_SETTINGS,
    [APP_SETTINGS_GROUP_TIMING]   = LV_SYMBOL_REFRESH,
    [APP_SETTINGS_GROUP_PROBE_UI] = LV_SYMBOL_GPS,
    [APP_SETTINGS_GROUP_MATERIALS] = LV_SYMBOL_LIST, /* material dropdown */
};

/* =========================================================================
 * Per-setting symbols  [group][key]
 * Unmentioned entries default to NULL (no icon shown).
 * ========================================================================= */

static const char * const g_setting_symbols
    [APP_SETTINGS_GROUP__COUNT][APP_SETTINGS_MAX_KEYS_PER_GROUP] = {

    [APP_SETTINGS_GROUP_JOG] = {
        [APP_SETTINGS_JOG_FEED_XY]        = LV_SYMBOL_RIGHT,
        [APP_SETTINGS_JOG_FEED_Z]         = LV_SYMBOL_UP,
        [APP_SETTINGS_JOG_ACCEL_X]        = LV_SYMBOL_CHARGE,
        [APP_SETTINGS_JOG_ACCEL_Y]        = LV_SYMBOL_CHARGE,
        [APP_SETTINGS_JOG_ACCEL_Z]        = LV_SYMBOL_CHARGE,
        [APP_SETTINGS_JOG_LEAD_AHEAD_MS]  = LV_SYMBOL_PLAY,
        [APP_SETTINGS_JOG_MIN_SLEEP_MS]   = LV_SYMBOL_PAUSE,
        [APP_SETTINGS_JOG_MAX_SLEEP_MS]   = LV_SYMBOL_STOP,
    },
    [APP_SETTINGS_GROUP_CAM] = {
        [APP_SETTINGS_CAM_FALLBACK_GRID_DX] = LV_SYMBOL_RIGHT,
        [APP_SETTINGS_CAM_FALLBACK_GRID_DY] = LV_SYMBOL_DOWN,
        [APP_SETTINGS_CAM_FALLBACK_GRID_NX] = LV_SYMBOL_LIST,
        [APP_SETTINGS_CAM_FALLBACK_GRID_NY] = LV_SYMBOL_LIST,
    },
    [APP_SETTINGS_GROUP_MACHINE] = {
        [APP_SETTINGS_MACHINE_SEND_INTERVAL_MS]  = LV_SYMBOL_PLAY,
        [APP_SETTINGS_MACHINE_POLL_NTH_INTERVAL] = LV_SYMBOL_LOOP,
    },
    [APP_SETTINGS_GROUP_TIMING] = {
        [APP_SETTINGS_TIMING_HUB_POLL_MS]          = LV_SYMBOL_WIFI,
        [APP_SETTINGS_TIMING_FULL_STATE_N]          = LV_SYMBOL_LIST,
        [APP_SETTINGS_TIMING_LOG_COALESCE_MS]       = LV_SYMBOL_EDIT,
        [APP_SETTINGS_TIMING_CAM_FRAME_TIMEOUT_MS]  = LV_SYMBOL_VIDEO,
        [APP_SETTINGS_TIMING_CAM_STATUS_TIMEOUT_MS] = LV_SYMBOL_VIDEO,
    },
    [APP_SETTINGS_GROUP_PROBE_UI] = {
        [APP_SETTINGS_PROBE_UI_TIP_RADIUS]   = LV_SYMBOL_GPS,
        [APP_SETTINGS_PROBE_UI_BACKOFF_MULT] = LV_SYMBOL_RIGHT,
    },
};

/* =========================================================================
 * Private types
 * ========================================================================= */

typedef struct lv_settings_priv_s lv_settings_priv_t;

typedef struct {
    lv_obj_t  *cont;       /* menu_cont — carries LV_STATE_FOCUSED highlight  */
    lv_obj_t  *slider;     /* numeric slider, NULL for bool                   */
    lv_obj_t  *sw;         /* lv_switch for bool, NULL for numeric            */
    lv_obj_t  *dd;         /* lv_dropdown for enum, NULL otherwise            */
    lv_obj_t  *value_lbl;  /* formatted value label, NULL for bool            */
    int        group;
    int        key;
    bool       updating;   /* reentrancy guard while syncing slider from code */
    lv_settings_priv_t *priv;
} setting_slot_t;

struct lv_settings_priv_s {
    lv_obj_t *root;                              /* the lv_menu object         */
    lv_obj_t *pages[APP_SETTINGS_GROUP__COUNT];  /* main content pages         */
    lv_obj_t *reload_page;                       /* "Reload Defaults" page     */

    setting_slot_t slots[LV_SETTINGS_MAX_SLOTS];
    int slot_count;

    int active_group;  /* currently shown group, or -1 for reload page        */
    int focused_row;   /* slot index with encoder focus, or -1                */

    lv_settings_changed_cb_t changed_cb;
    void                    *changed_user;
};

/* =========================================================================
 * State accessor
 * ========================================================================= */

static lv_settings_priv_t *_get_priv(lv_obj_t *obj)
{
    lv_settings_priv_t *p = (lv_settings_priv_t *)lv_obj_get_user_data(obj);
    return (p && p->root == obj) ? p : NULL;
}

/* =========================================================================
 * Value helpers
 * ========================================================================= */

static void _fmt_value(char *buf, size_t buf_len,
                       const app_setting_def_t *d, app_setting_val_t v)
{
    switch (d->type) {
        case APP_SETTING_TYPE_FLOAT: {
            int dp = (d->step.f < 0.1f) ? 3 : (d->step.f < 1.0f) ? 2 : 1;
            if (d->unit && d->unit[0])
                snprintf(buf, buf_len, "%.*f %s", dp, (double)v.f, d->unit);
            else
                snprintf(buf, buf_len, "%.*f", dp, (double)v.f);
            break;
        }
        case APP_SETTING_TYPE_INT32:
            if (d->unit && d->unit[0])
                snprintf(buf, buf_len, "%" PRId32 " %s", v.i, d->unit);
            else
                snprintf(buf, buf_len, "%" PRId32, v.i);
            break;
        case APP_SETTING_TYPE_ENUM:
            if (d->choices && v.i >= 0 && v.i < d->choice_count)
                snprintf(buf, buf_len, "%s", d->choices[v.i]);
            else
                snprintf(buf, buf_len, "%d", v.i);
            break;
        case APP_SETTING_TYPE_BOOL:
            snprintf(buf, buf_len, "%s", v.b ? "ON" : "OFF");
            break;
    }
}

static int32_t _slider_steps(const app_setting_def_t *d)
{
    if (d->type == APP_SETTING_TYPE_INT32 && d->step.i > 0)
        return (d->max.i - d->min.i) / d->step.i;
    if (d->type == APP_SETTING_TYPE_FLOAT && d->step.f > 0.0f)
        return (int32_t)((d->max.f - d->min.f) / d->step.f + 0.5f);
    return 100;
}

static int32_t _val_to_slider(const app_setting_def_t *d, app_setting_val_t v)
{
    if (d->type == APP_SETTING_TYPE_INT32 && d->step.i > 0)
        return (v.i - d->min.i) / d->step.i;
    if (d->type == APP_SETTING_TYPE_FLOAT && d->step.f > 0.0f)
        return (int32_t)((v.f - d->min.f) / d->step.f + 0.5f);
    return 0;
}

static app_setting_val_t _slider_to_val(const app_setting_def_t *d, int32_t pos)
{
    app_setting_val_t v = {0};
    if (d->type == APP_SETTING_TYPE_INT32) {
        v.i = d->min.i + pos * d->step.i;
        if (v.i < d->min.i) v.i = d->min.i;
        if (v.i > d->max.i) v.i = d->max.i;
    } else {
        v.f = d->min.f + (float)pos * d->step.f;
        if (v.f < d->min.f) v.f = d->min.f;
        if (v.f > d->max.f) v.f = d->max.f;
    }
    return v;
}

/* =========================================================================
 * Focus management
 * ========================================================================= */

static void _set_slot_focused(lv_settings_priv_t *priv, int idx)
{
    if (priv->focused_row >= 0 && priv->focused_row < priv->slot_count)
        lv_obj_clear_state(priv->slots[priv->focused_row].cont, LV_STATE_FOCUSED);
    priv->focused_row = idx;
    if (idx >= 0 && idx < priv->slot_count) {
        lv_obj_add_state(priv->slots[idx].cont, LV_STATE_FOCUSED);
        lv_obj_scroll_to_view(priv->slots[idx].cont, LV_ANIM_ON);
    }
}

/* =========================================================================
 * Value application (shared by slider callback, switch callback, encoder)
 * ========================================================================= */

static void _apply_slot_value(setting_slot_t *slot, app_setting_val_t v,
                               const app_setting_def_t *d)
{
    app_settings_set_val((app_settings_group_t)slot->group, slot->key, v);
    app_settings_save_group((app_settings_group_t)slot->group);

    if (d->type == APP_SETTING_TYPE_BOOL) {
        if (slot->sw) {
            if (v.b) lv_obj_add_state(slot->sw, LV_STATE_CHECKED);
            else     lv_obj_clear_state(slot->sw, LV_STATE_CHECKED);
        }
    } else if (d->type == APP_SETTING_TYPE_ENUM) {
        if (slot->dd) {
            lv_dropdown_set_selected(slot->dd, v.i);
        }
        if (slot->value_lbl) {
            char buf[40];
            _fmt_value(buf, sizeof(buf), d, v);
            lv_label_set_text(slot->value_lbl, buf);
        }
    } else {
        if (slot->value_lbl) {
            char buf[40];
            _fmt_value(buf, sizeof(buf), d, v);
            lv_label_set_text(slot->value_lbl, buf);
        }
        if (slot->slider) {
            slot->updating = true;
            lv_slider_set_value(slot->slider, _val_to_slider(d, v), LV_ANIM_OFF);
            slot->updating = false;
        }
    }

    if (slot->priv->changed_cb)
        slot->priv->changed_cb(slot->group, slot->key, slot->priv->changed_user);
}

static void _adjust_slot_value(setting_slot_t *slot, int delta)
{
    const app_setting_def_t *d = app_settings_get_def(
        (app_settings_group_t)slot->group, slot->key);
    if (!d) return;

    app_setting_val_t v = app_settings_get_val(
        (app_settings_group_t)slot->group, slot->key);

    switch (d->type) {
        case APP_SETTING_TYPE_FLOAT:
            v.f += (float)delta * d->step.f;
            if (v.f < d->min.f) v.f = d->min.f;
            if (v.f > d->max.f) v.f = d->max.f;
            break;
        case APP_SETTING_TYPE_INT32:
            v.i += (int32_t)delta * d->step.i;
            if (v.i < d->min.i) v.i = d->min.i;
            if (v.i > d->max.i) v.i = d->max.i;
            break;
        case APP_SETTING_TYPE_ENUM:
            if (d->choice_count > 0) {
                v.i += delta;
                if (v.i < 0) v.i = d->choice_count - 1;
                if (v.i >= d->choice_count) v.i = 0;
            }
            break;
        case APP_SETTING_TYPE_BOOL:
            if (delta != 0) v.b = !v.b;
            break;
    }

    LOGD(TAG, "Group %d key %d delta=%+d", slot->group, slot->key, delta);
    _apply_slot_value(slot, v, d);
}

/* =========================================================================
 * Event callbacks
 * ========================================================================= */

static void _slider_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    setting_slot_t *slot = (setting_slot_t *)lv_event_get_user_data(e);
    if (!slot || slot->updating) return;

    const app_setting_def_t *d = app_settings_get_def(
        (app_settings_group_t)slot->group, slot->key);
    if (!d) return;

    int32_t pos = lv_slider_get_value(slot->slider);
    app_setting_val_t v = _slider_to_val(d, pos);

    LOGD(TAG, "Slider g=%d k=%d pos=%d", slot->group, slot->key, (int)pos);
    _apply_slot_value(slot, v, d);
}

static void _switch_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    setting_slot_t *slot = (setting_slot_t *)lv_event_get_user_data(e);
    if (!slot) return;

    const app_setting_def_t *d = app_settings_get_def(
        (app_settings_group_t)slot->group, slot->key);
    if (!d) return;

    app_setting_val_t v = { .b = lv_obj_has_state(slot->sw, LV_STATE_CHECKED) };
    LOGD(TAG, "Switch g=%d k=%d = %s", slot->group, slot->key, v.b ? "ON" : "OFF");
    _apply_slot_value(slot, v, d);
}

static void _dropdown_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    setting_slot_t *slot = (setting_slot_t *)lv_event_get_user_data(e);
    if (!slot) return;

    const app_setting_def_t *d = app_settings_get_def(
        (app_settings_group_t)slot->group, slot->key);
    if (!d) return;

    app_setting_val_t v = { .i = lv_dropdown_get_selected(slot->dd) };
    LOGD(TAG, "Dropdown g=%d k=%d idx=%d", slot->group, slot->key, v.i);
    _apply_slot_value(slot, v, d);
}

/* Clicking a row cont toggles encoder focus. */
static void _cont_clicked_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    setting_slot_t *slot = (setting_slot_t *)lv_event_get_user_data(e);
    if (!slot) return;
    lv_settings_priv_t *priv = slot->priv;
    int idx = (int)(slot - priv->slots);
    _set_slot_focused(priv, (priv->focused_row == idx) ? -1 : idx);
}

static void _reload_cont_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_settings_priv_t *priv = (lv_settings_priv_t *)lv_event_get_user_data(e);
    if (!priv) return;
    app_settings_reset_all();
    lv_settings_refresh(priv->root);
    LOGI(TAG, "All settings reset to defaults");
}

/* Track which group page is shown for encoder navigation. */
static void _menu_value_changed_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t *menu = lv_event_get_target(e);
    lv_settings_priv_t *priv = (lv_settings_priv_t *)lv_event_get_user_data(e);
    if (!priv) return;

    lv_obj_t *cur = lv_menu_get_cur_main_page(menu);
    priv->focused_row = -1;

    for (int g = 0; g < APP_SETTINGS_GROUP__COUNT; g++) {
        if (cur == priv->pages[g]) { priv->active_group = g; return; }
    }
    if (cur == priv->reload_page) priv->active_group = -1;
}

static void _root_delete_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_settings_priv_t *priv = (lv_settings_priv_t *)lv_obj_get_user_data(obj);
    if (priv) { lv_free(priv); lv_obj_set_user_data(obj, NULL); }
}

/* =========================================================================
 * Row builders
 * ========================================================================= */

/*
 * Numeric row  (flex-row-wrap so slider wraps to a second line):
 *
 *   [icon]  Name label (flex-grow)       value + unit  (right, 110 px)
 *   [slider ------------- flex-grow, new-track ----------------------]
 */
static setting_slot_t *_add_numeric_slot(lv_settings_priv_t *priv,
                                         lv_obj_t *section,
                                         int group, int key)
{
    if (priv->slot_count >= LV_SETTINGS_MAX_SLOTS) {
        LOGW(TAG, "Slot array full g=%d k=%d", group, key);
        return NULL;
    }
    const app_setting_def_t *d = app_settings_get_def(
        (app_settings_group_t)group, key);
    if (!d) return NULL;

    const char *sym = (key >= 0 && key < APP_SETTINGS_MAX_KEYS_PER_GROUP)
                      ? g_setting_symbols[group][key] : NULL;
    app_setting_val_t v = app_settings_get_val((app_settings_group_t)group, key);

    setting_slot_t *slot = &priv->slots[priv->slot_count++];
    memset(slot, 0, sizeof(*slot));
    slot->group = group;
    slot->key   = key;
    slot->priv  = priv;

    /* Container — wrapping flex-row */
    slot->cont = lv_menu_cont_create(section);
    lv_obj_set_flex_flow(slot->cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_width(slot->cont, LV_PCT(100));
    lv_obj_set_style_pad_row(slot->cont, 8, 0);
    lv_obj_set_style_pad_column(slot->cont, 6, 0);
    lv_obj_set_style_bg_opa(slot->cont, LV_OPA_0, 0);
    lv_obj_set_style_bg_opa(slot->cont, LV_OPA_30, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(slot->cont, lv_color_hex(0x2196F3), LV_STATE_FOCUSED);
    lv_obj_set_style_margin_bottom(slot->cont, 5, 0);
    lv_obj_set_style_radius(slot->cont, 6, 0);
    // lv_obj_add_flag(slot->cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(slot->cont, _cont_clicked_cb, LV_EVENT_CLICKED, slot);

    /* Row 1 — optional icon */
    if (sym) {
        lv_obj_t *img = lv_image_create(slot->cont);
        lv_image_set_src(img, sym);
    }

    /* Row 1 — name label fills remaining space */
    lv_obj_t *name_lbl = lv_label_create(slot->cont);
    lv_label_set_text(name_lbl, d->name);
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_flex_grow(name_lbl, 1, 0);

    /* Row 1 — right-aligned value label */
    slot->value_lbl = lv_label_create(slot->cont);
    lv_obj_set_width(slot->value_lbl, LV_SETTINGS_VALUE_LBL_W);
    lv_obj_set_style_text_align(slot->value_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    char buf[40];
    _fmt_value(buf, sizeof(buf), d, v);
    lv_label_set_text(slot->value_lbl, buf);

    /* Row 2 — slider wrapped to the next flex line */
    slot->slider = lv_slider_create(slot->cont);
    lv_obj_set_style_flex_grow(slot->slider, 1, 0);
    lv_obj_add_flag(slot->slider, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    lv_slider_set_range(slot->slider, 0, _slider_steps(d));
    lv_slider_set_value(slot->slider, _val_to_slider(d, v), LV_ANIM_OFF);
    lv_obj_add_event_cb(slot->slider, _slider_cb, LV_EVENT_VALUE_CHANGED, slot);
    lv_obj_set_style_pad_top(slot->slider, 4, 0);
    lv_obj_set_style_pad_bottom(slot->slider, 4, 0);
    lv_obj_set_style_pad_bottom(slot->slider, 4, 0);

    lv_obj_set_ext_click_area(slot->slider, 5);

    return slot;
}

/*
 * Bool row  (single flex-row):
 *
 *   [icon]  Name label (flex-grow)                     [switch]
 */
static setting_slot_t *_add_bool_slot(lv_settings_priv_t *priv,
                                      lv_obj_t *section,
                                      int group, int key)
{
    if (priv->slot_count >= LV_SETTINGS_MAX_SLOTS) {
        LOGW(TAG, "Slot array full g=%d k=%d", group, key);
        return NULL;
    }
    const app_setting_def_t *d = app_settings_get_def(
        (app_settings_group_t)group, key);
    if (!d) return NULL;

    const char *sym = (key >= 0 && key < APP_SETTINGS_MAX_KEYS_PER_GROUP)
                      ? g_setting_symbols[group][key] : NULL;
    app_setting_val_t v = app_settings_get_val((app_settings_group_t)group, key);

    setting_slot_t *slot = &priv->slots[priv->slot_count++];
    memset(slot, 0, sizeof(*slot));
    slot->group = group;
    slot->key   = key;
    slot->priv  = priv;

    slot->cont = lv_menu_cont_create(section);
    lv_obj_set_style_bg_opa(slot->cont, LV_OPA_0, 0);
    lv_obj_set_style_bg_opa(slot->cont, LV_OPA_30, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(slot->cont, lv_color_hex(0x2196F3), LV_STATE_FOCUSED);
    lv_obj_set_style_margin_bottom(slot->cont, 5, 0);
    lv_obj_set_style_radius(slot->cont, 6, 0);
    //lv_obj_add_flag(slot->cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(slot->cont, _cont_clicked_cb, LV_EVENT_CLICKED, slot);

    if (sym) {
        lv_obj_t *img = lv_image_create(slot->cont);
        lv_image_set_src(img, sym);
    }

    lv_obj_t *name_lbl = lv_label_create(slot->cont);
    lv_label_set_text(name_lbl, d->name);
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_flex_grow(name_lbl, 1, 0);

    slot->sw = lv_switch_create(slot->cont);
    if (v.b) lv_obj_add_state(slot->sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(slot->sw, _switch_cb, LV_EVENT_VALUE_CHANGED, slot);

    return slot;
}

/* Enum row: similar layout to bool but with a dropdown controlling the value. */
static setting_slot_t *_add_enum_slot(lv_settings_priv_t *priv,
                                      lv_obj_t *section,
                                      int group, int key)
{
    if (priv->slot_count >= LV_SETTINGS_MAX_SLOTS) {
        LOGW(TAG, "Slot array full g=%d k=%d", group, key);
        return NULL;
    }
    const app_setting_def_t *d = app_settings_get_def(
        (app_settings_group_t)group, key);
    if (!d) return NULL;

    const char *sym = (key >= 0 && key < APP_SETTINGS_MAX_KEYS_PER_GROUP)
                      ? g_setting_symbols[group][key] : NULL;
    app_setting_val_t v = app_settings_get_val((app_settings_group_t)group, key);

    setting_slot_t *slot = &priv->slots[priv->slot_count++];
    memset(slot, 0, sizeof(*slot));
    slot->group = group;
    slot->key   = key;
    slot->priv  = priv;

    slot->cont = lv_menu_cont_create(section);
    lv_obj_set_style_bg_opa(slot->cont, LV_OPA_0, 0);
    lv_obj_set_style_bg_opa(slot->cont, LV_OPA_30, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(slot->cont, lv_color_hex(0x2196F3), LV_STATE_FOCUSED);
    lv_obj_set_style_margin_bottom(slot->cont, 5, 0);
    lv_obj_set_style_radius(slot->cont, 6, 0);
    lv_obj_add_event_cb(slot->cont, _cont_clicked_cb, LV_EVENT_CLICKED, slot);

    if (sym) {
        lv_obj_t *img = lv_image_create(slot->cont);
        lv_image_set_src(img, sym);
    }

    lv_obj_t *name_lbl = lv_label_create(slot->cont);
    lv_label_set_text(name_lbl, d->name);
    lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_flex_grow(name_lbl, 1, 0);

    /* build newline-separated options string */
    char buf[256] = {0};
    for (int i = 0; i < d->choice_count && i < 50; i++) {
        strlcat(buf, d->choices[i], sizeof(buf));
        if (i + 1 < d->choice_count) strlcat(buf, "\n", sizeof(buf));
    }

    slot->dd = lv_dropdown_create(slot->cont);
    lv_dropdown_set_options(slot->dd, buf);
    if (v.i >= 0) lv_dropdown_set_selected(slot->dd, v.i);
    lv_obj_add_event_cb(slot->dd, _dropdown_cb, LV_EVENT_VALUE_CHANGED, slot);

    return slot;
}

/* Build the main content page for one group. */
static lv_obj_t *_build_group_page(lv_settings_priv_t *priv,
                                    lv_obj_t *menu, int group)
{
    lv_obj_t *page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(page,
        lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_menu_separator_create(page);
    lv_obj_t *section = lv_menu_section_create(page);

    int key_count = app_settings_key_count((app_settings_group_t)group);
    for (int k = 0; k < key_count; k++) {
        const app_setting_def_t *d = app_settings_get_def(
            (app_settings_group_t)group, k);
        if (!d) continue;
        if (d->type == APP_SETTING_TYPE_BOOL) {
            _add_bool_slot(priv, section, group, k);
        } else if (d->type == APP_SETTING_TYPE_ENUM) {
            _add_enum_slot(priv, section, group, k);
        } else {
            _add_numeric_slot(priv, section, group, k);
        }
    }
    return page;
}

/* =========================================================================
 * Public API — create
 * ========================================================================= */

lv_obj_t *lv_settings_create(lv_obj_t *parent)
{
    lv_settings_priv_t *priv =
        (lv_settings_priv_t *)lv_malloc(sizeof(lv_settings_priv_t));
    if (!priv) { LOGE(TAG, "OOM allocating settings priv"); return NULL; }
    memset(priv, 0, sizeof(*priv));
    priv->active_group = 0;
    priv->focused_row  = -1;

    /* --- Create lv_menu -------------------------------------------------- */
    lv_obj_t *menu = lv_menu_create(parent);
    priv->root = menu;
    lv_obj_set_user_data(menu, priv);
    lv_obj_add_event_cb(menu, _root_delete_cb,        LV_EVENT_DELETE,        NULL);
    lv_obj_add_event_cb(menu, _menu_value_changed_cb, LV_EVENT_VALUE_CHANGED, priv);
    lv_obj_set_size(menu, LV_PCT(100), LV_PCT(100));
    lv_menu_set_mode_root_back_button(menu, LV_MENU_ROOT_BACK_BUTTON_DISABLED);

    /* --- Build content pages (do this BEFORE hiding header so the header  */
    /* --- padding helper still reads the header's style)                   */
    for (int g = 0; g < APP_SETTINGS_GROUP__COUNT; g++)
        priv->pages[g] = _build_group_page(priv, menu, g);

    /* --- "Reload Defaults" content page ---------------------------------- */
    priv->reload_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(priv->reload_page,
        lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);
    lv_menu_separator_create(priv->reload_page);
    {
        lv_obj_t *sec  = lv_menu_section_create(priv->reload_page);
        lv_obj_t *cont = lv_menu_cont_create(sec);
        lv_obj_t *img  = lv_image_create(cont);
        lv_image_set_src(img, LV_SYMBOL_REFRESH);
        lv_obj_t *lbl  = lv_label_create(cont);
        lv_label_set_text(lbl, "Reset All to Defaults");
        lv_obj_set_style_flex_grow(lbl, 1, 0);
        lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(cont, _reload_cont_cb, LV_EVENT_CLICKED, priv);
    }

    /* --- Hide main-panel header (back bar) ------------------------------- */
    lv_obj_t *main_hdr = lv_menu_get_main_header(menu);
    if (main_hdr) lv_obj_add_flag(main_hdr, LV_OBJ_FLAG_HIDDEN);

    /* --- Build sidebar (root) page --------------------------------------- */
    lv_obj_t *sb_page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_hor(sb_page,
        lv_obj_get_style_pad_left(lv_menu_get_main_header(menu), 0), 0);

    /* Section 1 — one entry per settings group.  Materials group is pushed
     * to the front even though its enum value is last so the user sees it
     * first. */
    {
        lv_obj_t *sect = lv_menu_section_create(sb_page);
        /* first add materials if present */
        if (APP_SETTINGS_GROUP_MATERIALS < APP_SETTINGS_GROUP__COUNT) {
            int g = APP_SETTINGS_GROUP_MATERIALS;
            const char *sym  = g_group_symbols[g];
            const char *name = app_settings_group_name((app_settings_group_t)g);
            lv_obj_t *item = lv_menu_cont_create(sect);
            if (sym) {
                lv_obj_t *img = lv_image_create(item);
                lv_image_set_src(img, sym);
            }
            lv_obj_t *lbl = lv_label_create(item);
            lv_label_set_text(lbl, name);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_flex_grow(lbl, 1, 0);
            lv_menu_set_load_page_event(menu, item, priv->pages[g]);
        }
        /* then add the remaining groups in order, skipping materials */
        for (int g = 0; g < APP_SETTINGS_GROUP__COUNT; g++) {
            if (g == APP_SETTINGS_GROUP_MATERIALS) continue;
            const char *sym  = g_group_symbols[g];
            const char *name = app_settings_group_name((app_settings_group_t)g);
            lv_obj_t *item = lv_menu_cont_create(sect);
            if (sym) {
                lv_obj_t *img = lv_image_create(item);
                lv_image_set_src(img, sym);
            }
            lv_obj_t *lbl = lv_label_create(item);
            lv_label_set_text(lbl, name);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_flex_grow(lbl, 1, 0);
            lv_menu_set_load_page_event(menu, item, priv->pages[g]);
        }
    }

    /* Section 2 — "Reload Defaults" entry */
    {
        lv_menu_separator_create(sb_page);
        lv_obj_t *sect = lv_menu_section_create(sb_page);
        lv_obj_t *item = lv_menu_cont_create(sect);
        lv_obj_t *img  = lv_image_create(item);
        lv_image_set_src(img, LV_SYMBOL_REFRESH);
        lv_obj_t *lbl  = lv_label_create(item);
        lv_label_set_text(lbl, "Reload Defaults");
        lv_obj_set_style_flex_grow(lbl, 1, 0);
        lv_menu_set_load_page_event(menu, item, priv->reload_page);
    }

    /* --- Activate sidebar and auto-click first group entry --------------- */
    lv_menu_set_sidebar_page(menu, sb_page);
    lv_obj_send_event(
        lv_obj_get_child(
            lv_obj_get_child(lv_menu_get_cur_sidebar_page(menu), 0), 0),
        LV_EVENT_CLICKED, NULL);

    LOGI(TAG, "lv_settings created: %d groups %d slots",
         APP_SETTINGS_GROUP__COUNT, priv->slot_count);
    return menu;
}

/* =========================================================================
 * Public API — refresh
 * ========================================================================= */

void lv_settings_refresh(lv_obj_t *obj)
{
    lv_settings_priv_t *priv = _get_priv(obj);
    if (!priv) return;
    for (int i = 0; i < priv->slot_count; i++) {
        setting_slot_t *slot = &priv->slots[i];
        const app_setting_def_t *d = app_settings_get_def(
            (app_settings_group_t)slot->group, slot->key);
        if (!d) continue;
        app_setting_val_t v = app_settings_get_val(
            (app_settings_group_t)slot->group, slot->key);
        if (d->type == APP_SETTING_TYPE_BOOL) {
            if (slot->sw) {
                if (v.b) lv_obj_add_state(slot->sw, LV_STATE_CHECKED);
                else     lv_obj_clear_state(slot->sw, LV_STATE_CHECKED);
            }
        } else {
            if (slot->value_lbl) {
                char buf[40];
                _fmt_value(buf, sizeof(buf), d, v);
                lv_label_set_text(slot->value_lbl, buf);
            }
            if (slot->slider) {
                slot->updating = true;
                lv_slider_set_value(slot->slider,
                    _val_to_slider(d, v), LV_ANIM_OFF);
                slot->updating = false;
            }
        }
    }
}

/* =========================================================================
 * Public API — encoder
 * ========================================================================= */

void lv_settings_encoder_input(lv_obj_t *obj, int32_t diff)
{
    if (diff == 0) return;
    lv_settings_priv_t *priv = _get_priv(obj);
    if (!priv) return;
    if (priv->focused_row >= 0) {
        int steps = diff > 0 ? (int)diff : -(int)(-diff);
        if (steps > 10) steps = 10;
        int dir = diff > 0 ? 1 : -1;
        for (int i = 0; i < steps; i++)
            _adjust_slot_value(&priv->slots[priv->focused_row], dir);
    } else {
        lv_obj_t *cur = lv_menu_get_cur_main_page(priv->root);
        if (cur) lv_obj_scroll_to_y(cur,
            lv_obj_get_scroll_y(cur) + diff * 40, LV_ANIM_OFF);
    }
}

bool lv_settings_has_encoder_focus(lv_obj_t *obj)
{
    lv_settings_priv_t *priv = _get_priv(obj);
    return priv ? priv->focused_row >= 0 : false;
}

void lv_settings_encoder_navigate(lv_obj_t *obj, int32_t dir)
{
    if (dir == 0) return;
    lv_settings_priv_t *priv = _get_priv(obj);
    if (!priv || priv->slot_count == 0 || priv->active_group < 0) return;

    int g = priv->active_group;
    int first = -1, last = -1;
    for (int i = 0; i < priv->slot_count; i++) {
        if (priv->slots[i].group == g) {
            if (first < 0) first = i;
            last = i;
        }
    }
    if (first < 0) return;

    int cur = priv->focused_row;
    int next;
    if (cur < first || cur > last) {
        next = dir > 0 ? first : last;
    } else {
        next = cur + (dir > 0 ? 1 : -1);
        if (next < first) next = last;
        if (next > last)  next = first;
    }
    _set_slot_focused(priv, next);
}

/* =========================================================================
 * Public API — change callback
 * ========================================================================= */

void lv_settings_set_changed_cb(lv_obj_t *obj,
                                  lv_settings_changed_cb_t cb,
                                  void *user_data)
{
    lv_settings_priv_t *priv = _get_priv(obj);
    if (!priv) return;
    priv->changed_cb   = cb;
    priv->changed_user = user_data;
}
