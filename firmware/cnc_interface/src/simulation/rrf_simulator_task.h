#ifndef RRF_SIMULATOR_TASK_H
#define RRF_SIMULATOR_TASK_H

#include "config.h"

#ifdef ENABLE_DEVICE_SIMULATOR_TASK

#ifdef __cplusplus
extern "C" {
#endif

// Starts the simulator task that pretends to be an RRF board
void rrf_device_simulator_start();

#ifdef __cplusplus
}
#endif

#endif // ENABLE_DEVICE_SIMULATOR_TASK
#endif // RRF_SIMULATOR_TASK_H
