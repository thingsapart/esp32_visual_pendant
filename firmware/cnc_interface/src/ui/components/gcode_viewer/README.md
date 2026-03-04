# G-Code Viewer Component

Drop-in LVGL component for visualising CNC toolpaths and machine state on ESP32-S3.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                   lv_gcode_viewer.c                     │
│              (LVGL widget + draw callback)              │
│                                                         │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │ gcode_parser │→ │  gcode_sim   │→ │  gcode_view  │  │
│  │  (parse)     │  │ (simulate)   │  │ (project)    │  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
│         ↑                ↑                              │
│     G-code text     machine_interface                   │
│                     (live position,                     │
│                      WCS, limits)                       │
└─────────────────────────────────────────────────────────┘
```

### Modules

| File | Purpose | Memory |
|------|---------|--------|
| `gcode_types.h` | Common types, defines, colour palette | 0 (header-only) |
| `gcode_parser.h/c` | Zero-alloc single-pass G-code parser | ~1 KB stack per call |
| `gcode_sim.h/c` | G-code state machine simulator | ~80 B for sim state |
| `gcode_view.h/c` | View projections (Top/Front/Right/Left/Back/Iso) | ~32 B per view |
| `lv_gcode_viewer.h/c` | LVGL widget wrapper with draw callback | ~20 KB for segment buffer |

### Memory Budget

- **Segment ring buffer**: `GCVIEW_MAX_SEGMENTS × 40 bytes` = 20 KB (default 512 segments)
- **Simulator state**: ~80 bytes
- **View transform**: ~32 bytes
- **Private struct**: ~120 bytes
- **Total**: ~21 KB heap (allocated via `calloc`, lands in PSRAM on ESP32)

All tunables are `#define`d in `gcode_types.h` — shrink `GCVIEW_MAX_SEGMENTS` if memory is tight.

## Usage

### Basic (no machine)

```c
#include "ui/components/gcode_viewer/lv_gcode_viewer.h"

lv_obj_t *viewer = lv_gcode_viewer_create(parent);

// Load G-code text
lv_gcode_viewer_load_gcode(viewer,
    "G21\nG90\nG1 X10 Y20 F600\nG1 X30\nG2 X10 Y20 I-10 J0\n");

// Change view
lv_gcode_viewer_set_view(viewer, GCVIEW_ISOMETRIC);

// Zoom to fit
lv_gcode_viewer_fit(viewer);
```

### With live machine state

```c
lv_obj_t *viewer = lv_gcode_viewer_create(parent);
lv_gcode_viewer_set_machine(viewer, machine);  // registers pos/home callbacks

// Position crosshair, WCS origin, limits box update automatically.
// Load gcode from movement queue:
lv_gcode_viewer_load_gcode(viewer, gcode_text);
```

### Streaming / incremental

```c
// Append lines one at a time (e.g. from movement queue callback):
lv_gcode_viewer_append_line(viewer, "G1 X10 Y20 F600");
lv_gcode_viewer_append_line(viewer, "G1 X30");

// Mark executed segments as "done" (dimmed):
lv_gcode_viewer_mark_done(viewer);
```

### From YAML UI spec

The component is registered in `api_spec_custom.json` and can be used from the YAML UI:

```yaml
- type: lv_gcode_viewer
  parent: some_container
```

## Views

| Mode | Projection | Axes shown |
|------|-----------|------------|
| `GCVIEW_TOP` | XY plan view | X horizontal, Y vertical |
| `GCVIEW_FRONT` | XZ front view | X horizontal, Z vertical |
| `GCVIEW_RIGHT` | YZ right view | Y horizontal, Z vertical |
| `GCVIEW_LEFT` | YZ flipped | Y flipped horizontal, Z vertical |
| `GCVIEW_BACK` | XZ flipped | X flipped horizontal, Z vertical |
| `GCVIEW_ISOMETRIC` | 30° axonometric | All three axes |

## Colour Palette

