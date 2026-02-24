// cam_receiver.c — Camera stream receiver implementation
//
// Assembles tile-based JPEG frames from cam-protocol messages into a single
// RGB565 frame buffer.  Diff frames only overwrite changed tiles, preserving
// previous pixel data for unchanged regions.

#define UI_DEBUG_LOCAL_LEVEL D_DEBUG
#include "debug.h"

#include "cam_receiver.h"
#include "cam_protocol.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

/* Prefer libjpeg-turbo when enabled in LVGL config */
#include "lv_conf.h"

#if defined(LV_USE_LIBJPEG_TURBO) && LV_USE_LIBJPEG_TURBO
#include "lv_conf.h"
#include <jpeglib.h>
#define CAM_USE_LIBJPEG_TURBO 1
#else
/* Software JPEG decoder: TJpgDec (lib/tjpgd_cam, JD_FORMAT=1 → RGB565 output) */
#include "tjpgd.h"
#define CAM_USE_TJPGD 1
#endif
#else
#include <pthread.h>
#include <sys/time.h>
#endif

static const char *TAG = "cam_recv";

// ---------------------------------------------------------------------------
// Dismissable debug system
// ---------------------------------------------------------------------------
// All CAM_RECV_DEBUG blocks are compiled out by default when
// -DCAM_RECV_NODEBUG is defined at build time.  Remove that define (or pass
// -DCAM_RECV_DEBUG=1 explicitly) to re-enable the verbose diagnostics.
//
//   Covers:
//   - Waiting-for-keyframe state (diff frames dropped before first key frame)
//   - Frame-start / frame-end lifecycle with timing
//   - Tile assembly: chunk arrivals, buffer overflows, completeness
//   - JPEG decode failures
//   - Frame-ID mismatches between FRAME_START and subsequent TILE_CHUNKs
//   - Abandoned assemblies (FRAME_END never arrived)
//   - STATUS heartbeat timeouts (camera may have disconnected)
//   - Periodic aggregate stats every 100 frames

#ifndef CAM_RECV_NODEBUG
#  define CAM_RECV_DEBUG
#endif

#ifdef CAM_RECV_DEBUG
#  define CAM_DBG(fmt, ...)  LOGD(TAG, "[dbg] " fmt, ##__VA_ARGS__)
#  define CAM_VERB(fmt, ...) LOGV(TAG, "[dbg] " fmt, ##__VA_ARGS__)
#else
#  define CAM_DBG(fmt, ...)  do {} while (0)
#  define CAM_VERB(fmt, ...) do {} while (0)
#endif

// Warn if FRAME_END is not seen within this many ms of FRAME_START.
#define CAM_RECV_FRAME_TIMEOUT_MS   2000u
// Warn if no STATUS heartbeat is received within this many ms.
#define CAM_RECV_STATUS_TIMEOUT_MS  5000u

// ---------------------------------------------------------------------------
// Platform helpers
// ---------------------------------------------------------------------------

#ifdef ESP_PLATFORM
  #define CAM_ALLOC_LARGE(sz)    heap_caps_malloc((sz), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
  #define CAM_FREE(p)            free(p)
  #define CAM_MUTEX_T            SemaphoreHandle_t
  #define CAM_MUTEX_CREATE()     xSemaphoreCreateMutex()
  #define CAM_MUTEX_LOCK(m)      xSemaphoreTake((m), portMAX_DELAY)
  #define CAM_MUTEX_UNLOCK(m)    xSemaphoreGive((m))
  #define CAM_MUTEX_DESTROY(m)   vSemaphoreDelete((m))
  static inline uint32_t cam_millis(void) {
      return (uint32_t)(esp_timer_get_time() / 1000);
  }
#else
  #define CAM_ALLOC_LARGE(sz)    malloc(sz)
  #define CAM_FREE(p)            free(p)
  #define CAM_MUTEX_T            pthread_mutex_t
  #define CAM_MUTEX_CREATE()     cam_posix_mutex_create_()
  #define CAM_MUTEX_LOCK(m)      pthread_mutex_lock(&(m))
  #define CAM_MUTEX_UNLOCK(m)    pthread_mutex_unlock(&(m))
  #define CAM_MUTEX_DESTROY(m)   pthread_mutex_destroy(&(m))
  static inline pthread_mutex_t cam_posix_mutex_create_(void) {
      pthread_mutex_t m; pthread_mutex_init(&m, NULL); return m;
  }
  static inline uint32_t cam_millis(void) {
      struct timeval tv; gettimeofday(&tv, NULL);
      return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
  }
#endif

// ---------------------------------------------------------------------------
// Callback slot
// ---------------------------------------------------------------------------
typedef struct { void *cb; void *user; } cb_slot_t;

// ---------------------------------------------------------------------------
// Per-tile assembly state
// ---------------------------------------------------------------------------
typedef struct {
    int8_t    pool_idx;          // Index into pool[], or -1 if no buffer assigned
    uint16_t  pool_gen;          // Generation of pool slot when assigned
    uint16_t  jpeg_len;          // Expected total JPEG length (from first chunk)
    uint16_t  received_len;      // Bytes accumulated so far
    uint8_t   total_chunks;
    uint8_t   chunks_received;   // Bitmask would be better, but count is fine
    bool      complete;
    bool      decoded;
} tile_asm_t;

// ---------------------------------------------------------------------------
// JPEG buffer pool — small set of reusable TILE_BUF_SIZE buffers.
// Since tiles decode immediately on completion, at most 1–2 are in-flight.
// ---------------------------------------------------------------------------
typedef struct {
    uint8_t  *buf;               // Heap-allocated TILE_BUF_SIZE buffer
    uint16_t  gen;               // Incremented every time the slot is recycled
    int8_t    owner_tile;        // Tile index using this slot, or -1 if free
    uint32_t  last_use;          // Monotonic counter for LRU eviction
} pool_slot_t;

// ---------------------------------------------------------------------------
// Receiver state
// ---------------------------------------------------------------------------
struct cam_receiver {
    cam_transport_t    *transport;

    // --- Frame buffer (single-buffered with mutex) ---
    uint8_t            *frame_buf;       // RGB565 pixel data
    size_t              frame_buf_size;
    uint16_t            img_width;
    uint16_t            img_height;
    uint16_t            max_width;
    uint16_t            max_height;
    CAM_MUTEX_T         display_mutex;
    CAM_MUTEX_T         grid_mutex;    ///< Protects grid heap pointers (px_points, points)

    // --- Frame assembly ---
    bool                assembling;
    uint16_t            asm_frame_id;
    uint8_t             asm_frame_type;  // cam_frame_type_t
    uint8_t             asm_tiles_x;
    uint8_t             asm_tiles_y;
    uint8_t             asm_encoding;
    uint8_t             asm_num_tiles;   // Number of tiles flagged in bitmap
    uint8_t             asm_tile_bitmap[(CAM_RECEIVER_MAX_TILES + 7) / 8];
    tile_asm_t          tiles[CAM_RECEIVER_MAX_TILES];

    // --- JPEG buffer pool ---
    pool_slot_t         pool[CAM_RECEIVER_POOL_SIZE];
    uint32_t            pool_clock;      // Monotonic counter for LRU

    // --- Tile JPEG decode temp buffer ---
    uint8_t            *tile_decode_buf;
    size_t              tile_decode_buf_size;

    // --- Published frame info ---
    cam_frame_info_t    last_frame;
    bool                has_frame;

    // --- Grid calibration ---
    cam_grid_info_t     grid;
    bool                has_grid;

    // --- Stored configuration for auto-resend on camera reconnect ---
    cam_config_cmd_t    stored_cfg;         ///< Last config pushed via cam_receiver_send_config
    bool                has_stored_cfg;     ///< True once at least one config has been sent
    uint32_t            last_cfg_send_ms;   ///< millis() of last auto-resend (0 = never)
    uint8_t             cfg_warmup_sends;   ///< Config resend counter during early-contact warmup

    // --- Device status ---
    cam_device_status_msg_t status;
    bool                has_status;

    // --- Callbacks ---
    cb_slot_t           frame_cbs[CAM_RECEIVER_MAX_CALLBACKS];
    cb_slot_t           grid_cbs[CAM_RECEIVER_MAX_CALLBACKS];
    cb_slot_t           status_cbs[CAM_RECEIVER_MAX_CALLBACKS];

    bool                running;

    // --- Keyframe / diff tracking (always present, affects correctness) ---
    bool                waiting_for_keyframe;  ///< true until first KEY frame received

    // --- Streaming session state ---
    bool                in_stream;              ///< STREAM_START sent, session active
    uint32_t            last_reconnect_ms;      ///< Monotonic ms of last reconnect beacon
    uint16_t            last_status_frame_id;   ///< frame_id from previous STATUS (reboot detection)

    // --- Desired output resolution (sent in STREAM_START) ---
    uint16_t            desired_width;
    uint16_t            desired_height;

    // --- TJpgDec work buffer (heap-allocated, reused per tile) ---
    uint8_t            *tjpgd_work;       ///< Work pool for jd_prepare/jd_decomp

