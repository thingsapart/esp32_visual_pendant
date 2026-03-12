// probe_gcode.h — Origin-aware MillenniumOS probe G-code dispatch
//
// Bridges the gap between the operator-facing UI (which talks about
// "front", "back", "left", "right" as seen from the machine's front) and
// the actual MillenniumOS G-code parameters (H for G6510.1, N for G6508.1 /
// G6520.1) that are defined in machine coordinates.
//
// The translation is governed by the `machine_origin` field in
// probe_settings_t:
//
//   PROBE_ORIGIN_FRONT_LEFT  — X+ = right, Y+ = back (away from operator).
//                              Most common: origin at the front-left corner.
//   PROBE_ORIGIN_BACK_LEFT   — X+ = right, Y+ = front (toward operator).
//                              Some Grbl / CoreXY machines put origin at back.
//
// Action-ID format (passed from the UI data-binding layer)
// ─────────────────────────────────────────────────────────
//   outer.<surface>   — single outside surface probe (G6510.1)
//   outer.<corner>    — outside corner probe        (G6508.1)
//   outer.boss        — outside circle / boss       (G6501.1)
//   inner.<surface>   — single inside (pocket-wall) probe (G6510.1)
//   inner.<corner>    — inside corner probe         (G6510.1 × 2)
//   inner.bore        — inside circle / bore        (G6500.1)
//   vise.<corner>     — vise corner (Z + outside corner, G6520.1)
//
//   Surfaces:  right | left | back | front | top
//   Corners:   front-left | front-right | back-left | back-right

#ifndef PROBE_GCODE_H
#define PROBE_GCODE_H

#include <stdbool.h>
#include <stddef.h>

#include "config/probe_settings.h"
#include "machine/machine_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Operation types
// ---------------------------------------------------------------------------

typedef enum {
    PROBE_GCODE_OP_BORE,           ///< G6500.1 — inside circle (bore)
    PROBE_GCODE_OP_BOSS,           ///< G6501.1 — outside circle (boss)
    PROBE_GCODE_OP_OUTER_CORNER,   ///< G6508.1 — outside corner;  p1 = N
    PROBE_GCODE_OP_SINGLE_SURFACE, ///< G6510.1 — single surface;  p1 = H
    PROBE_GCODE_OP_INNER_CORNER,   ///< G6510.1 × 2 — inner corner; p1 = H1, p2 = H2
    PROBE_GCODE_OP_VISE_CORNER,    ///< G6520.1 — vise corner (Z + outside corner); p1 = N
} probe_gcode_op_t;

// ---------------------------------------------------------------------------
// G6510.1 H-parameter constants (axis + direction the probe tool moves)
// ---------------------------------------------------------------------------

#define PROBE_H_POS_X  0  ///< Probe moves in +X direction
#define PROBE_H_NEG_X  1  ///< Probe moves in −X direction
#define PROBE_H_POS_Y  2  ///< Probe moves in +Y direction
#define PROBE_H_NEG_Y  3  ///< Probe moves in −Y direction
#define PROBE_H_NEG_Z  4  ///< Probe moves in −Z direction (downward)

// ---------------------------------------------------------------------------
// G6508.1 / G6520.1 N-parameter constants (corner index, machine coords)
//
// Derived from G6508.1.g  dirX/dirY logic:
//   N=0  dirX=−1  dirY=+1 → probe approaches -X (left) and +Y (back) surfaces
//   N=1  dirX=+1  dirY=+1 → probe approaches +X (right) and +Y back surfaces
//   N=2  dirX=−1  dirY=−1 → probe approaches -X (left) and −Y (front) surfaces
//   N=3  dirX=+1  dirY=−1 → probe approaches +X right and −Y front surfaces
//
// "Front" / "back" in the table above use FRONT_LEFT origin conventions
// (Y+ = back).  probe_gcode_corner_n() maps screen-space corners to the
// correct N value for any machine origin.
// ---------------------------------------------------------------------------

#define PROBE_N_MACH_0  0
#define PROBE_N_MACH_1  1
#define PROBE_N_MACH_2  2
#define PROBE_N_MACH_3  3

