// cam_webserver.cpp — AP mode + embedded web UI for configuration

#include "cam_webserver.h"
#include "cam_capture.h"
#include "cam_pins.h"
#include "cam_transform.h"
#include "app_log.h"

#include <WiFi.h>
#include <WebServer.h>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include "img_converters.h"

#undef ESP_LOGE
#undef ESP_LOGW
#undef ESP_LOGI
#undef ESP_LOGD
#undef ESP_LOGV
#define ESP_LOGE(tag, fmt, ...) APP_LOGE(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) APP_LOGW(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) APP_LOGI(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) APP_LOGD(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) APP_LOGV(tag, fmt, ##__VA_ARGS__)

static const char *TAG = "cam_web";
static WebServer *server = nullptr;
static cam_settings_t *s_settings = nullptr;
static bool s_running = false;
static cam_transform_t s_preview_transform = nullptr;
static uint16_t *s_preview_buf = nullptr;
static uint16_t s_preview_w = 0;
static uint16_t s_preview_h = 0;
static uint16_t s_preview_cap_w = 0;   // capture dimensions the transform was built for
static uint16_t s_preview_cap_h = 0;
static float s_preview_homography[9] = {0};
static bool s_preview_homography_valid = false;

static bool homography_changed(const float a[9], const float b[9]) {
  for (int i = 0; i < 9; i++) {
    if (fabsf(a[i] - b[i]) > 1e-6f) return true;
  }
  return false;
}

static bool ensure_preview_pipeline() {
  if (!s_settings) return false;

  uint16_t w = 0, h = 0;
  cam_capture_get_resolution(&w, &h);
  if (w == 0 || h == 0) return false;

  if (s_settings->calibrated) {
    cam_settings_compute_homography(s_settings, w, h);
  }

  // Determine desired output dimensions for cache comparison.
  uint16_t want_w = w, want_h = h;
  if (s_settings->output_width > 0 && s_settings->output_height > 0) {
    want_w = s_settings->output_width;
    want_h = s_settings->output_height;
  }

  bool homography_diff = !s_preview_homography_valid ||
                         homography_changed(s_preview_homography, s_settings->homography);

  if (s_preview_transform && s_preview_buf
      && s_preview_cap_w == w && s_preview_cap_h == h
      && s_preview_w == want_w && s_preview_h == want_h && !homography_diff) {
    return true;
  }

  if (homography_diff) {
    ESP_LOGI(TAG, "preview: homography changed, rebuilding transform");
  }

  if (s_preview_transform) {
    cam_transform_destroy(s_preview_transform);
    s_preview_transform = nullptr;
  }
  if (s_preview_buf) {
    heap_caps_free(s_preview_buf);
    s_preview_buf = nullptr;
  }

  // Determine output dimensions — match the normal-mode pipeline behaviour.
  // Priority: output_width/output_height > capture resolution.
  uint16_t out_w = w, out_h = h;
  if (s_settings->output_width > 0 && s_settings->output_height > 0) {
    out_w = s_settings->output_width;
    out_h = s_settings->output_height;
  }

  s_preview_transform = cam_transform_create(w, h, out_w, out_h, s_settings->homography);
  if (!s_preview_transform) {
    ESP_LOGE(TAG, "preview: failed to create transform (%ux%u → %ux%u)", w, h, out_w, out_h);
    return false;
  }

  size_t bytes = (size_t)out_w * out_h * sizeof(uint16_t);
  s_preview_buf = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (!s_preview_buf) {
    ESP_LOGE(TAG, "preview: failed to alloc warped buffer (%u bytes)", (unsigned)bytes);
    cam_transform_destroy(s_preview_transform);
    s_preview_transform = nullptr;
    return false;
  }

  s_preview_w = out_w;
  s_preview_h = out_h;
  s_preview_cap_w = w;
  s_preview_cap_h = h;
  memcpy(s_preview_homography, s_settings->homography, sizeof(s_preview_homography));
  s_preview_homography_valid = true;
  ESP_LOGI(TAG, "preview: pipeline ready (%ux%u)", s_preview_w, s_preview_h);
  return true;
}

