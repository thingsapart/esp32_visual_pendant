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
#include "freertos/task.h"
#else
#include <threads.h>
#include <time.h>
#endif

#define UI_DEBUG_LOG D_ERROR
#include "debug.h"
#include "machine/machine_interface.h"

#define TASK_STACK_SIZE (1024 * 7)
#define TASK_PRIORITY (tskIDLE_PRIORITY + 1)

// --- Configuration Constants ---
// RAM Use: ~ 8KB.
#define MAX_BUFFER_SLOTS 20  // Max number of lines (slots) to buffer
#define SHARED_BUFFER_SIZE \
  4096  // Total size of the underlying character buffer (adjust as needed)
#define MAX_LINE_LENGTH \
  4096  // Max length of a single line (including null terminator), must be <
        // SHARED_BUFFER_SIZE
#define QUEUE_LENGTH 10  // Length of the notification queue (can be small)

#define MAX_PROCESSING_TASKS 2

static const char *TAG = "MACHINE_RESP_PROC_TASK";

typedef struct {
  char *ptr;      // Pointer to the start of the string in the shared buffer
  size_t len;     // Length of the string (excluding null terminator)
  size_t offset;  // Offset within the shared buffer where the string starts
} line_slot_t;

typedef struct {
  char buffer[SHARED_BUFFER_SIZE];      // Underlying shared character buffer
  line_slot_t slots[MAX_BUFFER_SLOTS];  // Metadata for each stored line
  size_t start_slot;                    // Index of the oldest slot occupied
  size_t end_slot;  // Index *after* the last slot occupied (next free slot)
  size_t buffer_write_offset;  // Next potential write position in the shared
                               // buffer
#ifdef ESP32_HW
  SemaphoreHandle_t mutex;  // Mutex to protect access to the buffer
#else
  mtx_t mutex;
#endif
} ring_buffer_t;

static struct {
#ifdef ESP32_HW
  QueueHandle_t queue;
#else
  gcode_queue_t *queue;
#endif
  ring_buffer_t *ring_buffer;
} queue_buffers[MAX_PROCESSING_TASKS] = {NULL};

static bool ring_buffer_init(ring_buffer_t *rb);
static bool ring_buffer_add_line(ring_buffer_t *rb, const char *line,
                                 size_t len);
static bool ring_buffer_get_line(ring_buffer_t *rb, char *out_buffer,
                                 size_t max_len, size_t *out_len);
static void ring_buffer_free_oldest(ring_buffer_t *rb);
static bool acquire_mutex(ring_buffer_t *rb, long unsigned int msTimeout);
static void release_mutex(ring_buffer_t *rb);

// --- Ring Buffer Implementation ---
static bool acquire_mutex(ring_buffer_t *rb, long unsigned int msTimeout) {
#ifdef ESP32_HW
  // Acquire mutex - wait a short time, essential for shared access
  TickType_t timeout;
  if (msTimeout == ULONG_MAX) {
    timeout = portMAX_DELAY;
  } else {
    timeout = pdMS_TO_TICKS(msTimeout);
  }
  if (xSemaphoreTake(rb->mutex, timeout) != pdTRUE)
#else
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
  if (mtx_timedlock(&rb->mutex, &timeout) != thrd_success)
#endif
  {
    return false;
  }

  return true;
}

static void release_mutex(ring_buffer_t *rb) {
#ifdef ESP32_HW
  xSemaphoreGive(rb->mutex);
#else
  mtx_unlock(&rb->mutex);
#endif
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

#ifdef ESP32_HW
  rb->mutex = xSemaphoreCreateMutex();
  if (rb->mutex == NULL)
#else
  if (mtx_init(&rb->mutex, mtx_timed) != thrd_success)
#endif
  {
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
    _df(1, "Ring buffer empty, reset write offset");
  }
  _df(1, "Freed oldest slot, new start_slot: %u", rb->start_slot);
}

/**
 * @brief Adds a line to the ring buffer. Handles overwriting oldest entries if
 * full. This function assumes it might be called from a context where blocking
 * indefinitely is bad, but brief waits for a mutex are okay (like ESP-IDF
 * driver task context).
 */
