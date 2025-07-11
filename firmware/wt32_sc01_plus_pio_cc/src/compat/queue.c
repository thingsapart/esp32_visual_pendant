#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "config.h"
#include "debug.h"
#include "compat/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the G-code queue
void gcode_queue_init(gcode_queue_t *queue) {
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
}

// Add a G-code command to the queue (FIFO).  Handles wraparound.
bool gcode_queue_push(gcode_queue_t *queue, const char *gcode) {
    if (queue->count >= MAX_GCODE_Q_LEN) {
        _d(2, "G-code queue overflow!");
        return false; // Indicate failure
    }

    size_t len = strlen(gcode);
    if (len >= sizeof(queue->buffer[0])) {
        _df(2, "G-code command too long: %s", gcode);
        return false; // Command too long for buffer
    }

    strcpy(queue->buffer[queue->head], gcode);
    queue->head = (queue->head + 1) % MAX_GCODE_Q_LEN;
    queue->count++;
    return true;
}

// Retrieve a G-code command from the queue (FIFO).  Handles wraparound.
bool gcode_queue_pop(gcode_queue_t *queue, char *gcode) {
    if (queue->count == 0) {
        return false; // Queue is empty
    }

    strcpy(gcode, queue->buffer[queue->tail]);
    queue->tail = (queue->tail + 1) % MAX_GCODE_Q_LEN;
    queue->count--;
    return true;
}

// Peek at the next G-code command without removing it.
bool gcode_queue_peek(const gcode_queue_t *queue, char *gcode) {
    if (queue->count == 0) {
        return false; // Queue is empty
    }
    strcpy(gcode, queue->buffer[queue->tail]);
    return true;
}

size_t gcode_queue_count(const gcode_queue_t *queue) {
    return queue->count;
}

#ifdef __cplusplus
}
#endif