All colours are `#define`d in `gcode_types.h`:

| Element | Define | Default |
|---------|--------|---------|
| Rapid (G0) | `GCVIEW_COL_RAPID` | Red `#FF4444` |
| Feed (G1) | `GCVIEW_COL_FEED` | Green `#44CC44` |
| Arc CW (G2) | `GCVIEW_COL_ARC_CW` | Blue `#4488FF` |
| Arc CCW (G3) | `GCVIEW_COL_ARC_CCW` | Cyan `#44DDDD` |
| Done/executed | `GCVIEW_COL_DONE` | Grey `#666666` |
| Grid | `GCVIEW_COL_GRID` | Dark grey `#333333` |
| Position marker | `GCVIEW_COL_POSITION` | Bright green `#00FF88` |
| WCS origin | `GCVIEW_COL_WCS_ORIGIN` | Amber `#FFCC00` |
| Machine limits | `GCVIEW_COL_LIMITS` | Dim red `#884444` |
| Background | `GCVIEW_COL_BG` | Near-black `#1A1A1A` |

## G-Code Support

### Supported G-codes (simulator)

- **G0/G1**: Rapid/linear moves (absolute & relative)
- **G2/G3**: Arc moves (IJK offset form and R radius form)
- **G17/G18/G19**: Plane selection
- **G20/G21**: Units (inch/mm)
- **G28**: Return to home
- **G54–G59**: WCS selection
- **G90/G91**: Absolute/relative distance mode
- **M3/M4/M5**: Spindle on/off (tracked in state)
- **M7/M8/M9**: Coolant (tracked in state)
- **T**: Tool change (tracked in state)
- **F**: Feed rate (sticky)

### Not yet supported

- Canned cycles (G81–G89) — consumed silently
- G10 (set coordinate data)
- G43/G49 (tool length compensation)
- Subroutine calls, conditionals, variables

## machine_interface Extensions

Added to `machine_interface_t`:

```c
// Ring buffer of recent G-code lines (64 lines × 96 chars = ~6 KB)
char    gcode_line_buf[GCODE_LINE_BUF_LINES][GCODE_LINE_BUF_LINE_LEN];
uint16_t gcode_line_head;
uint16_t gcode_line_count;

// Callback when gcode buffer changes
machine_change_callback_t gcode_buffer_changed_cb[MAX_CALLBACKS];
```

Helper functions:
- `machine_interface_push_gcode_line(self, line)` — push a line, fire callback
- `machine_interface_get_gcode_line(self, i)` — read line at index
- `machine_interface_clear_gcode_lines(self)` — clear buffer

## RRF Integration Notes

### Getting G-code for visualisation

1. **Movement queue**: RRF doesn't directly expose the movement queue via the Object Model. However, the `move.currentMove` provides current move data.

2. **M37 Simulation mode**: `M37 S1` enables simulation mode — RRF will parse and simulate G-code without moving. Could be used to pre-visualise a file. `M37 S0` returns to normal mode.

3. **File download**: G-code files can be fetched from DuetWebControl via HTTP:
   - `GET /rr_download?name=/gcodes/filename.gcode`
   - Parse the entire file and feed it to the viewer.

4. **Log messages**: The `log_message_cb` already intercepts G-code echoes from RRF. These could be forwarded to `machine_interface_push_gcode_line()`.

### Future: Full file preview via DWC

```c
// Pseudocode for downloading and visualising a G-code file:
char *gcode = dwc_download_file("/gcodes/job.gcode");
lv_gcode_viewer_load_gcode(viewer, gcode);
free(gcode);
```

For large files, consider streaming with `lv_gcode_viewer_append_line()` to avoid loading the entire file into memory.

## Touch Interaction

- **Drag**: Pan the view
- **Future**: Pinch-to-zoom (requires gesture support in LVGL)
- **Programmatic zoom**: `lv_gcode_viewer_zoom(viewer, 1.5f)` / `lv_gcode_viewer_zoom(viewer, 0.67f)`
