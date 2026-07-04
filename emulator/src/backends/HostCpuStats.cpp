/**
 * \file HostCpuStats.cpp
 * \brief Host definition of cdc::core::CpuStats (the firmware version reads
 *        FreeRTOS run-time counters that do not exist off-device). Reports a
 *        plausible constant load - determinism over accuracy.
 */
#include "cdc_core/CpuStats.h"

extern "C" {
#include "esp_timer.h"
}

namespace cdc::core {

bool CpuStats::sample(uint64_t& idleUs, uint64_t& wallUs)
{
    wallUs = static_cast<uint64_t>(esp_timer_get_time());
    idleUs = wallUs * 93 / 100;  // 7% load
    return true;
}

uint8_t CpuStats::loadOverWindow(uint32_t windowMs)
{
    (void)windowMs;
    return 7;
}

}  // namespace cdc::core
