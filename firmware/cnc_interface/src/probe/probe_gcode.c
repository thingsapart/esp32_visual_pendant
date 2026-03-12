// probe_gcode.c — Origin-aware MillenniumOS probe G-code dispatch
//
// See probe_gcode.h for the full API contract.
//
// Y-axis origin translation
// ─────────────────────────
// G6510.1 H parameter encodes the direction the probe tool _moves_:
//   H=0 (POS_X): tool moves +X   H=1 (NEG_X): tool moves −X
//   H=2 (POS_Y): tool moves +Y   H=3 (NEG_Y): tool moves −Y
//   H=4 (NEG_Z): tool moves −Z (downward)
//
// "Front" for the operator == −Y in FRONT_LEFT origin, +Y in BACK_LEFT.
//
// For an OUTER surface the probe starts outside and moves toward the surface:
//   FRONT_LEFT + front: probe at −Y side → moves +Y → H_POS_Y
//   FRONT_LEFT + back:  probe at +Y side → moves −Y → H_NEG_Y
//   BACK_LEFT  + front: probe at +Y side → moves −Y → H_NEG_Y
//   BACK_LEFT  + back:  probe at −Y side → moves +Y → H_POS_Y
//
// For an INNER surface the probe is inside the pocket and moves toward the wall:
//   (Opposite: front wall is at −Y for FRONT_LEFT, approach from inside → −Y)
//   FRONT_LEFT + front: → H_NEG_Y     FRONT_LEFT + back: → H_POS_Y
//   BACK_LEFT  + front: → H_POS_Y     BACK_LEFT  + back: → H_NEG_Y
//
// G6508.1 / G6520.1 N-parameter corner index (machine coords):
//   N=0  dirX=−1, dirY=+1  (MillenniumOS "front-left"  in FRONT_LEFT origin)
//   N=1  dirX=+1, dirY=+1  (MillenniumOS "front-right" in FRONT_LEFT origin)
//   N=2  dirX=−1, dirY=−1  (MillenniumOS "back-left"   in FRONT_LEFT origin)
//   N=3  dirX=+1, dirY=−1  (MillenniumOS "back-right"  in FRONT_LEFT origin)
//
// For BACK_LEFT origin "front" in screen space = "back" in MillenniumOS coords,
// so the N mapping for front/back corners is inverted.

#include "probe_gcode.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"

static const char *TAG = "probe_gcode";

// ---------------------------------------------------------------------------
// Y-axis origin translation helpers
// ---------------------------------------------------------------------------

int probe_gcode_y_surface_h(bool is_front, bool is_outer,
                             probe_machine_origin_t origin)
{
    // In FRONT_LEFT origin the front surface is at the −Y extent of the
    // workpiece, so "front in screen space" ≡ "−Y in machine space".
    bool front_is_neg_y = (origin == PROBE_ORIGIN_FRONT_LEFT);

    // Does the named surface sit at the −Y extent in machine coords?
    bool surface_neg_y = (is_front == front_is_neg_y);

    if (is_outer) {
        // Outer: probe starts outside the surface (on the far side) and moves
        // toward it.  If surface is at −Y, probe is further in −Y and moves +Y.
        return surface_neg_y ? PROBE_H_POS_Y : PROBE_H_NEG_Y;
    } else {
        // Inner / pocket-wall: probe is inside the pocket and moves toward the
        // wall.  If the wall is at −Y the probe moves −Y.
        return surface_neg_y ? PROBE_H_NEG_Y : PROBE_H_POS_Y;
    }
}

int probe_gcode_corner_n(bool is_front, bool is_left,
                         probe_machine_origin_t origin)
{
    // MillenniumOS defines the N index with FRONT_LEFT assumptions:
    //   N=0  front-left    N=1  front-right
    //   N=2  back-left     N=3  back-right
    // For BACK_LEFT origin the operator's "front" = MillenniumOS "back",
    // so we flip the front/back sense before looking up the table.
    bool mos_front = (origin == PROBE_ORIGIN_FRONT_LEFT) ? is_front : !is_front;

    if (mos_front && is_left)  return PROBE_N_MACH_0;  // front-left
    if (mos_front && !is_left) return PROBE_N_MACH_1;  // front-right
    if (!mos_front && is_left) return PROBE_N_MACH_2;  // back-left
    return PROBE_N_MACH_3;                              // back-right
}

// ---------------------------------------------------------------------------
// Action parsing
// ---------------------------------------------------------------------------

