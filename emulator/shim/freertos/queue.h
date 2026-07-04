/**
 * \file queue.h (host shim)
 * \brief Queue API subset backed by an in-process FIFO (FreeRtosShim.cpp).
 */
#pragma once

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize);
void          vQueueDelete(QueueHandle_t queue);

BaseType_t  xQueueSend(QueueHandle_t queue, const void* item, TickType_t ticksToWait);
BaseType_t  xQueueSendFromISR(QueueHandle_t queue, const void* item, BaseType_t* woken);
BaseType_t  xQueueReceive(QueueHandle_t queue, void* out, TickType_t ticksToWait);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);

#ifdef __cplusplus
}
#endif