    // --- Diagnostic counters and timestamps ---
    uint32_t            asm_start_ms;         ///< millis when current FRAME_START arrived
    uint32_t            last_status_ms;        ///< millis of last STATUS heartbeat (0=none)
    uint32_t            last_kf_request_ms;    ///< millis of last force-keyframe request (throttle)
    uint32_t            dbg_frames_complete;   ///< total frames fully decoded
    uint32_t            dbg_frames_errors;     ///< frames that had ≥1 incomplete tile
    uint32_t            dbg_diffs_skipped;     ///< diff frames dropped before first key
    uint32_t            dbg_chunks_mismatch;   ///< tile chunks with wrong frame_id
    uint32_t            dbg_decode_failures;   ///< JPEG decode attempts that failed
};

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static void on_transport_rx(const uint8_t *data, size_t len, void *user_data);
static void handle_frame_start(cam_receiver_t *r, const uint8_t *data, size_t len);
static void handle_tile_chunk(cam_receiver_t *r, const uint8_t *data, size_t len);
static void handle_frame_end(cam_receiver_t *r, const uint8_t *data, size_t len);
static void handle_status(cam_receiver_t *r, const uint8_t *data, size_t len);
static void handle_grid_map(cam_receiver_t *r, const uint8_t *data, size_t len);
static int  decode_tile_into_frame(cam_receiver_t *r, uint8_t tile_idx);

// ---------------------------------------------------------------------------
// Callback helpers
// ---------------------------------------------------------------------------
static void add_cb(cb_slot_t slots[], void *cb, void *user)
{
    for (int i = 0; i < CAM_RECEIVER_MAX_CALLBACKS; i++) {
        if (!slots[i].cb) { slots[i].cb = cb; slots[i].user = user; return; }
    }
    LOGW(TAG, "callback slot full");
}

static void remove_cb(cb_slot_t slots[], void *cb)
{
    for (int i = 0; i < CAM_RECEIVER_MAX_CALLBACKS; i++) {
        if (slots[i].cb == cb) { slots[i].cb = NULL; slots[i].user = NULL; return; }
    }
}

#define NOTIFY_CBS(slots, type, arg)                                             \
    do {                                                                         \
        for (int _i = 0; _i < CAM_RECEIVER_MAX_CALLBACKS; _i++) {               \
            if (slots[_i].cb)                                                    \
                ((type)slots[_i].cb)(arg, slots[_i].user);                       \
        }                                                                        \
    } while (0)

// ---------------------------------------------------------------------------
// JPEG buffer pool helpers
// ---------------------------------------------------------------------------

// Release the pool slot assigned to a tile (if any).
static void pool_release(cam_receiver_t *r, uint8_t tile_idx)
{
    tile_asm_t *ta = &r->tiles[tile_idx];
    if (ta->pool_idx >= 0 && ta->pool_idx < CAM_RECEIVER_POOL_SIZE) {
        pool_slot_t *ps = &r->pool[ta->pool_idx];
        // Only release if the slot still belongs to this tile (not evicted).
        if (ps->gen == ta->pool_gen && ps->owner_tile == (int8_t)tile_idx) {
            ps->owner_tile = -1;
        }
    }
    ta->pool_idx = -1;
}

// Acquire a pool buffer for a tile.  Returns the JPEG buffer pointer, or
// NULL on failure (shouldn't happen with pool_size >= 2).  On eviction the
// previous owner's tile_asm is invalidated so stale chunks get rejected.
static uint8_t *pool_acquire(cam_receiver_t *r, uint8_t tile_idx)
{
    tile_asm_t *ta = &r->tiles[tile_idx];

    // If this tile already holds a valid slot, reuse it.
    if (ta->pool_idx >= 0 && ta->pool_idx < CAM_RECEIVER_POOL_SIZE) {
        pool_slot_t *ps = &r->pool[ta->pool_idx];
        if (ps->gen == ta->pool_gen && ps->owner_tile == (int8_t)tile_idx) {
            ps->last_use = ++r->pool_clock;
            return ps->buf;
        }
    }

    // Try to find a free slot.
    int best = -1;
    for (int i = 0; i < CAM_RECEIVER_POOL_SIZE; i++) {
        if (r->pool[i].owner_tile < 0) { best = i; break; }
    }

    // No free slot — evict LRU.
    if (best < 0) {
        uint32_t oldest = UINT32_MAX;
        for (int i = 0; i < CAM_RECEIVER_POOL_SIZE; i++) {
            if (r->pool[i].last_use < oldest) {
                oldest = r->pool[i].last_use;
                best = i;
            }
        }
        // Invalidate the evicted tile so its future chunks are discarded.
        pool_slot_t *evicted = &r->pool[best];
        if (evicted->owner_tile >= 0 && evicted->owner_tile < CAM_RECEIVER_MAX_TILES) {
            r->tiles[(uint8_t)evicted->owner_tile].pool_idx = -1;
            CAM_DBG("POOL: evicted tile %d (gen %u) from slot %d for tile %d",
                    evicted->owner_tile, evicted->gen, best, tile_idx);
        }
    }

    // Assign the slot.
    pool_slot_t *ps = &r->pool[best];
    ps->gen++;
    ps->owner_tile = (int8_t)tile_idx;
    ps->last_use   = ++r->pool_clock;
    ta->pool_idx   = (int8_t)best;
    ta->pool_gen   = ps->gen;
    return ps->buf;
}