bool probe_gcode_parse_action(const char *action_id,
                               probe_machine_origin_t origin,
                               probe_gcode_op_t *op,
                               int *p1, int *p2,
                               const char **msg)
{
    *p1 = 0;
    *p2 = 0;

    // -----------------------------------------------------------------------
    // Outer surface probes (single edge)
    // -----------------------------------------------------------------------
    if (strcmp(action_id, "outer.right") == 0) {
        *op  = PROBE_GCODE_OP_SINGLE_SURFACE;
        *p1  = PROBE_H_NEG_X;
        *msg = "Jog outside the right (+X) surface, then press OK.";
        return true;
    }
    if (strcmp(action_id, "outer.left") == 0) {
        *op  = PROBE_GCODE_OP_SINGLE_SURFACE;
        *p1  = PROBE_H_POS_X;
        *msg = "Jog outside the left (−X) surface, then press OK.";
        return true;
    }
    if (strcmp(action_id, "outer.front") == 0) {
        *op  = PROBE_GCODE_OP_SINGLE_SURFACE;
        *p1  = probe_gcode_y_surface_h(true, true, origin);
        *msg = "Jog outside the front surface, then press OK.";
        return true;
    }
    if (strcmp(action_id, "outer.back") == 0) {
        *op  = PROBE_GCODE_OP_SINGLE_SURFACE;
        *p1  = probe_gcode_y_surface_h(false, true, origin);
        *msg = "Jog outside the back surface, then press OK.";
        return true;
    }
    if (strcmp(action_id, "outer.top") == 0) {
        *op  = PROBE_GCODE_OP_SINGLE_SURFACE;
        *p1  = PROBE_H_NEG_Z;
        *msg = "Jog above the top surface, then press OK.";
        return true;
    }

    // -----------------------------------------------------------------------
    // Outer corners (G6508.1)
    // -----------------------------------------------------------------------
    if (strcmp(action_id, "outer.front-left") == 0) {
        *op  = PROBE_GCODE_OP_OUTER_CORNER;
        *p1  = probe_gcode_corner_n(true, true,   origin);
        *msg = "Jog above the front-left corner, then press OK.";
        return true;
    }
    if (strcmp(action_id, "outer.front-right") == 0) {
        *op  = PROBE_GCODE_OP_OUTER_CORNER;
        *p1  = probe_gcode_corner_n(true, false,  origin);
        *msg = "Jog above the front-right corner, then press OK.";
        return true;
    }
    if (strcmp(action_id, "outer.back-left") == 0) {
        *op  = PROBE_GCODE_OP_OUTER_CORNER;
        *p1  = probe_gcode_corner_n(false, true,  origin);
        *msg = "Jog above the back-left corner, then press OK.";
        return true;
    }
    if (strcmp(action_id, "outer.back-right") == 0) {
        *op  = PROBE_GCODE_OP_OUTER_CORNER;
        *p1  = probe_gcode_corner_n(false, false, origin);
        *msg = "Jog above the back-right corner, then press OK.";
        return true;
    }

    // -----------------------------------------------------------------------
    // Outer circle (boss, G6501.1)
    // -----------------------------------------------------------------------
    if (strcmp(action_id, "outer.boss") == 0) {
        *op  = PROBE_GCODE_OP_BOSS;
        *msg = "Jog above the center of the boss, then press OK.";
        return true;
    }

    // -----------------------------------------------------------------------
    // Inner surface probes (pocket-wall, single edge)
    // -----------------------------------------------------------------------
    if (strcmp(action_id, "inner.right") == 0) {
        *op  = PROBE_GCODE_OP_SINGLE_SURFACE;
        *p1  = PROBE_H_POS_X;
        *msg = "Jog inside the pocket near the right (+X) wall, then press OK.";
        return true;
    }
    if (strcmp(action_id, "inner.left") == 0) {
        *op  = PROBE_GCODE_OP_SINGLE_SURFACE;
        *p1  = PROBE_H_NEG_X;
        *msg = "Jog inside the pocket near the left (−X) wall, then press OK.";
        return true;
    }
    if (strcmp(action_id, "inner.front") == 0) {
        *op  = PROBE_GCODE_OP_SINGLE_SURFACE;
        *p1  = probe_gcode_y_surface_h(true, false, origin);
        *msg = "Jog inside the pocket near the front wall, then press OK.";
        return true;
    }
    if (strcmp(action_id, "inner.back") == 0) {
        *op  = PROBE_GCODE_OP_SINGLE_SURFACE;
        *p1  = probe_gcode_y_surface_h(false, false, origin);
        *msg = "Jog inside the pocket near the back wall, then press OK.";
        return true;
    }

    // -----------------------------------------------------------------------
    // Inner corners (two G6510.1 calls)
    // -----------------------------------------------------------------------
    if (strcmp(action_id, "inner.front-left") == 0) {
        *op = PROBE_GCODE_OP_INNER_CORNER;
        *p1 = PROBE_H_NEG_X;
        *p2 = probe_gcode_y_surface_h(true, false, origin);
        *msg = "Jog inside the pocket near the front-left corner, then press OK.";
        return true;
    }
    if (strcmp(action_id, "inner.front-right") == 0) {
        *op = PROBE_GCODE_OP_INNER_CORNER;
        *p1 = PROBE_H_POS_X;
        *p2 = probe_gcode_y_surface_h(true, false, origin);
        *msg = "Jog inside the pocket near the front-right corner, then press OK.";
        return true;
    }
    if (strcmp(action_id, "inner.back-left") == 0) {
        *op = PROBE_GCODE_OP_INNER_CORNER;
        *p1 = PROBE_H_NEG_X;
        *p2 = probe_gcode_y_surface_h(false, false, origin);
        *msg = "Jog inside the pocket near the back-left corner, then press OK.";
        return true;
    }
    if (strcmp(action_id, "inner.back-right") == 0) {
        *op = PROBE_GCODE_OP_INNER_CORNER;
        *p1 = PROBE_H_POS_X;
        *p2 = probe_gcode_y_surface_h(false, false, origin);
        *msg = "Jog inside the pocket near the back-right corner, then press OK.";
        return true;
    }

    // -----------------------------------------------------------------------
    // Inner circle (bore, G6500.1)
    // -----------------------------------------------------------------------
    if (strcmp(action_id, "inner.bore") == 0) {
        *op  = PROBE_GCODE_OP_BORE;
        *msg = "Jog above the center of the bore, then press OK.";
        return true;
    }

    // -----------------------------------------------------------------------
    // Vise corner (G6520.1 — Z probe + outside corner)
    // -----------------------------------------------------------------------
    if (strcmp(action_id, "vise.front-left") == 0) {
        *op  = PROBE_GCODE_OP_VISE_CORNER;
        *p1  = probe_gcode_corner_n(true, true,   origin);
        *msg = "Jog above the front-left corner of the workpiece.\n"
               "G6520.1 will probe Z first, then the two corner surfaces.\n"
               "Press OK when ready.";
        return true;
    }
    if (strcmp(action_id, "vise.front-right") == 0) {
        *op  = PROBE_GCODE_OP_VISE_CORNER;
        *p1  = probe_gcode_corner_n(true, false,  origin);
        *msg = "Jog above the front-right corner of the workpiece.\n"
               "G6520.1 will probe Z first, then the two corner surfaces.\n"
               "Press OK when ready.";
        return true;
    }
    if (strcmp(action_id, "vise.back-left") == 0) {
        *op  = PROBE_GCODE_OP_VISE_CORNER;
        *p1  = probe_gcode_corner_n(false, true,  origin);
        *msg = "Jog above the back-left corner of the workpiece.\n"
               "G6520.1 will probe Z first, then the two corner surfaces.\n"
               "Press OK when ready.";
        return true;
    }
    if (strcmp(action_id, "vise.back-right") == 0) {
        *op  = PROBE_GCODE_OP_VISE_CORNER;
        *p1  = probe_gcode_corner_n(false, false, origin);
        *msg = "Jog above the back-right corner of the workpiece.\n"
               "G6520.1 will probe Z first, then the two corner surfaces.\n"
               "Press OK when ready.";
        return true;
    }

    return false; // unknown action
}

