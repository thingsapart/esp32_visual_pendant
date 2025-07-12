#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "config.h"
#ifndef __QUEUE_H__
#define __QUEUE_H__

#define MAX_GCODE_Q_LEN 20

typedef struct {
  char buffer[MAX_GCODE_Q_LEN]
             [MAX_GCODE_STR_LEN];  // Fixed-size buffer for G-code commands.
                                   // Adjust size as needed.
  size_t head;
  size_t tail;
  size_t count;
} gcode_queue_t;

// G-code queue functions
void gcode_queue_init(gcode_queue_t *queue);
bool gcode_queue_push(gcode_queue_t *queue, const char *gcode);
bool gcode_queue_pop(gcode_queue_t *queue, char *gcode);
bool gcode_queue_peek(const gcode_queue_t *queue, char *gcode);
size_t gcode_queue_count(const gcode_queue_t *queue);

#endif