// ---------------------------------------------------------------------------
// Origin-translation helpers
// ---------------------------------------------------------------------------

/// Translate a screen-space Y-axis surface descriptor to a G6510.1 H value.
///
/// @param is_front  true  = the surface faces the operator (front of machine).
///                  false = the far surface (back of machine).
/// @param is_outer  true  = probe starts outside the surface (outer probe).
///                  false = probe starts inside a pocket (pocket-wall probe).
/// @param origin    Machine origin from probe_settings_t::machine_origin.
/// @return          PROBE_H_POS_Y or PROBE_H_NEG_Y.
int probe_gcode_y_surface_h(bool is_front, bool is_outer,
                             probe_machine_origin_t origin);

/// Translate a screen-space corner descriptor to a G6508.1 / G6520.1 N value.
///
/// @param is_front  true = corner is on the operator-facing side.
/// @param is_left   true = corner is on the −X (left) side.
/// @param origin    Machine origin.
/// @return          0..3 (PROBE_N_MACH_0 .. PROBE_N_MACH_3).
int probe_gcode_corner_n(bool is_front, bool is_left,
                         probe_machine_origin_t origin);

// ---------------------------------------------------------------------------
// Action parsing
// ---------------------------------------------------------------------------

/// Parse a probe action-ID string and fill in the operation parameters.
///
/// The action_id is the part after "probe." in the full action name, e.g.
/// "outer.front-left", "inner.bore", "vise.back-right".
///
/// The p1/p2 values returned are already translated to machine coordinates
/// using @p origin, so they can be passed directly to probe_gcode_build().
///
/// @param action_id  Input action string (not NULL).
/// @param origin     Machine origin for Y-axis / corner translation.
/// @param[out] op    Populated with the operation type.
/// @param[out] p1    Populated with H or N (operation-specific).
/// @param[out] p2    Populated with H2 for INNER_CORNER; 0 otherwise.
/// @param[out] msg   Points to a static confirm-modal message string.
/// @return           true if action_id was recognized, false otherwise.
bool probe_gcode_parse_action(const char *action_id,
                               probe_machine_origin_t origin,
                               probe_gcode_op_t *op,
                               int *p1, int *p2,
                               const char **msg);

// ---------------------------------------------------------------------------
// G-code building
// ---------------------------------------------------------------------------

/// Build the MillenniumOS probe G-code string for the given operation.
///
/// @param op      Operation type (from probe_gcode_parse_action).
/// @param p1      H or N parameter (already in machine coords).
/// @param p2      H2 for INNER_CORNER; ignored otherwise.
/// @param x y z   Current machine position (machine->position[0..2]).
/// @param s       Probe settings (not NULL).
/// @param buf     Output buffer.
/// @param buf_len Size of @p buf.
/// @return        true on success, false if buf is too small or op unknown.
bool probe_gcode_build(probe_gcode_op_t op, int p1, int p2,
                       float x, float y, float z,
                       const probe_settings_t *s,
                       char *buf, size_t buf_len);

// ---------------------------------------------------------------------------
// Convenience dispatch
// ---------------------------------------------------------------------------

/// Read the current machine position, build the G-code, and call
/// machine->probe().  Reads machine_origin from probe_settings_get().
///
/// @param machine    Live machine interface.
/// @param op         Pre-parsed operation (from probe_gcode_parse_action).
/// @param p1         Pre-translated machine-coord H or N parameter.
/// @param p2         Pre-translated H2 for INNER_CORNER; 0 otherwise.
/// @return           true if the G-code was dispatched successfully.
bool probe_gcode_execute_op(machine_interface_t *machine,
                             probe_gcode_op_t op, int p1, int p2);

/// Convenience wrapper: parse action_id → build → dispatch.
/// Reads machine_origin from probe_settings_get().
///
/// @return true if action_id was recognized and the G-code was dispatched.
bool probe_gcode_execute(machine_interface_t *machine, const char *action_id);

#ifdef __cplusplus
}
#endif

#endif // PROBE_GCODE_H
