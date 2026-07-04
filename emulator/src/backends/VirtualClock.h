/**
 * \file VirtualClock.h
 * \brief Single time source for the whole emulator (FR-034).
 *
 * Everything that tells time - the esp_timer shim, the FreeRTOS tick shim,
 * host_api_time, plugin_on_tick scheduling - reads this clock. In
 * deterministic mode it only moves when advance() is called (scripted runs,
 * snapshot tests); in realtime mode syncRealtime() folds elapsed wall-clock
 * time in so an interactive window feels live.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace emu {

class VirtualClock {
public:
    static VirtualClock& instance();

    /// Microseconds since emulator start.
    int64_t nowUs() const;
    /// Milliseconds since emulator start.
    uint32_t nowMs() const { return static_cast<uint32_t>(nowUs() / 1000); }

    /// Move time forward and fire every one-shot/periodic timer whose
    /// deadline falls inside the advanced window, in deadline order.
    void advanceUs(int64_t us);
    void advanceMs(int64_t ms) { advanceUs(ms * 1000); }

    /// Realtime mode: syncRealtime() advances by elapsed wall-clock time.
    void enableRealtime();
    bool isRealtime() const { return realtime_; }
    void syncRealtime();

    // ---- esp_timer emulation -------------------------------------------
    struct Timer;
    Timer* timerCreate(void (*cb)(void*), void* arg, const char* name);
    bool   timerStart(Timer* t, uint64_t timeout_us, bool periodic);
    bool   timerStop(Timer* t);
    void   timerDelete(Timer* t);

private:
    VirtualClock() = default;

    void fireDueTimersLocked(std::unique_lock<std::recursive_mutex>& lock);

    mutable std::recursive_mutex mutex_;
    // Start at 1 ms, not 0: firmware code uses "timestamp == 0" as a
    // not-yet-set sentinel (e.g. ToastView's auto-dismiss arming, fixed
    // upstream but present in the pinned vendor snapshot), and on real
    // hardware uptime is never 0 by the time a plugin runs.
    int64_t                      now_us_ = 1000;
    bool                         realtime_ = false;
    int64_t                      last_wall_us_ = 0;
    std::vector<Timer*>          timers_;
};

}  // namespace emu