static bool capture_processed_jpeg(uint8_t **jpeg_buf, size_t *jpeg_len) {
  if (!jpeg_buf || !jpeg_len) return false;
  *jpeg_buf = nullptr;
  *jpeg_len = 0;

  if (!cam_capture_is_ready()) {
    ESP_LOGW(TAG, "preview: camera not ready, trying init");
    if (!cam_capture_init(s_settings)) {
      ESP_LOGE(TAG, "preview: camera init failed: %s (0x%x)",
           cam_capture_last_error_name(), (unsigned)cam_capture_last_error());
      return false;
    }
  }

  if (!ensure_preview_pipeline()) {
    ESP_LOGE(TAG, "preview: pipeline unavailable");
    return false;
  }

  uint8_t *raw_buf = nullptr;
  size_t raw_len = 0;
  uint16_t fw = 0, fh = 0;
  if (!cam_capture_rgb565(&raw_buf, &raw_len, &fw, &fh)) {
    ESP_LOGE(TAG, "preview: RGB565 capture failed: %s", cam_capture_last_error_name());
    return false;
  }

  if (fw != s_preview_cap_w || fh != s_preview_cap_h) {
    ESP_LOGW(TAG, "preview: capture size changed %ux%u -> %ux%u, rebuilding",
             s_preview_cap_w, s_preview_cap_h, fw, fh);
    s_preview_cap_w = 0;
    s_preview_cap_h = 0;
    s_preview_w = 0;
    s_preview_h = 0;
    if (!ensure_preview_pipeline()) {
      cam_capture_release();
      return false;
    }
  }

  cam_transform_apply(s_preview_transform, (const uint16_t *)raw_buf, s_preview_buf);
  cam_capture_release();

  bool ok = fmt2jpg((uint8_t *)s_preview_buf,
            (size_t)s_preview_w * s_preview_h * sizeof(uint16_t),
            s_preview_w, s_preview_h,
            PIXFORMAT_RGB565,
            s_settings ? s_settings->jpeg_quality : 12,
            jpeg_buf, jpeg_len);
  if (!ok || !*jpeg_buf || *jpeg_len == 0) {
    ESP_LOGE(TAG, "preview: processed JPEG encode failed");
    if (*jpeg_buf) {
      free(*jpeg_buf);
      *jpeg_buf = nullptr;
    }
    *jpeg_len = 0;
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// Embedded HTML/JS — served from flash (PROGMEM)
// ---------------------------------------------------------------------------
static const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>CNC Camera Companion</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: -apple-system, BlinkMacSystemFont, sans-serif;
         background: #1a1a2e; color: #e0e0e0; padding: 16px; }
  h1 { color: #0f3460; background: #e0e0e0; padding: 12px; border-radius: 8px;
       text-align: center; margin-bottom: 16px; font-size: 1.2em; }
  .card { background: #16213e; border-radius: 8px; padding: 16px;
          margin-bottom: 16px; }
  .card h2 { font-size: 1em; color: #53d8fb; margin-bottom: 12px; }
  label { display: block; margin: 8px 0 4px; font-size: 0.85em; color: #a0a0a0; }
  input, select { width: 100%; padding: 8px; border: 1px solid #333;
                  border-radius: 4px; background: #0f3460; color: #fff;
                  font-size: 0.9em; }
  input[type=range] { padding: 0; }
  .row { display: flex; gap: 8px; }
  .row > * { flex: 1; }
  button { padding: 10px 20px; border: none; border-radius: 6px;
           cursor: pointer; font-weight: bold; font-size: 0.95em; }
  .btn-primary { background: #53d8fb; color: #1a1a2e; }
  .btn-danger  { background: #e94560; color: #fff; }
  .btn-success { background: #2ecc71; color: #fff; }
  #snap-container { position: relative; display: inline-block; cursor: crosshair;
                    max-width: 100%; padding: 0; border: 2px solid #ffffff;
                    border-radius: 6px; background: #0f3460; }
  #snap-container img { max-width: 100%; display: block; border-radius: 4px;
                        touch-action: none; }
  #snap-container canvas { position: absolute; top:0; left:0;
                           pointer-events: none; }
  #grid-snap-container { position: relative; display: inline-block; cursor: crosshair;
                         max-width: 100%; padding: 0; border: 2px solid #ffffff;
                         border-radius: 6px; background: #0f3460; }
  #grid-snap-container img { max-width: 100%; display: block; border-radius: 4px;
                             touch-action: none; }
  #grid-snap-container canvas { position: absolute; top:0; left:0;
                                width: 100%; height: 100%; pointer-events: none; }
  .dot { width:10px; height:10px; border-radius: 50%; background: #e94560;
         border: 2px solid #fff; position: absolute;
         transform: translate(-50%,-50%); pointer-events: none; }
  .grid-dot { background: #2f8efb; border: 2px solid #fff; width:8px; height:8px; }
  .target-dot { box-sizing: border-box; width:18px; height:18px; border-radius:50%; border:3px solid rgba(47,142,251,0.9); background: rgba(0,0,0,0); position:absolute; transform: translate(-50%,-50%); pointer-events:none; }
  .status { font-size: 0.8em; color: #888; margin-top: 8px; }
  #corner-list { font-size: 0.8em; color: #53d8fb; margin-top: 8px;
                 font-family: monospace; }
  .actions { display: flex; gap: 8px; margin-top: 16px; }
</style>
</head>
<body>

<h1>&#128247; CNC Camera Companion</h1>

<!-- Snapshot + Calibration -->
<div class="card">
  <h2>Calibration — Select 4 Bed Corners</h2>
  <p style="font-size:0.8em; color:#888; margin-bottom:8px;">
    Click the 4 corners of your CNC bed in order: Top-Left, Top-Right,
    Bottom-Right, Bottom-Left.
  </p>
  <div style="margin-bottom:8px;">
    <button class="btn-primary" onclick="refreshProcessedSnapshot()">&#128247; Refresh Snapshot</button>
    <button class="btn-danger" onclick="clearCorners()" style="margin-left:4px;">Clear</button>
    <button id="btn-raw" class="btn-primary" onclick="setViewMode('raw')" style="margin-left:4px;">Show Raw</button>
    <button id="btn-proc" class="btn-primary" onclick="setViewMode('processed')" style="margin-left:4px;">Show Processed</button>
  </div>
  <div id="snap-container">
    <img id="snap" src="/snapshot" alt="Camera Snapshot" crossorigin="anonymous">
    <canvas id="overlay"></canvas>
  </div>
  <div id="corner-list">Click image to set corners...</div>
</div>

<!-- Grid Calibration -->
<div class="card">
  <h2>Grid Calibration — Pixel → Real Mapping</h2>
  <p style="font-size:0.8em; color:#888; margin-bottom:8px;">
    Define the real-world extent and grid spacing that correspond to the
    currently visible area, then run the wizard to mark each grid point
    by placing a tape measure, ruler intersection, or moving a visible
    object to the requested location and clicking the image.
  </p>
  <!-- Min/Max removed; Min assumed 0, Max = project width/height -->
  <div class="row">
    <div>
      <label>Grid DX <input type="number" id="grid_dx" step="0.1" value="25"></label>
    </div>
    <div>
      <label>Grid DY <input type="number" id="grid_dy" step="0.1" value="25"></label>
    </div>
  </div>
  <div class="row">
    <div>
      <label>Project Width (real units) <input type="number" id="proj_w" step="0.1" value="100"></label>
    </div>
    <div>
      <label>Project Height (real units) <input type="number" id="proj_h" step="0.1" value="100"></label>
    </div>
  </div>
  <div style="font-size:0.8em;color:#888;margin-bottom:6px;">
    Extra image area around the work-area corners in output pixels. The
    calibration corners land exactly <em>image_margin</em> pixels inside
    the output image edge, so the camera captures that extra raw area.
    The grid spans the corner-bounded work area.
  </div>
  <div class="row" style="margin-top:8px;">
    <div>
      <label>Image Margin Left (px) <input type="number" id="image_margin_left" min="0" value="0"></label>
    </div>
    <div>
      <label>Image Margin Top (px) <input type="number" id="image_margin_top" min="0" value="0"></label>
    </div>
  </div>
  <div class="row">
    <div>
      <label>Image Margin Right (px) <input type="number" id="image_margin_right" min="0" value="0"></label>
    </div>
    <div>
      <label>Image Margin Bottom (px) <input type="number" id="image_margin_bottom" min="0" value="0"></label>
    </div>
  </div>
  <div style="margin-top:8px;">
    <button class="btn-primary" onclick="generateDefaultGrid()">Generate Default Grid</button>
    <button class="btn-primary" onclick="startGridWizard()" style="margin-left:8px;">Start Grid Wizard</button>
    <button class="btn-danger" onclick="clearGrid()" style="margin-left:8px;">Clear Grid</button>
  </div>
  <!-- Separate grid preview used only for the grid wizard so corners don't interfere -->
  <div id="grid-snap-container" style="margin-top:8px; display:none;">
    <img id="grid-snap" src="/snapshot" alt="Grid Snapshot" crossorigin="anonymous">
    <canvas id="grid-overlay"></canvas>
  </div>
  <div id="grid-wizard-status" style="margin-top:8px; font-size:0.9em; color:#a0e0a0;"></div>
</div>

<!-- Camera Settings -->
<div class="card">
  <h2>Camera Settings</h2>
  <label>Capture Resolution
    <select id="resolution">
      <option value="0">QVGA (320×240)</option>
      <option value="1">VGA (640×480)</option>
      <option value="2">SVGA (800×600)</option>
      <option value="3">XGA (1024×768)</option>
      <option value="4">SXGA (1280×1024)</option>
      <option value="5">UXGA (1600×1200)</option>
    </select>
  </label>
  <div class="row">
    <div>
      <label>Output Width (0=same) <input type="number" id="out_w" min="0" max="1600" value="0"></label>
    </div>
    <div>
      <label>Output Height (0=same) <input type="number" id="out_h" min="0" max="1200" value="0"></label>
    </div>
  </div>
  <div class="row">
    <div>
      <label>JPEG Quality (1–63) <input type="number" id="quality" min="1" max="63" value="12"></label>
    </div>
    <div>
      <label>Diff Threshold (0–255) <input type="number" id="diff_thr" min="0" max="255" value="15"></label>
    </div>
  </div>
  <div class="row">
    <div>
      <label>Tiles X <input type="number" id="tiles_x" min="1" max="8" value="4"></label>
    </div>
    <div>
      <label>Tiles Y <input type="number" id="tiles_y" min="1" max="8" value="4"></label>
    </div>
  </div>
  <label>Keyframe Interval <input type="number" id="kf_interval" min="1" max="255" value="30"></label>
  <label>Send Interval (ms) <input type="number" id="send_ms" min="1" max="100" value="8"></label>
</div>

<!-- Sensor Settings -->
<div class="card">
  <h2>Sensor / Lighting</h2>
  <p style="font-size:0.85em;color:#aaa;margin-top:0">Changes apply live — hover &#9432; for details</p>
  <div class="row">
    <div>
      <label title="Overall image brightness bias. Adjusts the target luminance level the sensor aims for.">Brightness (-2..+2) &#9432; <input type="number" id="brightness" min="-2" max="2" value="0" class="sensor-ctrl"></label>
    </div>
    <div>
      <label title="Image contrast. Higher values increase the difference between light and dark areas.">Contrast (-2..+2) &#9432; <input type="number" id="contrast" min="-2" max="2" value="0" class="sensor-ctrl"></label>
    </div>
  </div>
  <div class="row">
    <div>
      <label title="Auto Exposure Control. When enabled, the sensor automatically adjusts exposure time. Disable to set a fixed exposure value manually."><input type="checkbox" id="aec" checked class="sensor-ctrl"> Auto Exposure &#9432;</label>
    </div>
    <div>
      <label title="Auto Gain Control. When enabled, the sensor automatically adjusts analog gain. Disable to use a fixed gain value. Higher gain amplifies the signal but also noise."><input type="checkbox" id="agc" checked class="sensor-ctrl"> Auto Gain &#9432;</label>
    </div>
  </div>
  <div class="row">
    <div>
      <label title="Manual exposure value (0–1200). Only used when Auto Exposure is OFF. Higher values = longer exposure = brighter image but more motion blur.">Exposure Value &#9432; <input type="number" id="aec_val" min="0" max="1200" value="300" class="sensor-ctrl"></label>
    </div>
    <div>
      <label title="Manual gain value (0–30). Only used when Auto Gain is OFF. Higher values amplify the signal more, making the image brighter but noisier.">Gain Ceiling &#9432; <input type="number" id="agc_gain" min="0" max="30" value="0" class="sensor-ctrl"></label>
    </div>
  </div>
  <div class="row">
    <div>
      <label title="AE Level: brightness bias for auto-exposure (-2..+2). Positive values make auto-exposure target a brighter image; negative values darker.">AE Level (-2..+2) &#9432; <input type="number" id="ae_level" min="-2" max="2" value="0" class="sensor-ctrl"></label>
    </div>
    <div>
      <label title="Maximum analog gain the AEC/AGC algorithm may use. Higher ceiling allows brighter images in low light but increases noise. 2x=minimal gain, 128x=maximum.">Gain Ceiling Mode &#9432;
        <select id="gainceiling" class="sensor-ctrl">
          <option value="0">2x</option>
          <option value="1">4x</option>
          <option value="2">8x</option>
          <option value="3">16x</option>
          <option value="4">32x</option>
          <option value="5">64x</option>
          <option value="6">128x</option>
        </select>
      </label>
    </div>
  </div>
    <div class="row">
      <div>
        <label title="AE lock interval in frames. 0 = disabled (AEC/AGC left running). Higher values will periodically re-sample exposure every N frames.">AE Lock Interval (frames, 0=disabled) &#9432; <input type="number" id="ae_lock_interval" min="0" max="255" value="0" class="sensor-ctrl"></label>
      </div>
    </div>
  <div class="row">
    <div>
      <label title="Colour saturation (-2..+2). Higher values produce more vivid colours; lower values move towards greyscale.">Saturation (-2..+2) &#9432; <input type="number" id="saturation" min="-2" max="2" value="0" class="sensor-ctrl"></label>
    </div>
    <div>
      <label title="Edge sharpness (-2..+2). Increases perceived detail but may amplify noise at high values.">Sharpness (-2..+2) &#9432; <input type="number" id="sharpness" min="-2" max="2" value="0" class="sensor-ctrl"></label>
    </div>
  </div>
  <div class="row">
    <div>
      <label title="Spatial denoise level (0–10). 0=auto (sensor decides), higher values reduce noise but soften details.">Denoise (0..10) &#9432; <input type="number" id="denoise" min="0" max="10" value="0" class="sensor-ctrl"></label>
    </div>
    <div>
      <label title="White balance mode. Auto lets the sensor adjust colour temperature automatically. Fixed presets: Sunny≈5500K, Cloudy≈6500K, Office≈4000K fluorescent, Home≈2800K tungsten.">White Balance &#9432;
        <select id="wb_mode" class="sensor-ctrl">
          <option value="0">Auto</option>
          <option value="1">Sunny</option>
          <option value="2">Cloudy</option>
          <option value="3">Office</option>
          <option value="4">Home</option>
        </select>
      </label>
    </div>
  </div>
  <div class="row">
    <div>
      <label title="Anti-banding / night mode. Reduces flicker from artificial lighting (50/60 Hz). Recommended ON under indoor lighting."><input type="checkbox" id="aec2" checked class="sensor-ctrl"> Anti-banding (AEC2) &#9432;</label>
    </div>
    <div>
      <label title="Black pixel correction. Compensates for hot/stuck dark pixels on the sensor. Usually best left ON."><input type="checkbox" id="bpc" checked class="sensor-ctrl"> Black Pixel Corr. &#9432;</label>
    </div>
  </div>
  <div class="row">
    <div>
      <label title="White pixel correction. Compensates for bright stuck pixels. Usually best left ON."><input type="checkbox" id="wpc" checked class="sensor-ctrl"> White Pixel Corr. &#9432;</label>
    </div>
    <div>
      <label title="Gamma correction. Applies a standard gamma curve to make the image look more natural. Disable for linear (raw) output."><input type="checkbox" id="raw_gma" checked class="sensor-ctrl"> Gamma Correction &#9432;</label>
    </div>
  </div>
  <div class="row">
    <div>
      <label title="Lens shading/vignetting correction. Compensates for light falloff at image edges caused by the lens. Usually best left ON."><input type="checkbox" id="lenc" checked class="sensor-ctrl"> Lens Correction &#9432;</label>
    </div>
    <div>
      <label title="Downsize enable. Allows the sensor to use its internal scaler for lower resolutions. Should be ON for resolutions below the sensor's native resolution."><input type="checkbox" id="dcw" checked class="sensor-ctrl"> Downsize Enable &#9432;</label>
    </div>
  </div>
  <div class="row">
    <div>
      <label title="Mirror the image horizontally. Useful if the camera is mounted facing the opposite direction."><input type="checkbox" id="hmirror" class="sensor-ctrl"> Horizontal Mirror &#9432;</label>
    </div>
    <div>
      <label title="Flip the image vertically. Useful if the camera is mounted upside down."><input type="checkbox" id="vflip" class="sensor-ctrl"> Vertical Flip &#9432;</label>
    </div>
  </div>
</div>

<!-- Actions -->
<div class="actions">
  <button class="btn-success" onclick="saveConfig()">&#128190; Save & Reboot</button>
  <button class="btn-warning" onclick="applyCamera()">&#9881; Apply (no save)</button>
  <button class="btn-danger" onclick="if(confirm('Reset all settings?')) resetConfig()">Reset Defaults</button>
</div>
<div class="status" id="status"></div>

<script>
const corners = []; // source-space normalized corners (0..1)
let gridWizardActive = false;
let gridTargets = []; // array of {x:real, y:real, display:[nx,ny]}
let gridPoints = [];  // array of source-space normalized [x,y] matches
let grid_nx = 0, grid_ny = 0;
let gridAutoGenerated = false;
let wizardIndex = 0;
const img = document.getElementById('snap');
const overlay = document.getElementById('overlay');
const container = document.getElementById('snap-container');
// Grid-specific elements (separate preview for wizard)
const gridImg = document.getElementById('grid-snap');
const gridOverlay = document.getElementById('grid-overlay');
const gridContainer = document.getElementById('grid-snap-container');
let dragIndex = -1;
let hasDragged = false;
let refreshTimer = null;
let currentHomography = [1,0,0, 0,1,0, 0,0,1];
let viewMode = 'processed';
// Capture and output dimensions (filled in by syncPreviewCalibration from server response).
// Needed to correctly scale homography coordinates (H maps capture pixels -> output pixels).
let capW = 0, capH = 0; // camera capture resolution
let outW = 0, outH = 0; // processed output resolution

function clamp01(v) {
  return Math.max(0, Math.min(1, v));
}

function imageWidthPx() {
  return Math.max(1, img.naturalWidth || img.clientWidth || 640);
}

function imageHeightPx() {
  return Math.max(1, img.naturalHeight || img.clientHeight || 480);
}

// Source (capture) dimensions for homography input scaling.
// Falls back to the displayed image size when unknown (works when cap==out).
function srcWidthPx()  { return capW > 0 ? capW : imageWidthPx(); }
function srcHeightPx() { return capH > 0 ? capH : imageHeightPx(); }
// Destination (output) dimensions for homography output scaling.
function dstWidthPx()  { return outW > 0 ? outW : imageWidthPx(); }
function dstHeightPx() { return outH > 0 ? outH : imageHeightPx(); }

function gridImageWidthPx() {
  return Math.max(1, gridImg.naturalWidth || gridImg.clientWidth || 640);
}

function gridImageHeightPx() {
  return Math.max(1, gridImg.naturalHeight || gridImg.clientHeight || 480);
}

function gridContainerWidthPx() {
  return Math.max(1, gridContainer.clientWidth || gridImageWidthPx());
}

function gridContainerHeightPx() {
  return Math.max(1, gridContainer.clientHeight || gridImageHeightPx());
}

function containerWidthPx() {
  return Math.max(1, container.clientWidth || imageWidthPx());
}

function containerHeightPx() {
  return Math.max(1, container.clientHeight || imageHeightPx());
}

function borderFracX() {
  const cr = container.getBoundingClientRect();
  const ir = img.getBoundingClientRect();
  if (cr.width <= 0) return 0;
  return Math.max(0, Math.min(0.49, (ir.left - cr.left) / cr.width));
}

function borderFracY() {
  const cr = container.getBoundingClientRect();
  const ir = img.getBoundingClientRect();
  if (cr.height <= 0) return 0;
  return Math.max(0, Math.min(0.49, (ir.top - cr.top) / cr.height));
}

function gridBorderFracX() {
  const cr = gridContainer.getBoundingClientRect();
  const ir = gridImg.getBoundingClientRect();
  if (cr.width <= 0) return 0;
  return Math.max(0, Math.min(0.49, (ir.left - cr.left) / cr.width));
}

function gridBorderFracY() {
  const cr = gridContainer.getBoundingClientRect();
  const ir = gridImg.getBoundingClientRect();
  if (cr.height <= 0) return 0;
  return Math.max(0, Math.min(0.49, (ir.top - cr.top) / cr.height));
}

function gridDisplayToUiX(x) {
  const bx = gridBorderFracX();
  return (x + bx) / (1 + 2 * bx);
}

function gridDisplayToUiY(y) {
  const by = gridBorderFracY();
  return (y + by) / (1 + 2 * by);
}

function gridUiToDisplayX(x) {
  const bx = gridBorderFracX();
  return x * (1 + 2 * bx) - bx;
}

function gridUiToDisplayY(y) {
  const by = gridBorderFracY();
  return y * (1 + 2 * by) - by;
}

function displayToUiX(x) {
  const bx = borderFracX();
  return (x + bx) / (1 + 2 * bx);
}

function displayToUiY(y) {
  const by = borderFracY();
  return (y + by) / (1 + 2 * by);
}

function uiToDisplayX(x) {
  const bx = borderFracX();
  return x * (1 + 2 * bx) - bx;
}

function uiToDisplayY(y) {
  const by = borderFracY();
  return y * (1 + 2 * by) - by;
}

function identityH() {
  return [1,0,0, 0,1,0, 0,0,1];
}

function isValidHomography(h) {
  return Array.isArray(h) && h.length === 9 && h.every(v => Number.isFinite(v));
}

function invert3x3(m) {
  const a = m[0], b = m[1], c = m[2];
  const d = m[3], e = m[4], f = m[5];
  const g = m[6], h = m[7], i = m[8];
  const A =  (e*i - f*h);
  const B = -(d*i - f*g);
  const C =  (d*h - e*g);
  const D = -(b*i - c*h);
  const E =  (a*i - c*g);
  const F = -(a*h - b*g);
  const G =  (b*f - c*e);
  const H = -(a*f - c*d);
  const I =  (a*e - b*d);
  const det = a*A + b*B + c*C;
  if (!Number.isFinite(det) || Math.abs(det) < 1e-9) return null;
  const invDet = 1.0 / det;
  return [A*invDet, D*invDet, G*invDet,
          B*invDet, E*invDet, H*invDet,
          C*invDet, F*invDet, I*invDet];
}

// Apply homography h (in pixel space) to a point.
// inSw/inSh: scale for the input point (capture dims for forward H, output dims for inverse).
// outSw/outSh: scale for the output point (output dims for forward H, capture dims for inverse).
function applyHomographyNormScaled(pt, h, inSw, inSh, outSw, outSh) {
  const sx = pt[0] * (inSw - 1);
  const sy = pt[1] * (inSh - 1);
  const den = h[6] * sx + h[7] * sy + h[8];
  if (!Number.isFinite(den) || Math.abs(den) < 1e-9) return [pt[0], pt[1]];
  const dx = (h[0] * sx + h[1] * sy + h[2]) / den;
  const dy = (h[3] * sx + h[4] * sy + h[5]) / den;
  return [dx / (outSw - 1), dy / (outSh - 1)];
}

// H maps capture pixels -> output pixels.
// sourceToDisplayNorm: src-normalized -> dst-normalized (forward H).
function applyHomographyNorm(pt, h) {
  return applyHomographyNormScaled(pt, h, srcWidthPx(), srcHeightPx(), dstWidthPx(), dstHeightPx());
}

// displayToSource uses H inverse: dst-normalized -> src-normalized.
function applyHomographyNormInv(pt, h) {
  return applyHomographyNormScaled(pt, h, dstWidthPx(), dstHeightPx(), srcWidthPx(), srcHeightPx());
}

// Returns the homography to use for display<->source projection.
// When the stored H is identity but capture and output resolutions differ,
// the /snapshot image is a pure downsample. The identity matrix does NOT
// encode that scale, so display-coord -> source-coord conversions would
// return values that are (out_w/cap_w) × too small. We substitute the
// implicit scale matrix [[sc_x,0,0],[0,sc_y,0],[0,0,1]] in that case so
// that all coordinate conversions are geometrically correct.
function getEffectiveHomography() {
  const H = isValidHomography(currentHomography) ? currentHomography : identityH();
  const isIdentity = H[0]===1&&H[1]===0&&H[2]===0&&
                     H[3]===0&&H[4]===1&&H[5]===0&&
                     H[6]===0&&H[7]===0&&H[8]===1;
  if (isIdentity && capW > 1 && outW > 1 && (capW !== outW || capH !== outH)) {
    const sx = (outW - 1) / (capW - 1);
    const sy = (outH - 1) / (capH - 1);
    return [sx, 0, 0,  0, sy, 0,  0, 0, 1];
  }
  return H;
}

function sourceToDisplayNorm(srcPt) {
  if (viewMode === 'raw') return [srcPt[0], srcPt[1]];
  return applyHomographyNorm(srcPt, getEffectiveHomography());
}

function displayToSourceNorm(dstPt) {
  if (viewMode === 'raw') return [dstPt[0], dstPt[1]];
  const H = getEffectiveHomography();
  const inv = invert3x3(H);
  if (!inv) return [dstPt[0], dstPt[1]];
  return applyHomographyNormInv(dstPt, inv);
}

// Grid variants: same logic, but use the grid preview image dimensions (which
// are the same output dimensions when showing a processed snapshot).
function gridApplyHomographyNorm(pt, h) {
  return applyHomographyNormScaled(pt, h, srcWidthPx(), srcHeightPx(), dstWidthPx(), dstHeightPx());
}

function gridApplyHomographyNormInv(pt, h) {
  return applyHomographyNormScaled(pt, h, dstWidthPx(), dstHeightPx(), srcWidthPx(), srcHeightPx());
}

function gridDisplayToSourceNorm(dstPt) {
  if (viewMode === 'raw') return [dstPt[0], dstPt[1]];
  const H = getEffectiveHomography();
  const inv = invert3x3(H);
  if (!inv) return [dstPt[0], dstPt[1]];
  return gridApplyHomographyNormInv(dstPt, inv);
}

function gridSourceToDisplayNorm(srcPt) {
  if (viewMode === 'raw') return [srcPt[0], srcPt[1]];
  return gridApplyHomographyNorm(srcPt, getEffectiveHomography());
}

function getRenderedCorners() {
  return corners.map(c => sourceToDisplayNorm(c));
}

function updateViewButtons() {
  const rawBtn = document.getElementById('btn-raw');
  const procBtn = document.getElementById('btn-proc');
  if (!rawBtn || !procBtn) return;
  const activeRaw = viewMode === 'raw';
  rawBtn.style.opacity = activeRaw ? '1.0' : '0.65';
  procBtn.style.opacity = activeRaw ? '0.65' : '1.0';
}

function setViewMode(mode) {
  viewMode = (mode === 'raw') ? 'raw' : 'processed';
  updateViewButtons();
  drawCorners();
  updateCornerList();
}

function getSourceCornersForSubmit() {
  if (corners.length !== 4) return null;
  return corners.map(p => [p[0], p[1]]);
}

function setCornersFromSource(srcCorners) {
  corners.length = 0;
  (srcCorners || []).forEach(p => corners.push([p[0], p[1]]));
  normalizeCornerOrder();
}

function sortCorners(points) {
  if (points.length !== 4) return points.slice();

  const pts = points.map(p => [p[0], p[1]]);
  const byY = pts.slice().sort((a, b) => a[1] - b[1]);
  const top = byY.slice(0, 2).sort((a, b) => a[0] - b[0]);
  const bot = byY.slice(2, 4).sort((a, b) => a[0] - b[0]);

  const tl = top[0];
  const tr = top[1];
  const bl = bot[0];
  const br = bot[1];
  return [tl, tr, br, bl];
}

function normalizeCornerOrder() {
  if (corners.length === 4) {
    const rendered = getRenderedCorners();
    const indexed = rendered.map((p, idx) => ({ idx, x: p[0], y: p[1] }));
    const byY = indexed.slice().sort((a, b) => a.y - b.y);
    const top = byY.slice(0, 2).sort((a, b) => a.x - b.x);
    const bot = byY.slice(2, 4).sort((a, b) => a.x - b.x);
    const order = [top[0].idx, top[1].idx, bot[1].idx, bot[0].idx]; // TL,TR,BR,BL
    const ordered = order.map(i => corners[i]);
    corners.length = 0;
    ordered.forEach(p => corners.push(p));
  }
}

function refreshSnap() {
  img.src = '/snapshot?t=' + Date.now();
}

function schedulePreviewRefresh(delayMs = 120) {
  if (refreshTimer) clearTimeout(refreshTimer);
  refreshTimer = setTimeout(() => {
    refreshProcessedSnapshot();
  }, delayMs);
}

async function syncPreviewCalibration() {
  try {
    const ins = getMargins();
    const body = {
      cal_src: corners.length === 4 ? getSourceCornersForSubmit() : null,
      image_margin_left:   ins.left,
      image_margin_top:    ins.top,
      image_margin_right:  ins.right,
      image_margin_bottom: ins.bottom
    };
    const r = await fetch('/preview_calib', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(body),
    });
    const j = await r.json();
    if (j && Array.isArray(j.homography) && j.homography.length === 9) {
      currentHomography = j.homography;
      // Update capture/output dimensions from server response so homography
      // scaling uses the correct pixel coordinates on both sides.
      if (j.cap_w > 0) capW = j.cap_w;
      if (j.cap_h > 0) capH = j.cap_h;
      if (j.out_w > 0) outW = j.out_w;
      if (j.out_h > 0) outH = j.out_h;
      drawCorners();
      updateCornerList();
      // After updating homography/corners, fetch config to pick up any server-generated grid
      try {
        const rc = await fetch('/config');
        const cc = await rc.json();
        if (cc && cc.grid_calibrated) {
          gridPoints = [];
          if (Array.isArray(cc.grid_points)) {
            // Server-stored grid_points are source-normalised (0..1 over capture image).
            // Use them directly; sourceToDisplayNorm() will project them onto the
            // processed-view image for rendering.
            cc.grid_points.forEach(p => { if (Array.isArray(p) && p.length === 2) gridPoints.push([p[0], p[1]]); });
            if (typeof cc.grid_nx !== 'undefined') grid_nx = cc.grid_nx;
            if (typeof cc.grid_ny !== 'undefined') grid_ny = cc.grid_ny;
            document.getElementById('grid_dx').value = cc.grid_dx || document.getElementById('grid_dx').value;
            document.getElementById('grid_dy').value = cc.grid_dy || document.getElementById('grid_dy').value;
            drawGridPoints();
            document.getElementById('grid-wizard-status').textContent = 'Loaded generated grid';
          }
        }
      } catch (e) { /* ignore config fetch errors */ }
    }
  } catch (e) {
    console.warn('Preview calibration sync failed', e);
  }
}

async function refreshProcessedSnapshot() {
  await syncPreviewCalibration();
  refreshSnap();
}

img.addEventListener('click', function(e) {
  if (gridWizardActive) {
    // Ignore clicks on main preview while grid wizard uses its own preview
    return;
  }
  if (hasDragged) {
    hasDragged = false;
    return;
  }

  const rect = img.getBoundingClientRect();
  const x = (e.clientX - rect.left) / rect.width;
  const y = (e.clientY - rect.top) / rect.height;
  

  if (corners.length >= 4) return;
  corners.push(displayToSourceNorm([x, y]));
  normalizeCornerOrder();
  drawCorners();
  updateCornerList();
  if (corners.length === 4) {
    refreshProcessedSnapshot();
  }
});

// Grid preview click handler: used only when wizard is active
gridImg.addEventListener('click', function(e) {
  if (!gridWizardActive) return;
  const rect = gridImg.getBoundingClientRect();
  const ux = (e.clientX - rect.left) / rect.width;
  const uy = (e.clientY - rect.top) / rect.height;
  // Convert UI coords to display normalized coords for the grid preview
  const dx = gridUiToDisplayX(ux);
  const dy = gridUiToDisplayY(uy);
  // Convert display -> source normalized using grid-specific mapping
  const src = gridDisplayToSourceNorm([dx, dy]);
  gridPoints.push(src);
  drawGridPointsGrid();
  const status = document.getElementById('grid-wizard-status');
  status.textContent = `Marked ${gridPoints.length} / ${gridTargets.length}`;
  wizardIndex = gridPoints.length;
  drawWizardGuide();
  if (gridPoints.length >= gridTargets.length) {
    gridWizardActive = false;
    status.textContent = 'Grid capture complete — uploading...';
    submitGridPoints();
  }
});

function clearCorners() {
  corners.length = 0;
  drawCorners();
  updateCornerList();
  refreshProcessedSnapshot();
}

function findNearestCorner(x, y, threshold = 0.05) {
  const rendered = getRenderedCorners();
  let best = -1;
  let bestDist = threshold;
  for (let i = 0; i < rendered.length; i++) {
    const dx = rendered[i][0] - x;
    const dy = rendered[i][1] - y;
    const d = Math.sqrt(dx * dx + dy * dy);
    if (d < bestDist) {
      bestDist = d;
      best = i;
    }
  }
  return best;
}

function pointerToNormalized(e) {
  const rect = container.getBoundingClientRect();
  const ux = (e.clientX - rect.left) / Math.max(1, rect.width);
  const uy = (e.clientY - rect.top) / Math.max(1, rect.height);
  return [uiToDisplayX(ux), uiToDisplayY(uy)];
}

container.addEventListener('pointerdown', (e) => {
  if (corners.length === 0) return;
  const [x, y] = pointerToNormalized(e);
  const idx = findNearestCorner(x, y);
  if (idx >= 0) {
    dragIndex = idx;
    hasDragged = false;
    container.setPointerCapture(e.pointerId);
    e.preventDefault();
  }
});

container.addEventListener('pointermove', (e) => {
  if (dragIndex < 0) return;
  const [x, y] = pointerToNormalized(e);
  corners[dragIndex] = displayToSourceNorm([x, y]);
  normalizeCornerOrder();
  dragIndex = findNearestCorner(x, y, 0.25);
  hasDragged = true;
  drawCorners();
  updateCornerList();
  if (corners.length === 4) {
    schedulePreviewRefresh();
  }
  e.preventDefault();
});

container.addEventListener('pointerup', (e) => {
  if (dragIndex >= 0 && corners.length === 4) {
    refreshProcessedSnapshot();
  }
  dragIndex = -1;
  try { container.releasePointerCapture(e.pointerId); } catch (_) {}
});

container.addEventListener('pointercancel', () => {
  dragIndex = -1;
});

function drawCorners() {
  const rendered = getRenderedCorners();
  // Remove old corner dots but keep any grid/target dots
  container.querySelectorAll('.dot:not(.grid-dot):not(.target-dot)').forEach(d => d.remove());
  const labels = ['TL', 'TR', 'BR', 'BL'];
  // If the grid wizard is active, hide corner DOM markers so they don't interfere
  if (!gridWizardActive) {
    rendered.forEach((c, i) => {
      const dot = document.createElement('div');
      dot.className = 'dot';
      dot.style.left = (displayToUiX(c[0]) * 100) + '%';
      dot.style.top = (displayToUiY(c[1]) * 100) + '%';
      dot.title = labels[i];
      container.appendChild(dot);
    });
  }
  // Draw lines on canvas
  const ctx = overlay.getContext('2d');
  overlay.width = containerWidthPx();
  overlay.height = containerHeightPx();
  ctx.clearRect(0, 0, overlay.width, overlay.height);

  // Draw target output border (image area) in white for visual alignment.
  const bx = borderFracX() * overlay.width;
  const by = borderFracY() * overlay.height;
  const bw = overlay.width * (1 - 2 * borderFracX());
  const bh = overlay.height * (1 - 2 * borderFracY());
  ctx.strokeStyle = 'rgba(255,255,255,0.85)';
  ctx.lineWidth = 1.5;
  ctx.strokeRect(bx, by, bw, bh);

  // Don't draw the corner polygon while the wizard is active so it doesn't obscure targets
  if (!gridWizardActive && rendered.length > 1) {
    ctx.strokeStyle = '#e94560';
    ctx.lineWidth = 2;
    ctx.beginPath();
    rendered.forEach((c, i) => {
      const px = displayToUiX(c[0]) * overlay.width;
      const py = displayToUiY(c[1]) * overlay.height;
      i === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py);
    });
    if (rendered.length === 4) ctx.closePath();
    ctx.stroke();
  }
}

function updateCornerList() {
  const rendered = getRenderedCorners();
  const labels = ['TL', 'TR', 'BR', 'BL'];
  const el = document.getElementById('corner-list');
  if (rendered.length === 0) {
    el.textContent = 'Click 4 points in any order, then drag dots to fine-tune...';
  } else {
    el.innerHTML = rendered.map((c, i) =>
      `${labels[i]}: (${(c[0]*100).toFixed(1)}%, ${(c[1]*100).toFixed(1)}%)`
    ).join('<br>');
    if (rendered.length === 4) {
      el.innerHTML += '<br><b style="color:#2ecc71">✓ All 4 corners set</b>';
    }
  }
}

function drawGridPoints() {
  // Remove old dots (keep corner dots possible)
  container.querySelectorAll('.grid-dot').forEach(d => d.remove());
  container.querySelectorAll('.target-dot').forEach(d => d.remove());
  // Draw grid points
  gridPoints.forEach((p, i) => {
    const disp = sourceToDisplayNorm(p);
    const dot = document.createElement('div');
    dot.className = 'dot grid-dot';
    dot.style.left = (displayToUiX(disp[0]) * 100) + '%';
    dot.style.top = (displayToUiY(disp[1]) * 100) + '%';
    dot.title = `G${i}`;
    container.appendChild(dot);
  });

  // Draw grid lines on canvas if we know dimensions
  const ctx = overlay.getContext('2d');
  overlay.width = containerWidthPx();
  overlay.height = containerHeightPx();
  // leave previous drawings (corners) — redraw lines over them
  if (gridPoints.length >= 2 && grid_nx > 1 && grid_ny > 1) {
    ctx.strokeStyle = 'rgba(47,142,251,0.45)';
    ctx.lineWidth = 1;
    // helper to get display px
    const pointAt = (ix, iy) => {
      const idx = iy * grid_nx + ix;
      if (idx < 0 || idx >= gridPoints.length) return null;
      const d = sourceToDisplayNorm(gridPoints[idx]);
      return [displayToUiX(d[0]) * overlay.width, displayToUiY(d[1]) * overlay.height];
    };
    // horizontal lines
    for (let iy = 0; iy < grid_ny; iy++) {
      ctx.beginPath();
      for (let ix = 0; ix < grid_nx; ix++) {
        const p = pointAt(ix, iy);
        if (!p) continue;
        if (ix === 0) ctx.moveTo(p[0], p[1]); else ctx.lineTo(p[0], p[1]);
      }
      ctx.stroke();
    }
    // vertical lines
    for (let ix = 0; ix < grid_nx; ix++) {
      ctx.beginPath();
      for (let iy = 0; iy < grid_ny; iy++) {
        const p = pointAt(ix, iy);
        if (!p) continue;
        if (iy === 0) ctx.moveTo(p[0], p[1]); else ctx.lineTo(p[0], p[1]);
      }
      ctx.stroke();
    }
  }
}

// Draw grid points into the dedicated grid preview overlay and container
function drawGridPointsGrid() {
  // Remove old dots in grid container
  gridContainer.querySelectorAll('.grid-dot').forEach(d => d.remove());
  gridContainer.querySelectorAll('.target-dot').forEach(d => d.remove());
  // Draw points
  gridPoints.forEach((p, i) => {
    const disp = gridSourceToDisplayNorm(p);
    const dot = document.createElement('div');
    dot.className = 'dot grid-dot';
    // Map display normalized (0..1) to grid container UI coords
    const left = gridDisplayToUiX(disp[0]);
    const top = gridDisplayToUiY(disp[1]);
    dot.style.left = (left * 100) + '%';
    dot.style.top = (top * 100) + '%';
    dot.title = `G${i}`;
    gridContainer.appendChild(dot);
  });

  const ctx = gridOverlay.getContext('2d');
  gridOverlay.width = gridContainerWidthPx();
  gridOverlay.height = gridContainerHeightPx();
  ctx.clearRect(0, 0, gridOverlay.width, gridOverlay.height);

  if (gridPoints.length >= 2 && grid_nx > 1 && grid_ny > 1) {
    ctx.strokeStyle = 'rgba(47,142,251,0.45)';
    ctx.lineWidth = 1;
      const pointAt = (ix, iy) => {
      const idx = iy * grid_nx + ix;
      if (idx < 0 || idx >= gridPoints.length) return null;
      const d = gridSourceToDisplayNorm(gridPoints[idx]);
      return [gridDisplayToUiX(d[0]) * gridOverlay.width, gridDisplayToUiY(d[1]) * gridOverlay.height];
    };
    for (let iy = 0; iy < grid_ny; iy++) {
      ctx.beginPath();
      for (let ix = 0; ix < grid_nx; ix++) {
        const p = pointAt(ix, iy);
        if (!p) continue;
        if (ix === 0) ctx.moveTo(p[0], p[1]); else ctx.lineTo(p[0], p[1]);
      }
      ctx.stroke();
    }
    for (let ix = 0; ix < grid_nx; ix++) {
      ctx.beginPath();
      for (let iy = 0; iy < grid_ny; iy++) {
        const p = pointAt(ix, iy);
        if (!p) continue;
        if (iy === 0) ctx.moveTo(p[0], p[1]); else ctx.lineTo(p[0], p[1]);
      }
      ctx.stroke();
    }
  }
}

function invertHomography(H) {
  const A =  (H[4]*H[8] - H[5]*H[7]);
  const B = -(H[3]*H[8] - H[5]*H[6]);
  const C =  (H[3]*H[7] - H[4]*H[6]);
  const D = -(H[1]*H[8] - H[2]*H[7]);
  const E =  (H[0]*H[8] - H[2]*H[6]);
  const F = -(H[0]*H[7] - H[1]*H[6]);
  const G =  (H[1]*H[5] - H[2]*H[4]);
  const K = -(H[0]*H[5] - H[2]*H[3]);
  const L =  (H[0]*H[4] - H[1]*H[3]);
  const det = H[0]*A + H[1]*B + H[2]*C;
  if (Math.abs(det) < 1e-12) return null;
  const inv = 1.0 / det;
  return [A*inv, D*inv, G*inv, B*inv, E*inv, K*inv, C*inv, F*inv, L*inv];
}

function getMargins() {
  return {
    left:   parseInt(document.getElementById('image_margin_left').value) || 0,
    top:    parseInt(document.getElementById('image_margin_top').value) || 0,
    right:  parseInt(document.getElementById('image_margin_right').value) || 0,
    bottom: parseInt(document.getElementById('image_margin_bottom').value) || 0
  };
}

function generateDefaultGrid() {
  const projW = parseFloat(document.getElementById('proj_w').value);
  const projH = parseFloat(document.getElementById('proj_h').value);
  const dx = parseFloat(document.getElementById('grid_dx').value);
  const dy = parseFloat(document.getElementById('grid_dy').value);
  const ins = getMargins();
  if (!isFinite(projW) || !isFinite(projH) || !isFinite(dx) || !isFinite(dy) ||
      projW <= 0 || projH <= 0 || dx <= 0 || dy <= 0) {
    alert('Invalid grid parameters'); return;
  }
  const nx = Math.round(projW / dx) + 1;
  const ny = Math.round(projH / dy) + 1;
  // Output (destination) image dimensions — these are the pixel dimensions of
  // the processed snapshot and are the coordinate space for the homography output.
  const oW = dstWidthPx();
  const oH = dstHeightPx();
  // Capture (source) image dimensions — coordinate space for homography input.
  const cW = srcWidthPx();
  const cH = srcHeightPx();
  const activeW = oW - ins.left - ins.right;
  const activeH = oH - ins.top - ins.bottom;
  if (activeW <= 0 || activeH <= 0) { alert('Image margins are too large for image'); return; }

  const Hinv = invertHomography(currentHomography);
  if (!Hinv) { alert('Homography is singular — set calibration corners first'); return; }

  gridPoints = [];
  grid_nx = nx;
  grid_ny = ny;
  for (let iy = 0; iy < ny; iy++) {
    for (let ix = 0; ix < nx; ix++) {
      // Grid position in output pixel coordinates.
      const dst_x = (nx > 1) ? (ins.left + ix * (activeW - 1) / (nx - 1)) : (ins.left + activeW * 0.5);
      const dst_y = (ny > 1) ? (ins.top + iy * (activeH - 1) / (ny - 1)) : (ins.top + activeH * 0.5);
      // Back-project through H⁻¹: dst pixel coords -> src pixel coords.
      const den = Hinv[6]*dst_x + Hinv[7]*dst_y + Hinv[8];
      if (Math.abs(den) < 1e-9) continue;
      const src_x = (Hinv[0]*dst_x + Hinv[1]*dst_y + Hinv[2]) / den;
      const src_y = (Hinv[3]*dst_x + Hinv[4]*dst_y + Hinv[5]) / den;
      // Normalize by capture (source) dimensions.
      const sxn = Math.max(0, Math.min(1, src_x / (cW > 1 ? cW - 1 : 1)));
      const syn = Math.max(0, Math.min(1, src_y / (cH > 1 ? cH - 1 : 1)));
      gridPoints.push([sxn, syn]);
    }
  }

  gridAutoGenerated = true;
  drawCorners();
  drawGridPoints();

  // Submit to server
  const body = {
    proj_w: projW, proj_h: projH, dx, dy, nx, ny,
    auto_generated: true,
    margin_left: ins.left, margin_top: ins.top,
    margin_right: ins.right, margin_bottom: ins.bottom,
    points: gridPoints.map(p => [p[0], p[1]])
  };
  fetch('/grid_calib', { method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(body) })
    .then(r => r.json()).then(j => {
      document.getElementById('grid-wizard-status').textContent = j.message || 'Default grid generated.';
      setTimeout(() => { document.getElementById('grid-wizard-status').textContent = ''; }, 4000);
    }).catch(e => {
      document.getElementById('grid-wizard-status').textContent = 'Grid generation failed: ' + e;
    });
}

let autoRegenTimer = null;
function setupAutoRegenListeners() {
  const ids = ['proj_w', 'proj_h', 'grid_dx', 'grid_dy',
               'image_margin_left', 'image_margin_top', 'image_margin_right', 'image_margin_bottom'];
  const marginIds = new Set(['image_margin_left', 'image_margin_top', 'image_margin_right', 'image_margin_bottom']);
  ids.forEach(id => {
    document.getElementById(id).addEventListener('input', () => {
      // Margin changes always refresh the homography so the processed preview
      // immediately reflects the new border around the calibration corners.
      if (marginIds.has(id)) {
        schedulePreviewRefresh(400);
      }
      if (!gridAutoGenerated) return;
      if (autoRegenTimer) clearTimeout(autoRegenTimer);
      autoRegenTimer = setTimeout(() => { generateDefaultGrid(); }, 500);
    });
  });
}

function startGridWizard() {
  const projW = parseFloat(document.getElementById('proj_w').value);
  const projH = parseFloat(document.getElementById('proj_h').value);
  const dx = parseFloat(document.getElementById('grid_dx').value);
  const dy = parseFloat(document.getElementById('grid_dy').value);
  const ins = getMargins();
  if (!isFinite(projW) || !isFinite(projH) || !isFinite(dx) || !isFinite(dy) || projW <= 0 || projH <= 0 || dx <= 0 || dy <= 0) {
    alert('Invalid grid parameters'); return;
  }
  gridTargets = [];
  gridPoints = [];
  gridAutoGenerated = false;
  // Generate grid in row-major (Y then X). Include all points inside extents.
  grid_nx = Math.round(projW / dx) + 1;
  grid_ny = Math.round(projH / dy) + 1;
  // Compute display normalized positions accounting for margins
  const imgW = gridImg.naturalWidth || img.naturalWidth || 640;
  const imgH = gridImg.naturalHeight || img.naturalHeight || 480;
  const activeW = Math.max(1, imgW - ins.left - ins.right);
  const activeH = Math.max(1, imgH - ins.top - ins.bottom);
  for (let iy = 0; iy < grid_ny; iy++) {
    const gy = iy * dy; // min assumed 0
    for (let ix = 0; ix < grid_nx; ix++) {
      const gx = ix * dx;
      const dst_px_x = (grid_nx > 1) ? (ins.left + ix * (activeW - 1) / (grid_nx - 1)) : (ins.left + activeW * 0.5);
      const dst_px_y = (grid_ny > 1) ? (ins.top + iy * (activeH - 1) / (grid_ny - 1)) : (ins.top + activeH * 0.5);
      const dst_norm_x = dst_px_x / (imgW > 1 ? imgW - 1 : 1);
      const dst_norm_y = dst_px_y / (imgH > 1 ? imgH - 1 : 1);
      gridTargets.push({x: gx, y: gy, disp: [dst_norm_x, dst_norm_y]});
    }
  }
  // Start wizard
  gridWizardActive = true;
  wizardIndex = 0;
  const status = document.getElementById('grid-wizard-status');
  status.textContent = `Grid wizard started — click ${gridTargets.length} points on image. First: X=${gridTargets[0].x}, Y=${gridTargets[0].y}`;
  // Show grid-specific snapshot and hide main preview so corners cannot interfere
  container.style.display = 'none';
  gridContainer.style.display = 'block';
  // refresh grid image and draw guide when loaded
  gridImg.src = '/snapshot?t=' + Date.now();
  gridImg.onload = () => {
    // Ensure container matches image size so UI mapping is 1:1.
    try {
      gridContainer.style.width = gridImg.clientWidth + 'px';
      gridContainer.style.height = gridImg.clientHeight + 'px';
    } catch (e) {}
    drawGridPointsGrid();
    drawWizardGuide();
  };
}

function submitGridPoints() {
  const projW = parseFloat(document.getElementById('proj_w').value);
  const projH = parseFloat(document.getElementById('proj_h').value);
  const dx = parseFloat(document.getElementById('grid_dx').value);
  const dy = parseFloat(document.getElementById('grid_dy').value);
  const ins = getMargins();
  const nx = grid_nx;
  const ny = grid_ny;
  const pts = gridPoints.slice(0, gridTargets.length).map(p => [p[0], p[1]]);
  const body = { proj_w: projW, proj_h: projH, dx, dy, nx, ny,
                 auto_generated: false,
                 margin_left: ins.left, margin_top: ins.top,
                 margin_right: ins.right, margin_bottom: ins.bottom,
                 points: pts };
  fetch('/grid_calib', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify(body) })
    .then(r => r.json()).then(j => {
      document.getElementById('grid-wizard-status').textContent = j.message || 'Grid saved.';
      // update local grid dims
      grid_nx = body.nx; grid_ny = body.ny;
      // hide grid preview and show main preview again — reset explicit size
      gridContainer.style.width = '';
      gridContainer.style.height = '';
      gridContainer.style.display = 'none';
      container.style.display = 'inline-block';
      drawGridPoints();
      setTimeout(() => { document.getElementById('grid-wizard-status').textContent = ''; }, 4000);
    }).catch(e => {
      document.getElementById('grid-wizard-status').textContent = 'Save failed: ' + e;
    });
}

function clearGrid() {
  gridWizardActive = false;
  gridAutoGenerated = false;
  gridTargets = [];
  gridPoints = [];
  document.getElementById('grid-wizard-status').textContent = 'Grid cleared';
  drawGridPoints();
  // restore main preview — reset any explicit size set by startGridWizard
  gridContainer.style.width = '';
  gridContainer.style.height = '';
  gridContainer.style.display = 'none';
  container.style.display = 'inline-block';
  // Also clear on server
  fetch('/grid_calib', { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({clear:true}) })
    .then(() => {}).catch(() => {});
}

function drawWizardGuide() {
  // show next target marker and instruction
  container.querySelectorAll('.target-dot').forEach(d => d.remove());
  gridContainer.querySelectorAll('.target-dot').forEach(d => d.remove());
  if (!gridWizardActive || wizardIndex >= gridTargets.length) return;
  const t = gridTargets[wizardIndex];
  // display normalized target (processed view)
  const dx = t.disp[0];
  const dy = t.disp[1];
  const dot = document.createElement('div');
  dot.className = 'target-dot';
  dot.style.left = (gridDisplayToUiX(dx) * 100) + '%';
  dot.style.top = (gridDisplayToUiY(dy) * 100) + '%';
  // place guide marker into grid preview so corners don't mask it
  gridContainer.appendChild(dot);
  const status = document.getElementById('grid-wizard-status');
  status.textContent = `Point ${wizardIndex+1}/${gridTargets.length}: place tape at X=${t.x}, Y=${t.y} then click image`;
}

async function loadConfig() {
  try {
    const r = await fetch('/config');
    const c = await r.json();
    updateViewButtons();
    // Capture/output dimensions for homography coordinate scaling.
    if (c.cap_w > 0) capW = c.cap_w;
    if (c.cap_h > 0) capH = c.cap_h;
    if (c.out_w > 0) outW = c.out_w;
    if (c.out_h > 0) outH = c.out_h;
    document.getElementById('resolution').value = c.resolution;
    document.getElementById('out_w').value = c.output_width || 0;
    document.getElementById('out_h').value = c.output_height || 0;
    document.getElementById('quality').value = c.jpeg_quality;
    document.getElementById('diff_thr').value = c.diff_threshold;
    document.getElementById('tiles_x').value = c.tiles_x;
    document.getElementById('tiles_y').value = c.tiles_y;
    document.getElementById('kf_interval').value = c.keyframe_interval;
    document.getElementById('send_ms').value = c.send_interval_ms;
    document.getElementById('brightness').value = c.brightness;
    document.getElementById('contrast').value = c.contrast;
    document.getElementById('aec').checked = c.aec_enable;
    document.getElementById('agc').checked = c.agc_enable;
    document.getElementById('aec_val').value = c.aec_value;
    document.getElementById('agc_gain').value = c.agc_gain;
    if (typeof c.ae_lock_interval !== 'undefined') document.getElementById('ae_lock_interval').value = c.ae_lock_interval;
    // Extended sensor controls
    if (typeof c.ae_level !== 'undefined')    document.getElementById('ae_level').value = c.ae_level;
    if (typeof c.gainceiling !== 'undefined') document.getElementById('gainceiling').value = c.gainceiling;
    if (typeof c.saturation !== 'undefined')  document.getElementById('saturation').value = c.saturation;
    if (typeof c.sharpness !== 'undefined')   document.getElementById('sharpness').value = c.sharpness;
    if (typeof c.denoise !== 'undefined')     document.getElementById('denoise').value = c.denoise;
    if (typeof c.wb_mode !== 'undefined')     document.getElementById('wb_mode').value = c.wb_mode;
    if (typeof c.aec2 !== 'undefined')        document.getElementById('aec2').checked = c.aec2;
    if (typeof c.bpc !== 'undefined')         document.getElementById('bpc').checked = c.bpc;
    if (typeof c.wpc !== 'undefined')         document.getElementById('wpc').checked = c.wpc;
    if (typeof c.raw_gma !== 'undefined')     document.getElementById('raw_gma').checked = c.raw_gma;
    if (typeof c.lenc !== 'undefined')        document.getElementById('lenc').checked = c.lenc;
    if (typeof c.dcw !== 'undefined')         document.getElementById('dcw').checked = c.dcw;
    if (typeof c.hmirror !== 'undefined')     document.getElementById('hmirror').checked = c.hmirror;
    if (typeof c.vflip !== 'undefined')       document.getElementById('vflip').checked = c.vflip;
    // Project surface dims
    if (typeof c.surface_width !== 'undefined') document.getElementById('proj_w').value = c.surface_width;
    if (typeof c.surface_height !== 'undefined') document.getElementById('proj_h').value = c.surface_height;
    if (c.cal_src) {
      if (Array.isArray(c.homography) && c.homography.length === 9) {
        currentHomography = c.homography;
      }
      setCornersFromSource(c.cal_src);
      drawCorners();
      updateCornerList();
    }
    // Image margins
    if (typeof c.image_margin_left !== 'undefined')   document.getElementById('image_margin_left').value = c.image_margin_left;
    if (typeof c.image_margin_top !== 'undefined')    document.getElementById('image_margin_top').value = c.image_margin_top;
    if (typeof c.image_margin_right !== 'undefined')  document.getElementById('image_margin_right').value = c.image_margin_right;
    if (typeof c.image_margin_bottom !== 'undefined') document.getElementById('image_margin_bottom').value = c.image_margin_bottom;
    // Grid calibration load
    if (typeof c.grid_calibrated !== 'undefined' && c.grid_calibrated) {
      // Project size
      if (typeof c.surface_width !== 'undefined') document.getElementById('proj_w').value = c.surface_width;
      if (typeof c.surface_height !== 'undefined') document.getElementById('proj_h').value = c.surface_height;
      document.getElementById('grid_dx').value = c.grid_dx || 0;
      document.getElementById('grid_dy').value = c.grid_dy || 0;
      gridPoints = [];
      if (Array.isArray(c.grid_points)) {
        c.grid_points.forEach(p => { if (Array.isArray(p) && p.length === 2) gridPoints.push([p[0], p[1]]); });
        if (typeof c.grid_nx !== 'undefined') grid_nx = c.grid_nx;
        if (typeof c.grid_ny !== 'undefined') grid_ny = c.grid_ny;
        drawGridPoints();
        document.getElementById('grid-wizard-status').textContent = 'Loaded saved grid';
      }
      if (typeof c.grid_auto_generated !== 'undefined') gridAutoGenerated = c.grid_auto_generated;
    }
    setupAutoRegenListeners();
    document.getElementById('status').textContent = 'Config loaded.';
  } catch(e) {
    document.getElementById('status').textContent = 'Failed to load config: ' + e;
  }
}

async function saveConfig() {
  const srcCorners = corners.length === 4 ? getSourceCornersForSubmit() : null;
  const body = {
    resolution: parseInt(document.getElementById('resolution').value),
    output_width: parseInt(document.getElementById('out_w').value),
    output_height: parseInt(document.getElementById('out_h').value),
    jpeg_quality: parseInt(document.getElementById('quality').value),
    diff_threshold: parseInt(document.getElementById('diff_thr').value),
    tiles_x: parseInt(document.getElementById('tiles_x').value),
    tiles_y: parseInt(document.getElementById('tiles_y').value),
    keyframe_interval: parseInt(document.getElementById('kf_interval').value),
    send_interval_ms: parseInt(document.getElementById('send_ms').value),
    brightness: parseInt(document.getElementById('brightness').value),
    contrast: parseInt(document.getElementById('contrast').value),
    aec_enable: document.getElementById('aec').checked,
    agc_enable: document.getElementById('agc').checked,
    aec_value: parseInt(document.getElementById('aec_val').value),
    agc_gain: parseInt(document.getElementById('agc_gain').value),
    ae_lock_interval: parseInt(document.getElementById('ae_lock_interval').value) || 0,
    ae_level: parseInt(document.getElementById('ae_level').value),
    gainceiling: parseInt(document.getElementById('gainceiling').value),
    saturation: parseInt(document.getElementById('saturation').value),
    sharpness: parseInt(document.getElementById('sharpness').value),
    denoise: parseInt(document.getElementById('denoise').value),
    wb_mode: parseInt(document.getElementById('wb_mode').value),
    aec2: document.getElementById('aec2').checked,
    bpc: document.getElementById('bpc').checked,
    wpc: document.getElementById('wpc').checked,
    raw_gma: document.getElementById('raw_gma').checked,
    lenc: document.getElementById('lenc').checked,
    dcw: document.getElementById('dcw').checked,
    hmirror: document.getElementById('hmirror').checked,
    vflip: document.getElementById('vflip').checked,
    cal_src: srcCorners,
    surface_width: parseFloat(document.getElementById('proj_w').value),
    surface_height: parseFloat(document.getElementById('proj_h').value),
    image_margin_left: parseInt(document.getElementById('image_margin_left').value) || 0,
    image_margin_top: parseInt(document.getElementById('image_margin_top').value) || 0,
    image_margin_right: parseInt(document.getElementById('image_margin_right').value) || 0,
    image_margin_bottom: parseInt(document.getElementById('image_margin_bottom').value) || 0,
  };
  try {
    const r = await fetch('/config', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(body),
    });
    const j = await r.json();
    document.getElementById('status').textContent = j.message || 'Saved!';
    if (j.reboot) {
      document.getElementById('status').textContent = 'Saved! Rebooting in 2s...';
      setTimeout(() => { window.location.reload(); }, 5000);
    }
  } catch(e) {
    document.getElementById('status').textContent = 'Save failed: ' + e;
  }
}

