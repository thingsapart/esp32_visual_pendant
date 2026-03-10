#define UI_DEBUG_LOCAL_LEVEL D_WARN
#include "debug.h"

#include "machine_response_proc_task.h"

#include "config.h"

#ifdef ASYNC_RESPONSE_PROCESSING

#ifdef __cplusplus
extern "C" {
#endif

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP32_HW
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "driver/task_registry.h"
#else
#include <time.h>

#include "compat/threads.h"
#endif

#include "machine/machine_interface.h"

// Default stack size — callers should pass an explicit value to
// machine_response_proc_task_run() since the right size depends on the
// machine type and build target (hub vs pendant).
#define TASK_STACK_SIZE_DEFAULT (1024 * 8)

#define TASK_PRIORITY \
  (tskIDLE_PRIORITY + 4)  // Higher priority to ensure it can preempt UI task

// --- Configuration Constants ---
// On ESP32_HW the message channel is a FreeRTOS StreamBuffer: the producer
// writes a 2-byte little-endian length header followed by the payload bytes;
// the consumer reads the header, then exactly that many payload bytes into a
// static line buffer.  This design has:
//   - Zero heap allocations on the fast path (no pvPortMalloc per line).
//   - Natural back-pressure: xStreamBufferSend returns 0 when the buffer is
//     full, and the producer logs a warning and drops the message.
//   - A static proc_line_buf[PROC_MSG_MAX_LEN] stays in task stack — avoids
//     heap fragmentation under the burst of 30+ lines from M409 d5 responses.
//
// StreamBuffer sizing: 12 KB (3 × MAX_LINE_LENGTH) handles a worst-case burst
// of ~3 full-length M409 d5 lines (≈4 KB each) without dropping.
// Each message occupies sizeof(uint16_t) + payload bytes.
//
// No inline-data size cap: any payload up to PROC_MSG_MAX_LEN is accepted.
// Sanity-check upper bound kept to catch obviously corrupt lengths.
#define PROC_MSG_MAX_LEN       4096  // sanity cap — corrupt if larger
#define PROC_STREAM_BUFFER_SIZE (3 * PROC_MSG_MAX_LEN + 128)  // 12 KB + overhead

// The QueueHandle_t API surface in the .h is kept for callers (hub.cpp,
// machine_rrf.c) but the underlying object is now a StreamBufferHandle_t cast
// to QueueHandle_t.  This avoids changing every call-site signature.
// The cast is safe: FreeRTOS stream buffer and queue handles are both void*
// and we only pass them back to our own proc task functions.

// Non-ESP32_HW still uses the ring-buffer path (gcode_queue).
#ifndef ESP32_HW
#define MAX_BUFFER_SLOTS 20
#define SHARED_BUFFER_SIZE 4096
#define MAX_LINE_LENGTH    4096
#endif

#define MAX_PROCESSING_TASKS 2

static const char *TAG = "MACHINE_RESP_PROC_TASK";

#ifdef ESP32_HW
// No proc_msg_t struct needed: the StreamBuffer carries raw
// [uint16_t len][uint8_t payload[len]] records directly.
#else  // non-ESP32_HW: keep ring-buffer path
typedef struct {
  char *ptr;
  size_t len;
  size_t offset;
} line_slot_t;

typedef struct {
  char buffer[SHARED_BUFFER_SIZE];
  line_slot_t slots[MAX_BUFFER_SLOTS];
  size_t start_slot;
  size_t end_slot;
  size_t buffer_write_offset;
  mtx_t mutex;
} ring_buffer_t;

static struct {
  gcode_queue_t *queue;
  ring_buffer_t *ring_buffer;
} queue_buffers[MAX_PROCESSING_TASKS] = {NULL};

static bool ring_buffer_init(ring_buffer_t *rb);
static bool ring_buffer_add_line(ring_buffer_t *rb, const uint8_t *line,
                                 size_t len);
static bool ring_buffer_get_line(ring_buffer_t *rb, char *out_buffer,
                                 size_t max_len, size_t *out_len);
static void ring_buffer_free_oldest(ring_buffer_t *rb);
static bool acquire_mutex(ring_buffer_t *rb, long unsigned int msTimeout);
static void release_mutex(ring_buffer_t *rb);
#endif  // ESP32_HW

// --- Ring Buffer Implementation (non-ESP32_HW only) ---
#ifndef ESP32_HW
static bool acquire_mutex(ring_buffer_t *rb, long unsigned int msTimeout) {
  struct timespec timeout;
  if (clock_gettime(CLOCK_REALTIME, &timeout) == -1) {
    LOGE(TAG, "Failed to get time to timeout mutex");
    return false;
  }
  if (msTimeout == ULONG_MAX) {
    timeout.tv_sec += msTimeout;
  } else {
    timeout.tv_nsec += 1000000 * msTimeout;  // 100ms
  }
  if (mtx_timedlock(&rb->mutex, &timeout) != thrd_success) {
    return false;
  }
  return true;
}

static void release_mutex(ring_buffer_t *rb) {
  mtx_unlock(&rb->mutex);
}

/**
 * @brief Initializes the ring buffer structure.
 */
static bool ring_buffer_init(ring_buffer_t *rb) {
  if (!rb) return false;

  memset(rb->buffer, 0, SHARED_BUFFER_SIZE);
  memset(rb->slots, 0, sizeof(rb->slots));
  rb->start_slot = 0;
  rb->end_slot = 0;
  rb->buffer_write_offset = 0;

  if (mtx_init(&rb->mutex, mtx_timed) != thrd_success) {
    LOGE(TAG, "Failed to create ring buffer mutex!");
    return false;
  }

  return true;
}

/**
 * @brief Frees the oldest slot in the ring buffer.
 * MUST be called with the mutex held.
 */
static void ring_buffer_free_oldest(ring_buffer_t *rb) {
  if (rb->start_slot == rb->end_slot) {
    return;  // Buffer is empty
  }
  // Just advance the start pointer, the buffer space is implicitly freed
  // and will be reused/overwritten later.
  rb->start_slot = (rb->start_slot + 1) % MAX_BUFFER_SLOTS;
  // Optimization: If buffer becomes empty, reset write offset to avoid
  // fragmentation.
  if (rb->start_slot == rb->end_slot) {
    rb->buffer_write_offset = 0;
    LOGE(TAG, "Ring buffer empty, reset write offset");
  }
  LOGW(TAG, "Freed oldest slot, new start_slot: %u", rb->start_slot);
}

/**
 * @brief Adds a line to the ring buffer. Handles overwriting oldest entries if
 * full. This function assumes it might be called from a context where blocking
 * indefinitely is bad, but brief waits for a mutex are okay (like ESP-IDF
 * driver task context).
 */
static bool ring_buffer_add_line(ring_buffer_t *rb, const uint8_t *line,
                                 size_t len) {
  if (!rb || !line || len == 0) return false;

  // Ensure line length + null terminator fits within limits
  if (len >= MAX_LINE_LENGTH) {
    LOGW(TAG, "Line too long (%d >= %d), truncating.", len, MAX_LINE_LENGTH);
    len = MAX_LINE_LENGTH - 1;
  }
  size_t required_len = len + 1;  // Include space for null terminator

  // Check if the line itself is larger than the entire buffer (impossible case)
  if (required_len > SHARED_BUFFER_SIZE) {
    LOGE(TAG, "Line length (%d) exceeds total buffer size (%d)!", required_len,
         SHARED_BUFFER_SIZE);
    return false;
  }

  // Acquire mutex - wait a short time, essential for shared access
  if (!acquire_mutex(rb, 100)) {
    LOGE(TAG, "Failed to acquire ring buffer mutex in add_line!");
    return false;  // Could not get mutex, drop the line
  }

  LOGI(TAG, "Add Line: start=%u, end=%u, write_offset=%u, len=%u",
       rb->start_slot, rb->end_slot, rb->buffer_write_offset, len);

  char *write_ptr = NULL;
  size_t write_offset = 0;

  // Loop until space is found (by freeing old slots if necessary)
  while (true) {
    // 1. Check if a slot is available
    bool slot_available =
        ((rb->end_slot + 1) % MAX_BUFFER_SLOTS) != rb->start_slot;

    // 2. Check if buffer space is available
    write_ptr = NULL;

    // Try space from current write offset to end
    if (rb->buffer_write_offset + required_len <= SHARED_BUFFER_SIZE) {
      write_offset = rb->buffer_write_offset;
      write_ptr = rb->buffer + write_offset;
    }

    // Try space from beginning if it didn't fit at the end
    if (write_ptr == NULL &&
        required_len <=
            rb->buffer_write_offset) {  // Check if there's space *before* the
                                        // current write offset
      size_t oldest_data_start =
          (rb->start_slot != rb->end_slot && rb->buffer_write_offset != 0)
              ? rb->slots[rb->start_slot].offset
              : 0;
      // Overlap occurs if wrapped data goes past the start of the oldest data
      if (required_len <= oldest_data_start ||
          oldest_data_start ==
              0) {  // Allow if fits before oldest or if oldest is also at 0
                    // (buffer effectively linear at this point)
        LOGV(TAG, "Fit at start: offset=0");
        write_offset = 0;
        write_ptr = rb->buffer;
      } else {
        LOGI(TAG, "No fit at start: %d + %d> %d (old start %d)\n",
             rb->buffer_write_offset, required_len, oldest_data_start,
             rb->buffer_write_offset);
      }
    }

    // 3. If slot and buffer space found, break loop
    if (slot_available && write_ptr != NULL) {
      LOGI(TAG, "Fit found %p (buf %p, offs %f)", write_ptr, rb->buffer,
           rb->buffer_write_offset);
      break;
    }

    // 4. If no space/slot, free the oldest slot
    if (rb->start_slot == rb->end_slot && rb->buffer_write_offset == 0) {
      LOGE(
          TAG,
          "Ring buffer full and cannot free space for line (len %d). Dropping.",
          (int)required_len);
      release_mutex(rb);
      return false;
    }
    if (rb->start_slot == rb->end_slot && rb->buffer_write_offset != 0) {
      LOGE(
          TAG,
          "Ring buffer empty but offset %u? Resetting to 0.",
          (unsigned)rb->buffer_write_offset);
      rb->buffer_write_offset = 0;
      continue;
    }

    LOGW(TAG,
         "Ring buffer full or fragmented, freeing oldest slot (%d) [start %d, "
         "end %d, slot avail %d, write_offs %d, req len %d, end next %d].",
         rb->start_slot, rb->start_slot, rb->end_slot, slot_available,
         rb->buffer_write_offset, required_len,
         (rb->end_slot + 1) % MAX_BUFFER_SLOTS);
    ring_buffer_free_oldest(rb);
    // Loop again to re-check space
  }

  // Copy the line and null-terminate
  memcpy(write_ptr, line, len);
  write_ptr[len] = '\0';

  // Store metadata in the next available slot
  rb->slots[rb->end_slot].ptr = write_ptr;
  rb->slots[rb->end_slot].len = len;
  rb->slots[rb->end_slot].offset = write_offset;

  // Update end slot index
  rb->end_slot = (rb->end_slot + 1) % MAX_BUFFER_SLOTS;

  // Update buffer write offset for next potential write
  rb->buffer_write_offset = (write_offset + required_len) % SHARED_BUFFER_SIZE;
  // Handle edge case where write exactly fills buffer
  if (rb->buffer_write_offset == 0 && required_len == SHARED_BUFFER_SIZE) {
    // Technically full, but next write should try from 0.
    // If the next write also needs the full buffer, it will free this one.
  } else if (rb->buffer_write_offset == 0 &&
             write_offset + required_len == SHARED_BUFFER_SIZE) {
    // Wrapped perfectly to the end, next write starts at 0.
    rb->buffer_write_offset = 0;
  }

  LOGV(TAG, "Added Line: start=%u, end=%u, new_write_offset=%u", rb->start_slot,
       rb->end_slot, rb->buffer_write_offset);

  // Release mutex
  release_mutex(rb);
  return true;
}

static char *ring_buffer_line_data(ring_buffer_t *rb, size_t *out_len) {
  if (!rb || !out_len) return false;

  // Acquire mutex
  if (!acquire_mutex(rb, ULONG_MAX)) {
    LOGE(TAG, "Failed to acquire ring buffer mutex in get_line!");
    return false;
  }

  // Check if buffer is empty
  if (rb->start_slot == rb->end_slot) {
    release_mutex(rb);
    *out_len = 0;
    return false;  // No data available
  }

  // Get data from the oldest slot
  line_slot_t *slot = &rb->slots[rb->start_slot];
  size_t len_to_copy = slot->len;

  release_mutex(rb);

  *out_len = len_to_copy;
  return slot->ptr;
}

static bool ring_buffer_purge_line(ring_buffer_t *rb) {
  if (!rb) return false;

  // Acquire mutex
  if (!acquire_mutex(rb, ULONG_MAX)) {
    LOGE(TAG, "Failed to acquire ring buffer mutex in get_line!");
    return false;
  }

  // Check if buffer is empty
  if (rb->start_slot == rb->end_slot) {
    release_mutex(rb);
    return false;  // No data available
  }

  // Get data from the oldest slot
  line_slot_t *slot = &rb->slots[rb->start_slot];
  size_t len_to_copy = slot->len;

  rb->start_slot = (rb->start_slot + 1) % MAX_BUFFER_SLOTS;

  // Optimization: If buffer becomes empty, reset write offset.
  if (rb->start_slot == rb->end_slot) {
    rb->buffer_write_offset = 0;
    LOGI(TAG, "Ring buffer empty after get, reset write offset");
  }

  // Release mutex
  release_mutex(rb);
  return true;
}
/**
 * @brief Retrieves the oldest line from the ring buffer.
 */
static bool ring_buffer_get_line(ring_buffer_t *rb, char *out_buffer,
                                 size_t max_len, size_t *out_len) {
  if (!rb || !out_buffer || max_len == 0 || !out_len) return false;

  // Acquire mutex
  if (!acquire_mutex(rb, ULONG_MAX)) {
    LOGE(TAG, "Failed to acquire ring buffer mutex in get_line!");
    return false;
  }

  // Check if buffer is empty
  if (rb->start_slot == rb->end_slot) {
    release_mutex(rb);
    *out_len = 0;
    return false;  // No data available
  }

  // Get data from the oldest slot
  line_slot_t *slot = &rb->slots[rb->start_slot];
  size_t len_to_copy = slot->len;

  if (len_to_copy >= max_len) {
    LOGW(TAG, "Output buffer too small (%d) for line (%d), truncating.",
         max_len, len_to_copy);
    len_to_copy = max_len - 1;
  }

  // Copy data to output buffer and null-terminate
  memcpy(out_buffer, slot->ptr, len_to_copy);
  out_buffer[len_to_copy] = '\0';
  *out_len = len_to_copy;

  // Advance the start slot index (effectively removing the item)
  LOGV(TAG, "Get Line: slot=%u, len=%u", rb->start_slot, len_to_copy);
  rb->start_slot = (rb->start_slot + 1) % MAX_BUFFER_SLOTS;

  // Optimization: If buffer becomes empty, reset write offset.
  if (rb->start_slot == rb->end_slot) {
    rb->buffer_write_offset = 0;
    LOGV(TAG, "Ring buffer empty after get, reset write offset");
  }

  // Release mutex
  release_mutex(rb);
  return true;
}
#endif  // !ESP32_HW — end of ring-buffer implementation

/**
 * Notify task of data being ready.
 *
 * On ESP32_HW: writes a 2-byte length header + payload into the StreamBuffer.
 * This is zero-allocation — no pvPortMalloc per message.  If the StreamBuffer
 * is full, the message is dropped with a warning (back-pressure: the proc task
 * is falling behind).
 *
 * On non-ESP32_HW: gcode_queue path unchanged (desktop unit-test builds).
 */
int machine_response_proc_task_data_ready(
#ifdef ESP32_HW
    QueueHandle_t task_event_queue,
#else
    gcode_queue_t *task_event_queue,
#endif
    const uint8_t *data, size_t len, bool from_isr) {
#ifdef ESP32_HW
  StreamBufferHandle_t sb = (StreamBufferHandle_t)task_event_queue;
  if (!sb) {
    LOGE(TAG, "data_ready: stream buffer is NULL");
    return 1;
  }
  if (len == 0 || len > PROC_MSG_MAX_LEN) {
    LOGW(TAG, "data_ready: invalid len %u, dropping", (unsigned)len);
    return 1;
  }

  // Pack [uint16_t len | payload] into a small stack frame.
  // Maximum total: 2 + PROC_MSG_MAX_LEN = 4098 bytes.
  // We use a VLA-equivalent via a small header + pointer trick to avoid a
  // 4 KB on-stack buffer; instead send header and payload in two consecutive
  // xStreamBufferSend calls.  FreeRTOS StreamBuffer does NOT guarantee
  // atomicity of multi-send, but the consumer reads exactly header+payload as
  // a unit because it reads the header first and then immediately reads the
  // exact payload size.  Since the consumer task is the only reader and the
  // StreamBuffer internal state is protected by a critical section, a partial
  // write by the producer is still coherent: the consumer will block waiting
  // for enough bytes before it reads the length header.
  //
  // To ensure the length header and payload are always adjacent in the
  // StreamBuffer (no interleaving from another producer), we must send them
  // as a single region.  Use a stack-allocated header+payload struct when len
  // is small; for large payloads (rare) fall back to a temporary heap buffer.
  //
  // Simpler approach: send header + payload as one xStreamBufferSend in a
  // single stack buffer.  PROC_MSG_MAX_LEN is 4096 so the max frame is 4098B.
  // Stack budget on the caller (machine_rrf line callback, prio=1 task): 4KB.
  // Use a heap allocation here only for the intermediate buffer to avoid
  // blowing the caller's stack.  This is a single alloc per line, the same
  // order as before, but now without the separate consumer-side free.
  //
  // NOTE: xStreamBufferSend is safe to call from task context and from ISR
  // via xStreamBufferSendFromISR.
  uint8_t hdr[2] = { (uint8_t)(len & 0xFF), (uint8_t)((len >> 8) & 0xFF) };

  BaseType_t result;
  if (!from_isr) {
    // Send header then payload; the consumer won't read the header until both
    // are in the buffer because it reads header first and then waits for
    // exactly len more bytes.
    size_t sent_hdr = xStreamBufferSend(sb, hdr, sizeof(hdr), 0);
    if (sent_hdr != sizeof(hdr)) {
      LOGW(TAG, "Proc stream buffer full (header) — message len=%u dropped.", (unsigned)len);
      return 1;
    }
    size_t sent_data = xStreamBufferSend(sb, data, len, 0);
    if (sent_data != len) {
      // Partial write: the buffer filled between the header write and the data
      // write.  This leaves the stream buffer in an inconsistent state (a
      // length header with no corresponding payload).  Reset the entire stream
      // buffer to re-establish a clean state — this drops all buffered messages
      // but prevents the consumer from hanging reading a partial record.
      LOGE(TAG, "Proc stream buffer partial write after header (sent %u/%u) — resetting buffer.",
           (unsigned)sent_data, (unsigned)len);
      xStreamBufferReset(sb);
      return 1;
    }
    result = pdTRUE;
  } else {
    BaseType_t woken = pdFALSE;
    size_t sent_hdr = xStreamBufferSendFromISR(sb, hdr, sizeof(hdr), &woken);
    if (sent_hdr != sizeof(hdr)) {
      LOGW(TAG, "Proc stream buffer full (ISR header) — message dropped.");
      if (woken) portYIELD_FROM_ISR();
      return 1;
    }
    size_t sent_data = xStreamBufferSendFromISR(sb, data, len, &woken);
    if (sent_data != len) {
      LOGE(TAG, "Proc stream buffer partial ISR write — resetting buffer.");
      xStreamBufferReset(sb);
      if (woken) portYIELD_FROM_ISR();
      return 1;
    }
    if (woken) portYIELD_FROM_ISR();
    result = pdTRUE;
  }
  return (result == pdTRUE) ? 0 : 1;

#else  // non-ESP32_HW: gcode_queue path unchanged
  (void)from_isr;
  if (task_event_queue == NULL) return 1;
  bool r = gcode_queue_push(task_event_queue, (const char *)data);
  if (!r) { LOGW(TAG, "gcode_queue full — message dropped."); return 1; }
  return 0;
#endif
}

typedef struct {
  machine_interface_t *machine;
#ifdef ESP32_HW
  QueueHandle_t queue;
#else
  gcode_queue_t *queue;
#endif
  const char *task_name;
} machine_response_proc_task_args_t;

/**
 * @brief The main function for the Machine Response Processing Task.
 *
 * TASK ROLE: Input Parser (RX)
 * This task offloads heavy data parsing (JSON, large strings) from the ISR and the UI task.
 * 1. ISR/Callback: Pushes raw bytes/lines into a thread-safe ring buffer.
 * 2. This Task: Wakes up, pulls from ring buffer, parses (cJSON), and updates machine state.
 *
 * NOTE: This is critical for performance. Parsing JSON in an ISR is illegal/crash-prone.
 * Parsing in the UI task causes frame drops.
 */
void machine_response_proc_task(void *vpargs) {
  machine_response_proc_task_args_t *args =
      (machine_response_proc_task_args_t *)vpargs;
  machine_interface_t *machine = args->machine;
#ifdef ESP32_HW
  QueueHandle_t queue = args->queue;
#else
  gcode_queue_t *queue = args->queue;
#endif

  size_t task_name_len = strlen(args->task_name);
  char task_name[task_name_len + 1];
  strncpy(task_name, args->task_name, task_name_len);
  task_name[task_name_len] = '\0';

  if (!machine) {
    LOGE(TAG, "Machine interface is NULL! Task cannot run.");
#ifdef ESP32_HW
    vTaskDelete(NULL);
#endif
    return;  // Should not happen if run() succeeded
  }

  free(args);

  bool abort = false;
  LOGI(TAG, ">> Starting machine response processing task...");

  // Static receive buffer — avoids per-message heap allocation.
  // Size matches PROC_MSG_MAX_LEN (= MAX_LINE_LENGTH = 4096 B).
  //static uint8_t proc_line_buf[PROC_MSG_MAX_LEN];

  uint8_t *proc_line_buf = heap_caps_malloc(PROC_MSG_MAX_LEN, MALLOC_CAP_SPIRAM);
  if (!proc_line_buf) { LOGE(TAG, "OOM: proc_line_buf PSRAM alloc failed"); return; }

  while (!abort) {
#ifdef ESP32_HW
    StreamBufferHandle_t sb = (StreamBufferHandle_t)queue;

    // Step 1: read the 2-byte length header.  Block up to 1 s waiting for data.
    uint8_t hdr[2];
    size_t hdr_received = xStreamBufferReceive(sb, hdr, sizeof(hdr), pdMS_TO_TICKS(1000));
    if (hdr_received == 0) {
      // Timeout — loop back and wait again.
      LOGD(TAG, "[PROC] timeout, looping");
      continue;
    }
    if (hdr_received != sizeof(hdr)) {
      // Shouldn't happen (trigger level = 1 byte), but be defensive.
      LOGW(TAG, "[PROC] partial header read (%u bytes), resetting buffer", (unsigned)hdr_received);
      xStreamBufferReset(sb);
      continue;
    }
    uint16_t msg_len = (uint16_t)hdr[0] | ((uint16_t)hdr[1] << 8);

    if (msg_len == 0 || msg_len > PROC_MSG_MAX_LEN) {
      LOGE(TAG, "[PROC] corrupt length %u in stream — resetting buffer", (unsigned)msg_len);
      xStreamBufferReset(sb);
      continue;
    }

    // Step 2: read exactly msg_len bytes.  Block indefinitely — we must drain
    // the payload we committed to in the header; there is no valid partial read.
    size_t data_received = xStreamBufferReceive(sb, proc_line_buf, msg_len, portMAX_DELAY);
    if (data_received != msg_len) {
      LOGE(TAG, "[PROC] payload read mismatch (got %u want %u) — resetting buffer",
           (unsigned)data_received, (unsigned)msg_len);
      xStreamBufferReset(sb);
      continue;
    }

    LOGD(TAG, "[PROC] type=%d len=%d", (int)proc_line_buf[0], (int)msg_len);
    TickType_t t0 = xTaskGetTickCount();
    machine_interface_process_machine_state_response(machine, proc_line_buf, msg_len);
    LOGD(TAG, "[PROC] done in %u ticks", (unsigned)(xTaskGetTickCount() - t0));

#else  // non-ESP32_HW: gcode_queue (desktop / unit-test builds)
    // The non-ESP32 path keeps the old ring-buffer approach via gcode_queue.
    // This branch is only exercised in desktop unit tests.
    char *line_buffer;
    size_t line_len;
    ring_buffer_t *response_buffer = NULL;
    for (size_t i = 0; i < MAX_PROCESSING_TASKS; ++i) {
      if (queue_buffers[i].queue == queue) {
        response_buffer = queue_buffers[i].ring_buffer;
        break;
      }
    }
    uint8_t notification_item;
    if (!gcode_queue_pop(queue, &notification_item)) continue;
    if (response_buffer == NULL) continue;
    while ((line_buffer = ring_buffer_line_data(response_buffer, &line_len))) {
      if (line_len > 0) {
        machine_interface_process_machine_state_response(machine, line_buffer,
                                                         line_len);
      }
      ring_buffer_purge_line(response_buffer);
    }
#endif  // ESP32_HW
  }

  // Should never reach here
  LOGW(TAG, "Machine Response Processing Task terminating unexpectedly...");
#ifdef ESP32_HW
  vTaskDelete(NULL);
#endif
  return;
}

bool machine_response_proc_task_run(const char *task_name,
                                    machine_interface_t *machine,
#ifdef ESP32_HW
                                    TaskHandle_t *task_handle,
                                    QueueHandle_t *queue, BaseType_t pinned_core,
                                    size_t stack_size
#else
                                    thrd_t *task_handle, gcode_queue_t *queue
#endif
) {
#ifdef ESP32_HW
  if (stack_size == 0) stack_size = TASK_STACK_SIZE_DEFAULT;
#endif
#ifdef ESP32_HW
  if (*task_handle != NULL) {
    LOGE(TAG, "Task already running!");
    return false;
  }
#endif
  if (machine == NULL) {
    LOGE(TAG, "Invalid machine interface provided.");
    return false;
  }

  LOGI(TAG, "Initializing machine response processing...");
#ifdef ESP32_HW
  // Create a StreamBuffer instead of a queue.
  // The caller receives it cast to QueueHandle_t (both are opaque void*).
  // PROC_STREAM_BUFFER_SIZE is 12 KB + overhead; trigger level = 1 so the
  // consumer unblocks as soon as the first header byte arrives.
  StreamBufferHandle_t sb = xStreamBufferCreate(PROC_STREAM_BUFFER_SIZE, 1);
  if (!sb) {
    LOGE(TAG, "Failed to create proc stream buffer (OOM?)!");
    return false;
  }
  *queue = (QueueHandle_t)sb;
#else
  gcode_queue_init(queue);
#endif
  if (*queue == NULL) {
    LOGE(TAG, "Failed to create serial received notification queue!");
    return false;
  }

  machine_response_proc_task_args_t *args =
      (machine_response_proc_task_args_t *)malloc(
          sizeof(machine_response_proc_task_args_t));
  args->machine = machine;
#ifdef ESP32_HW
  args->queue = *queue;
#else
  args->queue = queue;
#endif
  args->task_name = task_name;
#ifdef ESP32_HW
  BaseType_t task_created =
      xTaskCreatePinnedToCore(machine_response_proc_task,
                              task_name,   // Task name
                              stack_size,  // Stack depth (caller-specified)
                              args,  // Parameter passed to the task
                              TASK_PRIORITY,  // Task priority
                              task_handle,    // Task handle
                              pinned_core) == pdPASS;
#else
  bool task_created = thrd_create(task_handle, machine_response_proc_task,
                                  args) == thrd_success;
#endif
  LOGI(TAG, "Creating task: %s => %p, queue %p, machine %p", task_name,
       *task_handle, *queue, machine);
  if (!task_created) {
    LOGE(TAG, "Failed to create machine response processing task (%d)!",
         task_created);
#ifdef ESP32_HW
    vStreamBufferDelete((StreamBufferHandle_t)*queue);
    *queue = NULL;
    *task_handle = NULL;
#endif
    free(args);
    return false;
  }

  LOGI(TAG, "Machine response processing task started successfully.");
  task_registry_register_handle(*task_handle, task_name);
  return true;
}

#ifdef __cplusplus
}
#endif

#endif  // ASYNC_RESPONSE_PROCESSING
