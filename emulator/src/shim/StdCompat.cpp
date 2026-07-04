/**
 * \file StdCompat.cpp
 * \brief Non-standard libc helpers the vendored sources expect from newlib
 *        (itoa/utoa are ESP32/newlib extensions missing on glibc/macOS/MSVC),
 *        plus the host definitions of small firmware singletons whose real
 *        implementations do not compile off-device.
 */
#include <cstdio>

extern "C" {

char* itoa(int value, char* str, int base)
{
    if (base == 16) {
        snprintf(str, 34, "%x", value);
    } else if (base == 8) {
        snprintf(str, 34, "%o", value);
    } else {
        snprintf(str, 34, "%d", value);
    }
    return str;
}

char* utoa(unsigned value, char* str, int base)
{
    if (base == 16) {
        snprintf(str, 34, "%x", value);
    } else if (base == 8) {
        snprintf(str, 34, "%o", value);
    } else {
        snprintf(str, 34, "%u", value);
    }
    return str;
}

}  // extern "C"

/* --- PinManager host definitions (PinEntryView shows badge-lockout state;
 * the emulator has no PIN subsystem, so the badge is never blocked). ------ */
#include "cdc_core/PinManager.h"

namespace cdc::core {

PinManager& PinManager::instance()
{
    static PinManager manager;
    return manager;
}

bool PinManager::isBadgeBlocked() const { return false; }

uint32_t PinManager::getLockoutRemainingMs() const { return 0; }

}  // namespace cdc::core
