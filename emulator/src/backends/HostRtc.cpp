#include "HostRtc.h"

#include <cstdio>

#include "VirtualClock.h"

namespace emu {

HostRtc& HostRtc::instance()
{
    static HostRtc rtc;
    return rtc;
}

bool HostRtc::init()
{
    state_ = cdc::core::ServiceState::INITIALIZED;
    return true;
}

void HostRtc::useHostTime()
{
    base_ = time(nullptr);
    base_uptime_us_ = VirtualClock::instance().nowUs();
}

time_t HostRtc::getTimestamp() const
{
    const int64_t elapsed_us = VirtualClock::instance().nowUs() - base_uptime_us_;
    return base_ + static_cast<time_t>(elapsed_us / 1000000);
}

void HostRtc::setTimestamp(time_t timestamp)
{
    base_ = timestamp;
    base_uptime_us_ = VirtualClock::instance().nowUs();
    time_set_ = true;
}

void HostRtc::getTime(struct tm* timeinfo) const
{
    if (!timeinfo) {
        return;
    }
    const time_t now = getTimestamp() + tz_offset_ * 3600;
#ifdef _WIN32
    gmtime_s(timeinfo, &now);
#else
    gmtime_r(&now, timeinfo);
#endif
}

void HostRtc::getTimeStr(char* buf, size_t bufLen) const
{
    struct tm t = {};
    getTime(&t);
    snprintf(buf, bufLen, "%02d:%02d", t.tm_hour, t.tm_min);
}

void HostRtc::getDateStr(char* buf, size_t bufLen) const
{
    struct tm t = {};
    getTime(&t);
    snprintf(buf, bufLen, "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1,
             t.tm_mday);
}

void HostRtc::setTime(int hour, int minute, int second)
{
    struct tm t = {};
    getTime(&t);
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
#ifdef _WIN32
    setTimestamp(_mkgmtime(&t) - tz_offset_ * 3600);
#else
    setTimestamp(timegm(&t) - tz_offset_ * 3600);
#endif
}

void HostRtc::setDate(int year, int month, int day)
{
    struct tm t = {};
    getTime(&t);
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
#ifdef _WIN32
    setTimestamp(_mkgmtime(&t) - tz_offset_ * 3600);
#else
    setTimestamp(timegm(&t) - tz_offset_ * 3600);
#endif
}

}  // namespace emu

namespace cdc::hal {

IRtc* getRtcInstance()
{
    return &emu::HostRtc::instance();
}

}  // namespace cdc::hal
