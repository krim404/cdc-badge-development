/**
 * \file HostMessageTransfer.cpp
 * \brief Host definitions of the cdc::msg::MessageTransfer members the reused
 *        host_api_msg.cpp calls - BLE replaced by JSON packet files.
 *
 * Outbound: every send (sendTo / beginInteractiveSend) is written to
 * `<data>/msg/out/<seq>_<mime>.json` as {"mime", "payload" (base64),
 * "uptime_ms", "peer"} - the exchange format, directly re-injectable.
 * Inbound: emulatorMsgInject(file) parses the same format and runs the
 * firmware's delivery path: live handler stash first (drained by
 * plg_msg_pump on the tick), deferred-handler fallback second (which loads
 * the handler plugin headless via PluginManager::activateForMessageType).
 */
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "JsonStore.h"
#include "cJSON.h"
#include "cdc_msg/MessageTransfer.h"

extern "C" {
#include "esp_timer.h"
}

namespace fs = std::filesystem;

namespace {

std::string g_base_dir = ".emu-data";
uint32_t    g_out_seq = 0;

struct LiveHandler {
    cdc::msg::DeliverFn deliver;
};

std::map<std::string, LiveHandler>   g_handlers;
cdc::msg::MessageTransfer::CanHandleFn        g_deferred_can;
cdc::msg::MessageTransfer::DeferredDeliverFn  g_deferred_deliver;

std::string sanitizeMime(const char* mime)
{
    std::string name = mime ? mime : "unknown";
    std::replace_if(
        name.begin(), name.end(),
        [](char c) { return !(isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.'); },
        '_');
    return name;
}

bool writeOutbound(const char* mime, const uint8_t* data, uint32_t len,
                   const char* peer)
{
    char name[96];
    snprintf(name, sizeof(name), "%03u_%s.json", g_out_seq++,
             sanitizeMime(mime).c_str());
    const fs::path path = fs::path(g_base_dir) / "msg" / "out" / name;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "mime", mime ? mime : "");
    cJSON_AddStringToObject(root, "payload",
                            emu::base64Encode(data, len).c_str());
    cJSON_AddNumberToObject(root, "uptime_ms",
                            static_cast<double>(esp_timer_get_time() / 1000));
    cJSON_AddStringToObject(root, "peer", peer);
    const bool ok = emu::jsonWriteFile(path.string(), root);
    cJSON_Delete(root);
    if (ok) {
        fprintf(stderr, "I (HostMsg) outbound packet -> %s (%u bytes)\n",
                path.string().c_str(), (unsigned)len);
    }
    return ok;
}

}  // namespace

namespace emu {

void hostMsgSetBaseDir(const std::string& dir)
{
    g_base_dir = dir;
}

bool msgInject(const std::string& file, std::string& error)
{
    cJSON* root = emu::jsonReadFile(file);
    if (!root) {
        error = "cannot read/parse " + file;
        return false;
    }
    const cJSON* mime = cJSON_GetObjectItemCaseSensitive(root, "mime");
    const cJSON* payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (!cJSON_IsString(mime) || !cJSON_IsString(payload)) {
        cJSON_Delete(root);
        error = "packet needs string fields 'mime' and 'payload' (base64)";
        return false;
    }
    const std::vector<uint8_t> data = emu::base64Decode(payload->valuestring);
    const char*                peer = "emulator-peer";

    bool accepted = false;
    auto handler = g_handlers.find(mime->valuestring);
    if (handler != g_handlers.end() && handler->second.deliver) {
        // Live handler: the host_api_msg callback stashes the payload; the
        // next plg_msg_pump() on the tick fires the plugin action.
        accepted = handler->second.deliver(data.data(),
                                           static_cast<uint32_t>(data.size()),
                                           mime->valuestring, peer);
        if (!accepted) {
            error = "live handler rejected the payload";
        }
    } else if (g_deferred_can && g_deferred_can(mime->valuestring) &&
               g_deferred_deliver) {
        accepted = g_deferred_deliver(data.data(),
                                      static_cast<uint32_t>(data.size()),
                                      mime->valuestring, peer);
        if (!accepted) {
            error = "deferred handler rejected the payload";
        }
    } else {
        error = std::string("no handler registered for '") +
                mime->valuestring + "'";
    }
    cJSON_Delete(root);
    return accepted;
}

}  // namespace emu

namespace cdc::msg {

MessageTransfer& MessageTransfer::instance()
{
    static MessageTransfer transfer;
    return transfer;
}

/* IService lifecycle: nothing to bring up off-device (no BLE beacon). */
bool MessageTransfer::init()
{
    state_ = cdc::core::ServiceState::INITIALIZED;
    return true;
}

bool MessageTransfer::start()
{
    state_ = cdc::core::ServiceState::STARTED;
    return true;
}

void MessageTransfer::stop()
{
    state_ = cdc::core::ServiceState::STOPPED;
}

bool MessageTransfer::registerHandler(const char* mime, const char* descKey,
                                      DeliverFn deliver)
{
    (void)descKey;
    if (!mime || !deliver) {
        return false;
    }
    g_handlers[mime] = LiveHandler{std::move(deliver)};
    return true;
}

void MessageTransfer::unregisterHandler(const char* mime)
{
    if (mime) {
        g_handlers.erase(mime);
    }
}

bool MessageTransfer::hasHandler(const char* mime)
{
    if (!mime) {
        return false;
    }
    if (g_handlers.count(mime)) {
        return true;
    }
    return g_deferred_can && g_deferred_can(mime);
}

void MessageTransfer::setDeferredHandler(CanHandleFn canHandle,
                                         DeferredDeliverFn deliver)
{
    g_deferred_can = std::move(canHandle);
    g_deferred_deliver = std::move(deliver);
}

bool MessageTransfer::sendTo(const uint8_t addr[6], uint8_t addrType,
                             const char* mime, const uint8_t* data,
                             uint32_t len, bool persistent)
{
    (void)addrType;
    (void)persistent;
    char peer[24];
    snprintf(peer, sizeof(peer), "%02x:%02x:%02x:%02x:%02x:%02x", addr[0],
             addr[1], addr[2], addr[3], addr[4], addr[5]);
    return writeOutbound(mime, data, len, peer);
}

bool MessageTransfer::beginInteractiveSend(const char* mime, const uint8_t* data,
                                           uint32_t len, bool persistent)
{
    // No peer picker off-device: the "nearby badge" is the outbox directory.
    (void)persistent;
    return writeOutbound(mime, data, len, "interactive");
}

}  // namespace cdc::msg