async function applyCamera() {
  const srcCorners = corners.length === 4 ? getSourceCornersForSubmit() : null;
  const body = {
    resolution: parseInt(document.getElementById('resolution').value),
    output_width: parseInt(document.getElementById('out_w').value),
    output_height: parseInt(document.getElementById('out_h').value),
    jpeg_quality: parseInt(document.getElementById('quality').value),
    diff_threshold: parseInt(document.getElementById('diff_thr').value),
    tiles_x: parseInt(document.getElementById('tiles_x').value),
    tiles_y: parseInt(document.getElementById('tiles_y').value),
    keyframe_interval: parseInt(document.getElementById('kf_interval').value),
    send_interval_ms: parseInt(document.getElementById('send_ms').value),
    brightness: parseInt(document.getElementById('brightness').value),
    contrast: parseInt(document.getElementById('contrast').value),
    aec_enable: document.getElementById('aec').checked,
    agc_enable: document.getElementById('agc').checked,
    aec_value: parseInt(document.getElementById('aec_val').value),
    agc_gain: parseInt(document.getElementById('agc_gain').value),
    ae_lock_interval: parseInt(document.getElementById('ae_lock_interval').value) || 0,
    ae_level: parseInt(document.getElementById('ae_level').value),
    gainceiling: parseInt(document.getElementById('gainceiling').value),
    saturation: parseInt(document.getElementById('saturation').value),
    sharpness: parseInt(document.getElementById('sharpness').value),
    denoise: parseInt(document.getElementById('denoise').value),
    wb_mode: parseInt(document.getElementById('wb_mode').value),
    aec2: document.getElementById('aec2').checked,
    bpc: document.getElementById('bpc').checked,
    wpc: document.getElementById('wpc').checked,
    raw_gma: document.getElementById('raw_gma').checked,
    lenc: document.getElementById('lenc').checked,
    dcw: document.getElementById('dcw').checked,
    hmirror: document.getElementById('hmirror').checked,
    vflip: document.getElementById('vflip').checked,
    cal_src: srcCorners,
  };
  try {
    const r = await fetch('/apply_camera', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(body),
    });
    const j = await r.json();
    document.getElementById('status').textContent = j.message || (j.ok ? 'Applied.' : 'Apply failed');
  } catch(e) {
    document.getElementById('status').textContent = 'Apply failed: ' + e;
  }
}

