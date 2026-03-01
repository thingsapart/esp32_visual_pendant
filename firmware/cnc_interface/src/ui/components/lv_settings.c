// lv_settings.c — LVGL settings panel widget implementation
//
// Two-panel layout:
//   left  (≈160 px) — group selector built with lv_btn list
//   right (flex)    — scrollable list of setting rows for the active group
//
// Each setting row: [name label] [« dec] [value label] [inc »]
// Bool settings: [name label] [ON/OFF toggle]
//
// See lv_settings.h for encoder integration instructions.

#define UI_DEBUG_LOCAL_LEVEL D_INFO
#include "debug.h"

#include "lv_settings.h"
#include "config/app_settings.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

static const char *TAG = "lv_settings";

// ---------------------------------------------------------------------------
// Compile-time geometry constants
// ---------------------------------------------------------------------------

#define LV_SETTINGS_MAX_ROWS   32
#define LV_SETTINGS_LEFT_W    160
#define LV_SETTINGS_ROW_H      46
#define LV_SETTINGS_VALUE_W    80
#define LV_SETTINGS_BTN_W      38

// ---------------------------------------------------------------------------
// Widget private state
// ---------------------------------------------------------------------------

typedef struct {
    lv_obj_t *row;
    lv_obj_t *value_lbl;
    lv_obj_t *dec_btn;
    lv_obj_t *inc_btn;
    int group;
    int key;
} row_slot_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *left_panel;
    lv_obj_t *right_panel;
    lv_obj_t *group_title;
    lv_obj_t *rows_cont;

    lv_obj_t *group_btns[APP_SETTINGS_GROUP__COUNT];

    row_slot_t rows[LV_SETTINGS_MAX_ROWS];
    int        row_count;

    int active_group;
    int focused_row;   // -1 = none highlighted

    lv_settings_changed_cb_t changed_cb;
    void                    *changed_user;
} lv_settings_priv_t;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static void _build_right_panel(lv_settings_priv_t *priv, int group);
static void _update_row_value(row_slot_t *rs);
static void _set_row_focused(lv_settings_priv_t *priv, int idx);
static void _adjust_row_value(lv_settings_priv_t *priv, int idx, int delta);

// ---------------------------------------------------------------------------
// Private state accessor — walks up parent chain to root
// ---------------------------------------------------------------------------

static lv_settings_priv_t *_get_priv(lv_obj_t *obj)
{
    while (obj) {
        lv_settings_priv_t *p =
            (lv_settings_priv_t *)lv_obj_get_user_data(obj);
        if (p && p->root == obj) return p;
        obj = lv_obj_get_parent(obj);
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Event callbacks
// ---------------------------------------------------------------------------

static void _group_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t *btn = lv_event_get_current_target(e);
    lv_settings_priv_t *priv =
        (lv_settings_priv_t *)lv_event_get_user_data(e);
    if (!priv) return;

    for (int g = 0; g < APP_SETTINGS_GROUP__COUNT; g++) {
        if (priv->group_btns[g] == btn && priv->active_group != g) {
            priv->active_group = g;
            priv->focused_row  = -1;
            _build_right_panel(priv, g);
            break;
        }
    }
}

static void _dec_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    row_slot_t *rs = (row_slot_t *)lv_event_get_user_data(e);
    if (!rs) return;
    lv_settings_priv_t *priv = _get_priv(rs->row);
    if (!priv) return;
    _adjust_row_value(priv, (int)(rs - priv->rows), -1);
}

static void _inc_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    row_slot_t *rs = (row_slot_t *)lv_event_get_user_data(e);
    if (!rs) return;
    lv_settings_priv_t *priv = _get_priv(rs->row);
    if (!priv) return;
    _adjust_row_value(priv, (int)(rs - priv->rows), +1);
}

