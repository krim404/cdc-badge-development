/**
 * \file PluginManagerHost.cpp
 * \brief Host definitions of the PluginManager members the reused sources
 *        reference. The firmware's PluginManager.cpp (tick task, storage
 *        discovery, background slots) is not compiled off-device; the
 *        emulator runs exactly one foreground plugin and EmulatorCore drives
 *        its lifecycle. Only the dispatch/cmd surface used by PluginUiState
 *        and the host_api_* families is provided here.
 */
#include <cstring>

#include "cdc_log.h"
#include "plugin_manager/PluginManager.h"
#include "plugin_manager/host_api.h"

extern "C" void plg_msg_init(void);

namespace cdc::plugin_manager {

namespace {

/// The one foreground plugin, owned by EmulatorCore (raw view here).
Plugin* g_active = nullptr;

/// Message wake-up support: the MIME types the session's plugin declares
/// (kept even while the instance is unloaded) and EmulatorCore's headless
/// loader, so an injected packet can wake an unloaded handler plugin like
/// the badge's activateForMessageType does.
std::vector<std::string> g_message_types;
bool (*g_activate_hook)(void*) = nullptr;
void* g_activate_ctx = nullptr;

}  // namespace

/// Emulator-side hook: EmulatorCore announces the active plugin.
void emulatorSetActivePlugin(Plugin* plugin)
{
    g_active = plugin;
}

void emulatorSetMessageInfo(std::vector<std::string> types,
                            bool (*activate)(void*), void* ctx)
{
    g_message_types = std::move(types);
    g_activate_hook = activate;
    g_activate_ctx = ctx;
}

bool PluginManager::messageTypeInstalled(const char* mime) const
{
    if (!mime) {
        return false;
    }
    for (const auto& type : g_message_types) {
        if (type == mime) {
            return true;
        }
    }
    return false;
}

bool PluginManager::activateForMessageType(const char* mime)
{
    if (!messageTypeInstalled(mime)) {
        return false;
    }
    if (g_active) {
        return true;  // already loaded (foreground or background resident)
    }
    // Wake the unloaded handler plugin headless, like loadIntoBackground.
    return g_activate_hook && g_activate_hook(g_activate_ctx);
}

PluginManager::PluginManager() = default;
PluginManager::~PluginManager() = default;

PluginManager& PluginManager::instance() noexcept
{
    static PluginManager manager;
    return manager;
}

bool PluginManager::init()
{
    // The emulator's fake lockscreen is the permanent root view (depth 1),
    // mirroring the badge's system UI below the plugin. Plugin views start
    // at pluginBaseDepth() + 1, which host_ui_pop_to_plugin relies on.
    plugin_base_depth_ = 1;
    plg_msg_init();  // wire the deferred message-handler resolver (as on device)
    initialised_ = true;
    return true;
}

void PluginManager::dispatchAction(uint32_t action_id, uint32_t idx,
                                   uint32_t user_data)
{
    if (!g_active) {
        return;
    }
    if (!g_active->callI("plugin_on_action",
                         {static_cast<int32_t>(action_id),
                          static_cast<int32_t>(idx),
                          static_cast<int32_t>(user_data)})) {
        if (g_active->lastCallTrapped()) {
            LOG_E("PluginMgr", "plugin_on_action trapped: %s",
                  g_active->lastTrapMessage());
        }
    }
}

void PluginManager::dispatchActionTo(Plugin* plugin, uint32_t action_id,
                                     uint32_t idx, uint32_t user_data)
{
    // Single-plugin emulator: only the foreground plugin can be a target.
    if (plugin && plugin == g_active) {
        dispatchAction(action_id, idx, user_data);
    }
}

int PluginManager::consumeCmd(char* out, size_t out_size)
{
    if (!out || out_size == 0) {
        return HOST_ERR_INVALID_ARG;
    }
    if (pending_cmd_.empty()) {
        out[0] = '\0';
        return 0;
    }
    const size_t n =
        pending_cmd_.size() < out_size - 1 ? pending_cmd_.size() : out_size - 1;
    std::memcpy(out, pending_cmd_.data(), n);
    out[n] = '\0';
    pending_cmd_.clear();
    return static_cast<int>(n);
}

std::vector<std::string> PluginManager::listInstalledIds() const
{
    std::vector<std::string> ids;
    if (g_active) {
        ids.push_back(g_active->id());
    }
    return ids;
}

std::optional<PluginManifest> PluginManager::getManifest(const std::string& id) const
{
    if (g_active && g_active->id() == id) {
        return g_active->manifest();
    }
    return std::nullopt;
}

bool PluginManager::dispatchCmd(const std::string& id, const char* cmd, size_t len)
{
    if (!g_active || !cmd || g_active->id() != id) {
        return false;
    }
    pending_cmd_.assign(cmd, len);
    if (!g_active->callI("plugin_on_cmd", {static_cast<int32_t>(len)})) {
        pending_cmd_.clear();
        return false;
    }
    return true;
}

}  // namespace cdc::plugin_manager