async function resetConfig() {
  try {
    await fetch('/reset', { method: 'POST' });
    document.getElementById('status').textContent = 'Defaults restored. Rebooting...';
    setTimeout(() => { window.location.reload(); }, 5000);
  } catch(e) {
    document.getElementById('status').textContent = 'Reset failed: ' + e;
  }
}

// ---------- Live sensor update ----------
// Debounce: collect changes for 300ms then POST to /apply_sensor.
let sensorTimer = null;
function applySensorLive() {
  if (sensorTimer) clearTimeout(sensorTimer);
  sensorTimer = setTimeout(() => {
    const body = {
      brightness: parseInt(document.getElementById('brightness').value),
      contrast: parseInt(document.getElementById('contrast').value),
      aec_enable: document.getElementById('aec').checked,
      agc_enable: document.getElementById('agc').checked,
      aec_value: parseInt(document.getElementById('aec_val').value),
      agc_gain: parseInt(document.getElementById('agc_gain').value),
      ae_lock_interval: parseInt(document.getElementById('ae_lock_interval').value) || 0,
      ae_level: parseInt(document.getElementById('ae_level').value),
      gainceiling: parseInt(document.getElementById('gainceiling').value),
      saturation: parseInt(document.getElementById('saturation').value),
      sharpness: parseInt(document.getElementById('sharpness').value),
      denoise: parseInt(document.getElementById('denoise').value),
      wb_mode: parseInt(document.getElementById('wb_mode').value),
      aec2: document.getElementById('aec2').checked,
      bpc: document.getElementById('bpc').checked,
      wpc: document.getElementById('wpc').checked,
      raw_gma: document.getElementById('raw_gma').checked,
      lenc: document.getElementById('lenc').checked,
      dcw: document.getElementById('dcw').checked,
      hmirror: document.getElementById('hmirror').checked,
      vflip: document.getElementById('vflip').checked,
    };
    fetch('/apply_sensor', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(body),
    }).catch(() => {});
  }, 300);
}
// Attach listeners to all sensor controls
document.querySelectorAll('.sensor-ctrl').forEach(el => {
  el.addEventListener('input', applySensorLive);
  el.addEventListener('change', applySensorLive);
});

