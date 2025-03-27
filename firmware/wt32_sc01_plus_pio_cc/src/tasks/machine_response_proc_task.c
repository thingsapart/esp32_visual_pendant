#include "machine_response_proc_task.h"

// --- Standard Includes ---
#include <string.h>
#include <stdlib.h> // For NULL
#include <stdbool.h>

// --- FreeRTOS Includes ---
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h" // For mutexes

// --- Project Includes ---
#include "debug.h"
#include "machine/machine_interface.h"
#include "arduino_serial_wrapper.h" // For serial_handle_t, serial_line_callback_t

// --- Task Handle ---
TaskHandle_t machine_response_proc_task_handle = NULL;

// --- Logging Tag ---
static const char *TAG = "MACHINE_RESP_PROC_TASK";

// --- Configuration Constants ---
// RAM Use: ~ 12KB.
#define MAX_BUFFER_SLOTS 20        // Max number of lines (slots) to buffer
#define SHARED_BUFFER_SIZE 6144    // Total size of the underlying character buffer (adjust as needed)
#define MAX_LINE_LENGTH 4096       // Max length of a single line (including null terminator), must be < SHARED_BUFFER_SIZE
#define QUEUE_LENGTH 10            // Length of the notification queue (can be small)
#define TASK_STACK_SIZE 8192       // Stack size for the processing task
#define TASK_PRIORITY (tskIDLE_PRIORITY + 3) // Priority of the processing task

// --- Ring Buffer Data Structures ---
typedef struct {
    char *ptr;      // Pointer to the start of the string in the shared buffer
    size_t len;     // Length of the string (excluding null terminator)
    size_t offset;  // Offset within the shared buffer where the string starts
} line_slot_t;

typedef struct {
    char buffer[SHARED_BUFFER_SIZE]; // Underlying shared character buffer
    line_slot_t slots[MAX_BUFFER_SLOTS]; // Metadata for each stored line
    size_t start_slot;               // Index of the oldest slot occupied
    size_t end_slot;                 // Index *after* the last slot occupied (next free slot)
    size_t buffer_write_offset;      // Next potential write position in the shared buffer
    SemaphoreHandle_t mutex;         // Mutex to protect access to the buffer
} ring_buffer_t;

// --- Global Variables ---
static QueueHandle_t serial_received_queue = NULL;
static ring_buffer_t response_buffer;
static machine_interface_t *s_machine_interface = NULL; // Store the machine interface pointer globally for the task

// --- Forward Declarations for Ring Buffer Helpers ---
static bool ring_buffer_init(ring_buffer_t *rb);
static bool ring_buffer_add_line(ring_buffer_t *rb, const char *line, size_t len);
static bool ring_buffer_get_line(ring_buffer_t *rb, char *out_buffer, size_t max_len, size_t *out_len);
static void ring_buffer_free_oldest(ring_buffer_t *rb);

// --- Ring Buffer Implementation ---

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

    rb->mutex = xSemaphoreCreateMutex();
    if (rb->mutex == NULL) {
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
        return; // Buffer is empty
    }
    // Just advance the start pointer, the buffer space is implicitly freed
    // and will be reused/overwritten later.
    rb->start_slot = (rb->start_slot + 1) % MAX_BUFFER_SLOTS;
    // Optimization: If buffer becomes empty, reset write offset to avoid fragmentation.
    if (rb->start_slot == rb->end_slot) {
        rb->buffer_write_offset = 0;
         _df(1, "Ring buffer empty, reset write offset");
    }
    _df(1, "Freed oldest slot, new start_slot: %u", rb->start_slot);
}

/**
 * @brief Adds a line to the ring buffer. Handles overwriting oldest entries if full.
 * This function assumes it might be called from a context where blocking indefinitely
 * is bad, but brief waits for a mutex are okay (like ESP-IDF driver task context).
 */
