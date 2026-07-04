/**
 * \file HostNet.h
 * \brief WiFi-always-on host backend (FR-035): status queries report a
 *        plausible connected station; connect/scan/disconnect succeed as
 *        no-ops. HTTP/socket traffic runs over the host's real network.
 *
 * --offline forces the plugin-visible network-error contract on HTTP/socket
 * calls while WiFi still reports connected (FR-036).
 */
#pragma once

#include "cdc_hal/IWifiController.h"

namespace emu {

class HostWifi : public cdc::hal::IWifiController {
public:
    static HostWifi& instance();

    /// --offline: HTTP/socket fail cleanly; WiFi still reports connected.
    static void setOffline(bool offline);
    static bool isOffline();

    // IService
    bool init() override
    {
        state_ = cdc::core::ServiceState::INITIALIZED;
        return true;
    }
    bool start() override { return true; }
    void stop() override {}
    cdc::core::ServiceState getState() const override { return state_; }
    const char* getName() const override { return "HostWifi"; }

    // IWifiController - always connected/ON.
    bool enable(cdc::hal::WifiMode mode) override
    {
        (void)mode;
        return true;
    }
    void disable() override {}
    bool isEnabled() const override { return true; }
    cdc::hal::WifiMode getMode() const override { return cdc::hal::WifiMode::STA; }
    cdc::hal::WifiState getWifiState() const override
    {
        return cdc::hal::WifiState::GOT_IP;
    }
    bool connect(const char* ssid, const char* password, uint32_t timeoutMs) override
    {
        (void)ssid;
        (void)password;
        (void)timeoutMs;
        return true;
    }
    void disconnect() override {}
    bool isConnected() const override { return true; }
    const char* getCurrentSsid() const override { return "emulator"; }
    bool getIpAddress(char* ip, size_t len) const override;
    bool getMacAddress(uint8_t* mac) const override;
    int8_t getRssi() const override { return -42; }
    bool startScan() override { return true; }
    bool isScanComplete() const override { return true; }
    uint8_t getScanResults(cdc::hal::WifiScanResult* results,
                           uint8_t maxResults) override;
    bool startAp(const char* ssid, const char* password, uint8_t channel) override
    {
        (void)ssid;
        (void)password;
        (void)channel;
        return false;
    }
    uint8_t getConnectedStations() const override { return 0; }

private:
    HostWifi() = default;

    cdc::core::ServiceState state_ = cdc::core::ServiceState::UNINITIALIZED;
};

}  // namespace emu
