// machine_chipload.c
//
// Chipload calculation helpers for the machine interface.
//
// chipload (mm/tooth) = feed_mm_per_min / (spindle_rpm * flute_count)
//
// The "relative chipload" maps the current value onto a 0–150 scale for
// a given material and tool diameter, where 100 = optimal midpoint.
//
// Tables were compiled from standard machinist reference data.
// Reference diameters: 3, 6, 9.53 (3/8"), 12.7 (1/2"), 19.05 (3/4"), 25.4 (1").
//
// On ESP32 devices with PSRAM the table is placed in external RAM to preserve
// the limited internal DRAM; on host (desktop) builds it lives in normal BSS.

#include "machine_interface.h"

#include <math.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// PSRAM / memory placement
// ---------------------------------------------------------------------------
// Const lookup tables are stored in flash RODATA by default on ESP32
// (accessed via the MMC/instruction cache at near-zero penalty for small
// tables read infrequently).  No special attribute is needed.

// ---------------------------------------------------------------------------
// Reference diameter/chipload table entry
// ---------------------------------------------------------------------------
typedef struct {
  float diameter_mm;  ///< Reference tool diameter (mm)
  float optimal_mm;   ///< Optimal chipload at this diameter (mm/tooth)
} chipload_ref_t;

#define NUM_REF_DIAMETERS 6u

// ---------------------------------------------------------------------------
// Per-material chipload tables
// Each row is a chipload_ref_t {diameter_mm, optimal_mm}.
// optimal_mm = midpoint of the recommended min–max range converted to mm.
// ---------------------------------------------------------------------------
static const chipload_ref_t
    chipload_table[TOOL_MATERIAL_COUNT][NUM_REF_DIAMETERS] = {

  // TOOL_MATERIAL_ALUMINIUM
  // Range (IPT): 0.004–0.008 / 0.007–0.015 / 0.010–0.020 /
  //              0.012–0.025 / 0.018–0.035 / 0.020–0.040
  [TOOL_MATERIAL_ALUMINIUM] = {
    {  3.000f, 0.152f },  // 0.006"
    {  6.000f, 0.279f },  // 0.011"
    {  9.525f, 0.381f },  // 0.015"
    { 12.700f, 0.470f },  // 0.0185"
    { 19.050f, 0.686f },  // 0.027"
    { 25.400f, 0.762f },  // 0.030"
  },

  // TOOL_MATERIAL_STEEL_MILD
  // Range (IPT): 0.001–0.003 / 0.002–0.005 / 0.003–0.006 /
  //              0.004–0.008 / 0.005–0.010 / 0.006–0.012
  [TOOL_MATERIAL_STEEL_MILD] = {
    {  3.000f, 0.051f },  // 0.002"
    {  6.000f, 0.089f },  // 0.0035"
    {  9.525f, 0.114f },  // 0.0045"
    { 12.700f, 0.152f },  // 0.006"
    { 19.050f, 0.191f },  // 0.0075"
    { 25.400f, 0.229f },  // 0.009"
  },

  // TOOL_MATERIAL_STEEL_STAINLESS
  // Range (IPT): 0.0008–0.002 / 0.001–0.003 / 0.0015–0.004 /
  //              0.002–0.005 / 0.003–0.006 / 0.003–0.007
  [TOOL_MATERIAL_STEEL_STAINLESS] = {
    {  3.000f, 0.036f },  // 0.0014"
    {  6.000f, 0.051f },  // 0.002"
    {  9.525f, 0.070f },  // 0.00275"
    { 12.700f, 0.089f },  // 0.0035"
    { 19.050f, 0.114f },  // 0.0045"
    { 25.400f, 0.127f },  // 0.005"
  },

  // TOOL_MATERIAL_HARD_PLASTIC  (PC, Nylon, ABS, Delrin)
  // Range (IPT): 0.004–0.010 / 0.007–0.018 / 0.010–0.022 /
  //              0.012–0.025 / 0.015–0.030 / 0.018–0.035
  [TOOL_MATERIAL_HARD_PLASTIC] = {
    {  3.000f, 0.178f },  // 0.007"
    {  6.000f, 0.318f },  // 0.0125"
    {  9.525f, 0.406f },  // 0.016"
    { 12.700f, 0.470f },  // 0.0185"
    { 19.050f, 0.572f },  // 0.0225"
    { 25.400f, 0.673f },  // 0.0265"
  },

  // TOOL_MATERIAL_ACRYLIC  (PMMA, cast/extruded)
  // Range (IPT): 0.003–0.008 / 0.005–0.013 / 0.008–0.018 /
  //              0.010–0.020 / 0.012–0.025 / 0.015–0.030
  [TOOL_MATERIAL_ACRYLIC] = {
    {  3.000f, 0.140f },  // 0.0055"
    {  6.000f, 0.229f },  // 0.009"
    {  9.525f, 0.330f },  // 0.013"
    { 12.700f, 0.381f },  // 0.015"
    { 19.050f, 0.470f },  // 0.0185"
    { 25.400f, 0.572f },  // 0.0225"
  },

  // TOOL_MATERIAL_MDF
  // Range (IPT): 0.008–0.020 / 0.012–0.030 / 0.018–0.040 /
  //              0.020–0.050 / 0.025–0.060 / 0.030–0.070
  [TOOL_MATERIAL_MDF] = {
    {  3.000f, 0.356f },  // 0.014"
    {  6.000f, 0.533f },  // 0.021"
    {  9.525f, 0.737f },  // 0.029"
    { 12.700f, 0.889f },  // 0.035"
    { 19.050f, 1.080f },  // 0.0425"
    { 25.400f, 1.270f },  // 0.050"
  },

  // TOOL_MATERIAL_SOFTWOOD  (pine, plywood, LVL)
  // Range (IPT): 0.008–0.025 / 0.012–0.035 / 0.018–0.050 /
  //              0.020–0.060 / 0.025–0.070 / 0.030–0.080
  [TOOL_MATERIAL_SOFTWOOD] = {
    {  3.000f, 0.419f },  // 0.0165"
    {  6.000f, 0.597f },  // 0.0235"
    {  9.525f, 0.864f },  // 0.034"
    { 12.700f, 1.016f },  // 0.040"
    { 19.050f, 1.207f },  // 0.0475"
    { 25.400f, 1.397f },  // 0.055"
  },

  // TOOL_MATERIAL_HARDWOOD  (oak, maple, birch)
  // Range (IPT): 0.004–0.012 / 0.007–0.020 / 0.010–0.028 /
  //              0.012–0.030 / 0.015–0.040 / 0.020–0.050
  [TOOL_MATERIAL_HARDWOOD] = {
    {  3.000f, 0.203f },  // 0.008"
    {  6.000f, 0.343f },  // 0.0135"
    {  9.525f, 0.483f },  // 0.019"
    { 12.700f, 0.533f },  // 0.021"
    { 19.050f, 0.699f },  // 0.0275"
    { 25.400f, 0.889f },  // 0.035"
  },
};

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Return the optimal chipload (mm/tooth) for a given tool diameter and
/// material by finding the closest reference point and linearly interpolating
/// between the two nearest reference diameters.
static float _chipload_optimal(float diameter_mm, tool_material_t material) {
  if (material >= TOOL_MATERIAL_COUNT) return 0.0f;
  const chipload_ref_t *row = chipload_table[material];

  // Clamp below the smallest reference diameter.
  if (diameter_mm <= row[0].diameter_mm) return row[0].optimal_mm;

  // Clamp above the largest reference diameter.
  if (diameter_mm >= row[NUM_REF_DIAMETERS - 1].diameter_mm)
    return row[NUM_REF_DIAMETERS - 1].optimal_mm;

  // Linear interpolation between the two bracketing entries.
  for (size_t i = 0; i < NUM_REF_DIAMETERS - 1; i++) {
    if (diameter_mm < row[i + 1].diameter_mm) {
      float t = (diameter_mm - row[i].diameter_mm) /
                (row[i + 1].diameter_mm - row[i].diameter_mm);
      return row[i].optimal_mm + t * (row[i + 1].optimal_mm - row[i].optimal_mm);
    }
  }
  return row[NUM_REF_DIAMETERS - 1].optimal_mm;
}