static void _row_clicked_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    row_slot_t *rs = (row_slot_t *)lv_event_get_user_data(e);
    if (!rs) return;
    lv_settings_priv_t *priv = _get_priv(rs->row);
    if (!priv) return;
    int idx = (int)(rs - priv->rows);
    /* Toggle focus: tapping the already-focused row clears it. */
    _set_row_focused(priv, (priv->focused_row == idx) ? -1 : idx);
}

static void _reset_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_settings_priv_t *priv =
        (lv_settings_priv_t *)lv_event_get_user_data(e);
    if (!priv) return;
    app_settings_reset_all();
    _build_right_panel(priv, priv->active_group);
    LOGI(TAG, "All settings reset to defaults");
}

static void _root_delete_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_settings_priv_t *priv =
        (lv_settings_priv_t *)lv_obj_get_user_data(obj);
    if (priv) {
        lv_free(priv);
        lv_obj_set_user_data(obj, NULL);
    }
}

// ---------------------------------------------------------------------------
// Value formatting
// ---------------------------------------------------------------------------

static void _fmt_value(char *buf, size_t buf_len,
                       const app_setting_def_t *d,
                       app_setting_val_t v)
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
        case APP_SETTING_TYPE_BOOL:
            snprintf(buf, buf_len, "%s", v.b ? "ON" : "OFF");
            break;
    }
}

static void _update_row_value(row_slot_t *rs)
{
    if (!rs->value_lbl) return;
    const app_setting_def_t *d = app_settings_get_def(
        (app_settings_group_t)rs->group, rs->key);
    if (!d) return;
    char buf[40];
    _fmt_value(buf, sizeof(buf), d,
               app_settings_get_val((app_settings_group_t)rs->group, rs->key));
    lv_label_set_text(rs->value_lbl, buf);
}

// ---------------------------------------------------------------------------
// Focus management
// ---------------------------------------------------------------------------

static void _set_row_focused(lv_settings_priv_t *priv, int idx)
{
    if (priv->focused_row >= 0 && priv->focused_row < priv->row_count)
        lv_obj_clear_state(priv->rows[priv->focused_row].row,
                           LV_STATE_FOCUSED);
    priv->focused_row = idx;
    if (idx >= 0 && idx < priv->row_count) {
        lv_obj_add_state(priv->rows[idx].row, LV_STATE_FOCUSED);
        lv_obj_scroll_to_view(priv->rows[idx].row, LV_ANIM_ON);
    }
}

// ---------------------------------------------------------------------------
// Value adjustment (shared by touch +/- and encoder)
// ---------------------------------------------------------------------------

static void _adjust_row_value(lv_settings_priv_t *priv, int idx, int delta)
{
    if (idx < 0 || idx >= priv->row_count) return;
    row_slot_t *rs = &priv->rows[idx];
    const app_setting_def_t *d = app_settings_get_def(
        (app_settings_group_t)rs->group, rs->key);
    if (!d) return;

    app_setting_val_t v = app_settings_get_val(
        (app_settings_group_t)rs->group, rs->key);

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
        case APP_SETTING_TYPE_BOOL:
            if (delta != 0) v.b = !v.b;
            break;
    }

    app_settings_set_val((app_settings_group_t)rs->group, rs->key, v);
    app_settings_save_group((app_settings_group_t)rs->group);
    _update_row_value(rs);

    /* Keep bool toggle in sync */
    if (d->type == APP_SETTING_TYPE_BOOL && rs->inc_btn && rs->value_lbl) {
        lv_label_set_text(rs->value_lbl, v.b ? "ON" : "OFF");
        if (v.b) lv_obj_add_state(rs->inc_btn,   LV_STATE_CHECKED);
        else     lv_obj_clear_state(rs->inc_btn,  LV_STATE_CHECKED);
    }

    LOGD(TAG, "Group %d key %d by %+d", rs->group, rs->key, delta);
    if (priv->changed_cb)
        priv->changed_cb(rs->group, rs->key, priv->changed_user);
}