// Load config on page load
window.addEventListener('load', loadConfig);
</script>
</body>
</html>
)rawhtml";

// ---------------------------------------------------------------------------
// Route handlers
// ---------------------------------------------------------------------------
static void handle_root() {
    server->send_P(200, "text/html", INDEX_HTML);
}

static void handle_snapshot() {
    uint8_t *buf = nullptr;
    size_t   len = 0;
    ESP_LOGD(TAG, "snapshot: request (processed)");

    if (capture_processed_jpeg(&buf, &len)) {
    WiFiClient client = server->client();
    if (!client || !client.connected()) {
      if (buf) free(buf);
      return;
    }

    // Send raw HTTP response so we can stream binary JPEG data reliably.
    client.printf("HTTP/1.1 200 OK\r\n");
    client.printf("Content-Type: image/jpeg\r\n");
    client.printf("Content-Length: %u\r\n", (unsigned)len);
    client.printf("Cache-Control: no-cache\r\n");
    client.printf("Access-Control-Allow-Origin: *\r\n");
    client.printf("\r\n");
    client.write(buf, len);
    client.flush();
    free(buf);
  } else {
    ESP_LOGE(TAG, "snapshot: processed capture failed");
    String msg = String("Processed capture failed. camera=") + cam_capture_last_error_name() +
                 " (0x" + String((unsigned)cam_capture_last_error(), HEX) + ")";
    server->send(500, "text/plain", msg);
  }
}

