// probe_settings.h — Global probe operation parameters
//
// Stores safe_z, max_z_depth, and xy_probe_depth that are used by the
// camera-assisted probe wizard.  Defaults are applied at first boot;
// values persist across resets via NVS.
//
// A dedicated settings screen (planned) will expose these for editing.
// Use probe_settings_save() to commit changes from that screen.

#ifndef PROBE_SETTINGS_H
#define PROBE_SETTINGS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Settings structure
// ---------------------------------------------------------------------------

typedef struct {
    /// Safe Z retract height in WCS mm.  The toolhead retracts to this Z
    /// before any XY move.  0.0 = WCS Z=0 (machine home), which is safe for
    /// most setups that use a tool-length-probed WCS.
    float safe_z;

    /// Maximum downward probe travel in mm.  The Z probe stops and reports
    /// an error if the probe tip travels more than this distance without
    /// triggering.  Default: 10 mm.
    float max_z_depth;

    /// Depth below the probed Z surface at which sidewall probing occurs.
    /// The toolhead moves to (z_surface - xy_probe_depth) before probing
    /// pocket / rect / bore / boss walls.  Default: 1 mm.
    float xy_probe_depth;

    // -----------------------------------------------------------------------
    // Probe-view UI settings (set via numeric_input_dialog actions)
    // -----------------------------------------------------------------------

    /// Width (X extent) of workpiece feature, or radius of bore/boss (mm).
    /// Used as H parameter (X surface length) for corner/rect probes and
    /// as 2*width for bore/boss diameter.  Default: 100 mm.
    float width;

    /// Height (Y extent) of workpiece feature, or depth below current Z at
    /// which sidewall probing occurs (mm).  Used as I parameter (Y surface
    /// length / Z descent).  Default: 100 mm.
    float height;

    /// Clearance distance away from expected surface before probing (mm).
    /// Maps to the T parameter in G6501.1 / G6508.1.  Default: 5 mm.
    float clearance;

    /// Overtravel distance — how far past the expected surface the probe
    /// may travel before erroring.  Maps to the O parameter.  Default: 2 mm.
    float overtravel;

    /// Quick mode: 1 probe point per surface instead of 2 (Q=1).
    /// Faster but cannot detect surface rotation.  Default: false.
    bool quick_mode;
} probe_settings_t;

// ---------------------------------------------------------------------------
// Compile-time defaults (also used when NVS is empty)
// ---------------------------------------------------------------------------
#define PROBE_SETTINGS_DEFAULT_SAFE_Z          0.0f
#define PROBE_SETTINGS_DEFAULT_MAX_Z_DEPTH    10.0f
#define PROBE_SETTINGS_DEFAULT_XY_PROBE_DEPTH  1.0f

#define PROBE_SETTINGS_DEFAULT_WIDTH         100.0f
#define PROBE_SETTINGS_DEFAULT_HEIGHT        100.0f
#define PROBE_SETTINGS_DEFAULT_CLEARANCE       5.0f
#define PROBE_SETTINGS_DEFAULT_OVERTRAVEL      2.0f
#define PROBE_SETTINGS_DEFAULT_QUICK_MODE    false

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/// Initialise the module.  Must be called once before any other function.
/// Sets in-RAM values to compile-time defaults; does NOT touch NVS.
void probe_settings_init(void);

/// Load persisted values from NVS.  Returns true on success.
/// Falls back to module defaults on failure (non-fatal).
bool probe_settings_load(void);

/// Persist the given settings to NVS.  Returns true on success.
/// Also updates the in-RAM copy on success.
bool probe_settings_save(const probe_settings_t *s);

/// Return a pointer to the current in-RAM settings.
/// Valid after probe_settings_init(); never NULL.
const probe_settings_t *probe_settings_get(void);

#ifdef __cplusplus
}
#endif

#endif // PROBE_SETTINGS_H
