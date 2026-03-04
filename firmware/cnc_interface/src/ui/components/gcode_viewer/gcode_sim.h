/**
 * @file gcode_sim.h
 * @brief G-code state machine simulator.
 *
 * Takes parsed G-code lines and "executes" them in a virtual machine,
 * producing toolpath segments for rendering.  The simulator tracks all
 * modal state (motion mode, distance mode, plane, units, WCS, etc.)
 * and can be initialised from the real machine state so visualisation
 * "projects forward" from the current physical position.
 *
 * Memory: the simulator itself is ~80 bytes.  Output goes to a
 * caller-owned gc_segbuf_t ring buffer.
 */
#ifndef GCODE_SIM_H
#define GCODE_SIM_H

#include "gcode_types.h"
#include "gcode_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── segment ring buffer ──────────────────────────────────────────────── */

/** Initialise (clear) a segment buffer. */
void gc_segbuf_init(gc_segbuf_t *buf);

/** Push a new segment.  If the buffer is full the oldest entry is
 *  overwritten (ring semantics).  Returns a pointer to the new slot. */
gc_segment_t *gc_segbuf_push(gc_segbuf_t *buf);

/** Get segment at logical index `i` (0 = oldest).
 *  Returns NULL if `i >= buf->count`. */
const gc_segment_t *gc_segbuf_get(const gc_segbuf_t *buf, uint16_t i);

/** Clear all segments. */
void gc_segbuf_clear(gc_segbuf_t *buf);

/** Mark all segments up to and including the current head as "done". */
void gc_segbuf_mark_done(gc_segbuf_t *buf);

/* ─── simulator ────────────────────────────────────────────────────────── */

/** Initialise the simulator state to default G-code power-on defaults.
 *  Position is set to (0,0,0), G90, G21, G17, G0 mode. */
void gc_sim_init(gc_sim_state_t *sim);

/** Seed the simulator from a live machine_interface_t so that the
 *  visualisation starts from the current physical state.
 *
 *  @param sim       Simulator state to seed.
 *  @param pos       Machine position [3] in mm (may be NULL → keep current).
 *  @param wcs_pos   WCS position [3] in mm (may be NULL).
 *  @param feed      Current feed rate in mm/min (<=0 → keep).
 *  @param wcs       Active WCS index (0‥8, or -1 → keep).
 */
void gc_sim_seed(gc_sim_state_t *sim,
                 const float *pos,
                 const float *wcs_pos,
                 float feed,
                 int wcs);

/** Process one parsed G-code line through the simulator.
 *
 *  If the line causes motion, one or more segments are appended to @p buf.
 *  Modal state in @p sim is updated accordingly.
 *
 *  @param sim   Simulator state (updated in-place).
 *  @param line  Parsed G-code line.
 *  @param buf   Output segment buffer (may be NULL → state update only).
 *  @return      Number of segments produced (0, 1, or rarely >1 for canned cycles).
 */
int gc_sim_process(gc_sim_state_t *sim,
                   const gc_parsed_line_t *line,
                   gc_segbuf_t *buf);

/** Convenience: parse a raw G-code text string and simulate all lines,
 *  appending to @p buf.
 *
 *  @return Number of segments produced.
 */
int gc_sim_run_text(gc_sim_state_t *sim,
                    const char *gcode_text,
                    gc_segbuf_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* GCODE_SIM_H */
