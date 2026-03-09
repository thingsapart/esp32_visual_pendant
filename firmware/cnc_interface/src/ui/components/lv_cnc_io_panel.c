// lv_cnc_io_panel.c — CNC I/O Control & Monitor panel widget
//
// Categorised display of all machine I/O channels via the generic mc_io_channel_t
// model.  Controller-agnostic: works with RRF, GRBL, FlexiHAL, etc.
//
// Layout for 480×320 (no title bar assumed — parent fills available space):
//
//  [INS] [OUTS] [SENS] [ACT]                         ← 34 px tab bar
//  ┌──────────────┬──────────────────────────────┐    ← flex row, 44 px / row
//  │ Name         │ Indicator / Control          │
//  └──────────────┴──────────────────────────────┘
//
// See lv_cnc_io_panel.h for integration notes.

#define UI_DEBUG_LOCAL_LEVEL D_WARN
#include "debug.h"

#include "lv_cnc_io_panel.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "lv_io_panel";

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

#define IO_PANEL_TAB_H       34
#define IO_PANEL_ROW_H       44
#define IO_PANEL_NAME_W     148  ///< Fixed name-column width (px)
#define IO_PANEL_LED_FONT  (&lv_font_montserrat_14)
#define IO_PANEL_NAME_FONT (&lv_font_montserrat_14)
#define IO_PANEL_VAL_FONT  (&lv_font_montserrat_14)
#define IO_PANEL_MAX_ROWS   64

// ---------------------------------------------------------------------------
// Row types (aligned with the 4-category model)
// ---------------------------------------------------------------------------

typedef enum {
  IO_ROW_DIGITAL_IN,   ///< Digital input  — indicator bullet + state text
  IO_ROW_DIGITAL_OUT,  ///< Digital output — indicator bullet + toggle button
  IO_ROW_ANALOG_IN,    ///< Analog input   — read-only value + unit + health dot
  IO_ROW_ANALOG_OUT,   ///< Analog output  — value/setpoint + slider + toggle
} io_row_type_t;

typedef struct {
  lv_obj_t      *row;           ///< Container object
  io_row_type_t  type;
  uint8_t        source_index;  ///< original source index (for G-code)
  mc_io_role_t   role;          ///< role (FAN, HEATER, …) for command dispatch
  uint8_t        io_channel_idx;///< index into machine->io_channels[]
  // Value widgets (not all used for each type)
  lv_obj_t      *indicator;     ///< Bullet-label coloured LED
  lv_obj_t      *value_lbl;     ///< Text value
  lv_obj_t      *slider;        ///< Analog-out PWM slider
  lv_obj_t      *toggle;        ///< On/Off toggle button
  lv_obj_t      *setpt_lbl;     ///< "→ 60°C" for heaters
} io_row_t;

// ---------------------------------------------------------------------------
// Widget private state
// ---------------------------------------------------------------------------

typedef struct {
  lv_obj_t            *root;
  lv_obj_t            *tab_btns[IO_CAT__COUNT];
  lv_obj_t            *list_cont;      ///< Scrollable content container
  machine_interface_t *machine;
  io_category_t        active_cat;
  io_row_t             rows[IO_PANEL_MAX_ROWS];
  int                  row_count;
  size_t               last_total_channels; ///< priv->machine->num_io_channels at last rebuild
} lv_cnc_io_panel_priv_t;

// ---------------------------------------------------------------------------
// Private accessor (walk up parent chain to find root)
// ---------------------------------------------------------------------------

static lv_cnc_io_panel_priv_t *_get_priv(lv_obj_t *obj) {
  while (obj) {
    lv_cnc_io_panel_priv_t *p =
        (lv_cnc_io_panel_priv_t *)lv_obj_get_user_data(obj);
    if (p && p->root == obj) return p;
    obj = lv_obj_get_parent(obj);
  }
  return NULL;
}

// ---------------------------------------------------------------------------
// Category → (direction, signal) mapping
// ---------------------------------------------------------------------------

static bool _cat_matches(const mc_io_channel_t *ch, io_category_t cat) {
  switch (cat) {
    case IO_CAT_INPUTS:    return ch->direction == MC_IO_DIR_INPUT  && ch->signal == MC_IO_SIG_DIGITAL;
    case IO_CAT_OUTPUTS:   return ch->direction == MC_IO_DIR_OUTPUT && ch->signal == MC_IO_SIG_DIGITAL;
    case IO_CAT_SENSORS:   return ch->direction == MC_IO_DIR_INPUT  && ch->signal == MC_IO_SIG_ANALOG;
    case IO_CAT_ACTUATORS: return ch->direction == MC_IO_DIR_OUTPUT && ch->signal == MC_IO_SIG_ANALOG;
    default:               return false;
  }
}

// ---------------------------------------------------------------------------
// Colour helpers
// ---------------------------------------------------------------------------

static lv_color_t _indicator_color_active(bool active) {
  return active ? lv_color_hex(0xFF2200) : lv_color_hex(0x00BB44);
}