// Forward declaration for stream handler
static void handle_stream();

static void handle_get_config() {
    if (!s_settings) { server->send(500, "text/plain", "No settings"); return; }
    cam_settings_t *s = s_settings;

    // Determine actual capture and output dimensions for the JS homography math.
    uint16_t cfg_cap_w = 0, cfg_cap_h = 0;
    cam_capture_get_resolution(&cfg_cap_w, &cfg_cap_h);
    uint16_t cfg_out_w = (s->output_width  > 0) ? s->output_width  : cfg_cap_w;
    uint16_t cfg_out_h = (s->output_height > 0) ? s->output_height : cfg_cap_h;

    // Build JSON manually (no ArduinoJson dependency).
    // With up to 256 grid points (~22 chars each ≈ 5.6 KB) + ~1 KB header,
    // heap-allocate to avoid stack overflow.
    const size_t JSON_SZ = 8192;
    char *json = (char *)malloc(JSON_SZ);
    if (!json) { server->send(500, "text/plain", "alloc fail"); return; }
    int n = snprintf(json, JSON_SZ,
      "{"
      "\"resolution\":%d,"
      "\"output_width\":%u,"
      "\"output_height\":%u,"
      "\"cap_w\":%u,\"cap_h\":%u,\"out_w\":%u,\"out_h\":%u,"
      "\"jpeg_quality\":%d,"
      "\"diff_threshold\":%d,"
      "\"tiles_x\":%d,"
      "\"tiles_y\":%d,"
      "\"keyframe_interval\":%d,"
      "\"send_interval_ms\":%d,"
      "\"brightness\":%d,"
      "\"contrast\":%d,"
      "\"aec_enable\":%s,"
      "\"agc_enable\":%s,"
      "\"aec_value\":%d,"
      "\"agc_gain\":%d,"
      "\"ae_level\":%d,"
      "\"gainceiling\":%u,"
      "\"saturation\":%d,"
      "\"sharpness\":%d,"
      "\"denoise\":%d,"
      "\"wb_mode\":%u,"
      "\"aec2\":%s,"
      "\"bpc\":%s,"
      "\"wpc\":%s,"
      "\"raw_gma\":%s,"
      "\"lenc\":%s,"
      "\"dcw\":%s,"
      "\"hmirror\":%s,"
      "\"vflip\":%s,"
      "\"calibrated\":%s,"
      "\"homography\":[%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f],"
      "\"cal_src\":[[%.4f,%.4f],[%.4f,%.4f],[%.4f,%.4f],[%.4f,%.4f]],"
      "\"surface_width\":%.4f,\"surface_height\":%.4f,"
      "\"grid_calibrated\":%s,\"grid_auto_generated\":%s,"
      "\"image_margin_left\":%u,\"image_margin_top\":%u,\"image_margin_right\":%u,\"image_margin_bottom\":%u,"
      "\"grid_dx\":%.4f,\"grid_dy\":%.4f,\"grid_nx\":%u,\"grid_ny\":%u,\"grid_points\":[",
      s->resolution, (unsigned)s->output_width, (unsigned)s->output_height,
      (unsigned)cfg_cap_w, (unsigned)cfg_cap_h, (unsigned)cfg_out_w, (unsigned)cfg_out_h,
      s->jpeg_quality, s->diff_threshold,
      s->tiles_x, s->tiles_y, s->keyframe_interval, s->send_interval_ms,
      s->brightness, s->contrast,
      s->aec_enable ? "true" : "false",
      s->agc_enable ? "true" : "false",
      s->aec_value, s->agc_gain,
      s->ae_level, (unsigned)s->gainceiling,
      s->saturation, s->sharpness, s->denoise,
      (unsigned)s->wb_mode,
      s->aec2       ? "true" : "false",
      s->bpc        ? "true" : "false",
      s->wpc        ? "true" : "false",
      s->raw_gma    ? "true" : "false",
      s->lenc       ? "true" : "false",
      s->dcw        ? "true" : "false",
      s->hmirror    ? "true" : "false",
      s->vflip      ? "true" : "false",
      s->calibrated ? "true" : "false",
      s->homography[0], s->homography[1], s->homography[2],
      s->homography[3], s->homography[4], s->homography[5],
      s->homography[6], s->homography[7], s->homography[8],
      s->cal_src[0][0], s->cal_src[0][1],
      s->cal_src[1][0], s->cal_src[1][1],
      s->cal_src[2][0], s->cal_src[2][1],
      s->cal_src[3][0], s->cal_src[3][1],
      s->surface_width, s->surface_height,
      s->grid_calibrated ? "true" : "false",
      s->grid_auto_generated ? "true" : "false",
      (unsigned)s->image_margin_left, (unsigned)s->image_margin_top,
      (unsigned)s->image_margin_right, (unsigned)s->image_margin_bottom,
      s->grid_dx, s->grid_dy, s->grid_nx, s->grid_ny);
    (void)n;
    size_t off = strlen(json);
    for (uint16_t i = 0; i < s->grid_points_count && i < CAM_SETTINGS_MAX_GRID_POINTS; i++) {
        int r = snprintf(json + off, JSON_SZ - off, "%s[%.6f,%.6f]",
                         (i==0)?"":",", s->grid_points[i][0], s->grid_points[i][1]);
        if (r <= 0) break;
        off += r;
    }
    if (off < JSON_SZ - 4) {
      snprintf(json + off, JSON_SZ - off, "]}" );
    } else {
      strncat(json, "]}", JSON_SZ - strlen(json) - 1);
    }
    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->send(200, "application/json", json);
    free(json);
}

// Minimal JSON number parser.
static float json_float(const String &body, const char *key, float def) {
    String k = String("\"") + key + "\"";
    int idx = body.indexOf(k);
    if (idx < 0) return def;
    idx = body.indexOf(':', idx);
    if (idx < 0) return def;
    return body.substring(idx + 1).toFloat();
}
static int json_int(const String &body, const char *key, int def) {
    return (int)json_float(body, key, (float)def);
}
static bool json_bool(const String &body, const char *key, bool def) {
    String k = String("\"") + key + "\"";
    int idx = body.indexOf(k);
    if (idx < 0) return def;
    idx = body.indexOf(':', idx);
    if (idx < 0) return def;
    String val = body.substring(idx + 1, idx + 10);
    val.trim();
    return val.startsWith("true") || val.startsWith("1");
}

  static bool parse_cal_src(const String &body, float out_cal_src[4][2]) {
    int cal_idx = body.indexOf("\"cal_src\"");
    if (cal_idx < 0) return false;
    int arr_start = body.indexOf("[[", cal_idx);
    if (arr_start < 0) return false;

    int pos = arr_start + 1;
    for (int i = 0; i < 4 && pos > 0; i++) {
      int bracket = body.indexOf('[', pos);
      if (bracket < 0) return false;
      int comma = body.indexOf(',', bracket + 1);
      int close = body.indexOf(']', comma + 1);
      if (comma <= 0 || close <= 0) return false;
      out_cal_src[i][0] = body.substring(bracket + 1, comma).toFloat();
      out_cal_src[i][1] = body.substring(comma + 1, close).toFloat();
      pos = close + 1;
    }
    return true;
  }