// ---------------------------------------------------------------------------
// Right-panel builder
// ---------------------------------------------------------------------------

static void _build_right_panel(lv_settings_priv_t *priv, int group)
{
    lv_label_set_text(priv->group_title,
                      app_settings_group_name((app_settings_group_t)group));

    lv_obj_clean(priv->rows_cont);
    memset(priv->rows, 0, sizeof(priv->rows));
    priv->row_count   = 0;
    priv->focused_row = -1;

    for (int g = 0; g < APP_SETTINGS_GROUP__COUNT; g++) {
        if (!priv->group_btns[g]) continue;
        if (g == group) lv_obj_add_state  (priv->group_btns[g], LV_STATE_CHECKED);
        else            lv_obj_clear_state(priv->group_btns[g], LV_STATE_CHECKED);
    }

    int key_count = app_settings_key_count((app_settings_group_t)group);
    if (key_count > LV_SETTINGS_MAX_ROWS) key_count = LV_SETTINGS_MAX_ROWS;

    for (int k = 0; k < key_count; k++) {
        const app_setting_def_t *d = app_settings_get_def(
            (app_settings_group_t)group, k);
        if (!d) continue;

        row_slot_t *rs = &priv->rows[priv->row_count];
        rs->group = group;
        rs->key   = k;

        rs->row = lv_obj_create(priv->rows_cont);
        lv_obj_set_size(rs->row, LV_PCT(100), LV_SETTINGS_ROW_H);
        lv_obj_set_flex_flow(rs->row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(rs->row,
                               LV_FLEX_ALIGN_START,
                               LV_FLEX_ALIGN_CENTER,
                               LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(rs->row, 4, 0);
        lv_obj_set_style_pad_column(rs->row, 6, 0);
        lv_obj_set_style_border_width(rs->row, 0, 0);
        lv_obj_set_style_radius(rs->row, 6, 0);
        lv_obj_set_style_bg_opa(rs->row, LV_OPA_0, 0);
        lv_obj_set_style_bg_opa(rs->row, LV_OPA_30, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(rs->row, lv_color_hex(0x2196F3),
                                   LV_STATE_FOCUSED);
        lv_obj_add_flag(rs->row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(rs->row, _row_clicked_cb, LV_EVENT_CLICKED, rs);

        lv_obj_t *nlbl = lv_label_create(rs->row);
        lv_label_set_text(nlbl, d->name);
        lv_label_set_long_mode(nlbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_flex_grow(nlbl, 1);

        if (d->type == APP_SETTING_TYPE_BOOL) {
            lv_obj_t *tog = lv_btn_create(rs->row);
            lv_obj_set_size(tog, 70, 34);
            lv_obj_add_flag(tog, LV_OBJ_FLAG_CHECKABLE);
            app_setting_val_t v = app_settings_get_val(
                (app_settings_group_t)group, k);
            if (v.b) lv_obj_add_state(tog, LV_STATE_CHECKED);
            rs->value_lbl = lv_label_create(tog);
            lv_label_set_text(rs->value_lbl, v.b ? "ON" : "OFF");
            lv_obj_center(rs->value_lbl);
            rs->inc_btn = tog;
            lv_obj_add_event_cb(tog, _inc_btn_cb, LV_EVENT_CLICKED, rs);
        } else {
            rs->dec_btn = lv_btn_create(rs->row);
            lv_obj_set_size(rs->dec_btn, LV_SETTINGS_BTN_W, 36);
            lv_obj_center(lv_label_create(rs->dec_btn));
            lv_label_set_text(lv_obj_get_child(rs->dec_btn, 0), LV_SYMBOL_LEFT);
            lv_obj_add_event_cb(rs->dec_btn, _dec_btn_cb, LV_EVENT_CLICKED, rs);

            rs->value_lbl = lv_label_create(rs->row);
            lv_obj_set_width(rs->value_lbl, LV_SETTINGS_VALUE_W);
            lv_label_set_long_mode(rs->value_lbl, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_align(rs->value_lbl,
                                         LV_TEXT_ALIGN_CENTER, 0);

            rs->inc_btn = lv_btn_create(rs->row);
            lv_obj_set_size(rs->inc_btn, LV_SETTINGS_BTN_W, 36);
            lv_obj_center(lv_label_create(rs->inc_btn));
            lv_label_set_text(lv_obj_get_child(rs->inc_btn, 0), LV_SYMBOL_RIGHT);
            lv_obj_add_event_cb(rs->inc_btn, _inc_btn_cb, LV_EVENT_CLICKED, rs);

            _update_row_value(rs);
        }

        priv->row_count++;
    }

    LOGD(TAG, "Right panel: group=%d rows=%d", group, priv->row_count);
}

// ---------------------------------------------------------------------------
// Public API — create
// ---------------------------------------------------------------------------

lv_obj_t *lv_settings_create(lv_obj_t *parent)
{
    lv_settings_priv_t *priv =
        (lv_settings_priv_t *)lv_malloc(sizeof(lv_settings_priv_t));
    if (!priv) {
        LOGE(TAG, "Failed to alloc lv_settings_priv_t");
        return NULL;
    }
    memset(priv, 0, sizeof(*priv));
    priv->active_group = 0;
    priv->focused_row  = -1;

    /* ---- Root --------------------------------------------------------- */
    lv_obj_t *root = lv_obj_create(parent);
    priv->root = root;
    lv_obj_set_user_data(root, priv);
    lv_obj_add_event_cb(root, _root_delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(root, 4, 0);
    lv_obj_set_style_pad_column(root, 6, 0);
    lv_obj_set_style_border_width(root, 0, 0);

    /* ---- Left panel — group selector ---------------------------------- */
    priv->left_panel = lv_obj_create(root);
    lv_obj_set_size(priv->left_panel, LV_SETTINGS_LEFT_W, LV_PCT(100));
    lv_obj_set_flex_flow(priv->left_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(priv->left_panel, 6, 0);
    lv_obj_set_style_pad_row(priv->left_panel, 4, 0);
    lv_obj_set_scroll_dir(priv->left_panel, LV_DIR_VER);

    lv_obj_t *hdr = lv_label_create(priv->left_panel);
    lv_label_set_text(hdr, "GROUPS");
    lv_obj_set_style_text_color(hdr, lv_color_hex(0x888888), 0);

    for (int g = 0; g < APP_SETTINGS_GROUP__COUNT; g++) {
        lv_obj_t *btn = lv_btn_create(priv->left_panel);
        lv_obj_set_size(btn, LV_PCT(100), 44);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_t *bl = lv_label_create(btn);
        lv_label_set_text(bl, app_settings_group_name(
                               (app_settings_group_t)g));
        lv_label_set_long_mode(bl, LV_LABEL_LONG_CLIP);
        lv_obj_center(bl);
        lv_obj_add_event_cb(btn, _group_btn_cb, LV_EVENT_CLICKED, priv);
        priv->group_btns[g] = btn;
    }

    /* Thin divider line */
    lv_obj_t *div = lv_obj_create(priv->left_panel);
    lv_obj_set_size(div, LV_PCT(90), 2);
    lv_obj_set_style_bg_color(div, lv_color_hex(0x505050), 0);
    lv_obj_set_style_border_width(div, 0, 0);

    /* Reset-all button */
    lv_obj_t *rst = lv_btn_create(priv->left_panel);
    lv_obj_set_size(rst, LV_PCT(100), 44);
    lv_obj_t *rl = lv_label_create(rst);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH " Defaults");
    lv_obj_center(rl);
    lv_obj_add_event_cb(rst, _reset_btn_cb, LV_EVENT_CLICKED, priv);

    /* ---- Right panel — setting rows ----------------------------------- */
    priv->right_panel = lv_obj_create(root);
    lv_obj_set_flex_grow(priv->right_panel, 1);
    lv_obj_set_height(priv->right_panel, LV_PCT(100));
    lv_obj_set_flex_flow(priv->right_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(priv->right_panel, 6, 0);
    lv_obj_set_style_pad_row(priv->right_panel, 4, 0);
    lv_obj_set_style_border_width(priv->right_panel, 0, 0);

    priv->group_title = lv_label_create(priv->right_panel);
    lv_label_set_text(priv->group_title, "");
    lv_obj_set_style_text_color(priv->group_title,
                                 lv_color_hex(0xbbbbbb), 0);

    priv->rows_cont = lv_obj_create(priv->right_panel);
    lv_obj_set_flex_grow(priv->rows_cont, 1);
    lv_obj_set_width(priv->rows_cont, LV_PCT(100));
    lv_obj_set_flex_flow(priv->rows_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(priv->rows_cont, 2, 0);
    lv_obj_set_style_pad_row(priv->rows_cont, 2, 0);
    lv_obj_set_style_border_width(priv->rows_cont, 0, 0);
    lv_obj_set_scroll_dir(priv->rows_cont, LV_DIR_VER);

    /* Build first group */
    _build_right_panel(priv, 0);
    lv_obj_add_state(priv->group_btns[0], LV_STATE_CHECKED);

    LOGI(TAG, "lv_settings created (%d groups)", APP_SETTINGS_GROUP__COUNT);
    return root;
}

// ---------------------------------------------------------------------------
// Public API — refresh
// ---------------------------------------------------------------------------

void lv_settings_refresh(lv_obj_t *obj)
{
    lv_settings_priv_t *priv = _get_priv(obj);
    if (!priv) return;
    _build_right_panel(priv, priv->active_group);
}

// ---------------------------------------------------------------------------
// Public API — encoder
// ---------------------------------------------------------------------------

void lv_settings_encoder_input(lv_obj_t *obj, int32_t diff)
{
    if (diff == 0) return;
    lv_settings_priv_t *priv = _get_priv(obj);
    if (!priv) return;

    if (priv->focused_row >= 0) {
        /* Clamp: avoid runaway on fast spins */
        int steps = (diff > 0) ? (int)diff : -(int)(-diff);
        if (steps > 10) steps = 10;
        int dir = (diff > 0) ? 1 : -1;
        for (int i = 0; i < steps; i++)
            _adjust_row_value(priv, priv->focused_row, dir);
    } else {
        /* Scroll the rows list */
        int32_t y = lv_obj_get_scroll_y(priv->rows_cont);
        lv_obj_scroll_to_y(priv->rows_cont,
                           y + (int32_t)(diff * LV_SETTINGS_ROW_H / 2),
                           LV_ANIM_OFF);
    }
}

bool lv_settings_has_encoder_focus(lv_obj_t *obj)
{
    lv_settings_priv_t *priv = _get_priv(obj);
    if (!priv) return false;
    return priv->focused_row >= 0;
}

void lv_settings_encoder_navigate(lv_obj_t *obj, int32_t dir)
{
    if (dir == 0) return;
    lv_settings_priv_t *priv = _get_priv(obj);
    if (!priv || priv->row_count == 0) return;

    int next = priv->focused_row + ((dir > 0) ? 1 : -1);
    if (next < 0) next = priv->row_count - 1;
    if (next >= priv->row_count) next = 0;
    _set_row_focused(priv, next);
}

// ---------------------------------------------------------------------------
// Public API — change callback
// ---------------------------------------------------------------------------

void lv_settings_set_changed_cb(lv_obj_t *obj,
                                  lv_settings_changed_cb_t cb,
                                  void *user_data)
{
    lv_settings_priv_t *priv = _get_priv(obj);
    if (!priv) return;
    priv->changed_cb   = cb;
    priv->changed_user = user_data;
}