static void _set_indicator(lv_obj_t *lbl, bool active) {
  if (!lbl) return;
  lv_obj_set_style_text_color(lbl, _indicator_color_active(active), 0);
}

static void _set_indicator_unknown(lv_obj_t *lbl) {
  if (!lbl) return;
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x555555), 0);
}

// ---------------------------------------------------------------------------
// Basic row container factory
// ---------------------------------------------------------------------------

static lv_obj_t *_make_row(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_PCT(100), IO_PANEL_ROW_H);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                         LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(row, 4, 0);
  lv_obj_set_style_pad_column(row, 6, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_radius(row, 4, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  return row;
}

static lv_obj_t *_make_name_label(lv_obj_t *parent, const char *text) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_obj_set_style_text_font(lbl, IO_PANEL_NAME_FONT, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xCCCCCC), 0);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(lbl, IO_PANEL_NAME_W);
  lv_label_set_text(lbl, text ? text : "?");
  return lbl;
}

static lv_obj_t *_make_indicator_label(lv_obj_t *parent) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, LV_SYMBOL_BULLET);
  lv_obj_set_style_text_font(lbl, IO_PANEL_LED_FONT, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x555555), 0);
  lv_obj_set_style_pad_right(lbl, 4, 0);
  return lbl;
}

static lv_obj_t *_make_value_label(lv_obj_t *parent, int32_t w) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_obj_set_style_text_font(lbl, IO_PANEL_VAL_FONT, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xEEEEEE), 0);
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
  if (w > 0) lv_obj_set_width(lbl, w);
  else        lv_obj_set_flex_grow(lbl, 1);
  return lbl;
}

static lv_obj_t *_make_toggle_btn(lv_obj_t *parent, bool initially_on,
                                   lv_event_cb_t cb, void *ud) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_size(btn, 60, 30);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_style_radius(btn, 15, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x444444), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x0077FF), LV_STATE_CHECKED);
  if (initially_on) lv_obj_add_state(btn, LV_STATE_CHECKED);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_label_set_text(lbl, initially_on ? "ON" : "OFF");
  lv_obj_center(lbl);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_VALUE_CHANGED, ud);
  return btn;
}

// ---------------------------------------------------------------------------
// Event callbacks
// ---------------------------------------------------------------------------

static void _tab_btn_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t *btn = lv_event_get_current_target(e);
  lv_cnc_io_panel_priv_t *priv =
      (lv_cnc_io_panel_priv_t *)lv_event_get_user_data(e);
  if (!priv) return;
  for (int i = 0; i < IO_CAT__COUNT; i++) {
    if (priv->tab_btns[i] == btn) {
      lv_cnc_io_panel_set_category(priv->root, (io_category_t)i);
      break;
    }
  }
}

static void _analog_out_slider_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  io_row_t *r = (io_row_t *)lv_event_get_user_data(e);
  if (!r) return;
  lv_cnc_io_panel_priv_t *priv = _get_priv(r->row);
  if (!priv) return;

  int pct = (int)lv_slider_get_value(r->slider);
  bool on = (pct > 0);

  // Sync toggle visual
  if (r->toggle) {
    if (on) lv_obj_add_state(r->toggle, LV_STATE_CHECKED);
    else    lv_obj_clear_state(r->toggle, LV_STATE_CHECKED);
    lv_obj_t *tlbl = lv_obj_get_child(r->toggle, 0);
    if (tlbl) lv_label_set_text(tlbl, on ? "ON" : "OFF");
  }

  // Update displayed value
  if (r->value_lbl) {
    char buf[16];
    if (r->role == MC_IO_ROLE_FAN)
      snprintf(buf, sizeof(buf), "%d%%", pct);
    else
      snprintf(buf, sizeof(buf), "%.0f", (double)pct);
    lv_label_set_text(r->value_lbl, buf);
  }

  machine_interface_set_io_channel(priv->machine, r->io_channel_idx,
                                    (float)pct, on);
}

static void _analog_out_toggle_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  io_row_t *r = (io_row_t *)lv_event_get_user_data(e);
  if (!r) return;
  lv_cnc_io_panel_priv_t *priv = _get_priv(r->row);
  if (!priv) return;

  bool on = lv_obj_has_state(r->toggle, LV_STATE_CHECKED);
  lv_obj_t *tlbl = lv_obj_get_child(r->toggle, 0);
  if (tlbl) lv_label_set_text(tlbl, on ? "ON" : "OFF");

  // When turning on restore slider to something sensible
  int pct = r->slider ? (int)lv_slider_get_value(r->slider) : 100;
  if (on && pct == 0) {
    pct = 100;
    if (r->slider) lv_slider_set_value(r->slider, pct, LV_ANIM_OFF);
    if (r->value_lbl) {
      char buf[16];
      if (r->role == MC_IO_ROLE_FAN) snprintf(buf, sizeof(buf), "%d%%", pct);
      else                           snprintf(buf, sizeof(buf), "%.0f", (double)pct);
      lv_label_set_text(r->value_lbl, buf);
    }
  }
  if (!on && r->slider) {
    lv_slider_set_value(r->slider, 0, LV_ANIM_OFF);
    if (r->value_lbl) lv_label_set_text(r->value_lbl,
        (r->role == MC_IO_ROLE_FAN) ? "0%" : "0");
  }

  machine_interface_set_io_channel(priv->machine, r->io_channel_idx,
                                    (float)pct, on);
}