static bool ring_buffer_add_line(ring_buffer_t *rb, const char *line,
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

  _df(1, "Add Line: start=%u, end=%u, write_offset=%u, len=%u", rb->start_slot,
      rb->end_slot, rb->buffer_write_offset, len);

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
    if (rb->buffer_write_offset + required_len < SHARED_BUFFER_SIZE) {
      write_offset = rb->buffer_write_offset;
      write_ptr = rb->buffer + write_offset;
    }

    // Try space from beginning if it didn't fit at the end
    if (write_ptr == NULL &&
        required_len <
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
        _df(1, "Fit at start: offset=0");
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
    if (rb->start_slot == rb->end_slot && rb->buffer_write_offset != 0) {
      // Should not happen if required_len <= SHARED_BUFFER_SIZE, but safety
      // check
      LOGE(
          TAG,
          "Ring buffer full and cannot free space for line (len %d). Dropping.",
          required_len);
      release_mutex(rb);
      return false;
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

  _df(1, "Added Line: start=%u, end=%u, new_write_offset=%u", rb->start_slot,
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
  _df(1, "Get Line: slot=%u, len=%u", rb->start_slot, len_to_copy);
  rb->start_slot = (rb->start_slot + 1) % MAX_BUFFER_SLOTS;

  // Optimization: If buffer becomes empty, reset write offset.
  if (rb->start_slot == rb->end_slot) {
    rb->buffer_write_offset = 0;
    _df(1, "Ring buffer empty after get, reset write offset");
  }

  // Release mutex
  release_mutex(rb);
  return true;
}

/**
 * Notify task of data being ready.
 *
 * task_event_queue: queue to post notification to.
 * data: data that's ready.
 * len: data length.
 */
int machine_response_proc_task_data_ready(
#ifdef ESP32_HW
    QueueHandle_t task_event_queue,
#else
    gcode_queue_t *task_event_queue,
#endif
    const char *data, size_t len, bool from_isr) {
  ring_buffer_t *response_buffer = NULL;
  for (size_t i = 0; i < MAX_PROCESSING_TASKS; ++i) {
    if (queue_buffers[i].queue == task_event_queue) {
      response_buffer = queue_buffers[i].ring_buffer;
    }
  }
  if (response_buffer == NULL) {
    LOGE(TAG, "Cannot find response buffer for queue %p", task_event_queue);
    return 1;
  }

  LOGI(TAG, "Received %d data\n", len);

  // 1. Add the received line to the ring buffer
  if (ring_buffer_add_line(response_buffer, data, len)) {
    // 2. Notify the processing task queue that new data is available
    if (task_event_queue != NULL) {
      uint8_t dummy_notification = 1;  // Content doesn't matter, just the event
      // Use xQueueSend with a zero timeout - non-blocking. If queue is full,
      // notification is lost, but data is still in the ring buffer.
      // The task will eventually process it when it gets CPU time.
#ifdef ESP32_HW
      BaseType_t result = pdFAIL;
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      if (!from_isr) {
        result = xQueueSend(task_event_queue, &dummy_notification, 0);
      } else {
        result = xQueueSendFromISR(task_event_queue, &dummy_notification,
                                   &xHigherPriorityTaskWoken);
      }
      if (result != pdTRUE)
#else
      bool result = gcode_queue_push(task_event_queue, &dummy_notification);
      if (!result)
#endif
      {
        // This might happen if the processing task falls behind significantly.
        // Data is still buffered, so it's not critical, but indicates potential
        // bottleneck.
        LOGW(TAG, "Machine processing task queue full.");
      } else {
        LOGV(TAG, "Queue sent!");
#ifdef ESP32_HW
        if (xHigherPriorityTaskWoken) {
          portYIELD_FROM_ISR();
        }
#endif
      }
    } else {
      LOGE(TAG, "Serial received queue is NULL in callback!");
    }
  } else {
    // ring_buffer_add_line already logs errors/warnings
    LOGW(TAG, "Failed to add line to ring buffer. Line dropped.");
  }

  // IMPORTANT: Keep this callback short and fast.
  return 0;
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
 */
int machine_response_proc_task(void *vpargs) {
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
    return 1;  // Should not happen if run() succeeded
  }

  free(args);

  ring_buffer_t response_buffer;
  bool abort = false;

  if (!ring_buffer_init(&response_buffer)) {
    LOGE(TAG, "Failed to initialize response ring buffer.");
    abort = true;
  }

  for (size_t i = 0; i < MAX_PROCESSING_TASKS; ++i) {
    if (queue_buffers[i].ring_buffer == NULL) {
      queue_buffers[i].queue = queue;
      queue_buffers[i].ring_buffer = &response_buffer;
      break;
    }

    if (i == MAX_PROCESSING_TASKS - 1) {
      LOGE(
          TAG,
          "Number of response processing tasks exceeded MAX_PROCESSING_TASKS!");
      abort = true;
    }
  }

  LOGI(TAG, ">> Starting machine response processing task...");

#if 0
    char line_buffer[MAX_LINE_LENGTH]; // Buffer to hold line retrieved from ring buffer
#else
  char *line_buffer;
#endif

  uint8_t notification_item;  // Dummy item received from queue

  // LOGI(TAG, "%s task stack size high: %d\n", task_name,
  //     uxTaskGetStackHighWaterMark(NULL));

  while (!abort) {
#ifdef ESP32_HW
    // Block indefinitely waiting for a notification from the queue
    if (xQueueReceive(queue, &notification_item, portMAX_DELAY) == pdTRUE)
#else
    if (gcode_queue_pop(queue, &notification_item))
#endif
    {
      LOGI(TAG, "Response proc queue item received");

      // Notification received, try to get data from the ring buffer
      size_t line_len;

      // LOGI(TAG, "%s task stack size high: %d\n", task_name,
      //     uxTaskGetStackHighWaterMark(NULL));

      machine->set_connected(machine, true);

#if 0
            // Loop to process all available lines in the buffer before blocking again
            while (ring_buffer_get_line(&response_buffer, line_buffer, MAX_LINE_LENGTH, &line_len)) {
                LOGI(TAG, "Processing line (len %d): %s", line_len, line_buffer);
                if (line_len > 0) {
                    // Call the machine interface function to process the response
                    // Pass the line_buffer containing the null-terminated string
                    machine_interface_process_machine_state_response(machine, line_buffer);
                }
            }
#else
      // Loop to process all available lines in the buffer before blocking again
      while (
          (line_buffer = ring_buffer_line_data(&response_buffer, &line_len))) {
        LOGI(TAG, "[%s] Processing line (len %d): %p, machine %p, queue %p",
             task_name, line_len, line_buffer, machine, queue);
        if (line_len > 0) {
          // Call the machine interface function to process the response
          // Pass the line_buffer containing the null-terminated string
          machine_interface_process_machine_state_response(machine, line_buffer,
                                                           line_len);
        }
        ring_buffer_purge_line(&response_buffer);
      }

#endif
      // No more lines currently in the buffer, loop back to wait for next
      // notification
    }
    // If xQueueReceive fails unexpectedly (shouldn't with portMAX_DELAY), loop
    // continues
  }

  // Should never reach here, but good practice to include
  LOGW(TAG, "Machine Response Processing Task terminating unexpectedly...");
#ifdef ESP32_HW
  vSemaphoreDelete(response_buffer.mutex);  // Clean up mutex
  vTaskDelete(NULL);
#endif
  return 0;
}

bool machine_response_proc_task_run(const char *task_name,
                                    machine_interface_t *machine,
#ifdef ESP32_HW
                                    TaskHandle_t *task_handle,
                                    QueueHandle_t *queue, BaseType_t pinned_core
#else
                                    thrd_t *task_handle, gcode_queue_t *queue
#endif
) {
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
  // TODO: should consider moving this to class that calls if intent was to pass
  // between threads. This makes it clearer that this code doesn't own the queue
  // actually but the higher level processing does.
  *queue = xQueueCreate(QUEUE_LENGTH, sizeof(uint8_t));
#else
  gcode_queue_init(queue);
#endif
  if (queue == NULL) {
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
                              task_name,        // Task name
                              TASK_STACK_SIZE,  // Stack depth
                              args,  // Parameter passed to the task (using
                                     // global s_machine_interface instead)
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
    // TODO: if we move to higher up the queue creation we should also delete
    // there vs here.
    vQueueDelete(*queue);  // Clean up queue
    *task_handle = NULL;   // Ensure handle is NULL on failure
#endif
    free(args);
    return false;
  }

  LOGI(TAG, "Machine response processing task started successfully.");
  return true;
}

#ifdef __cplusplus
}
#endif

#endif  // ASYNC_RESPONSE_PROCESSING