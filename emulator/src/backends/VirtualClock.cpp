#include "VirtualClock.h"

#include <algorithm>
#include <chrono>

namespace emu {

struct VirtualClock::Timer {
    void (*cb)(void*) = nullptr;
    void*       arg = nullptr;
    const char* name = nullptr;
    int64_t     deadline_us = -1;  // -1 = not armed
    uint64_t    period_us = 0;     // 0 = one-shot
};

VirtualClock& VirtualClock::instance()
{
    static VirtualClock clock;
    return clock;
}

int64_t VirtualClock::nowUs() const
{
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    return now_us_;
}

void VirtualClock::advanceUs(int64_t us)
{
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    now_us_ += us;
    fireDueTimersLocked(lock);
}

void VirtualClock::enableRealtime()
{
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    realtime_ = true;
    last_wall_us_ = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
}

void VirtualClock::syncRealtime()
{
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    if (!realtime_) {
        return;
    }
    const int64_t wall = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
    const int64_t delta = wall - last_wall_us_;
    last_wall_us_ = wall;
    if (delta > 0) {
        now_us_ += delta;
        fireDueTimersLocked(lock);
    }
}

void VirtualClock::fireDueTimersLocked(std::unique_lock<std::recursive_mutex>& lock)
{
    // Fire in deadline order until nothing is due. Callbacks may re-arm or
    // create timers; the loop re-scans, so that is safe.
    for (;;) {
        Timer* due = nullptr;
        for (Timer* t : timers_) {
            if (t->deadline_us >= 0 && t->deadline_us <= now_us_ &&
                (!due || t->deadline_us < due->deadline_us)) {
                due = t;
            }
        }
        if (!due) {
            return;
        }
        if (due->period_us > 0) {
            due->deadline_us += static_cast<int64_t>(due->period_us);
        } else {
            due->deadline_us = -1;
        }
        auto cb = due->cb;
        auto arg = due->arg;
        lock.unlock();
        if (cb) {
            cb(arg);
        }
        lock.lock();
    }
}

VirtualClock::Timer* VirtualClock::timerCreate(void (*cb)(void*), void* arg,
                                               const char* name)
{
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    auto* t = new Timer{cb, arg, name, -1, 0};
    timers_.push_back(t);
    return t;
}

bool VirtualClock::timerStart(Timer* t, uint64_t timeout_us, bool periodic)
{
    if (!t) {
        return false;
    }
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    t->deadline_us = now_us_ + static_cast<int64_t>(timeout_us);
    t->period_us = periodic ? timeout_us : 0;
    return true;
}

bool VirtualClock::timerStop(Timer* t)
{
    if (!t) {
        return false;
    }
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    t->deadline_us = -1;
    return true;
}

void VirtualClock::timerDelete(Timer* t)
{
    if (!t) {
        return;
    }
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    timers_.erase(std::remove(timers_.begin(), timers_.end(), t), timers_.end());
    delete t;
}

}  // namespace emu
