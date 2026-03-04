/**
 * @file gcode_types.h
 * @brief Common types and configuration defines for the G-Code visualizer.
 *
 * All tunables are gathered here so the memory footprint, colour palette
 * and rendering behaviour can be tweaked in one place.
 */
#ifndef GCODE_TYPES_H
#define GCODE_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── tunables ─────────────────────────────────────────────────────────── */

/** Maximum number of toolpath segments kept in the ring buffer.
 *  Each segment is ~40 bytes.  512 segments ≈ 20 KB.  */
#ifndef GCVIEW_MAX_SEGMENTS
#define GCVIEW_MAX_SEGMENTS   512
#endif

/** Maximum length of a single G-code line that the parser will accept. */
#ifndef GCVIEW_MAX_LINE_LEN
#define GCVIEW_MAX_LINE_LEN   128
#endif

/** Number of sub-points used to approximate an arc segment for drawing. */
#ifndef GCVIEW_ARC_RESOLUTION
#define GCVIEW_ARC_RESOLUTION 16
#endif

/** Grid spacing in mm.  The grid auto-scales so this is just a hint. */
#ifndef GCVIEW_GRID_SPACING_MM
#define GCVIEW_GRID_SPACING_MM 10.0f
#endif

/** Default zoom level (pixels per mm). */
#ifndef GCVIEW_DEFAULT_PPM
#define GCVIEW_DEFAULT_PPM  2.0f
#endif

/** Minimum / maximum zoom (pixels per mm). */
#ifndef GCVIEW_MIN_PPM
#define GCVIEW_MIN_PPM  0.2f
#endif
#ifndef GCVIEW_MAX_PPM
#define GCVIEW_MAX_PPM  20.0f
#endif

/* ─── colour palette ───────────────────────────────────────────────────── */

/** Rapid move (G0) */
#ifndef GCVIEW_COL_RAPID
#define GCVIEW_COL_RAPID        0xFF4444   /* red */
#endif

/** Linear feed (G1) */
#ifndef GCVIEW_COL_FEED
#define GCVIEW_COL_FEED         0x44CC44   /* green */
#endif

/** CW arc (G2) */
#ifndef GCVIEW_COL_ARC_CW
#define GCVIEW_COL_ARC_CW       0x4488FF   /* blue */
#endif

/** CCW arc (G3) */
#ifndef GCVIEW_COL_ARC_CCW
#define GCVIEW_COL_ARC_CCW      0x44DDDD   /* cyan */
#endif

/** Already-executed portion of the path */
#ifndef GCVIEW_COL_DONE
#define GCVIEW_COL_DONE         0x666666   /* dim grey */
#endif

/** Grid lines */
#ifndef GCVIEW_COL_GRID
#define GCVIEW_COL_GRID         0x333333   /* dark grey */
#endif

/** Grid major axis highlight */
#ifndef GCVIEW_COL_GRID_MAJOR
#define GCVIEW_COL_GRID_MAJOR   0x555555
#endif

/** Machine origin marker */
#ifndef GCVIEW_COL_ORIGIN
#define GCVIEW_COL_ORIGIN       0xFFFFFF   /* white */
#endif

/** WCS origin marker */
#ifndef GCVIEW_COL_WCS_ORIGIN
#define GCVIEW_COL_WCS_ORIGIN   0xFFCC00   /* amber */
#endif

/** Current spindle position marker */
#ifndef GCVIEW_COL_POSITION
#define GCVIEW_COL_POSITION     0x00FF88   /* bright green */
#endif

/** Machine limits rectangle */
#ifndef GCVIEW_COL_LIMITS
#define GCVIEW_COL_LIMITS       0x884444   /* dim red */
#endif

/** Axes indicator (X = red, Y = green, Z = blue) */
#ifndef GCVIEW_COL_AXIS_X
#define GCVIEW_COL_AXIS_X       0xFF0000
#endif
#ifndef GCVIEW_COL_AXIS_Y
#define GCVIEW_COL_AXIS_Y       0x00FF00
#endif
#ifndef GCVIEW_COL_AXIS_Z
#define GCVIEW_COL_AXIS_Z       0x0088FF
#endif

