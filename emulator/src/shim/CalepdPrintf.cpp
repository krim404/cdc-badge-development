/**
 * \file CalepdPrintf.cpp
 * \brief Debug-log sink for the vendored CalEPD driver's printf output.
 *
 * See calepd_printf_redirect.h for why the driver's printf is intercepted.
 * Each non-empty line becomes one debug-level log line on stderr, so the
 * output is still available under a raised log level but never pollutes
 * stdout or the default log.
 */
#include <cstdarg>
#include <cstdio>

#include "cdc_log.h"

extern "C" int emu_calepd_printf(const char* fmt, ...)
{
    char    buf[512];
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n <= 0) {
        return n;
    }
    // The STATS output is a multi-line block; log line by line.
    char* line = buf;
    for (char* p = buf; ; ++p) {
        if (*p != '\n' && *p != '\0') {
            continue;
        }
        const bool end = (*p == '\0');
        *p = '\0';
        if (*line) {
            log_write(CDC_LOG_LEVEL_DEBUG, "CalEPD", "%s", line);
        }
        if (end) {
            break;
        }
        line = p + 1;
    }
    return n;
}
