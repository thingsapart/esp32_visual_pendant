#pragma once

#ifdef REMOTE_COMMS_C6_SDIO_BRIDGE

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include "esp_now.h"

#ifdef __cplusplus
extern "C" {
#endif

// Drop-in replacement for standard esp-now functions,
// but named with a prefix. Or we can just include standard esp_now.h
// and define the implementations here.

#ifdef __cplusplus
}
#endif

#endif