/** Background of the viewer widget */
#ifndef GCVIEW_COL_BG
#define GCVIEW_COL_BG           0x1A1A1A
#endif

/** Opacity for dim / done segments (0‥255) */
#ifndef GCVIEW_OPA_DONE
#define GCVIEW_OPA_DONE         100
#endif

/** Opacity for grid lines */
#ifndef GCVIEW_OPA_GRID
#define GCVIEW_OPA_GRID         80
#endif

/** Opacity for machine limit box */
#ifndef GCVIEW_OPA_LIMITS
#define GCVIEW_OPA_LIMITS       60
#endif

/* ─── line widths ──────────────────────────────────────────────────────── */

#ifndef GCVIEW_LINE_WIDTH
#define GCVIEW_LINE_WIDTH       2
#endif

#ifndef GCVIEW_GRID_LINE_WIDTH
#define GCVIEW_GRID_LINE_WIDTH  1
#endif

#ifndef GCVIEW_POSITION_SIZE
#define GCVIEW_POSITION_SIZE    8   /* half-arm length for crosshair (px) */
#endif

/* ─── enumerations ─────────────────────────────────────────────────────── */

/** Move type produced by the parser / consumed by the renderer. */
typedef enum {
    GCMOVE_RAPID   = 0,  /**< G0 rapid positioning */
    GCMOVE_LINEAR  = 1,  /**< G1 linear interpolation */
    GCMOVE_ARC_CW  = 2,  /**< G2 clockwise arc */
    GCMOVE_ARC_CCW = 3,  /**< G3 counter-clockwise arc */
} gc_move_type_t;

/** Active plane for arc interpretation. */
typedef enum {
    GCPLANE_XY = 0,  /**< G17 */
    GCPLANE_XZ = 1,  /**< G18 */
    GCPLANE_YZ = 2,  /**< G19 */
} gc_plane_t;

/** Distance mode. */
typedef enum {
    GCDIST_ABSOLUTE = 0,  /**< G90 */
    GCDIST_RELATIVE = 1,  /**< G91 */
} gc_dist_mode_t;

/** Units mode. */
typedef enum {
    GCUNIT_MM   = 0,  /**< G21 */
    GCUNIT_INCH = 1,  /**< G20 */
} gc_unit_mode_t;

/** Viewer projection / view mode. */
typedef enum {
    GCVIEW_TOP       = 0,  /**< XY plan view (Z up) */
    GCVIEW_FRONT     = 1,  /**< XZ front view (Y into screen) */
    GCVIEW_RIGHT     = 2,  /**< YZ right view (X into screen) */
    GCVIEW_LEFT      = 3,  /**< YZ flipped */
    GCVIEW_BACK      = 4,  /**< XZ flipped */
    GCVIEW_ISOMETRIC = 5,  /**< 30° axonometric projection */
    GCVIEW_COUNT     = 6,
} gc_view_mode_t;

/* ─── data structures ──────────────────────────────────────────────────── */

/** A 3-D point in machine coordinates (mm). */
typedef struct {
    float x, y, z;
} gc_vec3_t;

/** A 2-D point in screen / pixel coordinates. */
typedef struct {
    int16_t x, y;
} gc_pt2_t;

/** A single toolpath segment (line or arc) produced by the simulator.
 *
 *  For arcs the centre offset is stored in `ijk` and the arc is
 *  tessellated at draw-time so we don't explode memory.
 *
 *  Size: ~40 bytes.
 */
typedef struct {
    gc_vec3_t       from;       /**< start position (mm) */
    gc_vec3_t       to;         /**< end position (mm) */
    gc_vec3_t       ijk;        /**< arc centre offset (I, J, K) — only for arcs */
    gc_move_type_t  type;       /**< move type (rapid / linear / arc CW/CCW) */
    gc_plane_t      plane;      /**< active plane when this segment was issued */
    float           feed;       /**< feed rate in mm/min (0 = rapid) */
    uint16_t        seq;        /**< sequence number (for done-tracking) */
    uint8_t         _pad[2];
} gc_segment_t;

