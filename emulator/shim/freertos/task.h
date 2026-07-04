/**
 * \file task.h (host shim)
 * \brief Task API subset. The emulator is single-threaded by design (the
 *        firmware's tick task is replaced by EmulatorCore's loop), so task
 *        creation is unsupported and delays advance the virtual clock.
 */
#pragma once

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*TaskFunction_t)(void*);

/** Advance the virtual clock by the given tick count (1 tick = 1 ms). */
void vTaskDelay(TickType_t ticks);

/** Current virtual-clock time in ticks (ms). */
TickType_t xTaskGetTickCount(void);

/* Task creation is not supported off-device: the emulator drives everything
 * from one loop. Returns pdFAIL and logs, so accidental use is visible. */
BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stack,
                       void* arg, UBaseType_t prio, TaskHandle_t* out);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name,
                                   uint32_t stack, void* arg, UBaseType_t prio,
                                   TaskHandle_t* out, BaseType_t core);
void vTaskDelete(TaskHandle_t task);

uint32_t ulTaskNotifyTake(BaseType_t clearOnExit, TickType_t ticksToWait);
void     xTaskNotifyGive(TaskHandle_t task);

#ifdef __cplusplus
}
#endif