// Get the JPEG buffer for a tile, or NULL if the tile's slot was evicted.
static inline uint8_t *pool_get_buf(cam_receiver_t *r, uint8_t tile_idx)
{
    tile_asm_t *ta = &r->tiles[tile_idx];
    if (ta->pool_idx < 0 || ta->pool_idx >= CAM_RECEIVER_POOL_SIZE)
        return NULL;
    pool_slot_t *ps = &r->pool[ta->pool_idx];
    if (ps->gen != ta->pool_gen || ps->owner_tile != (int8_t)tile_idx)
        return NULL;  // Evicted — stale reference.
    return ps->buf;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

cam_receiver_t *cam_receiver_create(const cam_receiver_config_t *cfg)
{
    if (!cfg || !cfg->transport) return NULL;

    cam_receiver_t *r = calloc(1, sizeof(*r));
    if (!r) return NULL;

    r->transport  = cfg->transport;
    r->max_width  = cfg->max_width  ? cfg->max_width  : 640;
    r->max_height = cfg->max_height ? cfg->max_height : 480;
    r->desired_width  = cfg->desired_width;
    r->desired_height = cfg->desired_height;

    // Allocate the frame buffer (max resolution)
    r->frame_buf_size = (size_t)r->max_width * r->max_height * 2;
    r->frame_buf = CAM_ALLOC_LARGE(r->frame_buf_size);
    if (!r->frame_buf) {
        LOGE(TAG, "Failed to alloc frame_buf (%u bytes)", (unsigned)r->frame_buf_size);
        free(r);
        return NULL;
    }
    memset(r->frame_buf, 0, r->frame_buf_size);

    // Allocate JPEG buffer pool (small number of reusable buffers).
    // Tiles decode immediately on completion so at most 1–2 are in-flight.
    for (int i = 0; i < CAM_RECEIVER_POOL_SIZE; i++) {
        r->pool[i].buf = CAM_ALLOC_LARGE(CAM_RECEIVER_TILE_BUF_SIZE);
        if (!r->pool[i].buf) {
            LOGE(TAG, "Failed to alloc pool slot %d/%d (%u bytes each)",
                 i, CAM_RECEIVER_POOL_SIZE, (unsigned)CAM_RECEIVER_TILE_BUF_SIZE);
            for (int j = 0; j < i; j++) CAM_FREE(r->pool[j].buf);
            CAM_FREE(r->frame_buf);
            free(r);
            return NULL;
        }
        r->pool[i].gen        = 0;
        r->pool[i].owner_tile = -1;
        r->pool[i].last_use   = 0;
    }
    r->pool_clock = 0;

    // Initialise tile assembly slots (no buffer assigned yet).
    for (int i = 0; i < CAM_RECEIVER_MAX_TILES; i++) {
        r->tiles[i].pool_idx = -1;
    }

    // Neither TJpgDec (direct-to-frame_buf) nor libjpeg-turbo (alloc_sarray
    // row buffers) requires a persistent intermediate tile buffer.  Skip the
    // allocation to reclaim ~50 KB of PSRAM.
    r->tile_decode_buf      = NULL;
    r->tile_decode_buf_size = 0;

#ifdef CAM_USE_TJPGD
    r->tjpgd_work = CAM_ALLOC_LARGE(4096);
    if (!r->tjpgd_work) {
            for (int i = 0; i < CAM_RECEIVER_POOL_SIZE; i++) CAM_FREE(r->pool[i].buf);
            CAM_FREE(r->frame_buf);
            free(r);
        return NULL;
    }
#endif

    r->display_mutex = CAM_MUTEX_CREATE();
    r->grid_mutex    = CAM_MUTEX_CREATE();
    r->waiting_for_keyframe = true;  // Must receive a key frame before diffs work

    // Pre-populate stored_cfg with defaults so that the very first STATUS
    // heartbeat from the camera triggers an immediate config send — even if
    // the caller never explicitly calls cam_receiver_send_config().
    // swap_bytes is the critical field: it must match the build-time display
    // configuration (LV_COLOR_16_SWAP / CAM_RECV_COLOR_SWAP).
    r->stored_cfg.type             = CAM_CMD_SET_CONFIG;
    r->stored_cfg.resolution       = CAM_DEFAULT_RESOLUTION;
    r->stored_cfg.jpeg_quality     = CAM_DEFAULT_JPEG_QUALITY;
    r->stored_cfg.tiles_x          = CAM_DEFAULT_TILES_X;
    r->stored_cfg.tiles_y          = CAM_DEFAULT_TILES_Y;
    r->stored_cfg.diff_threshold   = CAM_DEFAULT_DIFF_THRESHOLD;
    r->stored_cfg.keyframe_interval = CAM_DEFAULT_KEYFRAME_INTERVAL;
    r->stored_cfg.brightness       = 0;
    r->stored_cfg.contrast         = 0;
    r->stored_cfg.aec_enable       = 1;
    r->stored_cfg.agc_enable       = 1;
    r->stored_cfg.aec_value        = 0;
    r->stored_cfg.agc_gain         = 0;
    // Identity homography (no perspective correction by default)
    r->stored_cfg.homography[0] = 1.0f; r->stored_cfg.homography[1] = 0.0f; r->stored_cfg.homography[2] = 0.0f;
    r->stored_cfg.homography[3] = 0.0f; r->stored_cfg.homography[4] = 1.0f; r->stored_cfg.homography[5] = 0.0f;
    r->stored_cfg.homography[6] = 0.0f; r->stored_cfg.homography[7] = 0.0f; r->stored_cfg.homography[8] = 1.0f;
    // Colour transforms are applied on the pendant side (cam_tjd_outfunc swaps
    // R↔B to correct for tjpgd's BGR565 output).  Camera-side transforms are
    // all off by default; they remain available via cam_receiver_send_config()
    // for displays with non-standard colour requirements.
    r->stored_cfg.swap_rb        = 0;
    r->stored_cfg.swap_bytes     = 0;
    r->stored_cfg.invert_colors  = 0;
    // Extended sensor controls (v2 fields).
    r->stored_cfg.ae_level       = CAM_DEFAULT_AE_LEVEL;
    r->stored_cfg.gainceiling    = CAM_DEFAULT_GAINCEILING;
    r->stored_cfg.bpc            = CAM_DEFAULT_BPC;
    r->stored_cfg.wpc            = CAM_DEFAULT_WPC;
    r->stored_cfg.raw_gma        = CAM_DEFAULT_RAW_GMA;
    r->stored_cfg.lenc           = CAM_DEFAULT_LENC;
    r->stored_cfg.hmirror        = CAM_DEFAULT_HMIRROR;
    r->stored_cfg.vflip          = CAM_DEFAULT_VFLIP;
    r->stored_cfg.dcw            = CAM_DEFAULT_DCW;
    r->stored_cfg.saturation     = CAM_DEFAULT_SATURATION;
    r->stored_cfg.sharpness      = CAM_DEFAULT_SHARPNESS;
    r->stored_cfg.denoise        = CAM_DEFAULT_DENOISE;
    r->stored_cfg.aec2           = CAM_DEFAULT_AEC2;
    r->stored_cfg.wb_mode        = CAM_DEFAULT_WB_MODE;
    r->has_stored_cfg    = true;
    r->last_cfg_send_ms  = 0;  // Force immediate send on first STATUS
    r->cfg_warmup_sends  = 0;

    LOGI(TAG, "Receiver created (max %ux%u, frame buf %u bytes, pool %d×%u)",
         r->max_width, r->max_height, (unsigned)r->frame_buf_size,
         CAM_RECEIVER_POOL_SIZE, (unsigned)CAM_RECEIVER_TILE_BUF_SIZE);
    CAM_DBG("Receiver %p waiting for initial key frame", (void *)r);
    return r;
}

void cam_receiver_destroy(cam_receiver_t *self)
{
    if (!self) return;
    cam_receiver_stop(self);

    for (int i = 0; i < CAM_RECEIVER_POOL_SIZE; i++) {
        CAM_FREE(self->pool[i].buf);
    }
    if (self->tile_decode_buf) CAM_FREE(self->tile_decode_buf);
    CAM_FREE(self->frame_buf);
#ifdef CAM_USE_TJPGD
    CAM_FREE(self->tjpgd_work);
#endif
    if (self->grid.points)    CAM_FREE(self->grid.points);
    if (self->grid.px_points) CAM_FREE(self->grid.px_points);

    CAM_MUTEX_DESTROY(self->display_mutex);
    CAM_MUTEX_DESTROY(self->grid_mutex);
    free(self);
}

void cam_receiver_start(cam_receiver_t *self)
{
    if (!self || self->running) return;
    cam_transport_set_rx_callback(self->transport, on_transport_rx, self);
    cam_transport_start(self->transport);
    self->running = true;

    // Proactively broadcast the initial config (swap_bytes, swap_rb) so the
    // camera applies the correct pixel format before the first frame is sent.
    // We use broadcast because the camera's MAC is not yet known.
    if (self->has_stored_cfg) {
        self->cfg_warmup_sends = 0;
        self->last_cfg_send_ms = 0;  // Allow STATUS handler to also send immediately
        int rc = cam_transport_send(self->transport,
                                    (const uint8_t *)&self->stored_cfg,
                                    sizeof(self->stored_cfg));
        LOGI(TAG, "Startup config broadcast: swap_bytes=%d swap_rb=%d (rc=%d)",
             self->stored_cfg.swap_bytes, self->stored_cfg.swap_rb, rc);
    }

    LOGI(TAG, "Receiver started");
}

void cam_receiver_stop(cam_receiver_t *self)
{
    if (!self || !self->running) return;
    self->running = false;
    cam_transport_stop(self->transport);
    cam_transport_set_rx_callback(self->transport, NULL, NULL);
    LOGI(TAG, "Receiver stopped");
}

// ---------------------------------------------------------------------------
// Callback registration
// ---------------------------------------------------------------------------

void cam_receiver_add_frame_cb(cam_receiver_t *s, cam_frame_ready_cb_t cb, void *u)
{ add_cb(s->frame_cbs, (void *)cb, u); }
void cam_receiver_remove_frame_cb(cam_receiver_t *s, cam_frame_ready_cb_t cb)
{ remove_cb(s->frame_cbs, (void *)cb); }

void cam_receiver_add_grid_cb(cam_receiver_t *s, cam_grid_update_cb_t cb, void *u)
{ add_cb(s->grid_cbs, (void *)cb, u); }
void cam_receiver_remove_grid_cb(cam_receiver_t *s, cam_grid_update_cb_t cb)
{ remove_cb(s->grid_cbs, (void *)cb); }

void cam_receiver_add_status_cb(cam_receiver_t *s, cam_status_cb_t cb, void *u)
{ add_cb(s->status_cbs, (void *)cb, u); }
void cam_receiver_remove_status_cb(cam_receiver_t *s, cam_status_cb_t cb)
{ remove_cb(s->status_cbs, (void *)cb); }

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

const cam_frame_info_t *cam_receiver_get_last_frame(cam_receiver_t *s)
{ return s->has_frame ? &s->last_frame : NULL; }

const cam_grid_info_t *cam_receiver_get_grid(cam_receiver_t *s)
{ return s->has_grid ? &s->grid : NULL; }

const cam_device_status_msg_t *cam_receiver_get_status(cam_receiver_t *s)
{ return s->has_status ? &s->status : NULL; }

bool cam_receiver_has_frame(cam_receiver_t *s)
{ return s && s->has_frame; }

void cam_receiver_lock_display(cam_receiver_t *s)
{ if (s) CAM_MUTEX_LOCK(s->display_mutex); }

void cam_receiver_unlock_display(cam_receiver_t *s)
{ if (s) CAM_MUTEX_UNLOCK(s->display_mutex); }

void cam_receiver_lock_grid(cam_receiver_t *s)
{ if (s) CAM_MUTEX_LOCK(s->grid_mutex); }

void cam_receiver_unlock_grid(cam_receiver_t *s)
{ if (s) CAM_MUTEX_UNLOCK(s->grid_mutex); }

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

int cam_receiver_request_frame(cam_receiver_t *self)
{
    // During the warmup window (first 8 frame requests, or until the camera
    // has confirmed with a STATUS) re-send the config before the frame
    // request so the camera is guaranteed to have the correct pixel format
    // before it encodes. ESP-NOW packets from the same sender are processed
    // in order on the remote side.
    if (self->has_stored_cfg && self->cfg_warmup_sends < 8) {
        self->cfg_warmup_sends++;
        cam_transport_send(self->transport,
                           (const uint8_t *)&self->stored_cfg,
                           sizeof(self->stored_cfg));
    }

    cam_request_frame_cmd_t cmd = {
        .type          = CAM_CMD_REQUEST_FRAME,
        .last_frame_id = self->last_frame.frame_id,
        .flags         = 0,
    };
    return cam_transport_send(self->transport, (const uint8_t *)&cmd, sizeof(cmd));
}

int cam_receiver_force_keyframe(cam_receiver_t *self)
{
    cam_force_keyframe_cmd_t cmd = { .type = CAM_CMD_FORCE_KEYFRAME };
    return cam_transport_send(self->transport, (const uint8_t *)&cmd, sizeof(cmd));
}

int cam_receiver_send_config(cam_receiver_t *self, const cam_config_cmd_t *cfg)
{
    // Ensure the swap_bytes field reflects the pendant build configuration
    // (LV_COLOR_16_SWAP / CAM_RECV_COLOR_SWAP).  We don't modify the caller's
    // struct; work on a local copy so the camera always gets the right value.
    cam_config_cmd_t local_cfg;
    memcpy(&local_cfg, cfg, sizeof(local_cfg));
#if (defined(LV_COLOR_16_SWAP) && (LV_COLOR_16_SWAP != 0)) || (defined(CAM_RECV_COLOR_SWAP) && (CAM_RECV_COLOR_SWAP != 0))
    local_cfg.swap_rb = 1;
    local_cfg.swap_bytes = 0;
#else
    local_cfg.swap_bytes = 0;
#endif

    // Remember this config so it can be automatically re-sent every time a
    // fresh STATUS heartbeat is received (keeps the camera in sync after a
    // reboot or reconnect without requiring the caller to re-push settings).
    self->stored_cfg       = local_cfg;
    self->has_stored_cfg   = true;
    self->last_cfg_send_ms = cam_millis();
    self->cfg_warmup_sends = 0;  // Re-arm warmup so next request_frame calls also re-send

    LOGD(TAG, "Sending config: swap_bytes=%d swap_rb=%d",
         local_cfg.swap_bytes, local_cfg.swap_rb);

    return cam_transport_send(self->transport, (const uint8_t *)&local_cfg, sizeof(local_cfg));
}

// ---------------------------------------------------------------------------
// Streaming session management
// ---------------------------------------------------------------------------

void cam_receiver_start_stream(cam_receiver_t *self)
{
    if (!self) return;
    self->in_stream          = true;
    self->waiting_for_keyframe = true;   // Always re-baseline on a new session
    self->last_reconnect_ms  = 0;        // Allow immediate beacon if needed

    cam_stream_start_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type  = CAM_CMD_STREAM_START;
    cmd.flags = 0x01;  // request keyframe
    cmd.desired_width  = self->desired_width;
    cmd.desired_height = self->desired_height;

    // Re-arm config warmup so the camera gets correct swap_bytes on reconnect.
    self->cfg_warmup_sends = 0;

    if (self->has_stored_cfg) {
        cam_transport_send(self->transport,
                           (const uint8_t *)&self->stored_cfg,
                           sizeof(self->stored_cfg));
    }
    cam_transport_send(self->transport, (const uint8_t *)&cmd, sizeof(cmd));
    LOGI(TAG, "Stream started — sent STREAM_START (desired %ux%u)",
         cmd.desired_width, cmd.desired_height);
}

void cam_receiver_stop_stream(cam_receiver_t *self)
{
    if (!self || !self->in_stream) return;
    self->in_stream = false;

    cam_stream_stop_cmd_t cmd;
    cmd.type = CAM_CMD_STREAM_STOP;
    cam_transport_send(self->transport, (const uint8_t *)&cmd, sizeof(cmd));
    LOGI(TAG, "Stream stopped — sent STREAM_STOP");
}

bool cam_receiver_is_streaming(cam_receiver_t *self)
{
    return self && self->in_stream;
}

bool cam_receiver_tick(cam_receiver_t *self)
{
    if (!self || !self->running || !self->in_stream) return false;

    uint32_t now = cam_millis();

    // If we haven't received a STATUS heartbeat from the camera for >5 s,
    // or if we've never seen one, broadcast a STREAM_START + config every 3 s
    // as a rediscovery beacon (handles camera reboot while pendant is open).
    bool status_stale = (self->last_status_ms == 0) ||
                        ((now - self->last_status_ms) > 5000u);
    if (status_stale && (now - self->last_reconnect_ms) > 3000u) {
        self->last_reconnect_ms = now;
        if (self->has_stored_cfg) {
            cam_transport_send(self->transport,
                               (const uint8_t *)&self->stored_cfg,
                               sizeof(self->stored_cfg));
        }
        cam_stream_start_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type  = CAM_CMD_STREAM_START;
        cmd.flags = 0x01;
        cmd.desired_width  = self->desired_width;
        cmd.desired_height = self->desired_height;
        cam_transport_send(self->transport, (const uint8_t *)&cmd, sizeof(cmd));
        LOGD(TAG, "Reconnect beacon sent (last_status %u ms ago)",
             self->last_status_ms ? (now - self->last_status_ms) : 0u);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Transport RX dispatcher
// ---------------------------------------------------------------------------

static void on_transport_rx(const uint8_t *data, size_t len, void *user_data)
{
    cam_receiver_t *r = (cam_receiver_t *)user_data;
    if (!r || !r->running || len == 0) return;

    uint8_t msg_type = data[0];
    switch (msg_type) {
        case CAM_MSG_FRAME_START:  handle_frame_start(r, data, len); break;
        case CAM_MSG_TILE_CHUNK:   handle_tile_chunk(r, data, len);  break;
        case CAM_MSG_FRAME_END:    handle_frame_end(r, data, len);   break;
        case CAM_MSG_STATUS:       handle_status(r, data, len);      break;
        case CAM_MSG_GRID_MAP:     handle_grid_map(r, data, len);    break;
        default:
            LOGD(TAG, "Unknown msg type 0x%02X (len %u)", msg_type, (unsigned)len);
            CAM_DBG("RX: unrecognised message type 0x%02X length %u \u2014 discarded",
                    msg_type, (unsigned)len);
            break;
    }
}

// ---------------------------------------------------------------------------
// FRAME_START handler
// ---------------------------------------------------------------------------

static void handle_frame_start(cam_receiver_t *r, const uint8_t *data, size_t len)
{
    if (len < CAM_FRAME_START_HEADER_SIZE) {
        LOGW(TAG, "FRAME_START too short (%u)", (unsigned)len);
        CAM_DBG("FRAME_START: rejected — payload %u < header %u bytes",
                (unsigned)len, (unsigned)CAM_FRAME_START_HEADER_SIZE);
        return;
    }

    const cam_frame_start_msg_t *msg = (const cam_frame_start_msg_t *)data;

    // Validate dimensions — dynamically reallocate if frame is larger than
    // current buffer, up to a hard limit (UXGA 1600×1200 = ~3.8 MB).
    #define CAM_RECV_HARD_MAX_W  1600
    #define CAM_RECV_HARD_MAX_H  1200
    if (msg->img_width > CAM_RECV_HARD_MAX_W || msg->img_height > CAM_RECV_HARD_MAX_H) {
        LOGW(TAG, "Image %ux%u exceeds hard max %ux%u",
             msg->img_width, msg->img_height,
             CAM_RECV_HARD_MAX_W, CAM_RECV_HARD_MAX_H);
        return;
    }

    size_t needed = (size_t)msg->img_width * msg->img_height * 2;
    if (needed > r->frame_buf_size) {
        LOGI(TAG, "Reallocating frame buffer: %ux%u (%u bytes) → %ux%u (%u bytes)",
             r->max_width, r->max_height, (unsigned)r->frame_buf_size,
             msg->img_width, msg->img_height, (unsigned)needed);
        CAM_MUTEX_LOCK(r->display_mutex);
        uint8_t *new_buf = CAM_ALLOC_LARGE(needed);
        if (!new_buf) {
            CAM_MUTEX_UNLOCK(r->display_mutex);
            LOGE(TAG, "Failed to realloc frame_buf to %u bytes", (unsigned)needed);
            return;
        }
        memset(new_buf, 0, needed);
        CAM_FREE(r->frame_buf);
        r->frame_buf      = new_buf;
        r->frame_buf_size = needed;
        r->max_width      = msg->img_width;
        r->max_height     = msg->img_height;
        CAM_MUTEX_UNLOCK(r->display_mutex);
    }
    uint16_t total_tiles = (uint16_t)msg->tiles_x * msg->tiles_y;
    if (total_tiles > CAM_RECEIVER_MAX_TILES) {
        LOGW(TAG, "Too many tiles: %u", total_tiles);
        CAM_DBG("FRAME_START id=%u: rejected — tile count %u > max %u",
                msg->frame_id, total_tiles, (unsigned)CAM_RECEIVER_MAX_TILES);
        return;
    }

    // Check bitmap length
    uint8_t bm_bytes = cam_tile_bitmap_bytes(msg->tiles_x, msg->tiles_y);
    if (len < CAM_FRAME_START_HEADER_SIZE + bm_bytes) {
        LOGW(TAG, "FRAME_START missing bitmap bytes");
        CAM_DBG("FRAME_START id=%u: rejected — need %u+%u bytes, got %u",
                msg->frame_id, (unsigned)CAM_FRAME_START_HEADER_SIZE,
                bm_bytes, (unsigned)len);
        return;
    }

    // --- Diff-before-keyframe: request keyframe and render partial frame ---
    if (msg->frame_type != CAM_FRAME_KEYFRAME && r->waiting_for_keyframe) {
        r->dbg_diffs_skipped++;
        // Throttle force-keyframe requests to once per second so we don't
        // flood the camera while still recovering quickly after first contact.
        uint32_t now_ms = cam_millis();
        if (now_ms - r->last_kf_request_ms > 1000) {
            r->last_kf_request_ms = now_ms;
            cam_force_keyframe_cmd_t kf_cmd = { .type = CAM_CMD_FORCE_KEYFRAME };
            cam_transport_send(r->transport, (const uint8_t *)&kf_cmd, sizeof(kf_cmd));
            CAM_DBG("FRAME_START id=%u: DIFF before keyframe — sent FORCE_KEYFRAME request",
                    msg->frame_id);
        }
        // Fall through: assemble the partial diff anyway so the display shows
        // something while we wait.  Uncovered tiles stay black (zeroed buffer).
    }

#ifdef CAM_RECV_DEBUG
    // Detect abandoned assembly (previous FRAME_START was never closed)
    if (r->assembling && r->asm_start_ms != 0) {
        uint32_t elapsed = cam_millis() - r->asm_start_ms;
        CAM_DBG("FRAME_START id=%u: previous frame id=%u abandoned after %u ms"
                " (no FRAME_END received)",
                msg->frame_id, r->asm_frame_id, elapsed);
    }
    // Heartbeat timeout check
    if (r->last_status_ms != 0) {
        uint32_t since_hb = cam_millis() - r->last_status_ms;
        if (since_hb > CAM_RECV_STATUS_TIMEOUT_MS) {
            CAM_DBG("FRAME_START id=%u: no STATUS heartbeat for %u ms"
                    " (threshold %u ms) — camera may be unreachable",
                    msg->frame_id, since_hb, CAM_RECV_STATUS_TIMEOUT_MS);
        }
    }
    r->asm_start_ms = cam_millis();
    CAM_VERB("FRAME_START id=%u type=%s dims=%ux%u tiles=%ux%u n_changed=%u enc=%u",
             msg->frame_id,
             msg->frame_type == CAM_FRAME_KEYFRAME ? "KEY" : "DIFF",
             msg->img_width, msg->img_height,
             msg->tiles_x, msg->tiles_y, msg->num_tiles, msg->encoding);
#else
    r->asm_start_ms = cam_millis();
#endif

    // Mark keyframe received
    if (msg->frame_type == CAM_FRAME_KEYFRAME) {
        if (r->waiting_for_keyframe) {
            LOGI(TAG, "First key frame received (id=%u) — diff frames now enabled",
                 msg->frame_id);
        }
        r->waiting_for_keyframe = false;
    }

    // If dimensions changed, update and clear
    if (msg->img_width != r->img_width || msg->img_height != r->img_height) {
        LOGI(TAG, "Image size changed to %ux%u", msg->img_width, msg->img_height);
        r->img_width  = msg->img_width;
        r->img_height = msg->img_height;
        CAM_MUTEX_LOCK(r->display_mutex);
        memset(r->frame_buf, 0, r->frame_buf_size);
        CAM_MUTEX_UNLOCK(r->display_mutex);
    }

    // Store assembly state
    r->assembling      = true;
    r->asm_frame_id    = msg->frame_id;
    r->asm_frame_type  = msg->frame_type;
    r->asm_tiles_x     = msg->tiles_x;
    r->asm_tiles_y     = msg->tiles_y;
    r->asm_encoding    = msg->encoding;
    r->asm_num_tiles   = msg->num_tiles;
    memcpy(r->asm_tile_bitmap, msg->tile_bitmap, bm_bytes);

    // Release pool slots from previous frame and reset tile assembly state.
    for (uint16_t t = 0; t < total_tiles; t++) {
        pool_release(r, (uint8_t)t);
        tile_asm_t *ta = &r->tiles[t];
        ta->pool_idx        = -1;
        ta->jpeg_len        = 0;
        ta->received_len    = 0;
        ta->total_chunks    = 0;
        ta->chunks_received = 0;
        ta->complete        = false;
        ta->decoded         = false;
    }

    LOGD(TAG, "FRAME_START id=%u type=%s tiles=%ux%u changed=%u",
         msg->frame_id,
         msg->frame_type == CAM_FRAME_KEYFRAME ? "KEY" : "DIFF",
         msg->tiles_x, msg->tiles_y, msg->num_tiles);
}

// ---------------------------------------------------------------------------
// TILE_CHUNK handler
// ---------------------------------------------------------------------------

static void handle_tile_chunk(cam_receiver_t *r, const uint8_t *data, size_t len)
{
    if (!r->assembling) return;
    if (len < CAM_TILE_CHUNK_HEADER_SIZE) return;

    const cam_tile_chunk_msg_t *msg = (const cam_tile_chunk_msg_t *)data;
    if (msg->frame_id != r->asm_frame_id) {
        CAM_DBG("TILE_CHUNK: frame_id mismatch (got %u, expected %u)"
                " — discarding chunk (total mismatches: %u)",
                msg->frame_id, r->asm_frame_id, r->dbg_chunks_mismatch + 1);
        r->dbg_chunks_mismatch++;
        LOGD(TAG, "TILE_CHUNK frame_id mismatch (%u vs %u)",
             msg->frame_id, r->asm_frame_id);
        return;
    }

    uint16_t total_tiles = (uint16_t)r->asm_tiles_x * r->asm_tiles_y;
    if (msg->tile_idx >= total_tiles) return;

    tile_asm_t *ta = &r->tiles[msg->tile_idx];

    // On first chunk, acquire a pool buffer and store total info.
    if (msg->chunk_idx == 0) {
        ta->jpeg_len     = msg->tile_data_len;
        ta->total_chunks = msg->total_chunks;
        ta->received_len = 0;
        ta->chunks_received = 0;
        ta->complete     = false;

        if (ta->jpeg_len > CAM_RECEIVER_TILE_BUF_SIZE) {
            LOGW(TAG, "Tile %u JPEG too large: %u", msg->tile_idx, ta->jpeg_len);
            CAM_DBG("TILE_CHUNK frame=%u tile=%u: JPEG size %u exceeds buffer %u bytes"
                    " — tile dropped",
                    msg->frame_id, msg->tile_idx, ta->jpeg_len,
                    (unsigned)CAM_RECEIVER_TILE_BUF_SIZE);
            return;
        }

        // Acquire a JPEG buffer from the pool (may evict another tile's buffer).
        if (!pool_acquire(r, msg->tile_idx)) {
            LOGW(TAG, "Tile %u: pool_acquire failed", msg->tile_idx);
            return;
        }
    }

    // Get the JPEG buffer for this tile.  NULL means our slot was evicted
    // by a newer tile — discard this chunk silently.
    uint8_t *jpeg_buf = pool_get_buf(r, msg->tile_idx);
    if (!jpeg_buf) {
        CAM_DBG("TILE_CHUNK frame=%u tile=%u chunk=%u: pool slot evicted — discarded",
                msg->frame_id, msg->tile_idx, msg->chunk_idx);
        return;
    }

    CAM_VERB("TILE_CHUNK frame=%u tile=%u chunk=%u/%u payload=%u recv=%u/%u",
             msg->frame_id, msg->tile_idx,
             msg->chunk_idx + 1,
             (unsigned)(ta->total_chunks ? ta->total_chunks : 1),
             (unsigned)(len - CAM_TILE_CHUNK_HEADER_SIZE),
             ta->received_len, ta->jpeg_len);

    // Calculate payload length in this chunk
    size_t payload_len = len - CAM_TILE_CHUNK_HEADER_SIZE;
    if (payload_len == 0) {
        CAM_VERB("TILE_CHUNK frame=%u tile=%u chunk=%u: zero-length payload — skipped",
                 msg->frame_id, msg->tile_idx, msg->chunk_idx);
        return;
    }

    // Copy chunk data into the tile's JPEG buffer at the right offset
    size_t offset = (size_t)msg->chunk_idx * CAM_TILE_CHUNK_MAX_PAYLOAD;
    if (offset + payload_len > CAM_RECEIVER_TILE_BUF_SIZE) {
        LOGW(TAG, "Tile %u chunk overflow", msg->tile_idx);
        CAM_DBG("TILE_CHUNK frame=%u tile=%u chunk=%u: write [%u..%u] overflows"
                " buffer size %u — chunk dropped",
                msg->frame_id, msg->tile_idx, msg->chunk_idx,
                (unsigned)offset, (unsigned)(offset + payload_len),
                (unsigned)CAM_RECEIVER_TILE_BUF_SIZE);
        return;
    }

    memcpy(jpeg_buf + offset, msg->data, payload_len);
    ta->received_len += (uint16_t)payload_len;
    ta->chunks_received++;

    // Check completeness
    if (ta->chunks_received >= ta->total_chunks &&
        ta->received_len >= ta->jpeg_len) {
        ta->complete = true;
        CAM_VERB("TILE_CHUNK frame=%u tile=%u: COMPLETE — %u bytes JPEG",
                 msg->frame_id, msg->tile_idx, ta->received_len);

        // Immediately decode this tile into the frame buffer and notify
        // subscribers so the UI can show partial updates as tiles arrive.
        CAM_MUTEX_LOCK(r->display_mutex);
        if (!ta->decoded) {
            if (decode_tile_into_frame(r, (uint8_t)msg->tile_idx) == 0) {
                ta->decoded = true;

                // Update published frame info
                r->last_frame.width        = r->img_width;
                r->last_frame.height       = r->img_height;
                r->last_frame.data         = r->frame_buf;
                r->last_frame.data_size    = (size_t)r->img_width * r->img_height * 2;
                r->last_frame.frame_id     = r->asm_frame_id;
                r->last_frame.timestamp_ms = cam_millis();
                r->has_frame = true;

                NOTIFY_CBS(r->frame_cbs, cam_frame_ready_cb_t, &r->last_frame);
            }
        }
        CAM_MUTEX_UNLOCK(r->display_mutex);

        // Tile fully decoded — return buffer to pool for reuse.
        pool_release(r, msg->tile_idx);
    }
}

// ---------------------------------------------------------------------------
// FRAME_END handler — decode tiles and publish
// ---------------------------------------------------------------------------

static void handle_frame_end(cam_receiver_t *r, const uint8_t *data, size_t len)
{
    if (!r->assembling) return;
    if (len < sizeof(cam_frame_end_msg_t)) return;

    const cam_frame_end_msg_t *msg = (const cam_frame_end_msg_t *)data;
    if (msg->frame_id != r->asm_frame_id) return;

    r->assembling = false;

    uint16_t total_tiles = (uint16_t)r->asm_tiles_x * r->asm_tiles_y;
    int decoded = 0;

    // Decode each flagged tile into the frame buffer
    CAM_MUTEX_LOCK(r->display_mutex);
    for (uint16_t t = 0; t < total_tiles; t++) {
        // Check if this tile is in the changed bitmap
        if (!(r->asm_tile_bitmap[t / 8] & (1 << (t % 8)))) continue;

        tile_asm_t *ta = &r->tiles[t];
        if (!ta->complete) {
            LOGW(TAG, "Tile %u incomplete (got %u/%u bytes, %u/%u chunks)",
                 t, ta->received_len, ta->jpeg_len,
                 ta->chunks_received, ta->total_chunks);
            CAM_DBG("FRAME_END id=%u tile=%u: INCOMPLETE — expected %u bytes/%u chunks,"
                    " have %u/%u",
                    r->asm_frame_id, t, ta->jpeg_len, ta->total_chunks,
                    ta->received_len, ta->chunks_received);
            continue;
        }

        // If this tile was already decoded when its last chunk arrived,
        // skip re-decoding it here.
        if (ta->decoded) {
            decoded++;
            continue;
        }

        if (decode_tile_into_frame(r, (uint8_t)t) == 0) {
            decoded++;
        } else {
            CAM_DBG("FRAME_END id=%u tile=%u: decode FAILED (total decode failures: %u)",
                    r->asm_frame_id, t, r->dbg_decode_failures + 1);
            r->dbg_decode_failures++;
        }
    }
    CAM_MUTEX_UNLOCK(r->display_mutex);

    // Update published frame info
    r->last_frame.width        = r->img_width;
    r->last_frame.height       = r->img_height;
    r->last_frame.data         = r->frame_buf;
    r->last_frame.data_size    = (size_t)r->img_width * r->img_height * 2;
    r->last_frame.frame_id     = r->asm_frame_id;
    r->last_frame.timestamp_ms = cam_millis();
    r->has_frame = true;

    LOGD(TAG, "FRAME_END id=%u decoded=%u/%u tiles",
         r->asm_frame_id, decoded, r->asm_num_tiles);

#ifdef CAM_RECV_DEBUG
    {
        uint32_t elapsed = (r->asm_start_ms != 0)
                           ? (cam_millis() - r->asm_start_ms) : 0;
        r->dbg_frames_complete++;
        if (decoded < (int)r->asm_num_tiles) {
            r->dbg_frames_errors++;
            CAM_DBG("FRAME_END id=%u: %u/%u tiles incomplete, %u ms assembly time"
                    " (errors so far: %u/%u)",
                    r->asm_frame_id,
                    (unsigned)(r->asm_num_tiles - (uint8_t)decoded),
                    r->asm_num_tiles, elapsed,
                    r->dbg_frames_errors, r->dbg_frames_complete);
        } else if (elapsed > CAM_RECV_FRAME_TIMEOUT_MS) {
            CAM_DBG("FRAME_END id=%u: assembly took %u ms (threshold %u ms)"
                    " — consider reducing resolution or tile count",
                    r->asm_frame_id, elapsed, CAM_RECV_FRAME_TIMEOUT_MS);
        } else {
            CAM_VERB("FRAME_END id=%u: OK — decoded %u/%u tiles in %u ms",
                     r->asm_frame_id, decoded, r->asm_num_tiles, elapsed);
        }
        // Periodic summary every 100 complete frames
        if (r->dbg_frames_complete % 100 == 0) {
            CAM_DBG("Stats @frame %u: complete=%u errors=%u diffs_skipped=%u"
                    " chunk_mismatches=%u decode_failures=%u",
                    r->asm_frame_id,
                    r->dbg_frames_complete, r->dbg_frames_errors,
                    r->dbg_diffs_skipped,   r->dbg_chunks_mismatch,
                    r->dbg_decode_failures);
        }
    }
#endif

    // Notify subscribers
    NOTIFY_CBS(r->frame_cbs, cam_frame_ready_cb_t, &r->last_frame);
}

// ---------------------------------------------------------------------------
// STATUS handler
// ---------------------------------------------------------------------------

static void handle_status(cam_receiver_t *r, const uint8_t *data, size_t len)
{
    if (len < sizeof(cam_status_msg_t)) {
        CAM_DBG("STATUS: payload too short (%u < %u) — discarded",
                (unsigned)len, (unsigned)sizeof(cam_status_msg_t));
        return;
    }
    const cam_status_msg_t *msg = (const cam_status_msg_t *)data;

#ifdef CAM_RECV_DEBUG
    uint32_t now = cam_millis();
    if (r->last_status_ms != 0) {
        uint32_t gap = now - r->last_status_ms;
        if (gap > CAM_RECV_STATUS_TIMEOUT_MS) {
            CAM_DBG("STATUS: heartbeat gap %u ms exceeds timeout %u ms"
                    " — camera reconnected or very busy",
                    gap, CAM_RECV_STATUS_TIMEOUT_MS);
        } else {
            CAM_VERB("STATUS: fps=%u.%u status=0x%02X frame_id=%u chan=%u gap=%u ms",
                     msg->fps_x10 / 10u, msg->fps_x10 % 10u,
                     msg->status, msg->frame_id, msg->wifi_channel, gap);
        }
    } else {
        CAM_DBG("STATUS: first heartbeat received — fps=%u.%u status=0x%02X"
                " frame_id=%u chan=%u",
                msg->fps_x10 / 10u, msg->fps_x10 % 10u,
                msg->status, msg->frame_id, msg->wifi_channel);
    }
    r->last_status_ms = now;
#else
    r->last_status_ms = cam_millis();
#endif

    r->status.status       = msg->status;
    r->status.frame_id     = msg->frame_id;
    r->status.fps_x10      = msg->fps_x10;
    r->status.wifi_channel = msg->wifi_channel;
    memcpy(r->status.mac, msg->mac, 6);

    // Detect camera reboot: frame_id has gone backwards since last STATUS.
    // On reboot the camera starts at frame_id 0, so if we previously saw a
    // higher id the drop signals a fresh boot.  Re-arm the config warmup and
    // require a keyframe so the pendant rebuilds its baseline.
    bool camera_rebooted = r->has_status &&
                           (msg->frame_id < r->last_status_frame_id) &&
                           (r->last_status_frame_id > 10u);
    if (camera_rebooted) {
        LOGI(TAG, "Camera reboot detected (frame_id %u → %u) — re-arming config + keyframe",
             r->last_status_frame_id, msg->frame_id);
        r->waiting_for_keyframe = true;
        r->cfg_warmup_sends     = 0;
        r->last_cfg_send_ms     = 0;  // Force immediate config re-send below
    }
    r->last_status_frame_id = msg->frame_id;
    r->has_status = true;

    // Auto-resend the stored configuration so the camera always picks up
    // swap_bytes / swap_rb correctly (e.g. after a camera reboot).
    // Send immediately on first contact; thereafter throttle to once per 10 s.
    if (r->has_stored_cfg) {
        uint32_t now_ms = cam_millis();
        bool first_contact = (r->last_cfg_send_ms == 0);
        if (first_contact || camera_rebooted ||
            (now_ms - r->last_cfg_send_ms >= 10000u)) {
            r->last_cfg_send_ms = now_ms;
            // Reset warmup sends so request_frame also retries the config
            // in case this STATUS signals a camera reboot.
            if (first_contact || camera_rebooted) r->cfg_warmup_sends = 0;
            int rc = cam_transport_send(r->transport,
                                        (const uint8_t *)&r->stored_cfg,
                                        sizeof(r->stored_cfg));
            if (rc == 0) {
                LOGD(TAG, "Auto-sent config to camera%s (swap_bytes=%d swap_rb=%d)",
                     first_contact ? " [first contact]" : (camera_rebooted ? " [reboot]" : ""),
                     r->stored_cfg.swap_bytes, r->stored_cfg.swap_rb);
            } else {
                LOGW(TAG, "Auto-send config failed (rc=%d)", rc);
            }

            // If a streaming session was active and the camera rebooted (or this
            // is the first contact with in_stream set), re-send STREAM_START so
            // the camera knows to begin streaming.
            if ((first_contact || camera_rebooted) && r->in_stream) {
                cam_stream_start_cmd_t start_cmd;
                memset(&start_cmd, 0, sizeof(start_cmd));
                start_cmd.type  = CAM_CMD_STREAM_START;
                start_cmd.flags = 0x01;
                start_cmd.desired_width  = r->desired_width;
                start_cmd.desired_height = r->desired_height;
                cam_transport_send(r->transport,
                                   (const uint8_t *)&start_cmd,
                                   sizeof(start_cmd));
                LOGI(TAG, "Re-sent STREAM_START after camera %s",
                     camera_rebooted ? "reboot" : "first contact");
            }
        }
    }

    NOTIFY_CBS(r->status_cbs, cam_status_cb_t, &r->status);
}

// ---------------------------------------------------------------------------
// GRID_MAP handler — parse standard or compact format
// ---------------------------------------------------------------------------

static void handle_grid_map(cam_receiver_t *r, const uint8_t *data, size_t len)
{
    // Minimum: type(1) + 4 floats(16) + 3 uint16(6) = 23 bytes (compact)
    // Standard: type(1) + 6 floats(24) + 3 uint16(6) = 31 bytes header
    if (len < 23) { LOGW(TAG, "GRID_MAP too short"); return; }

    const uint8_t *p = data + 1;  // skip type byte

    // Try to distinguish standard vs compact format by length heuristic.
    // Standard header = 31 bytes; compact header = 23 bytes.
    // Standard point payload: count * 8;  compact: count * 2.
    // Decision: if the first 6-float header + uint16s fit, it's standard.

    bool is_standard = false;
    if (len >= 31) {
        // Speculatively read as standard and check if payload matches
        float minx, maxx, miny, maxy, dx, dy;
        uint16_t nx, ny, count;
        memcpy(&minx, p +  0, 4);
        memcpy(&maxx, p +  4, 4);
        memcpy(&miny, p +  8, 4);
        memcpy(&maxy, p + 12, 4);
        memcpy(&dx,   p + 16, 4);
        memcpy(&dy,   p + 20, 4);
        memcpy(&nx,   p + 24, 2);
        memcpy(&ny,   p + 26, 2);
        memcpy(&count,p + 28, 2);
        size_t expected_standard = 31 + (size_t)count * 8;
        if (len >= expected_standard && count == (uint16_t)nx * ny) {
            is_standard = true;
        }
    }

    if (is_standard) {
        // ---- Standard format ----
        float minx, maxx, miny, maxy, dx, dy;
        uint16_t nx, ny, count;
        memcpy(&minx, p +  0, 4); memcpy(&maxx, p +  4, 4);
        memcpy(&miny, p +  8, 4); memcpy(&maxy, p + 12, 4);
        memcpy(&dx,   p + 16, 4); memcpy(&dy,   p + 20, 4);
        memcpy(&nx,   p + 24, 2); memcpy(&ny,   p + 26, 2);
        memcpy(&count,p + 28, 2);

        size_t pts_bytes = (size_t)count * 2 * sizeof(float);
        float *pts = CAM_ALLOC_LARGE(pts_bytes);
        if (!pts) { LOGE(TAG, "Grid alloc fail"); return; }
        memcpy(pts, p + 30, pts_bytes);

        CAM_MUTEX_LOCK(r->grid_mutex);
        if (r->grid.points)    CAM_FREE(r->grid.points);
        if (r->grid.px_points) CAM_FREE(r->grid.px_points);
        r->grid.min_x = minx;  r->grid.max_x = maxx;
        r->grid.min_y = miny;  r->grid.max_y = maxy;
        r->grid.dx = dx;       r->grid.dy = dy;
        r->grid.nx = nx;       r->grid.ny = ny;
        r->grid.point_count = count;
        r->grid.points = pts;
        r->grid.px_points  = NULL;  // standard format does not carry pixel positions
        r->grid.src_img_w  = 0;
        r->grid.src_img_h  = 0;
        r->has_grid = true;
        CAM_MUTEX_UNLOCK(r->grid_mutex);

        LOGI(TAG, "Grid (standard) %ux%u, bounds [%.1f..%.1f, %.1f..%.1f]",
             nx, ny, minx, maxx, miny, maxy);
    } else {
        // ---- Compact format ----
        // Layout (all after type byte):
        //   [w:4f][h:4f][dx:4f][dy:4f][nx:2][ny:2][count:2]
        //   optional extension (v2): [img_w:2][img_h:2]
        //   then: [count * 2 bytes of int8 offsets]
        float w, h, dx, dy;
        uint16_t nx, ny, count;
        memcpy(&w,    p +  0, 4); memcpy(&h,    p +  4, 4);
        memcpy(&dx,   p +  8, 4); memcpy(&dy,   p + 12, 4);
        memcpy(&nx,   p + 16, 2); memcpy(&ny,   p + 18, 2);
        memcpy(&count,p + 20, 2);

        // Detect extended format (v2) which carries img_w and img_h.
        // v1: header ends at p[22], total min len = 1+22+count*2
        // v2: header ends at p[26], total min len = 1+26+count*2
        uint16_t calib_img_w = 0, calib_img_h = 0;
        size_t offset_data = 22;
        bool has_img_dims = (len >= (size_t)(1 + 26 + count * 2));
        if (has_img_dims) {
            memcpy(&calib_img_w, p + 22, 2);
            memcpy(&calib_img_h, p + 24, 2);
            offset_data = 26;
        }

        if (len < (size_t)(1 + offset_data + count * 2)) {
            LOGW(TAG, "GRID_MAP compact too short for %u points (len=%u)",
                 count, (unsigned)len);
            return;
        }

        // Validate that the sender's point count matches the grid dimensions.
        // The drawing code iterates nx × ny times; if count < nx * ny the
        // allocated buffers below would be too small (heap overflow).
        if (count != (uint16_t)((uint32_t)nx * ny)) {
            LOGW(TAG, "GRID_MAP compact: count %u != nx*ny %u*%u — discarding",
                 count, nx, ny);
            return;
        }

        size_t pts_bytes = (size_t)count * 2 * sizeof(float);
        float *pts = CAM_ALLOC_LARGE(pts_bytes);
        if (!pts) { LOGE(TAG, "Grid alloc fail"); return; }

        // Reconstruct physical positions from offsets.
        // off_x/off_y are RAW PIXEL DIFFERENCES (actual_px - ideal_px).
        // For the physical coordinate reconstruction used by grid_interpolate
        // we scale the pixel difference by the physical pixel spacing:
        //   physical_spacing_per_px = dx / (img_w / (nx-1))
        // When img dimensions are unknown we fall back to the approximate
        // 1/127 * dx heuristic used by the previous implementation.
        float px_per_cell_x = (has_img_dims && calib_img_w > 0 && nx > 1)
                              ? ((float)calib_img_w / (float)(nx - 1)) : 0.0f;
        float px_per_cell_y = (has_img_dims && calib_img_h > 0 && ny > 1)
                              ? ((float)calib_img_h / (float)(ny - 1)) : 0.0f;

        for (uint16_t j = 0; j < ny; j++) {
            for (uint16_t i = 0; i < nx; i++) {
                uint16_t idx = j * nx + i;
                float ideal_x = (nx > 1) ? (i * w / (float)(nx - 1)) : 0.0f;
                float ideal_y = (ny > 1) ? (j * h / (float)(ny - 1)) : 0.0f;
                int8_t off_x = (int8_t)p[offset_data + idx * 2 + 0];
                int8_t off_y = (int8_t)p[offset_data + idx * 2 + 1];
                float phys_off_x, phys_off_y;
                if (px_per_cell_x > 0.0f) {
                    // Convert pixel offset → physical offset using known pixel scale.
                    phys_off_x = (float)off_x * (dx / px_per_cell_x);
                    phys_off_y = (float)off_y * (dy / px_per_cell_y);
                } else {
                    // Fallback: treat offsets as 1/127 of grid spacing.
                    phys_off_x = (float)off_x / 127.0f * dx;
                    phys_off_y = (float)off_y / 127.0f * dy;
                }
                pts[idx * 2 + 0] = ideal_x + phys_off_x;
                pts[idx * 2 + 1] = ideal_y + phys_off_y;
            }
        }

        // Reconstruct normalised pixel positions when img dimensions are known.
        // ideal_px = i / (nx-1) * calib_img_w  (assumes grid starts at image origin)
        // actual_px = ideal_px + off_x  (off_x is a direct pixel offset)
        float *px_pts = NULL;
        if (has_img_dims && calib_img_w > 0 && calib_img_h > 0 && count > 0) {
            px_pts = CAM_ALLOC_LARGE((size_t)count * 2 * sizeof(float));
            if (px_pts) {
                float iw_f = (float)calib_img_w;
                float ih_f = (float)calib_img_h;
                for (uint16_t j = 0; j < ny; j++) {
                    for (uint16_t i = 0; i < nx; i++) {
                        uint16_t idx = j * nx + i;
                        float ideal_px = (nx > 1) ? ((float)i / (float)(nx - 1) * iw_f) : 0.0f;
                        float ideal_py = (ny > 1) ? ((float)j / (float)(ny - 1) * ih_f) : 0.0f;
                        int8_t off_x = (int8_t)p[offset_data + idx * 2 + 0];
                        int8_t off_y = (int8_t)p[offset_data + idx * 2 + 1];
                        px_pts[idx * 2 + 0] = (ideal_px + (float)off_x) / iw_f;
                        px_pts[idx * 2 + 1] = (ideal_py + (float)off_y) / ih_f;
                    }
                }
            } else {
                LOGW(TAG, "Grid px_points alloc fail — grid lines will not be drawn");
            }
        }

        CAM_MUTEX_LOCK(r->grid_mutex);
        if (r->grid.points)    CAM_FREE(r->grid.points);
        if (r->grid.px_points) CAM_FREE(r->grid.px_points);
        r->grid.min_x = 0;        r->grid.max_x = w;
        r->grid.min_y = 0;        r->grid.max_y = h;
        r->grid.dx = dx;          r->grid.dy = dy;
        r->grid.nx = nx;          r->grid.ny = ny;
        r->grid.point_count = count;
        r->grid.points    = pts;
        r->grid.px_points = px_pts;
        r->grid.src_img_w = calib_img_w;
        r->grid.src_img_h = calib_img_h;
        r->has_grid = true;
        CAM_MUTEX_UNLOCK(r->grid_mutex);

        LOGI(TAG, "Grid (compact%s) %ux%u, size %.1f×%.1f",
             has_img_dims ? "+px" : "", nx, ny, w, h);
    }

    NOTIFY_CBS(r->grid_cbs, cam_grid_update_cb_t, &r->grid);
}

// ---------------------------------------------------------------------------
// TJpgDec callbacks (used when CAM_USE_TJPGD is defined)
// ---------------------------------------------------------------------------

#ifdef CAM_USE_TJPGD
typedef struct {
    const uint8_t *src;
    size_t         src_len;
    size_t         src_pos;
    // Direct-decode target: write MCU blocks straight into the full frame
    // buffer at the tile's pixel offset.  Eliminates the intermediate
    // tile_decode_buf and the subsequent memcpy blit.
    uint8_t       *frame;    ///< r->frame_buf base pointer
    uint16_t       frame_w;  ///< full image width in pixels (stride)
    uint16_t       tile_x0;  ///< tile origin X in the frame (pixels)
    uint16_t       tile_y0;  ///< tile origin Y in the frame (pixels)
} cam_tjd_ctx_t;

static size_t cam_tjd_infunc(JDEC *jd, uint8_t *buf, size_t nbytes)
{
    cam_tjd_ctx_t *ctx = (cam_tjd_ctx_t *)jd->device;
    size_t avail = ctx->src_len - ctx->src_pos;
    size_t n     = (nbytes < avail) ? nbytes : avail;
    if (buf) memcpy(buf, ctx->src + ctx->src_pos, n);
    ctx->src_pos += n;
    return n;
}

static int cam_tjd_outfunc(JDEC *jd, void *bitmap, JRECT *rect)
{
    /* JD_FORMAT=1: tjpgd outputs BGR565 (its YCbCr→RGB intermediate buffer is
     * written as B,G,R in memory order, then packed as bits[15:11]=B, [10:5]=G,
     * [4:0]=R).  LVGL (LV_COLOR_16_SWAP=0) expects RGB565 (bits[15:11]=R).
     * Swap R↔B in every pixel while writing directly into the frame buffer
     * at the tile's exact position — no intermediate tile_decode_buf needed. */
    cam_tjd_ctx_t  *ctx     = (cam_tjd_ctx_t *)jd->device;
    const uint16_t  blk_w   = rect->right  - rect->left + 1;
    const uint16_t  blk_h   = rect->bottom - rect->top  + 1;
    const uint16_t *src_row = (const uint16_t *)bitmap;

    for (uint16_t row = 0; row < blk_h; row++) {
        /* Destination row in the full frame buffer */
        uint16_t *dst = (uint16_t *)(ctx->frame +
                          ((size_t)(ctx->tile_y0 + rect->top  + row) * ctx->frame_w
                           + ctx->tile_x0 + rect->left) * 2u);
        const uint16_t *src = src_row + (size_t)row * blk_w;
        for (uint16_t col = 0; col < blk_w; col++) {
            uint16_t px = src[col];
            /* BGR565 → RGB565 */
            dst[col] = (uint16_t)(((px & 0x001Fu) << 11) |
                                   (px & 0x07E0u)        |
                                  ((px & 0xF800u) >> 11));
        }
    }
    return 1;  /* 1 = continue */
}
#endif /* CAM_USE_TJPGD */

// ---------------------------------------------------------------------------
// Tile JPEG decode → frame buffer blit
// ---------------------------------------------------------------------------

static int decode_tile_into_frame(cam_receiver_t *r, uint8_t tile_idx)
{
    tile_asm_t *ta = &r->tiles[tile_idx];
    uint16_t tile_w = cam_tile_width(r->img_width, r->asm_tiles_x);
    uint16_t tile_h = cam_tile_height(r->img_height, r->asm_tiles_y);

    // Resolve the JPEG data from the pool.
    uint8_t *jpeg_buf = pool_get_buf(r, tile_idx);
    if (!jpeg_buf) {
        LOGW(TAG, "Tile %u: no pool buffer for decode (evicted?)", tile_idx);
        return -1;
    }

    uint8_t ti_x = tile_idx % r->asm_tiles_x;
    uint8_t ti_y = tile_idx / r->asm_tiles_x;
    uint16_t dst_x = ti_x * tile_w;
    uint16_t dst_y = ti_y * tile_h;

    if (r->asm_encoding == CAM_ENC_JPEG) {
#if defined(CAM_USE_LIBJPEG_TURBO)
        // Decode JPEG using libjpeg (turbo) — write scanlines directly into
        // frame_buf at the tile's pixel position; no intermediate buffer needed.
        struct jpeg_decompress_struct cinfo;
        struct jpeg_error_mgr jerr;

        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_decompress(&cinfo);
        jpeg_mem_src(&cinfo, jpeg_buf, ta->jpeg_len);

        if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
            LOGW(TAG, "Tile %u JPEG header parse failed", tile_idx);
            jpeg_destroy_decompress(&cinfo);
            return -1;
        }

        cinfo.out_color_space = JCS_RGB;
        if (!jpeg_start_decompress(&cinfo)) {
            LOGW(TAG, "Tile %u jpeg_start_decompress failed", tile_idx);
            jpeg_destroy_decompress(&cinfo);
            return -1;
        }

        uint16_t out_w = (uint16_t)cinfo.output_width;
        uint16_t out_h = (uint16_t)cinfo.output_height;
        uint16_t copy_w = (out_w < tile_w) ? out_w : tile_w;
        uint16_t copy_h = (out_h < tile_h) ? out_h : tile_h;

        // Single-scanline row buffer (RGB888; allocated on the libjpeg pool,
        // freed automatically at jpeg_destroy_decompress).
        size_t row_stride = (size_t)cinfo.output_width * cinfo.output_components;
        JSAMPARRAY row_buf = (*cinfo.mem->alloc_sarray)(
                                (j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1);

        for (uint32_t y = 0; y < (uint32_t)out_h; y++) {
            if (jpeg_read_scanlines(&cinfo, row_buf, 1) != 1) break;
            if (y >= copy_h) continue;

            uint8_t   *src = (uint8_t *)row_buf[0];
            uint16_t  *row_dst = (uint16_t *)(r->frame_buf +
                                    ((size_t)(dst_y + y) * r->img_width + dst_x) * 2u);
            for (uint32_t x = 0; x < (uint32_t)copy_w; x++) {
                uint8_t rr = src[x * 3 + 0];
                uint8_t gg = src[x * 3 + 1];
                uint8_t bb = src[x * 3 + 2];
                row_dst[x] = (uint16_t)(((rr & 0xF8u) << 8) |
                                         ((gg & 0xFCu) << 3) |
                                          (bb >> 3));
            }
        }

        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);

#elif defined(CAM_USE_TJPGD)
        // Decode JPEG using TJpgDec — MCU blocks are written directly into
        // frame_buf at the tile's pixel origin.  No tile_decode_buf used.
        cam_tjd_ctx_t ctx;
        ctx.src     = jpeg_buf;
        ctx.src_len = ta->jpeg_len;
        ctx.src_pos = 0;
        ctx.frame   = r->frame_buf;
        ctx.frame_w = r->img_width;
        ctx.tile_x0 = dst_x;
        ctx.tile_y0 = dst_y;

        JDEC jdec;
        JRESULT jr = jd_prepare(&jdec, cam_tjd_infunc, r->tjpgd_work, 4096, &ctx);
        if (jr != JDR_OK) {
            LOGW(TAG, "Tile %u jd_prepare failed: %d", tile_idx, (int)jr);
            return -1;
        }
        jr = jd_decomp(&jdec, cam_tjd_outfunc, 0);
        if (jr != JDR_OK) {
            LOGW(TAG, "Tile %u jd_decomp failed: %d", tile_idx, (int)jr);
            return -1;
        }

#else
        // POSIX stub: fill tile with a grey pattern for testing
        uint16_t grey = 0x8410;
        size_t stride = (size_t)r->img_width * 2;
        for (uint16_t row = 0; row < tile_h; row++) {
            uint16_t *p = (uint16_t *)(r->frame_buf + (dst_y + row) * stride + dst_x * 2);
            for (uint16_t col = 0; col < tile_w; col++) p[col] = grey;
        }
#endif
    } else if (r->asm_encoding == CAM_ENC_RGB565) {
        // Raw RGB565 — copy directly from pool buffer into frame_buf.
        size_t   stride        = (size_t)r->img_width * 2;
        size_t   tile_row_bytes = (size_t)tile_w * 2;

        if (ta->jpeg_len == tile_w * tile_h * 2) {
            for (uint16_t row = 0; row < tile_h; row++) {
                uint8_t *dst = r->frame_buf + (dst_y + row) * stride + dst_x * 2;
                memcpy(dst, jpeg_buf + row * tile_row_bytes, tile_row_bytes);
            }
        }
    }

    return 0;
}
