/**
 * \file FreeRTOS.h (host shim)
 * \brief Minimal FreeRTOS surface for compiling firmware sources on a desktop.
 *
 * Backed by std::mutex / std::recursive_mutex and a simple in-process queue in
 * emulator/src/shim/FreeRtosShim.cpp. Ticks come from the emulator's
 * VirtualClock so scripted runs stay deterministic. Only the APIs the reused
 * source set actually calls are provided.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t TickType_t;
typedef int      BaseType_t;
typedef unsigned UBaseType_t;

#define pdFALSE ((BaseType_t)0)
#define pdTRUE  ((BaseType_t)1)
#define pdPASS  pdTRUE
#define pdFAIL  pdFALSE

#define portMAX_DELAY        ((TickType_t)0xffffffffUL)
#define configTICK_RATE_HZ   ((TickType_t)1000)
#define portTICK_PERIOD_MS   ((TickType_t)1)
#define pdMS_TO_TICKS(ms)    ((TickType_t)(ms))

/* Opaque handles; the real objects live in FreeRtosShim.cpp. */
typedef void* SemaphoreHandle_t;
typedef void* QueueHandle_t;
typedef void* TaskHandle_t;

/* ISR helpers are meaningless on the host. */
#define portYIELD_FROM_ISR(...) ((void)0)
#define portEND_SWITCHING_ISR(...) ((void)0)

#ifdef __cplusplus
}
#endif
