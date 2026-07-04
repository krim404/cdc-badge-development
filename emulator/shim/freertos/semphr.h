/**
 * \file semphr.h (host shim)
 * \brief Semaphore/mutex API subset backed by std::(recursive_)mutex.
 */
#pragma once

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
void              vSemaphoreDelete(SemaphoreHandle_t sem);

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticksToWait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t sem, TickType_t ticksToWait);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t sem);

#ifdef __cplusplus
}
#endif