static void _digital_out_toggle_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  io_row_t *r = (io_row_t *)lv_event_get_user_data(e);
  if (!r) return;
  lv_cnc_io_panel_priv_t *priv = _get_priv(r->row);
  if (!priv) return;

  bool on = lv_obj_has_state(r->toggle, LV_STATE_CHECKED);
  lv_obj_t *tlbl = lv_obj_get_child(r->toggle, 0);
  if (tlbl) lv_label_set_text(tlbl, on ? "ON" : "OFF");

  _set_indicator(r->indicator, on);
  machine_interface_set_io_channel(priv->machine, r->io_channel_idx,
                                    0.0f, on);
}

static void _root_delete_cb(lv_event_t *e) {
  lv_obj_t *root = (lv_obj_t *)lv_event_get_current_target(e);
  lv_cnc_io_panel_priv_t *priv =
      (lv_cnc_io_panel_priv_t *)lv_obj_get_user_data(root);
  if (priv) {
    lv_free(priv);
    lv_obj_set_user_data(root, NULL);
  }
}

// ---------------------------------------------------------------------------
// Category string helpers
// ---------------------------------------------------------------------------

static const char *_cat_label(io_category_t cat) {
  switch (cat) {
    case IO_CAT_INPUTS:    return "INS";
    case IO_CAT_OUTPUTS:   return "OUTS";
    case IO_CAT_SENSORS:   return "SENS";
    case IO_CAT_ACTUATORS: return "ACT";
    default:               return "?";
  }
}

static lv_color_t _cat_accent(io_category_t cat) {
  switch (cat) {
    case IO_CAT_INPUTS:    return lv_color_hex(0x44AAFF);  // blue   – digital in
    case IO_CAT_OUTPUTS:   return lv_color_hex(0x22BB44);  // green  – digital out
    case IO_CAT_SENSORS:   return lv_color_hex(0x44BB66);  // teal   – analog in
    case IO_CAT_ACTUATORS: return lv_color_hex(0xFF8800);  // orange – analog out
    default:               return lv_color_white();
  }
}

// ---------------------------------------------------------------------------
// Individual row builders
// ---------------------------------------------------------------------------

/// Digital input: [● indicator] [state text]
static void _build_digital_in_row(lv_cnc_io_panel_priv_t *priv,
                                   const mc_io_channel_t *ch, size_t idx) {
  if (priv->row_count >= IO_PANEL_MAX_ROWS) return;
  io_row_t *r = &priv->rows[priv->row_count];
  r->type         = IO_ROW_DIGITAL_IN;
  r->source_index = ch->source_index;
  r->role         = ch->role;

  r->row = _make_row(priv->list_cont);
  _make_name_label(r->row, ch->name ? ch->name : "Input");

  r->indicator = _make_indicator_label(r->row);
  if (ch->health == MC_IO_HEALTH_UNKNOWN)
    _set_indicator_unknown(r->indicator);
  else
    _set_indicator(r->indicator, ch->active);

  // State text depends on role
  r->value_lbl = _make_value_label(r->row, 0);
  const char *state_str = "?";
  if (ch->health == MC_IO_HEALTH_FAULT)
    state_str = "FAULT";
  else if (ch->health == MC_IO_HEALTH_UNKNOWN)
    state_str = "UNKN";
  else if (ch->role == MC_IO_ROLE_PROBE)
    state_str = ch->active ? "TRIG" : "OK";
  else if (ch->role == MC_IO_ROLE_ESTOP)
    state_str = ch->active ? "ESTOP!" : "OK";
  else if (ch->role == MC_IO_ROLE_DOOR)
    state_str = ch->active ? "OPEN" : "CLOSED";
  else if (ch->role == MC_IO_ROLE_LIMIT)
    state_str = ch->active ? "TRIG" : "OK";
  else
    state_str = ch->active ? "HI" : "LO";
  lv_label_set_text(r->value_lbl, state_str);

  (void)idx;
  priv->row_count++;
}

/// Digital output: [● indicator] [ON/OFF toggle]
static void _build_digital_out_row(lv_cnc_io_panel_priv_t *priv,
                                    const mc_io_channel_t *ch, size_t idx) {
  if (priv->row_count >= IO_PANEL_MAX_ROWS) return;
  io_row_t *r = &priv->rows[priv->row_count];
  r->type             = IO_ROW_DIGITAL_OUT;
  r->source_index     = ch->source_index;
  r->role             = ch->role;
  r->io_channel_idx   = (uint8_t)idx;

  r->row = _make_row(priv->list_cont);
  _make_name_label(r->row, ch->name ? ch->name : "Output");

  r->indicator = _make_indicator_label(r->row);
  _set_indicator(r->indicator, ch->setpoint_bool);

  r->toggle = _make_toggle_btn(r->row, ch->setpoint_bool,
                                _digital_out_toggle_cb, r);
  lv_obj_set_style_bg_color(r->toggle, lv_color_hex(0x22BB44),
                              LV_STATE_CHECKED);

  priv->row_count++;
}