static bool ring_buffer_add_line(ring_buffer_t *rb, const char *line, size_t len) {
    if (!rb || !line || len == 0) return false;

    // Ensure line length + null terminator fits within limits
    if (len >= MAX_LINE_LENGTH) {
        LOGW(TAG, "Line too long (%d >= %d), truncating.", len, MAX_LINE_LENGTH);
        len = MAX_LINE_LENGTH - 1;
    }
    size_t required_len = len + 1; // Include space for null terminator

    // Check if the line itself is larger than the entire buffer (impossible case)
    if (required_len > SHARED_BUFFER_SIZE) {
        LOGE(TAG, "Line length (%d) exceeds total buffer size (%d)!", required_len, SHARED_BUFFER_SIZE);
        return false;
    }

    // Acquire mutex - wait a short time, essential for shared access
    if (xSemaphoreTake(rb->mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        LOGE(TAG, "Failed to acquire ring buffer mutex in add_line!");
        return false; // Could not get mutex, drop the line
    }

    _df(1,"Add Line: start=%u, end=%u, write_offset=%u, len=%u", rb->start_slot, rb->end_slot, rb->buffer_write_offset, len);

    char *write_ptr = NULL;
    size_t write_offset = 0;

    // Loop until space is found (by freeing old slots if necessary)
    while (true) {
        // 1. Check if a slot is available
        bool slot_available = ((rb->end_slot + 1) % MAX_BUFFER_SLOTS) != rb->start_slot;

        // 2. Check if buffer space is available
        write_ptr = NULL;
        // Try space from current write offset to end
        if (rb->buffer_write_offset + required_len <= SHARED_BUFFER_SIZE) {
            // Check if this overlaps with the start of the *oldest* data if wrapped around
            size_t oldest_data_start = (rb->start_slot != rb->end_slot) ? rb->slots[rb->start_slot].offset : rb->buffer_write_offset; // If empty, no overlap possible
            bool wraps_around = rb->buffer_write_offset < oldest_data_start; // True if write ptr is earlier than read ptr
            
            // Overlap occurs if:
            // a) buffer is NOT wrapped AND new data goes past oldest data start
            // b) buffer IS wrapped (write ptr < oldest ptr) AND new data crosses buffer end OR goes past oldest data start
            bool overlaps = !wraps_around && (rb->buffer_write_offset + required_len > oldest_data_start) && oldest_data_start >= rb->buffer_write_offset; // Case a) simplified
            
            if (!overlaps) {
                 _df(1,"Fit at end: offset=%u", rb->buffer_write_offset);
                write_offset = rb->buffer_write_offset;
                write_ptr = rb->buffer + write_offset;
            }
        }

        // Try space from beginning if it didn't fit at the end
        if (write_ptr == NULL && required_len <= rb->buffer_write_offset) { // Check if there's space *before* the current write offset
             size_t oldest_data_start = (rb->start_slot != rb->end_slot) ? rb->slots[rb->start_slot].offset : 0;
             // Overlap occurs if wrapped data goes past the start of the oldest data
             if(required_len <= oldest_data_start || oldest_data_start == 0){ // Allow if fits before oldest or if oldest is also at 0 (buffer effectively linear at this point)
                _df(1,"Fit at start: offset=0");
                write_offset = 0;
                write_ptr = rb->buffer;
             }
        }


        // 3. If slot and buffer space found, break loop
        if (slot_available && write_ptr != NULL) {
            break;
        }

        // 4. If no space/slot, free the oldest slot
        if (rb->start_slot == rb->end_slot) {
            // Should not happen if required_len <= SHARED_BUFFER_SIZE, but safety check
            LOGE(TAG, "Ring buffer full and cannot free space for line (len %d). Dropping.", required_len);
            xSemaphoreGive(rb->mutex);
            return false;
        }
        
        LOGW(TAG, "Ring buffer full or fragmented, freeing oldest slot (%d).", rb->start_slot);
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
     } else if (rb->buffer_write_offset == 0 && write_offset + required_len == SHARED_BUFFER_SIZE) {
          // Wrapped perfectly to the end, next write starts at 0.
          rb->buffer_write_offset = 0;
     }


    _df(1,"Added Line: start=%u, end=%u, new_write_offset=%u", rb->start_slot, rb->end_slot, rb->buffer_write_offset);

    // Release mutex
    xSemaphoreGive(rb->mutex);
    return true;
}

/**
 * @brief Retrieves the oldest line from the ring buffer.
 */
static bool ring_buffer_get_line(ring_buffer_t *rb, char *out_buffer, size_t max_len, size_t *out_len) {
    if (!rb || !out_buffer || max_len == 0 || !out_len) return false;

    // Acquire mutex
    if (xSemaphoreTake(rb->mutex, portMAX_DELAY) != pdTRUE) {
        LOGE(TAG, "Failed to acquire ring buffer mutex in get_line!");
        return false;
    }

    // Check if buffer is empty
    if (rb->start_slot == rb->end_slot) {
        xSemaphoreGive(rb->mutex);
        *out_len = 0;
        return false; // No data available
    }

    // Get data from the oldest slot
    line_slot_t *slot = &rb->slots[rb->start_slot];
    size_t len_to_copy = slot->len;

    if (len_to_copy >= max_len) {
        LOGW(TAG, "Output buffer too small (%d) for line (%d), truncating.", max_len, len_to_copy);
        len_to_copy = max_len - 1;
    }

    // Copy data to output buffer and null-terminate
    memcpy(out_buffer, slot->ptr, len_to_copy);
    out_buffer[len_to_copy] = '\0';
    *out_len = len_to_copy;

    // Advance the start slot index (effectively removing the item)
     _df(1,"Get Line: slot=%u, len=%u", rb->start_slot, len_to_copy);
    rb->start_slot = (rb->start_slot + 1) % MAX_BUFFER_SLOTS;
    
    // Optimization: If buffer becomes empty, reset write offset.
    if (rb->start_slot == rb->end_slot) {
        rb->buffer_write_offset = 0;
        _df(1, "Ring buffer empty after get, reset write offset");
    }


    // Release mutex
    xSemaphoreGive(rb->mutex);
    return true;
}


// --- Serial Callback ---

/**
 * @brief Callback function registered with arduino_serial_wrapper.
 * Called when a complete line is received from the serial port.
 * NOTE: Assumes this callback runs in a context where brief mutex waits are acceptable.
 */
static void serial_line_received_callback(serial_handle_t handle, const char *line, size_t len) {
    // 1. Add the received line to the ring buffer
    if (ring_buffer_add_line(&response_buffer, line, len)) {
        // 2. Notify the processing task queue that new data is available
        if (serial_received_queue != NULL) {
            uint8_t dummy_notification = 1; // Content doesn't matter, just the event
            // Use xQueueSend with a zero timeout - non-blocking. If queue is full,
            // notification is lost, but data is still in the ring buffer.
            // The task will eventually process it when it gets CPU time.
            BaseType_t result = xQueueSend(serial_received_queue, &dummy_notification, 0);
            if (result != pdTRUE) {
                // This might happen if the processing task falls behind significantly.
                // Data is still buffered, so it's not critical, but indicates potential bottleneck.
                 _df(1,"Serial notification queue full.");
                // Consider logging less frequently if this occurs often.
                // LOGW(TAG, "Serial notification queue full.");
            }
        } else {
             LOGE(TAG,"Serial received queue is NULL in callback!");
        }
    } else {
        // ring_buffer_add_line already logs errors/warnings
        LOGW(TAG, "Failed to add line to ring buffer. Line dropped.");
    }
    // IMPORTANT: Keep this callback short and fast.
}

// --- Task Implementation ---

/**
 * @brief The main function for the Machine Response Processing Task.
 */
void machine_response_proc_task(void *args) {
    // The machine_interface_t instance is passed globally via s_machine_interface
    // Alternatively, it could be passed via 'args', but setup function needs it too.
    machine_interface_t *machine = s_machine_interface;

    if (!machine) {
         LOGE(TAG, "Machine interface is NULL! Task cannot run.");
         vTaskDelete(NULL);
         return; // Should not happen if run() succeeded
    }

    LOGI(TAG, ">> Starting machine response processing task...");

    char line_buffer[MAX_LINE_LENGTH]; // Buffer to hold line retrieved from ring buffer
    uint8_t notification_item;         // Dummy item received from queue

    while (1) {
        // Block indefinitely waiting for a notification from the queue
        if (xQueueReceive(serial_received_queue, ¬ification_item, portMAX_DELAY) == pdTRUE) {
            // Notification received, try to get data from the ring buffer
            size_t line_len;
            // Loop to process all available lines in the buffer before blocking again
            while (ring_buffer_get_line(&response_buffer, line_buffer, MAX_LINE_LENGTH, &line_len)) {
                 _df(1, "Processing line (len %d): %s", line_len, line_buffer);
                if (line_len > 0) {
                    // Call the machine interface function to process the response
                    // Pass the line_buffer containing the null-terminated string
                    machine_interface_process_machine_state_response(machine, line_buffer);
                }
            }
            // No more lines currently in the buffer, loop back to wait for next notification
        }
        // If xQueueReceive fails unexpectedly (shouldn't with portMAX_DELAY), loop continues
    }

    // Should never reach here, but good practice to include
    LOGW(TAG, "Machine Response Processing Task terminating unexpectedly...");
    s_machine_interface = NULL; // Clear global pointer
    vTaskDelete(NULL);
}

// --- Setup Routine ---

/**
 * @brief Sets up and starts the machine response processing task.
 *
 * @param serial_handle Handle to the serial port obtained from serial_init.
 * @param machine Pointer to the initialized machine_interface_t instance.
 * @param pinned_core The core to which the task is pinned to, or tskNO_AFFINITY if the task has no core affinity.
 * @return true on success, false on failure.
 */
bool machine_response_proc_task_run(serial_handle_t serial_handle, machine_interface_t *machine, BaseType_t pinned_core) {
    if (machine_response_proc_task_handle != NULL) {
        LOGE(TAG, "Task already running!");
        return false;
    }
    if (serial_handle == NULL) {
        LOGE(TAG, "Invalid serial handle provided.");
        return false;
    }
    if (machine == NULL) {
        LOGE(TAG, "Invalid machine interface provided.");
        return false;
    }

    LOGI(TAG, "Initializing machine response processing...");

    // 1. Initialize the Ring Buffer
    if (!ring_buffer_init(&response_buffer)) {
        LOGE(TAG, "Failed to initialize response ring buffer.");
        // No resources allocated yet other than potentially mutex, but cleanup is complex here.
        // Best to ensure init succeeds or prevent startup.
        return false;
    }

    // 2. Create the Notification Queue
    // Queue stores simple notifications (uint8_t), not the full lines.
    serial_received_queue = xQueueCreate(QUEUE_LENGTH, sizeof(uint8_t));
    if (serial_received_queue == NULL) {
        LOGE(TAG, "Failed to create serial received notification queue!");
        vSemaphoreDelete(response_buffer.mutex); // Clean up mutex
        return false;
    }

    // 3. Store machine interface pointer globally (needed by the task)
    s_machine_interface = machine;

    // 4. Register the Serial Line Callback
    if (!serial_register_line_callback(serial_handle, serial_line_received_callback)) {
        LOGE(TAG, "Failed to register serial line callback!");
        vQueueDelete(serial_received_queue);      // Clean up queue
        vSemaphoreDelete(response_buffer.mutex); // Clean up mutex
        s_machine_interface = NULL;
        return false;
    }

    // 5. Create the Machine Response Processing Task
    BaseType_t task_created = xTaskCreatePinnedToCore(
        machine_response_proc_task,
        "MachineRespProc",              // Task name
        TASK_STACK_SIZE,                // Stack depth
        NULL,                           // Parameter passed to the task (using global s_machine_interface instead)
        TASK_PRIORITY,                  // Task priority
        &machine_response_proc_task_handle, // Task handle
        pinned_core
    );

    if (task_created != pdPASS) {
        LOGE(TAG, "Failed to create machine response processing task!");
        serial_unregister_line_callback(serial_handle, serial_line_received_callback); // Clean up callback
        vQueueDelete(serial_received_queue);      // Clean up queue
        vSemaphoreDelete(response_buffer.mutex); // Clean up mutex
        machine_response_proc_task_handle = NULL; // Ensure handle is NULL on failure
        s_machine_interface = NULL;
        return false;
    }

    LOGI(TAG, "Machine response processing task started successfully.");
    return true;
}
