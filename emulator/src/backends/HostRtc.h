/**
 * \file HostRtc.h
 * \brief Host implementation of cdc::hal::IRtc driven by the VirtualClock.
 *
 * Wall-clock time = base timestamp + virtual uptime. The base defaults to a
 * fixed epoch so deterministic runs always see the same date/time (FR-034);
 * interactive runs may seed it from the host clock via useHostTime().
 */
#pragma once

#include <ctime>

#include "cdc_hal/IRtc.h"

namespace emu {

class HostRtc : public cdc::hal::IRtc {
public:
    static HostRtc& instance();

    /// Seed the base timestamp from the host's real clock (interactive runs).
    void useHostTime();

    // IService
    bool init() override;
    bool start() override { return true; }
    void stop() override {}
    cdc::core::ServiceState getState() const override { return state_; }
    const char* getName() const override { return "HostRtc"; }

    // IRtc
    void getTime(struct tm* timeinfo) const override;
    void getTimeStr(char* buf, size_t bufLen) const override;
    void getDateStr(char* buf, size_t bufLen) const override;
    void setTime(int hour, int minute, int second) override;
    void setDate(int year, int month, int day) override;
    void setTimestamp(time_t timestamp) override;
    time_t getTimestamp() const override;
    bool isTimeSet() const override { return time_set_; }
    void markTimeSet() override { time_set_ = true; }
    void setTimezoneOffset(int8_t hours) override { tz_offset_ = hours; }
    int8_t getTimezoneOffset() const override { return tz_offset_; }

private:
    HostRtc() = default;

    /// Fixed default epoch: 2026-01-01 12:00:00 UTC.
    static constexpr time_t kDefaultBase = 1767268800;

    time_t                  base_ = kDefaultBase;
    int64_t                 base_uptime_us_ = 0;
    bool                    time_set_ = true;
    int8_t                  tz_offset_ = 0;
    cdc::core::ServiceState state_ = cdc::core::ServiceState::UNINITIALIZED;
};

}  // namespace emu