/** Ring buffer of toolpath segments. */
typedef struct {
    gc_segment_t  segs[GCVIEW_MAX_SEGMENTS];
    uint16_t      head;         /**< next write index */
    uint16_t      count;        /**< number of valid segments */
    uint16_t      seq_counter;  /**< monotonic sequence counter */
    uint16_t      done_seq;     /**< segments with seq <= done_seq are "done" */
} gc_segbuf_t;

/** Simulator modal state — tracks the virtual machine's G-code state. */
typedef struct {
    gc_vec3_t       pos;            /**< current simulated position (mm) */
    gc_move_type_t  motion_mode;    /**< G0/G1/G2/G3 — sticky */
    gc_plane_t      plane;          /**< G17/G18/G19 */
    gc_dist_mode_t  dist_mode;      /**< G90/G91 */
    gc_unit_mode_t  unit_mode;      /**< G20/G21 */
    float           feed;           /**< F word (mm/min) */
    int             wcs;            /**< active WCS index (G54–G59 = 0–5) */
    int             tool;           /**< active tool number */
    bool            spindle_on;     /**< M3/M4 active */
    bool            spindle_cw;     /**< true = M3 (CW), false = M4 (CCW) */
    bool            coolant_mist;   /**< M7 */
    bool            coolant_flood;  /**< M8 */
} gc_sim_state_t;

/** A single parsed G-code line — intermediate representation. */
typedef struct {
    /* Word values — NAN if not present in the line. */
    float g;         /**< G word (e.g. 0, 1, 2, 3, 17, 20, 21, 28, 90, 91) */
    float m;         /**< M word */
    float x, y, z;   /**< X Y Z axis words */
    float i, j, k;   /**< I J K arc offsets */
    float f;         /**< F feed rate */
    float s;         /**< S spindle speed */
    float p;         /**< P parameter */
    float r;         /**< R arc radius form or param */
    int   t;         /**< T tool number (-1 = not present) */
    int   n;         /**< N line number (-1 = not present) */
    /* Bit-mask of which words were present. */
    uint32_t words;
} gc_parsed_line_t;

/** Bit positions for gc_parsed_line_t.words */
enum {
    GCW_G = (1u << 0),
    GCW_M = (1u << 1),
    GCW_X = (1u << 2),
    GCW_Y = (1u << 3),
    GCW_Z = (1u << 4),
    GCW_I = (1u << 5),
    GCW_J = (1u << 6),
    GCW_K = (1u << 7),
    GCW_F = (1u << 8),
    GCW_S = (1u << 9),
    GCW_P = (1u << 10),
    GCW_R = (1u << 11),
    GCW_T = (1u << 12),
    GCW_N = (1u << 13),
};

/** View transform: world (mm) → screen (px).
 *  Cached per-view; invalidated on resize / pan / zoom / view change. */
typedef struct {
    gc_view_mode_t  mode;
    float           ppm;            /**< pixels per mm (zoom) */
    float           pan_x, pan_y;   /**< pan offset in px */
    int16_t         vp_w, vp_h;     /**< viewport width / height (px) */
    int16_t         vp_x, vp_y;     /**< viewport origin on screen (px) */
    bool            dirty;          /**< true → recalc projection cache */
} gc_view_t;

/* ─── inline helpers ───────────────────────────────────────────────────── */

static inline gc_vec3_t gc_vec3(float x, float y, float z) {
    return (gc_vec3_t){x, y, z};
}

static inline float gc_vec3_dist(gc_vec3_t a, gc_vec3_t b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static inline bool gc_has_word(const gc_parsed_line_t *ln, uint32_t mask) {
    return (ln->words & mask) != 0;
}

#ifdef __cplusplus
}
#endif

#endif /* GCODE_TYPES_H */
