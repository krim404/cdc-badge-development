/**
 * \file HostLog.cpp
 * \brief Host implementation of the cdc_log.h API (the firmware's cdc_log.cpp
 *        writes to TinyUSB CDC and is not compiled off-device).
 *
 * Log lines go to stderr so PNG/frame data and machine-readable output on
 * stdout stay clean. The error-log ring buffer is kept so host_api_sysinfo
 * and friends behave as on the badge.
 */
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "cdc_log.h"
#include "esp_timer.h"

namespace {

std::mutex        g_mutex;
log_level_t       g_level = CDC_LOG_LEVEL_INFO;
error_log_entry_t g_errors[ERROR_LOG_MAX_ENTRIES];
size_t            g_error_count = 0;

char levelChar(log_level_t level)
{
    switch (level) {
    case CDC_LOG_LEVEL_ERROR:
        return 'E';
    case CDC_LOG_LEVEL_WARN:
        return 'W';
    case CDC_LOG_LEVEL_INFO:
        return 'I';
    case CDC_LOG_LEVEL_DEBUG:
        return 'D';
    default:
        return 'V';
    }
}

void recordError(log_level_t level, const char* message)
{
    if (g_error_count >= ERROR_LOG_MAX_ENTRIES) {
        std::memmove(&g_errors[0], &g_errors[1],
                     (ERROR_LOG_MAX_ENTRIES - 1) * sizeof(g_errors[0]));
        g_error_count = ERROR_LOG_MAX_ENTRIES - 1;
    }
    error_log_entry_t& e = g_errors[g_error_count++];
    e.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    e.level = level;
    std::snprintf(e.message, sizeof(e.message), "%s", message);
}

}  // namespace

extern "C" {

void log_init(void) {}

void log_set_level(log_level_t level) { g_level = level; }

log_level_t log_get_level(void) { return g_level; }

void log_write(log_level_t level, const char* tag, const char* fmt, ...)
{
    if (level > g_level || level == CDC_LOG_LEVEL_NONE) {
        return;
    }
    char body[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(g_mutex);
    fprintf(stderr, "%c (%u) %s: %s\n", levelChar(level),
            (unsigned)(esp_timer_get_time() / 1000), tag ? tag : "?", body);
    if (level == CDC_LOG_LEVEL_ERROR || level == CDC_LOG_LEVEL_WARN) {
        recordError(level, body);
    }
}

void log_raw(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::lock_guard<std::mutex> lock(g_mutex);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void log_hex(const char* tag, const char* label, const uint8_t* data, size_t len)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    fprintf(stderr, "I (%u) %s: %s (%zu bytes):",
            (unsigned)(esp_timer_get_time() / 1000), tag ? tag : "?",
            label ? label : "", len);
    for (size_t i = 0; i < len; ++i) {
        fprintf(stderr, " %02x", data[i]);
    }
    fprintf(stderr, "\n");
}

size_t error_log_get_entries(error_log_entry_t* entries, size_t max_entries)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    const size_t n = g_error_count < max_entries ? g_error_count : max_entries;
    std::memcpy(entries, g_errors, n * sizeof(g_errors[0]));
    return n;
}

size_t error_log_get_count(void)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_error_count;
}

void error_log_clear(void)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_error_count = 0;
}

void error_log_dump(void)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    for (size_t i = 0; i < g_error_count; ++i) {
        fprintf(stderr, "[%u] %c %s\n", (unsigned)g_errors[i].timestamp_ms,
                levelChar(g_errors[i].level), g_errors[i].message);
    }
}

/* Console I/O: the emulator has no serial command interface; the console API
 * degrades to stdout so nothing that prints crashes. */
void console_init(void) {}
bool console_available(void) { return false; }
int  console_getchar(void) { return -1; }
void console_print(const char* str) { fputs(str, stderr); }
void console_printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}
void console_putchar(char c) { fputc(c, stderr); }
void console_flush(void) { fflush(stderr); }
void console_register_output_hook(console_output_hook_t hook) { (void)hook; }
void console_register_input_hook(console_input_available_hook_t avail_hook,
                                 console_input_getchar_hook_t getchar_hook)
{
    (void)avail_hook;
    (void)getchar_hook;
}
void log_register_authgate_hook(log_authgate_hook_t hook) { (void)hook; }

}  // extern "C"
