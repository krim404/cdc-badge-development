/**
 * \file EspShim.cpp
 * \brief Host backing for esp_timer / esp_random / esp_err / esp_system shims.
 *
 * Randomness is a seeded PRNG by default so scripted runs are byte-identical
 * (FR-034); emu_random_seed() lets tests or the CLI change the seed.
 */
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <random>

#include "../backends/VirtualClock.h"

extern "C" {
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
}

namespace {

std::mt19937& rng()
{
    static std::mt19937 gen(0xCDCBADEu);
    return gen;
}

}  // namespace

extern "C" {

/* --- esp_timer ---------------------------------------------------------- */

int64_t esp_timer_get_time(void)
{
    return emu::VirtualClock::instance().nowUs();
}

esp_err_t esp_timer_create(const esp_timer_create_args_t* args,
                           esp_timer_handle_t* out_handle)
{
    if (!args || !out_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    auto* t = emu::VirtualClock::instance().timerCreate(args->callback, args->arg,
                                                        args->name);
    *out_handle = reinterpret_cast<esp_timer_handle_t>(t);
    return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us)
{
    auto* t = reinterpret_cast<emu::VirtualClock::Timer*>(timer);
    return emu::VirtualClock::instance().timerStart(t, timeout_us, false)
               ? ESP_OK
               : ESP_ERR_INVALID_ARG;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period_us)
{
    auto* t = reinterpret_cast<emu::VirtualClock::Timer*>(timer);
    return emu::VirtualClock::instance().timerStart(t, period_us, true)
               ? ESP_OK
               : ESP_ERR_INVALID_ARG;
}

esp_err_t esp_timer_stop(esp_timer_handle_t timer)
{
    auto* t = reinterpret_cast<emu::VirtualClock::Timer*>(timer);
    return emu::VirtualClock::instance().timerStop(t) ? ESP_OK
                                                      : ESP_ERR_INVALID_STATE;
}

esp_err_t esp_timer_delete(esp_timer_handle_t timer)
{
    auto* t = reinterpret_cast<emu::VirtualClock::Timer*>(timer);
    emu::VirtualClock::instance().timerDelete(t);
    return ESP_OK;
}

/* --- esp_random --------------------------------------------------------- */

void emu_random_seed(uint32_t seed) { rng().seed(seed); }

uint32_t esp_random(void) { return rng()(); }

void esp_fill_random(void* buf, size_t len)
{
    auto* bytes = static_cast<uint8_t*>(buf);
    for (size_t i = 0; i < len; ++i) {
        bytes[i] = static_cast<uint8_t>(rng()());
    }
}

/* --- esp_err / esp_system / esp_log ------------------------------------- */

const char* esp_err_to_name(esp_err_t code)
{
    switch (code) {
    case ESP_OK:
        return "ESP_OK";
    case ESP_FAIL:
        return "ESP_FAIL";
    case ESP_ERR_NO_MEM:
        return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG:
        return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE:
        return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_NOT_FOUND:
        return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_TIMEOUT:
        return "ESP_ERR_TIMEOUT";
    case ESP_ERR_NVS_NOT_FOUND:
        return "ESP_ERR_NVS_NOT_FOUND";
    default:
        return "ESP_ERR_UNKNOWN";
    }
}

void esp_restart(void)
{
    fprintf(stderr, "E (emul) esp_restart() called - exiting emulator\n");
    exit(1);
}

uint32_t esp_log_timestamp(void)
{
    return emu::VirtualClock::instance().nowMs();
}

void esp_log_level_set(const char* tag, esp_log_level_t level)
{
    (void)tag;
    (void)level;
}

esp_log_level_t esp_log_level_get(const char* tag)
{
    (void)tag;
    return ESP_LOG_INFO;
}

}  // extern "C"