static void handle_post_config() {
    if (!s_settings) { server->send(500, "text/plain", "No settings"); return; }
    String body = server->arg("plain");
    cam_settings_t *s = s_settings;

    s->resolution        = (uint8_t)json_int(body, "resolution", s->resolution);
    s->output_width      = (uint16_t)json_int(body, "output_width", s->output_width);
    s->output_height     = (uint16_t)json_int(body, "output_height", s->output_height);
    s->jpeg_quality      = (uint8_t)json_int(body, "jpeg_quality", s->jpeg_quality);
    s->diff_threshold    = (uint8_t)json_int(body, "diff_threshold", s->diff_threshold);
    s->tiles_x           = (uint8_t)json_int(body, "tiles_x", s->tiles_x);
    s->tiles_y           = (uint8_t)json_int(body, "tiles_y", s->tiles_y);
    s->keyframe_interval = (uint8_t)json_int(body, "keyframe_interval", s->keyframe_interval);
    s->send_interval_ms  = (uint8_t)json_int(body, "send_interval_ms", s->send_interval_ms);
    s->brightness        = (int8_t)json_int(body, "brightness", s->brightness);
    s->contrast          = (int8_t)json_int(body, "contrast", s->contrast);
    s->aec_enable        = json_bool(body, "aec_enable", s->aec_enable);
    s->agc_enable        = json_bool(body, "agc_enable", s->agc_enable);
    s->aec_value         = (int16_t)json_int(body, "aec_value", s->aec_value);
    s->agc_gain          = (uint8_t)json_int(body, "agc_gain", s->agc_gain);
    s->ae_lock_interval  = (uint8_t)json_int(body, "ae_lock_interval", s->ae_lock_interval);
    // Extended sensor controls
    s->ae_level          = (int8_t)json_int(body, "ae_level", s->ae_level);
    s->gainceiling       = (uint8_t)json_int(body, "gainceiling", s->gainceiling);
    s->saturation        = (int8_t)json_int(body, "saturation", s->saturation);
    s->sharpness         = (int8_t)json_int(body, "sharpness", s->sharpness);
    s->denoise           = (int8_t)json_int(body, "denoise", s->denoise);
    s->wb_mode           = (uint8_t)json_int(body, "wb_mode", s->wb_mode);
    s->aec2              = json_bool(body, "aec2", s->aec2) ? 1 : 0;
    s->bpc               = json_bool(body, "bpc", s->bpc) ? 1 : 0;
    s->wpc               = json_bool(body, "wpc", s->wpc) ? 1 : 0;
    s->raw_gma           = json_bool(body, "raw_gma", s->raw_gma) ? 1 : 0;
    s->lenc              = json_bool(body, "lenc", s->lenc) ? 1 : 0;
    s->dcw               = json_bool(body, "dcw", s->dcw) ? 1 : 0;
    s->hmirror           = json_bool(body, "hmirror", s->hmirror) ? 1 : 0;
    s->vflip             = json_bool(body, "vflip", s->vflip) ? 1 : 0;

    // AE lock interval (frames). 0 = disabled
    s->ae_lock_interval  = (uint8_t)json_int(body, "ae_lock_interval", s->ae_lock_interval);
    s->surface_width     = json_float(body, "surface_width", s->surface_width);
    s->surface_height    = json_float(body, "surface_height", s->surface_height);
    s->image_margin_left   = (uint16_t)json_int(body, "image_margin_left", s->image_margin_left);
    s->image_margin_top    = (uint16_t)json_int(body, "image_margin_top", s->image_margin_top);
    s->image_margin_right  = (uint16_t)json_int(body, "image_margin_right", s->image_margin_right);
    s->image_margin_bottom = (uint16_t)json_int(body, "image_margin_bottom", s->image_margin_bottom);

    float parsed_cal_src[4][2];
    if (parse_cal_src(body, parsed_cal_src)) {
      memcpy(s->cal_src, parsed_cal_src, sizeof(s->cal_src));
      s->calibrated = true;

      uint16_t w, h;
      cam_capture_get_resolution(&w, &h);
      cam_settings_compute_homography(s, w, h);
      s_preview_homography_valid = false;
    } else if (!s->calibrated) {
      // No corner calibration provided — mark as calibrated anyway with
      // identity homography so the device boots into camera mode after reboot.
      s->calibrated = true;
      // Identity homography (already set by defaults, but be explicit)
      s->homography[0] = 1.0f; s->homography[1] = 0.0f; s->homography[2] = 0.0f;
      s->homography[3] = 0.0f; s->homography[4] = 1.0f; s->homography[5] = 0.0f;
      s->homography[6] = 0.0f; s->homography[7] = 0.0f; s->homography[8] = 1.0f;
      ESP_LOGI(TAG, "No calibration corners — using identity homography");
    }

    cam_settings_save(s);
    cam_capture_apply_sensor(s);
    cam_capture_set_ae_lock_interval(s->ae_lock_interval);

    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->send(200, "application/json",
                 "{\"message\":\"Settings saved. Reboot to apply resolution changes.\","
                 "\"reboot\":true}");

    ESP_LOGI(TAG, "Config saved via web UI");

    // Delay then reboot.
    delay(2000);
    ESP.restart();
}

  static void handle_post_preview_calib() {
    if (!s_settings) { server->send(500, "text/plain", "No settings"); return; }

    String body = server->arg("plain");
    bool clear_requested = body.indexOf("\"cal_src\":null") >= 0;
    float parsed_cal_src[4][2];
    bool has_cal_src = parse_cal_src(body, parsed_cal_src);

    // Apply any margin overrides sent from the UI for live preview.
    // These are NOT persisted to flash — that still requires POST /config.
    s_settings->image_margin_left   = (uint16_t)json_int(body, "image_margin_left",   (int)s_settings->image_margin_left);
    s_settings->image_margin_top    = (uint16_t)json_int(body, "image_margin_top",    (int)s_settings->image_margin_top);
    s_settings->image_margin_right  = (uint16_t)json_int(body, "image_margin_right",  (int)s_settings->image_margin_right);
    s_settings->image_margin_bottom = (uint16_t)json_int(body, "image_margin_bottom", (int)s_settings->image_margin_bottom);

    if (has_cal_src) {
      memcpy(s_settings->cal_src, parsed_cal_src, sizeof(s_settings->cal_src));
      s_settings->calibrated = true;
      uint16_t w, h;
      cam_capture_get_resolution(&w, &h);
      cam_settings_compute_homography(s_settings, w, h);
      ESP_LOGI(TAG, "preview_calib: updated runtime homography from 4 corners");

      // If surface dimensions are set, auto-generate a grid using the
      // current surface/spacing settings; fall back to 6×6 if unset.
      const float projW = s_settings->surface_width  > 0.0f ? s_settings->surface_width  : 100.0f;
      const float projH = s_settings->surface_height > 0.0f ? s_settings->surface_height : 100.0f;
      const float gdx   = s_settings->grid_dx > 0.0f ? s_settings->grid_dx : (projW / 5.0f);
      const float gdy   = s_settings->grid_dy > 0.0f ? s_settings->grid_dy : (projH / 5.0f);
      uint16_t NX = (uint16_t)(roundf(projW / gdx) + 1.0f);
      uint16_t NY = (uint16_t)(roundf(projH / gdy) + 1.0f);
      if (NX < 2) NX = 2; if (NX > 32) NX = 32;
      if (NY < 2) NY = 2; if (NY > 32) NY = 32;
      if (s_settings->surface_width > 0.0f && s_settings->surface_height > 0.0f) {
        float H[9];
        for (int i = 0; i < 9; i++) H[i] = s_settings->homography[i];
        // Invert 3x3 matrix H -> Hinv
        float A =  (H[4]*H[8] - H[5]*H[7]);
        float B = -(H[3]*H[8] - H[5]*H[6]);
        float C =  (H[3]*H[7] - H[4]*H[6]);
        float D = -(H[1]*H[8] - H[2]*H[7]);
        float E =  (H[0]*H[8] - H[2]*H[6]);
        float F = -(H[0]*H[7] - H[1]*H[6]);
        float G =  (H[1]*H[5] - H[2]*H[4]);
        float K = -(H[0]*H[5] - H[2]*H[3]);
        float L =  (H[0]*H[4] - H[1]*H[3]);
        float det = H[0]*A + H[1]*B + H[2]*C;
        if (fabs(det) > 1e-12f) {
          float invDet = 1.0f / det;
          float Hinv[9];
          Hinv[0] = A * invDet; Hinv[1] = D * invDet; Hinv[2] = G * invDet;
          Hinv[3] = B * invDet; Hinv[4] = E * invDet; Hinv[5] = K * invDet;
          Hinv[6] = C * invDet; Hinv[7] = F * invDet; Hinv[8] = L * invDet;

          // Build grid in OUTPUT pixel space, then back-project to SOURCE
          // pixel space for storage.  All grid_points are stored as
          // source-normalised (0..1 over the capture image) so loadConfig()
          // and the generateDefaultGrid() path use the same coordinate system.
          // Margins are output pixels; use output dims (fall back to capture).
          uint16_t out_w2 = (s_settings->output_width  > 0) ? s_settings->output_width  : w;
          uint16_t out_h2 = (s_settings->output_height > 0) ? s_settings->output_height : h;
          uint16_t inL = s_settings->image_margin_left;
          uint16_t inT = s_settings->image_margin_top;
          uint16_t inR = s_settings->image_margin_right;
          uint16_t inB = s_settings->image_margin_bottom;
          float activeW = (float)((int)out_w2 - (int)inL - (int)inR);
          float activeH = (float)((int)out_h2 - (int)inT - (int)inB);
          if (activeW < 1.0f) activeW = (float)out_w2;
          if (activeH < 1.0f) activeH = (float)out_h2;
          // Source (capture) normalisation denominators.
          const float src_norm_w = (float)(w > 1 ? w - 1 : 1);
          const float src_norm_h = (float)(h > 1 ? h - 1 : 1);
          uint16_t count = 0;
          for (uint16_t iy = 0; iy < NY && count < CAM_SETTINGS_MAX_GRID_POINTS; iy++) {
            for (uint16_t ix = 0; ix < NX && count < CAM_SETTINGS_MAX_GRID_POINTS; ix++) {
              // Ideal grid position in output pixel coordinates.
              float dst_x = (float)inL + (float)ix * (activeW - 1.0f) / (float)(NX - 1);
              float dst_y = (float)inT + (float)iy * (activeH - 1.0f) / (float)(NY - 1);
              // Back-project through H⁻¹ → source pixel coords.
              float hi_den = Hinv[6]*dst_x + Hinv[7]*dst_y + Hinv[8];
              float src_x, src_y;
              if (fabsf(hi_den) < 1e-10f) {
                // Degenerate: fall back to identity mapping
                src_x = dst_x;
                src_y = dst_y;
              } else {
                src_x = (Hinv[0]*dst_x + Hinv[1]*dst_y + Hinv[2]) / hi_den;
                src_y = (Hinv[3]*dst_x + Hinv[4]*dst_y + Hinv[5]) / hi_den;
              }
              // Normalise by capture (source) dimensions.
              float sxnorm = src_x / src_norm_w;
              float synorm = src_y / src_norm_h;
              if (sxnorm < 0.0f) sxnorm = 0.0f; if (sxnorm > 1.0f) sxnorm = 1.0f;
              if (synorm < 0.0f) synorm = 0.0f; if (synorm > 1.0f) synorm = 1.0f;
              s_settings->grid_points[count][0] = sxnorm;
              s_settings->grid_points[count][1] = synorm;
              count++;
            }
          }
          s_settings->grid_points_count = count;
          s_settings->grid_calibrated = (count > 0);
          s_settings->grid_auto_generated = true;
          s_settings->grid_minx = 0.0f;
          s_settings->grid_miny = 0.0f;
          s_settings->grid_maxx = projW;
          s_settings->grid_maxy = projH;
          s_settings->grid_dx = gdx;
          s_settings->grid_dy = gdy;
          s_settings->grid_nx = NX;
          s_settings->grid_ny = NY;
          cam_settings_save(s_settings);
          ESP_LOGI(TAG, "preview_calib: auto-generated %u grid points (%ux%u)", (unsigned)count, NX, NY);
        } else {
          ESP_LOGW(TAG, "preview_calib: homography singular, skipping grid generation");
        }
      }
    } else if (clear_requested) {
      s_settings->calibrated = false;
      s_settings->homography[0] = 1.0f; s_settings->homography[1] = 0.0f; s_settings->homography[2] = 0.0f;
      s_settings->homography[3] = 0.0f; s_settings->homography[4] = 1.0f; s_settings->homography[5] = 0.0f;
      s_settings->homography[6] = 0.0f; s_settings->homography[7] = 0.0f; s_settings->homography[8] = 1.0f;
      s_settings->cal_src[0][0] = 0.0f; s_settings->cal_src[0][1] = 0.0f;
      s_settings->cal_src[1][0] = 1.0f; s_settings->cal_src[1][1] = 0.0f;
      s_settings->cal_src[2][0] = 1.0f; s_settings->cal_src[2][1] = 1.0f;
      s_settings->cal_src[3][0] = 0.0f; s_settings->cal_src[3][1] = 1.0f;
      ESP_LOGI(TAG, "preview_calib: cleared runtime homography to identity");
    }

    s_preview_homography_valid = false;
    ensure_preview_pipeline();

    uint16_t resp_cap_w = 0, resp_cap_h = 0;
    cam_capture_get_resolution(&resp_cap_w, &resp_cap_h);
    uint16_t resp_out_w = (s_settings->output_width  > 0) ? s_settings->output_width  : resp_cap_w;
    uint16_t resp_out_h = (s_settings->output_height > 0) ? s_settings->output_height : resp_cap_h;
    char out[320];
    snprintf(out, sizeof(out),
         "{\"ok\":true,"
         "\"cap_w\":%u,\"cap_h\":%u,\"out_w\":%u,\"out_h\":%u,"
         "\"homography\":[%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f]}",
         (unsigned)resp_cap_w, (unsigned)resp_cap_h,
         (unsigned)resp_out_w, (unsigned)resp_out_h,
         s_settings->homography[0], s_settings->homography[1], s_settings->homography[2],
         s_settings->homography[3], s_settings->homography[4], s_settings->homography[5],
         s_settings->homography[6], s_settings->homography[7], s_settings->homography[8]);
    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->send(200, "application/json", out);
  }

// POST /apply_sensor — apply sensor controls live without save/reboot
static void handle_apply_sensor() {
    if (!s_settings) { server->send(500, "text/plain", "No settings"); return; }
    String body = server->arg("plain");
    cam_settings_t *s = s_settings;

    // Update only sensor-related fields in the live settings struct.
    s->brightness   = (int8_t)json_int(body, "brightness", s->brightness);
    s->contrast     = (int8_t)json_int(body, "contrast", s->contrast);
    s->aec_enable   = json_bool(body, "aec_enable", s->aec_enable);
    s->agc_enable   = json_bool(body, "agc_enable", s->agc_enable);
    s->aec_value    = (int16_t)json_int(body, "aec_value", s->aec_value);
    s->agc_gain     = (uint8_t)json_int(body, "agc_gain", s->agc_gain);
    s->ae_level     = (int8_t)json_int(body, "ae_level", s->ae_level);
    s->gainceiling  = (uint8_t)json_int(body, "gainceiling", s->gainceiling);
    s->saturation   = (int8_t)json_int(body, "saturation", s->saturation);
    s->sharpness    = (int8_t)json_int(body, "sharpness", s->sharpness);
    s->denoise      = (int8_t)json_int(body, "denoise", s->denoise);
    s->wb_mode      = (uint8_t)json_int(body, "wb_mode", s->wb_mode);
    s->aec2         = json_bool(body, "aec2", s->aec2) ? 1 : 0;
    s->bpc          = json_bool(body, "bpc", s->bpc) ? 1 : 0;
    s->wpc          = json_bool(body, "wpc", s->wpc) ? 1 : 0;
    s->raw_gma      = json_bool(body, "raw_gma", s->raw_gma) ? 1 : 0;
    s->lenc         = json_bool(body, "lenc", s->lenc) ? 1 : 0;
    s->dcw          = json_bool(body, "dcw", s->dcw) ? 1 : 0;
    s->hmirror      = json_bool(body, "hmirror", s->hmirror) ? 1 : 0;
    s->vflip        = json_bool(body, "vflip", s->vflip) ? 1 : 0;

    // AE lock interval (frames). 0 = disabled
    s->ae_lock_interval = (uint8_t)json_int(body, "ae_lock_interval", s->ae_lock_interval);

    cam_capture_apply_sensor(s);
    cam_capture_set_ae_lock_interval(s->ae_lock_interval);

    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->send(200, "application/json", "{\"ok\":true}");
}

