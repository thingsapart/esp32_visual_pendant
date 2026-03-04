/**
 * @file gcode_sim.c
 * @brief G-code state machine simulator.
 *
 * Simulates G-code execution on a virtual machine, producing toolpath
 * segments.  Designed for minimal memory use on ESP32-S3.
 */

#include "gcode_sim.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * Segment ring buffer
 * ═══════════════════════════════════════════════════════════════════════════ */

void gc_segbuf_init(gc_segbuf_t *buf) {
    memset(buf, 0, sizeof(*buf));
}

gc_segment_t *gc_segbuf_push(gc_segbuf_t *buf) {
    gc_segment_t *seg = &buf->segs[buf->head];
    memset(seg, 0, sizeof(*seg));
    seg->seq = buf->seq_counter++;

    buf->head = (buf->head + 1) % GCVIEW_MAX_SEGMENTS;
    if (buf->count < GCVIEW_MAX_SEGMENTS)
        buf->count++;

    return seg;
}

const gc_segment_t *gc_segbuf_get(const gc_segbuf_t *buf, uint16_t i) {
    if (i >= buf->count) return NULL;
    uint16_t start;
    if (buf->count < GCVIEW_MAX_SEGMENTS)
        start = 0;
    else
        start = buf->head;  /* oldest is at head (just overwritten) */
    uint16_t idx = (start + i) % GCVIEW_MAX_SEGMENTS;
    return &buf->segs[idx];
}

void gc_segbuf_clear(gc_segbuf_t *buf) {
    buf->head  = 0;
    buf->count = 0;
    /* Keep seq_counter and done_seq for continuity */
}