/// Analog input: [● health dot] [value + unit]
static void _build_analog_in_row(lv_cnc_io_panel_priv_t *priv,
                                  const mc_io_channel_t *ch, size_t idx) {
  if (priv->row_count >= IO_PANEL_MAX_ROWS) return;
  io_row_t *r = &priv->rows[priv->row_count];
  r->type         = IO_ROW_ANALOG_IN;
  r->source_index = ch->source_index;
  r->role         = ch->role;

  r->row = _make_row(priv->list_cont);
  _make_name_label(r->row, ch->name ? ch->name : "Sensor");

  // Health dot: green=ok, red=fault, gray=unknown
  r->indicator = _make_indicator_label(r->row);
  if (ch->health == MC_IO_HEALTH_FAULT)
    lv_obj_set_style_text_color(r->indicator, lv_color_hex(0xFF2200), 0);
  else if (ch->health == MC_IO_HEALTH_UNKNOWN)
    _set_indicator_unknown(r->indicator);
  else
    lv_obj_set_style_text_color(r->indicator, lv_color_hex(0x44BB66), 0);

  // "23.5 °C" or "0.97" etc.
  r->value_lbl = _make_value_label(r->row, 90);
  char vbuf[24];
  if (ch->health == MC_IO_HEALTH_FAULT)
    snprintf(vbuf, sizeof(vbuf), "---");
  else if (ch->unit && ch->unit[0])
    snprintf(vbuf, sizeof(vbuf), "%.1f %s", (double)ch->value, ch->unit);
  else
    snprintf(vbuf, sizeof(vbuf), "%.3f", (double)ch->value);
  lv_label_set_text(r->value_lbl, vbuf);

  (void)idx;
  priv->row_count++;
}