// ---------------------------------------------------------------------------
// G-code building
// ---------------------------------------------------------------------------

bool probe_gcode_build(probe_gcode_op_t op, int p1, int p2,
                       float x, float y, float z,
                       const probe_settings_t *s,
                       char *buf, size_t buf_len)
{
    int n;

    switch (op) {
        case PROBE_GCODE_OP_BORE:
            // G6500.1: bore / inside circle.
            // H = bore diameter (2 × width setting).
            // L = current Z (safe travel height above opening).
            // Z = L − xy_probe_depth (Z at which sidewall probing occurs).
            n = snprintf(buf, buf_len,
                "T T{global.mosPTID}\n"
                "G6500.1 J{%.4f} K{%.4f} L{%.4f} Z{%.4f} H{%.4f} O{%.4f}",
                x, y, z, z - s->xy_probe_depth,
                2.0f * s->width, s->overtravel);
            break;

        case PROBE_GCODE_OP_BOSS:
            // G6501.1: boss / outside circle.
            // T = clearance (start probe this far outside expected boss edge).
            n = snprintf(buf, buf_len,
                "T T{global.mosPTID}\n"
                "G6501.1 J{%.4f} K{%.4f} L{%.4f} Z{%.4f} H{%.4f} T{%.4f} O{%.4f}",
                x, y, z, z - s->xy_probe_depth,
                2.0f * s->width, s->clearance, s->overtravel);
            break;

        case PROBE_GCODE_OP_OUTER_CORNER:
            // G6508.1: outside corner.  p1 = corner index N (machine coords).
            // H = X surface length, I = Y surface length.
            // Q = 0 (full, 2 pts/surface) or 1 (quick, 1 pt/surface).
            // Z = sidewall probe depth (absolute).
            n = snprintf(buf, buf_len,
                "T T{global.mosPTID}\n"
                "G6508.1 Q{%d} H{%.4f} I{%.4f} N{%d} T{%.4f} O{%.4f}"
                " J{%.4f} K{%.4f} L{%.4f} Z{%.4f}",
                s->quick_mode ? 1 : 0,
                s->width, s->height, p1,
                s->clearance, s->overtravel,
                x, y, z, z - s->xy_probe_depth);
            break;

        case PROBE_GCODE_OP_SINGLE_SURFACE: {
            // G6510.1: single axis probe.  p1 = H (PROBE_H_* constant).
            // For Z probes (H=4):   L = current Z, I = max_z_depth.
            // For X/Y probes:       L = current Z − xy_probe_depth, I = width.
            float probe_L = (p1 == PROBE_H_NEG_Z) ? z : (z - s->xy_probe_depth);
            float probe_I = (p1 == PROBE_H_NEG_Z) ? s->max_z_depth : s->width;
            n = snprintf(buf, buf_len,
                "T T{global.mosPTID}\n"
                "G6510.1 H{%d} I{%.4f} O{%.4f} J{%.4f} K{%.4f} L{%.4f}",
                p1, probe_I, s->overtravel, x, y, probe_L);
            break;
        }

        case PROBE_GCODE_OP_INNER_CORNER:
            // Two sequential G6510.1 calls to probe the two pocket walls.
            // p1 = H for the X-direction wall, p2 = H for the Y-direction wall.
            // G6509/G6509.1 are not yet implemented in MillenniumOS.
            // L = current Z − xy_probe_depth (descend before sidewall probing).
            n = snprintf(buf, buf_len,
                "T T{global.mosPTID}\n"
                "G6510.1 H{%d} I{%.4f} O{%.4f} J{%.4f} K{%.4f} L{%.4f}\n"
                "G6510.1 H{%d} I{%.4f} O{%.4f} J{%.4f} K{%.4f} L{%.4f}",
                p1, s->width,  s->overtravel, x, y, z - s->xy_probe_depth,
                p2, s->height, s->overtravel, x, y, z - s->xy_probe_depth);
            break;

        case PROBE_GCODE_OP_VISE_CORNER:
            // G6520.1: vise corner probe — probes Z first, then the two corner
            // surfaces, and sets all three WCS axes in one operation.
            // p1 = corner index N (machine coords, origin-translated).
            //
            // Parameters:
            //   J K L  = current start position (operator jogged above corner)
            //   N      = corner index
            //   Q      = 0 (full) or 1 (quick)
            //   H      = X surface length (width setting)
            //   I      = Y surface length (height setting)
            //   T      = surface clearance (also used internally as max Z probe depth)
            //   O      = overtravel
            //   P      = depth below probed Z surface to probe the sidewalls
            n = snprintf(buf, buf_len,
                "T T{global.mosPTID}\n"
                "G6520.1 Q{%d} H{%.4f} I{%.4f} N{%d}"
                " T{%.4f} O{%.4f}"
                " J{%.4f} K{%.4f} L{%.4f}"
                " P{%.4f}",
                s->quick_mode ? 1 : 0,
                s->width, s->height, p1,
                s->clearance, s->overtravel,
                x, y, z,
                s->xy_probe_depth);
            break;

        default:
            LOGW(TAG, "probe_gcode_build: unknown op %d", (int)op);
            return false;
    }

    if (n < 0 || (size_t)n >= buf_len) {
        LOGE(TAG, "probe_gcode_build: buffer too small (need %d, have %u)", n + 1, (unsigned)buf_len);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Convenience dispatch
// ---------------------------------------------------------------------------

bool probe_gcode_execute_op(machine_interface_t *machine,
                             probe_gcode_op_t op, int p1, int p2)
{
    const probe_settings_t *s = probe_settings_get();
    float x = machine->position[0];
    float y = machine->position[1];
    float z = machine->position[2];
    char gcode[512];

    if (!probe_gcode_build(op, p1, p2, x, y, z, s, gcode, sizeof(gcode))) {
        return false;
    }
    machine->probe(machine, gcode);
    return true;
}

bool probe_gcode_execute(machine_interface_t *machine, const char *action_id)
{
    const probe_settings_t *s = probe_settings_get();
    probe_gcode_op_t op;
    int p1 = 0, p2 = 0;
    const char *msg = NULL;

    if (!probe_gcode_parse_action(action_id, s->machine_origin,
                                   &op, &p1, &p2, &msg)) {
        LOGW(TAG, "probe_gcode_execute: unknown action '%s'", action_id);
        return false;
    }
    return probe_gcode_execute_op(machine, op, p1, p2);
}