void gc_segbuf_mark_done(gc_segbuf_t *buf) {
    if (buf->count > 0)
        buf->done_seq = buf->seq_counter - 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Simulator
 * ═══════════════════════════════════════════════════════════════════════════ */

void gc_sim_init(gc_sim_state_t *sim) {
    memset(sim, 0, sizeof(*sim));
    sim->motion_mode = GCMOVE_RAPID;    /* G0 */
    sim->plane       = GCPLANE_XY;      /* G17 */
    sim->dist_mode   = GCDIST_ABSOLUTE; /* G90 */
    sim->unit_mode   = GCUNIT_MM;       /* G21 */
    sim->feed        = 0.0f;
    sim->wcs         = 0;               /* G54 */
    sim->tool        = 0;
}

void gc_sim_seed(gc_sim_state_t *sim,
                 const float *pos,
                 const float *wcs_pos,
                 float feed,
                 int wcs)
{
    if (pos) {
        sim->pos.x = pos[0];
        sim->pos.y = pos[1];
        sim->pos.z = pos[2];
    }
    /* wcs_pos currently unused in sim — sim works in machine coords.
     * Could be relevant for WCS-relative preview in the future. */
    (void)wcs_pos;

    if (feed > 0.0f) sim->feed = feed;
    if (wcs >= 0)    sim->wcs  = wcs;
}

/* ─── internal: unit scaling ───────────────────────────────────────────── */

/** Convert a value from current units to mm. */
static inline float to_mm(const gc_sim_state_t *sim, float v) {
    return (sim->unit_mode == GCUNIT_INCH) ? v * 25.4f : v;
}

/* ─── internal: emit a linear / rapid segment ─────────────────────────── */

static int emit_linear(gc_sim_state_t *sim,
                       const gc_parsed_line_t *ln,
                       gc_segbuf_t *buf)
{
    gc_vec3_t target = sim->pos;
    bool moved = false;

    if (gc_has_word(ln, GCW_X)) {
        float xv = to_mm(sim, ln->x);
        target.x = (sim->dist_mode == GCDIST_RELATIVE) ? sim->pos.x + xv : xv;
        moved = true;
    }
    if (gc_has_word(ln, GCW_Y)) {
        float yv = to_mm(sim, ln->y);
        target.y = (sim->dist_mode == GCDIST_RELATIVE) ? sim->pos.y + yv : yv;
        moved = true;
    }
    if (gc_has_word(ln, GCW_Z)) {
        float zv = to_mm(sim, ln->z);
        target.z = (sim->dist_mode == GCDIST_RELATIVE) ? sim->pos.z + zv : zv;
        moved = true;
    }

    if (!moved) return 0;

    if (buf) {
        gc_segment_t *seg = gc_segbuf_push(buf);
        seg->from  = sim->pos;
        seg->to    = target;
        seg->type  = sim->motion_mode;  /* G0 or G1 */
        seg->plane = sim->plane;
        seg->feed  = (sim->motion_mode == GCMOVE_RAPID) ? 0.0f : sim->feed;
    }

    sim->pos = target;
    return 1;
}

/* ─── internal: emit an arc segment ────────────────────────────────────── */

static int emit_arc(gc_sim_state_t *sim,
                    const gc_parsed_line_t *ln,
                    gc_segbuf_t *buf,
                    bool clockwise)
{
    gc_vec3_t target = sim->pos;
    gc_vec3_t ijk    = {0.0f, 0.0f, 0.0f};

    /* Target coordinates (same logic as linear) */
    if (gc_has_word(ln, GCW_X)) {
        float xv = to_mm(sim, ln->x);
        target.x = (sim->dist_mode == GCDIST_RELATIVE) ? sim->pos.x + xv : xv;
    }
    if (gc_has_word(ln, GCW_Y)) {
        float yv = to_mm(sim, ln->y);
        target.y = (sim->dist_mode == GCDIST_RELATIVE) ? sim->pos.y + yv : yv;
    }
    if (gc_has_word(ln, GCW_Z)) {
        float zv = to_mm(sim, ln->z);
        target.z = (sim->dist_mode == GCDIST_RELATIVE) ? sim->pos.z + zv : zv;
    }

    /* Arc centre offsets (always relative to start) */
    if (gc_has_word(ln, GCW_I)) ijk.x = to_mm(sim, ln->i);
    if (gc_has_word(ln, GCW_J)) ijk.y = to_mm(sim, ln->j);
    if (gc_has_word(ln, GCW_K)) ijk.z = to_mm(sim, ln->k);

    /* Handle R-form arcs: convert R to IJK */
    if (gc_has_word(ln, GCW_R) && !gc_has_word(ln, GCW_I) &&
        !gc_has_word(ln, GCW_J) && !gc_has_word(ln, GCW_K)) {
        float radius = to_mm(sim, ln->r);
        float dx, dy;

        /* Select the two in-plane axes */
        switch (sim->plane) {
        case GCPLANE_XY: dx = target.x - sim->pos.x; dy = target.y - sim->pos.y; break;
        case GCPLANE_XZ: dx = target.x - sim->pos.x; dy = target.z - sim->pos.z; break;
        case GCPLANE_YZ: dx = target.y - sim->pos.y; dy = target.z - sim->pos.z; break;
        }

        float d2   = dx * dx + dy * dy;
        float d    = sqrtf(d2);
        if (d < 1e-6f) goto bail;

        float h2 = radius * radius - d2 / 4.0f;
        if (h2 < 0.0f) h2 = 0.0f;
        float h = sqrtf(h2);

        /* For R<0, pick the large arc; else small arc */
        if ((radius < 0.0f) != clockwise) h = -h;
        if (radius < 0.0f) radius = -radius;

        float mx = dx / 2.0f;
        float my = dy / 2.0f;
        float ox = -dy / d * h;
        float oy =  dx / d * h;

        switch (sim->plane) {
        case GCPLANE_XY: ijk.x = mx + ox; ijk.y = my + oy; break;
        case GCPLANE_XZ: ijk.x = mx + ox; ijk.z = my + oy; break;
        case GCPLANE_YZ: ijk.y = mx + ox; ijk.z = my + oy; break;
        }
    }

    if (buf) {
        gc_segment_t *seg = gc_segbuf_push(buf);
        seg->from  = sim->pos;
        seg->to    = target;
        seg->ijk   = ijk;
        seg->type  = clockwise ? GCMOVE_ARC_CW : GCMOVE_ARC_CCW;
        seg->plane = sim->plane;
        seg->feed  = sim->feed;
    }

bail:
    sim->pos = target;
    return 1;
}

/* ─── internal: process modal G-codes ──────────────────────────────────── */

/** Handle G-codes that change modal state. Returns true if the G word
 *  was consumed (no further action needed). */
static bool process_g_modal(gc_sim_state_t *sim, float g_val) {
    int g = (int)(g_val + 0.5f);  /* round to nearest int */

    switch (g) {
    /* Motion modes (sticky) */
    case 0:  sim->motion_mode = GCMOVE_RAPID;   return true;
    case 1:  sim->motion_mode = GCMOVE_LINEAR;  return true;
    case 2:  sim->motion_mode = GCMOVE_ARC_CW;  return true;
    case 3:  sim->motion_mode = GCMOVE_ARC_CCW; return true;

    /* Plane selection */
    case 17: sim->plane = GCPLANE_XY; return true;
    case 18: sim->plane = GCPLANE_XZ; return true;
    case 19: sim->plane = GCPLANE_YZ; return true;

    /* Distance mode */
    case 90: sim->dist_mode = GCDIST_ABSOLUTE; return true;
    case 91: sim->dist_mode = GCDIST_RELATIVE; return true;

    /* Units */
    case 20: sim->unit_mode = GCUNIT_INCH; return true;
    case 21: sim->unit_mode = GCUNIT_MM;   return true;

    /* WCS selection */
    case 54: sim->wcs = 0; return true;
    case 55: sim->wcs = 1; return true;
    case 56: sim->wcs = 2; return true;
    case 57: sim->wcs = 3; return true;
    case 58: sim->wcs = 4; return true;
    case 59: sim->wcs = 5; return true;

    /* G28 — return to home (simulate as move to 0,0,0) */
    case 28: return false;  /* let it pass through — axes in line will move */

    /* G10 — set coordinate system data (ignore for sim) */
    case 10: return true;

    /* G92 — set position (we could handle this but skip for now) */
    case 92: return true;

    /* G4 — dwell (no motion) */
    case 4: return true;

    /* G53 — move in machine coords (single-block, not sticky) */
    case 53: return false;

    /* Canned cycles — not yet supported, consume silently */
    case 80: case 81: case 82: case 83: case 84: case 85:
    case 86: case 87: case 88: case 89:
        return true;

    default:
        return true;  /* unknown G-code — ignore */
    }
}

/* ─── internal: process M-codes ────────────────────────────────────────── */

static void process_m_code(gc_sim_state_t *sim, float m_val) {
    int m = (int)(m_val + 0.5f);
    switch (m) {
    case 3:  sim->spindle_on = true;  sim->spindle_cw = true;  break;
    case 4:  sim->spindle_on = true;  sim->spindle_cw = false; break;
    case 5:  sim->spindle_on = false; break;
    case 7:  sim->coolant_mist  = true;  break;
    case 8:  sim->coolant_flood = true;  break;
    case 9:  sim->coolant_mist = false; sim->coolant_flood = false; break;
    case 0:  /* program stop */  break;
    case 1:  /* optional stop */ break;
    case 2:  /* program end */   break;
    case 30: /* program end */   break;
    default: break;
    }
}

/* ─── public API ───────────────────────────────────────────────────────── */

int gc_sim_process(gc_sim_state_t *sim,
                   const gc_parsed_line_t *line,
                   gc_segbuf_t *buf)
{
    if (!sim || !line) return 0;

    /* Feed rate update (sticky) */
    if (gc_has_word(line, GCW_F)) {
        sim->feed = to_mm(sim, line->f);
    }

    /* Tool change */
    if (gc_has_word(line, GCW_T)) {
        sim->tool = line->t;
    }

    /* M-code */
    if (gc_has_word(line, GCW_M)) {
        process_m_code(sim, line->m);
    }

    /* G-code — handle modal changes first */
    bool g_consumed = false;
    if (gc_has_word(line, GCW_G)) {
        g_consumed = process_g_modal(sim, line->g);
    }

    /* Motion — either explicit via G0/1/2/3 in this line or implicit
     * (sticky motion mode) if axis words are present. */
    bool has_motion_words = gc_has_word(line, GCW_X | GCW_Y | GCW_Z);
    bool has_arc_words    = gc_has_word(line, GCW_I | GCW_J | GCW_K | GCW_R);

    /* Explicit G0/G1/G2/G3 on this line sets motion mode already via
     * process_g_modal.  If G word present but not a motion code, don't
     * do implicit motion. */
    bool explicit_motion = false;
    if (gc_has_word(line, GCW_G)) {
        int g = (int)(line->g + 0.5f);
        explicit_motion = (g >= 0 && g <= 3);
    }

    if (has_motion_words || (has_arc_words && (explicit_motion || !gc_has_word(line, GCW_G)))) {
        switch (sim->motion_mode) {
        case GCMOVE_RAPID:
        case GCMOVE_LINEAR:
            return emit_linear(sim, line, buf);

        case GCMOVE_ARC_CW:
            return emit_arc(sim, line, buf, true);

        case GCMOVE_ARC_CCW:
            return emit_arc(sim, line, buf, false);
        }
    }

    return 0;
}

/* ─── callback context for gc_sim_run_text ─────────────────────────────── */

typedef struct {
    gc_sim_state_t *sim;
    gc_segbuf_t    *buf;
    int             count;
} sim_run_ctx_t;

static bool sim_run_cb(const gc_parsed_line_t *line, void *ctx) {
    sim_run_ctx_t *c = (sim_run_ctx_t *)ctx;
    c->count += gc_sim_process(c->sim, line, c->buf);
    return true;  /* keep going */
}

int gc_sim_run_text(gc_sim_state_t *sim,
                    const char *gcode_text,
                    gc_segbuf_t *buf)
{
    sim_run_ctx_t ctx = { .sim = sim, .buf = buf, .count = 0 };
    gcode_parse_block(gcode_text, sim_run_cb, &ctx);
    return ctx.count;
}