/// Analog output: [name] [actual/setpoint] [slider] [ON/OFF toggle]
/// Heaters show "actual → setpoint" text instead of a raw slider value.
static void _build_analog_out_row(lv_cnc_io_panel_priv_t *priv,
                                   const mc_io_channel_t *ch, size_t idx) {
  if (priv->row_count >= IO_PANEL_MAX_ROWS) return;
  io_row_t *r = &priv->rows[priv->row_count];
  r->type             = IO_ROW_ANALOG_OUT;
  r->source_index     = ch->source_index;
  r->role             = ch->role;
  r->io_channel_idx   = (uint8_t)idx;

  // Slightly taller row to accommodate slider
  r->row = lv_obj_create(priv->list_cont);
  lv_obj_set_size(r->row, LV_PCT(100), IO_PANEL_ROW_H + 4);
  lv_obj_set_flex_flow(r->row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r->row, LV_FLEX_ALIGN_START,
                         LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(r->row, 4, 0);
  lv_obj_set_style_pad_column(r->row, 6, 0);
  lv_obj_set_style_border_width(r->row, 0, 0);
  lv_obj_set_style_radius(r->row, 4, 0);
  lv_obj_set_style_bg_opa(r->row, LV_OPA_0, 0);
  lv_obj_clear_flag(r->row, LV_OBJ_FLAG_SCROLLABLE);

  _make_name_label(r->row, ch->name ? ch->name : "Output");

  if (ch->role == MC_IO_ROLE_HEATER) {
    // Heater: indicator dot + "actual → setpoint" label + toggle
    r->indicator = _make_indicator_label(r->row);
    bool active = (ch->health != MC_IO_HEALTH_UNKNOWN && ch->setpoint > 0.f);
    lv_obj_set_style_text_color(r->indicator,
        active ? lv_color_hex(0xFF8800) : lv_color_hex(0x555555), 0);

    r->value_lbl = _make_value_label(r->row, 140);
    char tbuf[40];
    bool valid = (ch->value > -270.f);
    if (ch->unit && ch->unit[0]) {
      if (valid)
        snprintf(tbuf, sizeof(tbuf), "%.1f %s \xE2\x86\x92 %.0f %s",
                 (double)ch->value, ch->unit, (double)ch->setpoint, ch->unit);
      else
        snprintf(tbuf, sizeof(tbuf), "--- %s \xE2\x86\x92 %.0f %s",
                 ch->unit, (double)ch->setpoint, ch->unit);
    } else {
      if (valid)
        snprintf(tbuf, sizeof(tbuf), "%.1f \xE2\x86\x92 %.0f",
                 (double)ch->value, (double)ch->setpoint);
      else
        snprintf(tbuf, sizeof(tbuf), "--- \xE2\x86\x92 %.0f", (double)ch->setpoint);
    }
    lv_label_set_text(r->value_lbl, tbuf);

    bool on = (ch->setpoint > 0.f);
    r->toggle = _make_toggle_btn(r->row, on, _analog_out_toggle_cb, r);
    lv_obj_set_style_bg_color(r->toggle, lv_color_hex(0xFF6600),
                               LV_STATE_CHECKED);

  } else {
    // Fan / spindle / generic: slider + value label + toggle
    float range = (ch->max_value > ch->min_value) ? (ch->max_value - ch->min_value) : 1.f;
    int slider_val = (int)(((ch->setpoint - ch->min_value) / range) * 100.f);
    if (slider_val < 0)   slider_val = 0;
    if (slider_val > 100) slider_val = 100;

    r->slider = lv_slider_create(r->row);
    lv_obj_set_size(r->slider, 140, 18);
    lv_slider_set_range(r->slider, 0, 100);
    lv_slider_set_value(r->slider, slider_val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(r->slider, _cat_accent(IO_CAT_ACTUATORS),
                               LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(r->slider, _cat_accent(IO_CAT_ACTUATORS),
                               LV_PART_KNOB);
    lv_obj_add_event_cb(r->slider, _analog_out_slider_cb,
                         LV_EVENT_VALUE_CHANGED, r);

    r->value_lbl = lv_label_create(r->row);
    lv_obj_set_style_text_font(r->value_lbl, IO_PANEL_VAL_FONT, 0);
    lv_obj_set_style_text_color(r->value_lbl, lv_color_hex(0xEEEEEE), 0);
    lv_obj_set_width(r->value_lbl, 42);
    lv_obj_set_style_text_align(r->value_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    char vbuf[14];
    if (ch->unit && ch->unit[0])
      snprintf(vbuf, sizeof(vbuf), "%d%s", slider_val, ch->unit);
    else
      snprintf(vbuf, sizeof(vbuf), "%d%%", slider_val);
    lv_label_set_text(r->value_lbl, vbuf);

    bool on = (ch->setpoint > ch->min_value + 0.01f);
    r->toggle = _make_toggle_btn(r->row, on, _analog_out_toggle_cb, r);
    // Thermostatic / AUTO mode: health==UNKNOWN signals controller-managed
    if (ch->health == MC_IO_HEALTH_UNKNOWN) {
      lv_obj_add_state(r->toggle, LV_STATE_DISABLED);
      lv_obj_t *tl = lv_obj_get_child(r->toggle, 0);
      if (tl) lv_label_set_text(tl, "AUTO");
    }
  }

  priv->row_count++;
}

/// Empty-state row shown when a category has no items
static void _build_empty_row(lv_cnc_io_panel_priv_t *priv, const char *msg) {
  lv_obj_t *row = lv_obj_create(priv->list_cont);
  lv_obj_set_size(row, LV_PCT(100), IO_PANEL_ROW_H);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *lbl = lv_label_create(row);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x555555), 0);
  lv_obj_set_style_text_font(lbl, IO_PANEL_NAME_FONT, 0);
  lv_label_set_text(lbl, msg);
  lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);
}

// ---------------------------------------------------------------------------
// Build all rows for the active category
// ---------------------------------------------------------------------------

static void _build_category(lv_cnc_io_panel_priv_t *priv) {
  lv_obj_clean(priv->list_cont);
  memset(priv->rows, 0, sizeof(priv->rows));
  priv->row_count = 0;
  priv->last_total_channels = priv->machine ? priv->machine->num_io_channels : 0;
  LOGI(TAG, "priv->last_total_channels %d", (int)priv->last_total_channels);

  // Update tab button highlight
  for (int i = 0; i < IO_CAT__COUNT; i++) {
    if (!priv->tab_btns[i]) continue;
    if (i == (int)priv->active_cat)
      lv_obj_add_state(priv->tab_btns[i], LV_STATE_CHECKED);
    else
      lv_obj_clear_state(priv->tab_btns[i], LV_STATE_CHECKED);
  }

  machine_interface_t *m = priv->machine;
  LOGI(TAG, "_build_category: cat=%d, machine=%p, num_io_channels=%d",
       (int)priv->active_cat, (void*)m, (int)(m ? m->num_io_channels : 0));
  if (!m || m->num_io_channels == 0) {
    _build_empty_row(priv, "No I/O data available");
    return;
  }

  // Walk generic channel array and build appropriate row per match
  bool any = false;
  for (size_t i = 0; i < m->num_io_channels; i++) {
    const mc_io_channel_t *ch = &m->io_channels[i];
    bool match = _cat_matches(ch, priv->active_cat);
        LOGI(TAG, "  ch[%u] dir=%d sig=%d role=%d name='%s' -> match=%d",
          (unsigned)i, (int)ch->direction, (int)ch->signal, (int)ch->role,
          ch->name ? ch->name : "(null)", (int)match);
    if (!match) continue;
    any = true;
    switch (priv->active_cat) {
      case IO_CAT_INPUTS:    _build_digital_in_row(priv, ch, i);  break;
      case IO_CAT_OUTPUTS:   _build_digital_out_row(priv, ch, i); break;
      case IO_CAT_SENSORS:   _build_analog_in_row(priv, ch, i);   break;
      case IO_CAT_ACTUATORS: _build_analog_out_row(priv, ch, i);  break;
      default: break;
    }
  }

  if (!any) {
    static const char *empty_msg[IO_CAT__COUNT] = {
      "No digital inputs reported",    // IO_CAT_INPUTS
      "No digital outputs configured", // IO_CAT_OUTPUTS
      "No analog sensors available",   // IO_CAT_SENSORS
      "No analog outputs configured",  // IO_CAT_ACTUATORS
    };
    _build_empty_row(priv, empty_msg[priv->active_cat]);
  }

  LOGD(TAG, "Category %d rebuilt: %d rows", (int)priv->active_cat, priv->row_count);
}

// ---------------------------------------------------------------------------
// Row value updaters (fast-path: update labels/sliders without rebuilding)
// ---------------------------------------------------------------------------

/// Find channel matching (direction, signal, role, source_index), or NULL.
static const mc_io_channel_t *_find_channel(machine_interface_t *m,
                                             mc_io_direction_t dir,
                                             mc_io_signal_t sig,
                                             mc_io_role_t role,
                                             uint8_t src_idx) {
  for (size_t i = 0; i < m->num_io_channels; i++) {
    const mc_io_channel_t *ch = &m->io_channels[i];
    if (ch->direction   != dir)     continue;
    if (ch->signal      != sig)     continue;
    if (ch->role        != role)    continue;
    if (ch->source_index != src_idx) continue;
    return ch;
  }
  return NULL;
}

static void _refresh_digital_in_row(io_row_t *r, machine_interface_t *m) {
  const mc_io_channel_t *ch = _find_channel(m,
      MC_IO_DIR_INPUT, MC_IO_SIG_DIGITAL, r->role, r->source_index);
  if (!ch) return;

  if (r->indicator) {
    if (ch->health == MC_IO_HEALTH_UNKNOWN)
      _set_indicator_unknown(r->indicator);
    else
      _set_indicator(r->indicator, ch->active);
  }
  if (r->value_lbl) {
    const char *s = "?";
    if      (ch->health == MC_IO_HEALTH_FAULT)     s = "FAULT";
    else if (ch->health == MC_IO_HEALTH_UNKNOWN)   s = "UNKN";
    else if (ch->role == MC_IO_ROLE_PROBE)          s = ch->active ? "TRIG" : "OK";
    else if (ch->role == MC_IO_ROLE_ESTOP)          s = ch->active ? "ESTOP!" : "OK";
    else if (ch->role == MC_IO_ROLE_DOOR)           s = ch->active ? "OPEN" : "CLOSED";
    else if (ch->role == MC_IO_ROLE_LIMIT)          s = ch->active ? "TRIG" : "OK";
    else                                            s = ch->active ? "HI" : "LO";
    lv_label_set_text(r->value_lbl, s);
  }
}

static void _refresh_digital_out_row(io_row_t *r, machine_interface_t *m) {
  const mc_io_channel_t *ch = _find_channel(m,
      MC_IO_DIR_OUTPUT, MC_IO_SIG_DIGITAL, r->role, r->source_index);
  if (!ch) return;

  if (r->indicator) _set_indicator(r->indicator, ch->setpoint_bool);
  if (r->toggle) {
    if (ch->setpoint_bool) lv_obj_add_state(r->toggle, LV_STATE_CHECKED);
    else                   lv_obj_clear_state(r->toggle, LV_STATE_CHECKED);
    lv_obj_t *lbl = lv_obj_get_child(r->toggle, 0);
    if (lbl) lv_label_set_text(lbl, ch->setpoint_bool ? "ON" : "OFF");
  }
}

static void _refresh_analog_in_row(io_row_t *r, machine_interface_t *m) {
  const mc_io_channel_t *ch = _find_channel(m,
      MC_IO_DIR_INPUT, MC_IO_SIG_ANALOG, r->role, r->source_index);
  if (!ch) return;

  if (r->indicator) {
    if (ch->health == MC_IO_HEALTH_FAULT)
      lv_obj_set_style_text_color(r->indicator, lv_color_hex(0xFF2200), 0);
    else if (ch->health == MC_IO_HEALTH_UNKNOWN)
      _set_indicator_unknown(r->indicator);
    else
      lv_obj_set_style_text_color(r->indicator, lv_color_hex(0x44BB66), 0);
  }
  if (r->value_lbl) {
    char vbuf[24];
    if (ch->health == MC_IO_HEALTH_FAULT)
      snprintf(vbuf, sizeof(vbuf), "---");
    else if (ch->unit && ch->unit[0])
      snprintf(vbuf, sizeof(vbuf), "%.1f %s", (double)ch->value, ch->unit);
    else
      snprintf(vbuf, sizeof(vbuf), "%.3f", (double)ch->value);
    lv_label_set_text(r->value_lbl, vbuf);
  }
}

static void _refresh_analog_out_row(io_row_t *r, machine_interface_t *m) {
  const mc_io_channel_t *ch = _find_channel(m,
      MC_IO_DIR_OUTPUT, MC_IO_SIG_ANALOG, r->role, r->source_index);
  if (!ch) return;

  if (r->role == MC_IO_ROLE_HEATER) {
    bool active = (ch->health != MC_IO_HEALTH_UNKNOWN && ch->setpoint > 0.f);
    if (r->indicator) {
      lv_obj_set_style_text_color(r->indicator,
          active ? lv_color_hex(0xFF8800) : lv_color_hex(0x555555), 0);
    }
    if (r->value_lbl) {
      char tbuf[40];
      bool valid = (ch->value > -270.f);
      if (ch->unit && ch->unit[0]) {
        if (valid)
          snprintf(tbuf, sizeof(tbuf), "%.1f %s \xE2\x86\x92 %.0f %s",
                   (double)ch->value, ch->unit, (double)ch->setpoint, ch->unit);
        else
          snprintf(tbuf, sizeof(tbuf), "--- %s \xE2\x86\x92 %.0f %s",
                   ch->unit, (double)ch->setpoint, ch->unit);
      } else {
        if (valid)
          snprintf(tbuf, sizeof(tbuf), "%.1f \xE2\x86\x92 %.0f",
                   (double)ch->value, (double)ch->setpoint);
        else
          snprintf(tbuf, sizeof(tbuf), "--- \xE2\x86\x92 %.0f", (double)ch->setpoint);
      }
      lv_label_set_text(r->value_lbl, tbuf);
    }
    if (r->toggle) {
      bool on = (ch->setpoint > 0.f);
      if (on) lv_obj_add_state(r->toggle, LV_STATE_CHECKED);
      else    lv_obj_clear_state(r->toggle, LV_STATE_CHECKED);
      lv_obj_t *lbl = lv_obj_get_child(r->toggle, 0);
      if (lbl) lv_label_set_text(lbl, on ? "ON" : "OFF");
    }
  } else {
    float range = (ch->max_value > ch->min_value) ? (ch->max_value - ch->min_value) : 1.f;
    int pct = (int)(((ch->setpoint - ch->min_value) / range) * 100.f);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    if (r->slider) lv_slider_set_value(r->slider, pct, LV_ANIM_OFF);
    if (r->value_lbl) {
      char vbuf[14];
      if (ch->unit && ch->unit[0])
        snprintf(vbuf, sizeof(vbuf), "%d%s", pct, ch->unit);
      else
        snprintf(vbuf, sizeof(vbuf), "%d%%", pct);
      lv_label_set_text(r->value_lbl, vbuf);
    }
    if (r->toggle) {
      bool on = (pct > 0);
      if (on) lv_obj_add_state(r->toggle, LV_STATE_CHECKED);
      else    lv_obj_clear_state(r->toggle, LV_STATE_CHECKED);
      lv_obj_t *tlbl = lv_obj_get_child(r->toggle, 0);
      if (tlbl) {
        if (ch->health == MC_IO_HEALTH_UNKNOWN) lv_label_set_text(tlbl, "AUTO");
        else                                    lv_label_set_text(tlbl, on ? "ON" : "OFF");
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Helper: count category items to detect structural changes
// ---------------------------------------------------------------------------

static int _cat_item_count(machine_interface_t *m, io_category_t cat) {
  if (!m) return 0;
  int count = 0;
  for (size_t i = 0; i < m->num_io_channels; i++) {
    if (_cat_matches(&m->io_channels[i], cat)) count++;
  }
  return count;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void lv_cnc_io_panel_set_category(lv_obj_t *panel, io_category_t cat) {
  lv_cnc_io_panel_priv_t *priv = _get_priv(panel);
  if (!priv || cat >= IO_CAT__COUNT) return;
  if ((int)cat == (int)priv->active_cat) return;
  priv->active_cat = cat;
  _build_category(priv);
}

void lv_cnc_io_panel_refresh(lv_obj_t *panel, interface_t *interface) {
  lv_cnc_io_panel_priv_t *priv = _get_priv(panel);
  if (!priv) { LOGW(TAG, "!! priv is NULL"); return; }
  bool first_machine = (!priv->machine && interface && interface->machine);
  if (first_machine) { priv->machine = interface->machine; }
  if (!priv->machine) { 
    LOGW(TAG, "Refreshing but no machine found: itf %p", interface); 
    return; 
  }

  LOGI(TAG, "Refreshing IO PANEL: row count %d", priv->row_count); 

  int current_count = _cat_item_count(priv->machine, priv->active_cat);
  size_t total_channels = priv->machine->num_io_channels;
  bool total_changed = (total_channels != priv->last_total_channels);
  LOGI(TAG, "refresh decision: first_machine=%d total=%d last_total=%d total_changed=%d row_count=%d current_count=%d",
       (int)first_machine, (int)total_channels, (int)priv->last_total_channels,
       (int)total_changed, priv->row_count, current_count);
  // Rebuild when:
  //  - machine just assigned (first_machine): last_total_channels is 0 but we
  //    now have a machine, covers the empty-placeholder case from create-time
  //  - total channel count changed (new data arrived or channels cleared)
  //  - category item count changed (structural change in active tab)
  if (first_machine || total_changed || priv->row_count != current_count) {
    LOGI(TAG, "Refreshing category");
    _build_category(priv);
    return;
  }

  // Update values in-place
  for (int i = 0; i < priv->row_count; i++) {
    io_row_t *r = &priv->rows[i];
    if (!r->row) { LOGI(TAG, "No row for idx %d", i); continue; }
    LOGI(TAG, "Refreshing row for idx %d -> type %d", i, r->type);
    switch (r->type) {
      case IO_ROW_DIGITAL_IN:  _refresh_digital_in_row(r,  priv->machine); break;
      case IO_ROW_DIGITAL_OUT: _refresh_digital_out_row(r, priv->machine); break;
      case IO_ROW_ANALOG_IN:   _refresh_analog_in_row(r,   priv->machine); break;
      case IO_ROW_ANALOG_OUT:  _refresh_analog_out_row(r,  priv->machine); break;
    }
  }
}

void lv_cnc_io_panel_destroy(lv_obj_t *panel) {
  if (panel && lv_obj_is_valid(panel)) {
    lv_obj_del(panel);
  }
}

lv_obj_t *lv_cnc_io_panel_create(lv_obj_t *parent) {
  lv_cnc_io_panel_priv_t *priv =
      (lv_cnc_io_panel_priv_t *)lv_malloc(sizeof(lv_cnc_io_panel_priv_t));
  if (!priv) {
    LOGE(TAG, "OOM allocating io panel private state");
    return NULL;
  }
  memset(priv, 0, sizeof(*priv));
  priv->machine             = NULL;
  priv->active_cat          = IO_CAT_INPUTS;
  priv->last_total_channels = 0;

  // ---- Root container ------------------------------------------------
  lv_obj_t *root = lv_obj_create(parent);
  priv->root = root;
  lv_obj_set_user_data(root, priv);
  lv_obj_add_event_cb(root, _root_delete_cb, LV_EVENT_DELETE, NULL);
  lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(root, 0, 0);
  lv_obj_set_style_pad_row(root, 0, 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  // ---- Tab bar -------------------------------------------------------
  lv_obj_t *tab_bar = lv_obj_create(root);
  lv_obj_set_size(tab_bar, LV_PCT(100), IO_PANEL_TAB_H);
  lv_obj_set_flex_flow(tab_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(tab_bar, LV_FLEX_ALIGN_START,
                         LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(tab_bar, 2, 0);
  lv_obj_set_style_pad_column(tab_bar, 2, 0);
  lv_obj_set_style_border_width(tab_bar, 0, 0);
  lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x1A1A2E), 0);
  lv_obj_set_style_bg_opa(tab_bar, LV_OPA_COVER, 0);
  lv_obj_clear_flag(tab_bar, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < IO_CAT__COUNT; i++) {
    lv_obj_t *btn = lv_button_create(tab_bar);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_height(btn, IO_PANEL_TAB_H - 4);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A3E), 0);
    lv_obj_set_style_bg_color(btn, _cat_accent((io_category_t)i),
                               LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), LV_STATE_CHECKED);
    lv_label_set_text(lbl, _cat_label((io_category_t)i));
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, _tab_btn_cb, LV_EVENT_CLICKED, priv);
    priv->tab_btns[i] = btn;
  }

  // Mark first tab as active
  lv_obj_add_state(priv->tab_btns[0], LV_STATE_CHECKED);

  // ---- Thin accent line below tab bar --------------------------------
  lv_obj_t *accent_line = lv_obj_create(root);
  lv_obj_set_size(accent_line, LV_PCT(100), 2);
  lv_obj_set_style_bg_color(accent_line, _cat_accent(priv->active_cat), 0);
  lv_obj_set_style_bg_opa(accent_line, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(accent_line, 0, 0);
  (void)accent_line;

  // ---- Scrollable list container ------------------------------------
  priv->list_cont = lv_obj_create(root);
  lv_obj_set_flex_grow(priv->list_cont, 1);
  lv_obj_set_width(priv->list_cont, LV_PCT(100));
  lv_obj_set_flex_flow(priv->list_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(priv->list_cont, 4, 0);
  lv_obj_set_style_pad_row(priv->list_cont, 2, 0);
  lv_obj_set_style_border_width(priv->list_cont, 0, 0);
  lv_obj_set_style_bg_opa(priv->list_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_scroll_dir(priv->list_cont, LV_DIR_VER);

  // ---- Build initial category ----------------------------------------
  _build_category(priv);

  LOGI(TAG, "I/O panel created");
  return root;
}
