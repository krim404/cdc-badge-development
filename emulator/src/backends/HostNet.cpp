#include "HostNet.h"

#include <cstring>

#include "cdc_os_ui/WifiHandlers.h"

namespace {

bool g_offline = false;

}  // namespace

namespace emu {

HostWifi& HostWifi::instance()
{
    static HostWifi wifi;
    return wifi;
}

void HostWifi::setOffline(bool offline) { g_offline = offline; }

bool HostWifi::isOffline() { return g_offline; }

bool HostWifi::getIpAddress(char* ip, size_t len) const
{
    if (!ip || len == 0) {
        return false;
    }
    std::strncpy(ip, "192.168.1.42", len - 1);
    ip[len - 1] = '\0';
    return true;
}

bool HostWifi::getMacAddress(uint8_t* mac) const
{
    if (!mac) {
        return false;
    }
    const uint8_t fixed[6] = {0xCD, 0xCB, 0xAD, 0x6E, 0x00, 0x01};
    std::memcpy(mac, fixed, sizeof(fixed));
    return true;
}

uint8_t HostWifi::getScanResults(cdc::hal::WifiScanResult* results,
                                 uint8_t maxResults)
{
    if (!results || maxResults == 0) {
        return 0;
    }
    // One plausible network so scan-driven plugin UIs have something to show.
    cdc::hal::WifiScanResult& r = results[0];
    std::memset(&r, 0, sizeof(r));
    std::strncpy(r.ssid, "emulator", sizeof(r.ssid) - 1);
    r.rssi = -42;
    r.security = cdc::hal::WifiSecurity::WPA2_PSK;
    r.channel = 6;
    return 1;
}

}  // namespace emu

namespace cdc::hal {

IWifiController* getWifiControllerInstance()
{
    return &emu::HostWifi::instance();
}

}  // namespace cdc::hal

/* --- WifiHandlers host definitions: the reused host_api_wifi.cpp routes
 * acquire/release/isConnected through this class; off-device the connection
 * is always up, so holds are counted but nothing is torn down. --- */
namespace cdc::ui {

WifiHandlers& WifiHandlers::instance()
{
    static WifiHandlers handlers;
    return handlers;
}

bool WifiHandlers::acquire()
{
    ++holdCount_;
    return true;
}

void WifiHandlers::release()
{
    if (holdCount_ > 0) {
        --holdCount_;
    }
}

bool WifiHandlers::isConnected() const { return true; }

}  // namespace cdc::ui
