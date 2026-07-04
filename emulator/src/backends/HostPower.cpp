/**
 * \file HostPower.cpp
 * \brief Host IPowerManager: plausible constant battery/USB values (the
 *        host-api-coverage contract's "trivial host values" for `power`).
 *
 * Also provides the host definitions of the cdc::ui::SleepManager methods the
 * reused host_api_power.cpp calls: sleep inhibitors are tracked (a plugin can
 * set and clear them) but never act - the emulator does not sleep.
 */
#include <cstring>

#include "cdc_hal/IPowerManager.h"
#include "cdc_os_ui/SleepManager.h"

namespace emu {

class HostPower : public cdc::hal::IPowerManager {
public:
    static HostPower& instance()
    {
        static HostPower power;
        return power;
    }

    // IService
    bool init() override
    {
        state_ = cdc::core::ServiceState::INITIALIZED;
        return true;
    }
    bool start() override { return true; }
    void stop() override {}
    cdc::core::ServiceState getState() const override { return state_; }
    const char* getName() const override { return "HostPower"; }

    // IPowerManager - a healthy, USB-powered badge at 87%.
    uint16_t getBatteryVoltage() const override { return 4050; }
    uint8_t getBatteryPercent() const override { return 87; }
    bool isUsbConnected() const override { return true; }
    cdc::hal::PowerSource getPowerSource() const override
    {
        return cdc::hal::PowerSource::USB;
    }
    cdc::hal::ChargeStatus getChargeStatus() const override
    {
        return cdc::hal::ChargeStatus::CHARGE_DONE;
    }
    bool isBatteryLow() const override { return false; }
    bool isBatteryCritical() const override { return false; }
    bool isBatteryPresent() const override { return true; }
    void setChargingEnabled(bool enabled) override { (void)enabled; }
    void enterShipMode() override {}
    void update() override {}
    void refresh() override {}
    void prepareForSleep() override {}
    void recoverFromSleep() override {}

private:
    HostPower() = default;

    cdc::core::ServiceState state_ = cdc::core::ServiceState::UNINITIALIZED;
};

}  // namespace emu

namespace cdc::hal {

IPowerManager* getPowerManagerInstance()
{
    return &emu::HostPower::instance();
}

}  // namespace cdc::hal

/* --- SleepManager host definitions (cdc_os_ui's SleepManager.cpp is not
 * compiled off-device; only the inhibitor API is reachable from plugins). --- */
namespace cdc::ui {

SleepManager& SleepManager::instance()
{
    static SleepManager manager;
    return manager;
}

bool SleepManager::addSleepInhibitor(const char* reason)
{
    if (!reason) {
        return false;
    }
    for (uint8_t i = 0; i < inhibitorCount_; ++i) {
        if (std::strcmp(inhibitors_[i], reason) == 0) {
            return false;
        }
    }
    if (inhibitorCount_ >= MAX_SLEEP_INHIBITORS) {
        return false;
    }
    inhibitors_[inhibitorCount_++] = reason;
    return true;
}

bool SleepManager::removeSleepInhibitor(const char* reason)
{
    if (!reason) {
        return false;
    }
    for (uint8_t i = 0; i < inhibitorCount_; ++i) {
        if (std::strcmp(inhibitors_[i], reason) == 0) {
            inhibitors_[i] = inhibitors_[--inhibitorCount_];
            inhibitors_[inhibitorCount_] = nullptr;
            return true;
        }
    }
    return false;
}

}  // namespace cdc::ui
