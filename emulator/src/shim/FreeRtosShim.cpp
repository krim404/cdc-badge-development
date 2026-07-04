/**
 * \file FreeRtosShim.cpp
 * \brief Host backing for the freertos/* shim headers: std mutexes, a simple
 *        FIFO queue, and delays that advance the VirtualClock.
 */
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "../backends/VirtualClock.h"

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
}

namespace {

/* One wrapper type serves plain, recursive and binary semaphores: a recursive
 * mutex satisfies all lock patterns the reused sources use (single-threaded
 * emulator; the lock still guards timer-callback reentrancy). */
struct Semaphore {
    std::recursive_mutex mutex;
};

struct Queue {
    std::mutex                      mutex;
    size_t                          item_size = 0;
    size_t                          max_items = 0;
    std::deque<std::vector<uint8_t>> items;
};

}  // namespace

extern "C" {

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return new Semaphore(); }
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return new Semaphore(); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return new Semaphore(); }

void vSemaphoreDelete(SemaphoreHandle_t sem)
{
    delete static_cast<Semaphore*>(sem);
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticksToWait)
{
    (void)ticksToWait;
    if (!sem) {
        return pdFALSE;
    }
    static_cast<Semaphore*>(sem)->mutex.lock();
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t sem)
{
    if (!sem) {
        return pdFALSE;
    }
    static_cast<Semaphore*>(sem)->mutex.unlock();
    return pdTRUE;
}

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t sem, TickType_t ticksToWait)
{
    return xSemaphoreTake(sem, ticksToWait);
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t sem)
{
    return xSemaphoreGive(sem);
}

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize)
{
    auto* q = new Queue();
    q->item_size = itemSize;
    q->max_items = length;
    return q;
}

void vQueueDelete(QueueHandle_t queue)
{
    delete static_cast<Queue*>(queue);
}

BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t ticksToWait)
{
    (void)ticksToWait;
    auto* q = static_cast<Queue*>(queue);
    if (!q || !item) {
        return pdFALSE;
    }
    std::lock_guard<std::mutex> lock(q->mutex);
    if (q->items.size() >= q->max_items) {
        return pdFALSE;
    }
    const auto* bytes = static_cast<const uint8_t*>(item);
    q->items.emplace_back(bytes, bytes + q->item_size);
    return pdTRUE;
}

BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void* item, BaseType_t* woken)
{
    if (woken) {
        *woken = pdFALSE;
    }
    return xQueueSend(queue, item, 0);
}

BaseType_t xQueueReceive(QueueHandle_t queue, void* out, TickType_t ticksToWait)
{
    (void)ticksToWait; /* no blocking: single-threaded, waiting would deadlock */
    auto* q = static_cast<Queue*>(queue);
    if (!q || !out) {
        return pdFALSE;
    }
    std::lock_guard<std::mutex> lock(q->mutex);
    if (q->items.empty()) {
        return pdFALSE;
    }
    std::memcpy(out, q->items.front().data(), q->item_size);
    q->items.pop_front();
    return pdTRUE;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue)
{
    auto* q = static_cast<Queue*>(queue);
    if (!q) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(q->mutex);
    return static_cast<UBaseType_t>(q->items.size());
}

void vTaskDelay(TickType_t ticks)
{
    auto& clock = emu::VirtualClock::instance();
    clock.advanceMs(static_cast<int64_t>(ticks));
    if (clock.isRealtime()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
    }
}

TickType_t xTaskGetTickCount(void)
{
    return static_cast<TickType_t>(emu::VirtualClock::instance().nowMs());
}

BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stack,
                       void* arg, UBaseType_t prio, TaskHandle_t* out)
{
    (void)fn;
    (void)stack;
    (void)arg;
    (void)prio;
    if (out) {
        *out = nullptr;
    }
    fprintf(stderr, "W (emul) xTaskCreate('%s') is unsupported off-device\n",
            name ? name : "?");
    return pdFAIL;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name,
                                   uint32_t stack, void* arg, UBaseType_t prio,
                                   TaskHandle_t* out, BaseType_t core)
{
    (void)core;
    return xTaskCreate(fn, name, stack, arg, prio, out);
}

void vTaskDelete(TaskHandle_t task) { (void)task; }

uint32_t ulTaskNotifyTake(BaseType_t clearOnExit, TickType_t ticksToWait)
{
    (void)clearOnExit;
    (void)ticksToWait;
    return 0;
}

void xTaskNotifyGive(TaskHandle_t task) { (void)task; }

}  // extern "C"
