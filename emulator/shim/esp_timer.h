/**
 * \file esp_timer.h (host shim)
 * \brief Time reads route to the emulator's VirtualClock (single time source,
 *        FR-034). One-shot timers are stored in the clock and fired when a
 *        deterministic advance crosses their deadline.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_timer* esp_timer_handle_t;

typedef void (*esp_timer_cb_t)(void* arg);

typedef struct {
    esp_timer_cb_t callback;
    void*          arg;
    int            dispatch_method; /* ignored on host */
    const char*    name;
    bool           skip_unhandled_events;
} esp_timer_create_args_t;

/** Microseconds since emulator start, from the VirtualClock. */
int64_t esp_timer_get_time(void);

esp_err_t esp_timer_create(const esp_timer_create_args_t* args,
                           esp_timer_handle_t* out_handle);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period_us);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
esp_err_t esp_timer_delete(esp_timer_handle_t timer);

#ifdef __cplusplus
}
#endif