// POST /apply_camera — apply full camera settings (in-memory) and rebuild
// camera pipeline without saving to NVS or rebooting the device.
static void handle_apply_camera() {
    if (!s_settings) { server->send(500, "text/plain", "No settings"); return; }
    cam_settings_t old = *s_settings; // copy to allow revert on failure
    String body = server->arg("plain");
    cam_settings_t *s = s_settings;

    // Update fields similar to config POST but do NOT persist to NVS here.
    s->resolution        = (uint8_t)json_int(body, "resolution", s->resolution);
    s->output_width      = (uint16_t)json_int(body, "output_width", s->output_width);
    s->output_height     = (uint16_t)json_int(body, "output_height", s->output_height);
    s->jpeg_quality      = (uint8_t)json_int(body, "jpeg_quality", s->jpeg_quality);
    s->diff_threshold    = (uint8_t)json_int(body, "diff_threshold", s->diff_threshold);
    s->tiles_x           = (uint8_t)json_int(body, "tiles_x", s->tiles_x);
    s->tiles_y           = (uint8_t)json_int(body, "tiles_y", s->tiles_y);
    s->keyframe_interval = (uint8_t)json_int(body, "keyframe_interval", s->keyframe_interval);
    s->send_interval_ms  = (uint8_t)json_int(body, "send_interval_ms", s->send_interval_ms);
    s->brightness        = (int8_t)json_int(body, "brightness", s->brightness);
    s->contrast          = (int8_t)json_int(body, "contrast", s->contrast);
    s->aec_enable        = json_bool(body, "aec_enable", s->aec_enable);
    s->agc_enable        = json_bool(body, "agc_enable", s->agc_enable);
    s->aec_value         = (int16_t)json_int(body, "aec_value", s->aec_value);
    s->agc_gain          = (uint8_t)json_int(body, "agc_gain", s->agc_gain);
    s->ae_level          = (int8_t)json_int(body, "ae_level", s->ae_level);
    s->gainceiling       = (uint8_t)json_int(body, "gainceiling", s->gainceiling);
    s->saturation        = (int8_t)json_int(body, "saturation", s->saturation);
    s->sharpness         = (int8_t)json_int(body, "sharpness", s->sharpness);
    s->denoise           = (int8_t)json_int(body, "denoise", s->denoise);
    s->wb_mode           = (uint8_t)json_int(body, "wb_mode", s->wb_mode);
    s->aec2              = json_bool(body, "aec2", s->aec2) ? 1 : 0;
    s->bpc               = json_bool(body, "bpc", s->bpc) ? 1 : 0;
    s->wpc               = json_bool(body, "wpc", s->wpc) ? 1 : 0;
    s->raw_gma           = json_bool(body, "raw_gma", s->raw_gma) ? 1 : 0;
    s->lenc              = json_bool(body, "lenc", s->lenc) ? 1 : 0;
    s->dcw               = json_bool(body, "dcw", s->dcw) ? 1 : 0;
    s->hmirror           = json_bool(body, "hmirror", s->hmirror) ? 1 : 0;
    s->vflip             = json_bool(body, "vflip", s->vflip) ? 1 : 0;

    float parsed_cal_src[4][2];
    if (parse_cal_src(body, parsed_cal_src)) {
      memcpy(s->cal_src, parsed_cal_src, sizeof(s->cal_src));
      s->calibrated = true;
      uint16_t w, h;
      cam_capture_get_resolution(&w, &h);
      cam_settings_compute_homography(s, w, h);
      s_preview_homography_valid = false;
    }

    // Apply sensor controls live first (brightness/AE/AGC etc.).
    cam_capture_apply_sensor(s);
    cam_capture_set_ae_lock_interval(s->ae_lock_interval);

    // Rebuild camera pipeline if capture-related fields changed (e.g. resolution)
    bool need_reinit = (s->resolution != old.resolution) ||
                       (s->output_width != old.output_width) ||
                       (s->output_height != old.output_height) ||
                       (s->tiles_x != old.tiles_x) ||
                       (s->tiles_y != old.tiles_y);

    if (need_reinit) {
      cam_capture_release();
      esp_camera_deinit();
      if (!cam_capture_init(s)) {
        // revert on failure
        *s_settings = old;
        if (!cam_capture_init(&old)) {
          server->sendHeader("Access-Control-Allow-Origin", "*");
          server->send(500, "application/json", "{\"ok\":false,\"message\":\"Camera reinit failed and revert failed\"}");
          return;
        }
        ensure_preview_pipeline();
        server->sendHeader("Access-Control-Allow-Origin", "*");
        server->send(500, "application/json", "{\"ok\":false,\"message\":\"Camera reinit failed; reverted\"}");
        return;
      }
      ensure_preview_pipeline();
    }

    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->send(200, "application/json", "{\"ok\":true,\"message\":\"Applied\"}");
}

static void handle_reset() {
    if (s_settings) {
        cam_settings_defaults(s_settings);
        cam_settings_save(s_settings);
    }
    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->send(200, "application/json", "{\"message\":\"Reset to defaults. Rebooting.\"}");
    delay(2000);
    ESP.restart();
}

// GET /grid_calib - return current grid calibration as JSON
static void handle_get_grid() {
    if (!s_settings) { server->send(500, "text/plain", "No settings"); return; }
    cam_settings_t *s = s_settings;

    // Build JSON — heap-allocate for large grids (up to 256 points).
    const size_t GJSON_SZ = 8192;
    char *json = (char *)malloc(GJSON_SZ);
    if (!json) { server->send(500, "text/plain", "alloc fail"); return; }
    int n = snprintf(json, GJSON_SZ,
      "{\"grid_calibrated\":%s,\"grid_auto_generated\":%s,"
      "\"proj_w\":%.4f,\"proj_h\":%.4f,"
      "\"grid_dx\":%.4f,\"grid_dy\":%.4f,\"grid_nx\":%u,\"grid_ny\":%u,"
      "\"image_margin_left\":%u,\"image_margin_top\":%u,\"image_margin_right\":%u,\"image_margin_bottom\":%u,"
      "\"grid_points\":[",
      s->grid_calibrated ? "true" : "false",
      s->grid_auto_generated ? "true" : "false",
      s->surface_width, s->surface_height,
      s->grid_dx, s->grid_dy, s->grid_nx, s->grid_ny,
      (unsigned)s->image_margin_left, (unsigned)s->image_margin_top,
      (unsigned)s->image_margin_right, (unsigned)s->image_margin_bottom);
    (void)n;
    size_t off = strlen(json);
    for (uint16_t i = 0; i < s->grid_points_count && i < CAM_SETTINGS_MAX_GRID_POINTS; i++) {
        int r = snprintf(json + off, GJSON_SZ - off, "%s[%.6f,%.6f]",
                         (i==0)?"":",", s->grid_points[i][0], s->grid_points[i][1]);
        if (r <= 0) break;
        off += r;
    }
    if (off < GJSON_SZ - 4) {
      snprintf(json + off, GJSON_SZ - off, "]}" );
    } else {
      strncat(json, "]}", GJSON_SZ - strlen(json) - 1);
    }

    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->send(200, "application/json", json);
    free(json);
}

// Minimal parser for grid POST body
static bool parse_grid_points(const String &body,
                float *out_proj_w, float *out_proj_h,
                float *out_dx, float *out_dy,
                uint16_t *out_nx, uint16_t *out_ny,
                float out_points[][2], uint16_t *out_count,
                bool *out_clear) {
  *out_clear = false;
  if (body.indexOf("\"clear\":true") >= 0) { *out_clear = true; return true; }
  // keys: proj_w, proj_h, dx, dy, nx, ny, points
  *out_proj_w = json_float(body, "proj_w", NAN);
  *out_proj_h = json_float(body, "proj_h", NAN);
  *out_dx     = json_float(body, "dx", NAN);
  *out_dy     = json_float(body, "dy", NAN);
  *out_nx     = (uint16_t)json_int(body, "nx", 0);
  *out_ny     = (uint16_t)json_int(body, "ny", 0);

    int pts_idx = body.indexOf("\"points\"");
    if (pts_idx < 0) { *out_count = 0; return true; }
    int arr = body.indexOf('[', pts_idx);
    if (arr < 0) { *out_count = 0; return true; }
    int pos = arr + 1;
    uint16_t count = 0;
    while (count < CAM_SETTINGS_MAX_GRID_POINTS) {
      int open = body.indexOf('[', pos);
      if (open < 0) break;
      int comma = body.indexOf(',', open + 1);
      int close = body.indexOf(']', comma + 1);
      if (comma < 0 || close < 0) break;
      float x = body.substring(open + 1, comma).toFloat();
      float y = body.substring(comma + 1, close).toFloat();
      out_points[count][0] = x;
      out_points[count][1] = y;
      count++;
      pos = close + 1;
      // find next comma or closing
      if (body.indexOf(']', pos) == pos) break;
    }
    *out_count = count;
    return true;
}

static void handle_post_grid() {
    if (!s_settings) { server->send(500, "text/plain", "No settings"); return; }
    String body = server->arg("plain");
    float minx, maxx, miny, maxy, dx, dy;
    uint16_t nx, ny;
    float points[CAM_SETTINGS_MAX_GRID_POINTS][2];
    uint16_t count = 0;
    bool clear = false;
    float proj_w, proj_h;
    if (!parse_grid_points(body, &proj_w, &proj_h, &dx, &dy, &nx, &ny, points, &count, &clear)) {
      server->send(400, "application/json", "{\"error\":true,\"message\":\"Invalid payload\"}");
      return;
    }
    if (clear) {
      s_settings->grid_calibrated = false;
      s_settings->grid_points_count = 0;
      cam_settings_save(s_settings);
      server->sendHeader("Access-Control-Allow-Origin", "*");
      server->send(200, "application/json", "{\"message\":\"Grid cleared\"}");
      return;
    }

    s_settings->grid_calibrated = true;
    s_settings->grid_auto_generated = json_bool(body, "auto_generated", false);
    // Min assumed 0
    s_settings->grid_minx = 0.0f; s_settings->grid_maxx = proj_w;
    s_settings->grid_miny = 0.0f; s_settings->grid_maxy = proj_h;
    s_settings->grid_dx = dx; s_settings->grid_dy = dy;
    s_settings->grid_nx = nx; s_settings->grid_ny = ny;
    s_settings->image_margin_left   = (uint16_t)json_int(body, "margin_left", s_settings->image_margin_left);
    s_settings->image_margin_top    = (uint16_t)json_int(body, "margin_top", s_settings->image_margin_top);
    s_settings->image_margin_right  = (uint16_t)json_int(body, "margin_right", s_settings->image_margin_right);
    s_settings->image_margin_bottom = (uint16_t)json_int(body, "margin_bottom", s_settings->image_margin_bottom);
    s_settings->grid_points_count = count;
    for (uint16_t i = 0; i < count && i < CAM_SETTINGS_MAX_GRID_POINTS; i++) {
      s_settings->grid_points[i][0] = points[i][0];
      s_settings->grid_points[i][1] = points[i][1];
    }
    cam_settings_save(s_settings);

    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->send(200, "application/json", "{\"message\":\"Grid saved\"}");
    ESP_LOGI(TAG, "Grid calibration saved (%u points)", (unsigned)s_settings->grid_points_count);
}

static void handle_cors() {
    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server->sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server->send(204);
}

// ---------------------------------------------------------------------------
// Start / Stop
// ---------------------------------------------------------------------------
void cam_webserver_start(cam_settings_t *settings) {
    s_settings = settings;

    // Start AP.
    WiFi.mode(WIFI_AP);
    WiFi.softAP(settings->ap_ssid,
                settings->ap_pass[0] ? settings->ap_pass : nullptr);

    IPAddress ip = WiFi.softAPIP();
    ESP_LOGI(TAG, "AP started: SSID=%s  IP=%s", settings->ap_ssid, ip.toString().c_str());

    // Reuse existing camera if already running (normal mode → webserver toggle).
    // Only (re)init when the camera isn't ready (e.g. initial config boot).
    if (cam_capture_is_ready()) {
      ESP_LOGI(TAG, "Camera already running — reusing for AP mode");
      ensure_preview_pipeline();
    } else if (!cam_capture_init(settings)) {
      ESP_LOGE(TAG, "Camera init in AP mode failed: %s (0x%x)",
           cam_capture_last_error_name(), (unsigned)cam_capture_last_error());
    } else {
      ESP_LOGI(TAG, "Camera init in AP mode OK");
      ensure_preview_pipeline();
    }

    // Start web server.
    server = new WebServer(80);
    server->on("/",         HTTP_GET,  handle_root);
    server->on("/snapshot", HTTP_GET,  handle_snapshot);
    server->on("/stream",   HTTP_GET,  [](){ handle_stream(); });
    server->on("/config",   HTTP_GET,  handle_get_config);
    server->on("/config",   HTTP_POST, handle_post_config);
    server->on("/config",   HTTP_OPTIONS, handle_cors);
    server->on("/apply_sensor", HTTP_POST, handle_apply_sensor);
    server->on("/apply_sensor", HTTP_OPTIONS, handle_cors);
    server->on("/apply_camera", HTTP_POST, handle_apply_camera);
    server->on("/apply_camera", HTTP_OPTIONS, handle_cors);
    server->on("/preview_calib", HTTP_POST, handle_post_preview_calib);
    server->on("/preview_calib", HTTP_OPTIONS, handle_cors);
    server->on("/grid_calib", HTTP_GET, handle_get_grid);
    server->on("/grid_calib", HTTP_POST, handle_post_grid);
    server->on("/grid_calib", HTTP_OPTIONS, handle_cors);
    server->on("/reset",    HTTP_POST, handle_reset);
    server->begin();
    s_running = true;

    ESP_LOGI(TAG, "Web server started on port 80");
}

void cam_webserver_stop(void) {
    if (server) {
        server->stop();
        delete server;
        server = nullptr;
    }
    WiFi.softAPdisconnect(true);
    if (s_preview_transform) {
      cam_transform_destroy(s_preview_transform);
      s_preview_transform = nullptr;
    }
    if (s_preview_buf) {
      heap_caps_free(s_preview_buf);
      s_preview_buf = nullptr;
    }
    s_preview_w = 0;
    s_preview_h = 0;
    s_preview_cap_w = 0;
    s_preview_cap_h = 0;
    s_preview_homography_valid = false;
    s_running = false;
}

bool cam_webserver_is_running(void) {
    return s_running;
}

// Stream handler implemented as a blocking handler — WebServer invokes
// it when a client connects to /stream. It writes a multipart MJPEG
// response until the client disconnects.
static void handle_stream() {
    WiFiClient client = server->client();
    if (!client || !client.connected()) return;

    // Send initial headers
    // Use WebServer to send the initial response headers so the framework
    // knows the socket is in use; then obtain the raw client for streaming.
    server->sendHeader("Connection", "close");
    server->send(200, "multipart/x-mixed-replace; boundary=frame", "");

    client = server->client();
    if (!client || !client.connected()) return;

  ESP_LOGI(TAG, "stream: client connected (processed)");

    while (client.connected() && s_running) {
        uint8_t *buf = nullptr;
        size_t len = 0;
        if (!capture_processed_jpeg(&buf, &len)) {
          ESP_LOGE(TAG, "stream: processed capture failed (%s)", cam_capture_last_error_name());
          break;
        }

        client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", (unsigned)len);
        client.write(buf, len);
        client.print("\r\n");
        free(buf);

        // Yield and limit frame rate (~10-12 fps)
        delay(100);
    }

    if (client.connected()) {
        client.print("--frame--\r\n");
        client.stop();
    }
    ESP_LOGI(TAG, "stream: client disconnected");
}

void cam_webserver_handle(void) {
    if (server && s_running) {
        server->handleClient();
    }
}