// ---------------------------------------------------------------------------
// Public API  (declared in machine_interface.h)
// ---------------------------------------------------------------------------

float machine_interface_compute_chipload(const machine_interface_t *self) {
  if (!self) return CHIPLOAD_SPINDLE_STOPPED;

  int rpm = (self->num_spindles > 0 && self->spindles)
                ? self->spindles[0].rpm : 0;
  if (rpm == 0) return CHIPLOAD_SPINDLE_STOPPED;
  if (self->tool_flute_count <= 0) return CHIPLOAD_SPINDLE_STOPPED;

  // feed is in mm/min; chipload = feed / (rpm * flutes)
  return self->feed / ((float)rpm * (float)self->tool_flute_count);
}

float machine_interface_compute_chipload_relative(const machine_interface_t *self,
                                                  tool_material_t material) {
  float cl = machine_interface_compute_chipload(self);
  if (cl == CHIPLOAD_SPINDLE_STOPPED) return CHIPLOAD_SPINDLE_STOPPED;

  float optimal = _chipload_optimal(self->tool_diameter_mm, material);
  if (optimal <= 0.0f) return CHIPLOAD_SPINDLE_STOPPED;

  // Linear scale: 100 = optimal, clamped to [0, 150].
  float relative = (cl / optimal) * 100.0f;
  if (relative < 0.0f)   relative = 0.0f;
  if (relative > 150.0f) relative = 150.0f;
  return relative;
}
