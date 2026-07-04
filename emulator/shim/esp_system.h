/**
 * \file esp_system.h (host shim)
 */
#pragma once

#include <stdint.h>
#include <stdlib.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t esp_get_free_heap_size(void) { return 4 * 1024 * 1024; }
static inline uint32_t esp_get_minimum_free_heap_size(void) { return 4 * 1024 * 1024; }

void esp_restart(void);

#ifdef __cplusplus
}
#endif
