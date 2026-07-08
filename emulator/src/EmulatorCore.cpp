#include "EmulatorCore.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#include "backends/HostDisplay.h"
#include "backends/HostKeypad.h"
#include "backends/HostNet.h"
#include "backends/HostSecureElement.h"
#include "backends/LockscreenView.h"
#include "backends/VirtualClock.h"

#include "cdc_core/EventBus.h"
#include "cdc_ui/I18n.h"
#include "cdc_ui/ViewStack.h"
#include "plugin_manager/CapabilityChecker.h"
#include "plugin_manager/LockscreenRegistry.h"
#include "plugin_manager/Plugin.h"
#include "plugin_manager/PluginManager.h"
#include "plugin_manager/PluginManifest.h"
#include "plugin_manager/PluginStorage.h"
#include "plugin_manager/Prerequisites.h"
#include "plugin_manager/WamrImports.h"
#include "plugin_manager/host_api.h"

extern "C" {
#include "wasm_export.h"
void plg_set_active_plugin(void* plugin);
}

/* cdc_log.h clashes with wasm_export.h over log_level_t (the same conflict
 * the firmware isolates via plugin_log_bridge); this file logs directly. */
#define LOG_E(tag, fmt, ...) fprintf(stderr, "E %s: " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) fprintf(stderr, "W %s: " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...) fprintf(stderr, "I %s: " fmt "\n", tag, ##__VA_ARGS__)

namespace cdc::plugin_manager {
// Emulator-side hooks defined in PluginManagerHost.cpp / PluginStorageHost.cpp.
void emulatorSetActivePlugin(Plugin* plugin);
void emulatorSetMessageInfo(std::vector<std::string> types,
                            bool (*activate)(void*), void* ctx);
void emulatorSetStorageBase(const std::string& dir);
void emulatorSetActiveBinary(const std::string& id, const std::string& wasmPath,
                             const std::string& langPath);
}  // namespace cdc::plugin_manager

extern "C" void plg_msg_pump(void);
extern "C" void plg_msg_on_unload(void* plugin);
extern "C" void plg_ext_feature_pump(void);
extern "C" void plg_ext_feature_on_unload(void* plugin);
extern "C" void plg_net_pump(void);
extern "C" void plg_net_on_unload(void* plugin);
extern "C" void plg_surface_on_unload(void* plugin);
extern "C" void plg_http_on_unload(void* plugin);
extern "C" void plg_socket_on_unload(void* plugin);

namespace emu {

// The nvs shim backend also needs the base dir (HostNvs.cpp).
void hostNvsSetBaseDir(const std::string& dir);
// Message-packet backend hooks (HostMessageTransfer.cpp).
void hostMsgSetBaseDir(const std::string& dir);
bool msgInject(const std::string& file, std::string& error);
// 64-bit-safe native overrides (WamrImports64.cpp).
bool registerHostImportOverrides();

namespace {

constexpr const char* TAG = "EmulatorCore";

/// Firmware plg_tick task cadence (PluginManager.cpp TICK_INTERVAL_MS).
constexpr int64_t kTickIntervalMs = 50;

bool readWholeFile(const std::string& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    out = buf.str();
    return true;
}

/// Report capability families the emulator stubs (out of scope) or cannot
/// emulate (device residency semantics) so the developer is never misled
/// (FR-009, FR-037, T051).
void reportCapabilities(const cdc::plugin_manager::PluginManifest& mf)
{
    const auto& c = mf.capabilities;
    if (c.ble) {
        LOG_W(TAG, "capability 'ble' is declared but UNAVAILABLE off-device "
                   "(calls fail cleanly)");
    }
    if (!c.gpio_pins.empty() || !c.pwm_pins.empty() || !c.adc_pins.empty() ||
        !c.i2c_bus.empty() || c.sao || c.grove) {
        LOG_W(TAG, "gpio/pwm/adc/i2c/sao capabilities are declared but "
                   "UNAVAILABLE off-device (calls fail cleanly)");
    }
    if (c.pixel_strip) {
        LOG_W(TAG, "capability 'pixel_strip' is declared but UNAVAILABLE off-device");
    }
    if (c.usb_cdc) {
        LOG_W(TAG, "capability 'usb_cdc' is declared but UNAVAILABLE off-device");
    }
    if (!c.message_types.empty()) {
        LOG_I(TAG, "badge-to-badge messaging is emulated via JSON packet "
                   "files: sends land in <data>/msg/out/, inject received "
                   "packets with --msg-in or the console's msg-in command");
    }
    if (c.background) {
        LOG_I(TAG, "capability 'background' is emulated (simplified): the "
                   "plugin stays loaded and keeps ticking while locked (L / "
                   "'lock' / leaving the plugin view)");
    }
    if (c.autoload || c.prevent_sleep) {
        LOG_W(TAG, "residency capabilities (autoload/prevent_sleep) are NOT "
                   "EMULATED - they depend on badge boot/power management");
    }
}

}  // namespace

struct EmulatorCore::ManifestHolder {
    cdc::plugin_manager::PluginManifest manifest;
};

namespace {

void unlockTrampoline(void* ctx)
{
    static_cast<EmulatorCore*>(ctx)->unlock();
}

void lockscreenTopTrampoline(void* ctx)
{
    // The plugin's last view popped off the root lockscreen - the user left
    // the plugin, whatever path popped it (N key, host_ui_pop from a tick,
    // a trapped view). Run the residency transition.
    static_cast<EmulatorCore*>(ctx)->lock();
}

}  // namespace

EmulatorCore::EmulatorCore(EmulatorOptions options)
    : options_(std::move(options)), manifest_(new ManifestHolder())
{
}

EmulatorCore::~EmulatorCore()
{
    shutdown();
}

const cdc::plugin_manager::PluginManifest& EmulatorCore::manifest() const
{
    return manifest_->manifest;
}

bool EmulatorCore::initRuntime()
{
    if (runtime_ready_) {
        return true;
    }
    if (!wasm_runtime_init()) {
        LOG_E(TAG, "WAMR runtime init failed");
        return false;
    }
    if (!cdc::plugin_manager::register_host_imports()) {
        LOG_E(TAG, "failed to register the \"cdc\" native table");
        wasm_runtime_destroy();
        return false;
    }
    // Shadow the struct-passing natives with 64-bit-safe wrappers; must come
    // AFTER the firmware table (WAMR resolves newest-first).
    if (!registerHostImportOverrides()) {
        LOG_E(TAG, "failed to register the 64-bit native overrides");
        wasm_runtime_destroy();
        return false;
    }
    runtime_ready_ = true;
    return true;
}

bool EmulatorCore::load()
{
    using namespace cdc::plugin_manager;

    // Wire the persistence backends to the chosen data dir before anything
    // reads them (NVS namespaces, vfat sandbox, SE store).
    emulatorSetStorageBase(options_.data_dir);
    hostNvsSetBaseDir(options_.data_dir);
    hostMsgSetBaseDir(options_.data_dir);
    HostSecureElement::instance().setBaseDir(options_.data_dir);
    HostWifi::setOffline(options_.offline);

    std::string metaJson;
    if (!readWholeFile(options_.meta_path, metaJson)) {
        LOG_E(TAG, "cannot read manifest %s", options_.meta_path.c_str());
        return false;
    }
    if (!PluginManifest::parse(metaJson.c_str(), metaJson.size(),
                               manifest_->manifest)) {
        LOG_E(TAG, "invalid meta.json (%s)", options_.meta_path.c_str());
        return false;
    }
    plugin_id_ = manifest_->manifest.id;

    const auto check = CapabilityChecker::validate(manifest_->manifest);
    if (!check.ok()) {
        LOG_E(TAG, "capability validation failed: %s", check.detail.c_str());
        return false;
    }
    reportCapabilities(manifest_->manifest);

    // Serve the resolved .wasm (and a sibling .lang, if any) through the
    // host PluginStorage so the reused Plugin::load works unchanged.
    std::string langPath;
    const size_t dot = options_.wasm_path.rfind(".wasm");
    if (dot != std::string::npos) {
        langPath = options_.wasm_path.substr(0, dot) + ".lang";
    }
    emulatorSetActiveBinary(plugin_id_, options_.wasm_path, langPath);
    emulatorSetMessageInfo(
        manifest_->manifest.capabilities.message_types,
        [](void* ctx) {
            // Wake the unloaded handler plugin headless (load + init +
            // prerequisites, no on_enter) - the badge's loadIntoBackground.
            auto* core = static_cast<EmulatorCore*>(ctx);
            if (!core->instantiatePlugin()) {
                return false;
            }
            int32_t rc = 0;
            if (!core->plugin_->callI("plugin_init", {}, &rc) || rc != 0) {
                return false;
            }
            core->residency_ = Residency::Background;
            return core->runPrerequisites();
        },
        this);
    PluginStorage::mount();

    if (!initRuntime()) {
        return false;
    }

    // Bring the display/UI singletons up before the first host call.
    HostDisplay::instance().init();
    HostKeypad::instance().init();
    cdc::ui::I18n::instance().init();
    (void)cdc::core::EventBus::instance().init();
    (void)PluginManager::instance().init();  // plugin base depth = lockscreen root

    return instantiatePlugin();
}

bool EmulatorCore::instantiatePlugin()
{
    using cdc::plugin_manager::Plugin;
    plugin_.reset(new Plugin());
    if (!plugin_->load(plugin_id_, manifest_->manifest)) {
        LOG_E(TAG, "wasm load/instantiate failed for %s", plugin_id_.c_str());
        plugin_.reset();
        return false;
    }
    plg_set_active_plugin(plugin_.get());
    cdc::plugin_manager::emulatorSetActivePlugin(plugin_.get());
    residency_ = Residency::Foreground;
    return validateExports();
}

void EmulatorCore::unloadPluginInstance()
{
    if (!plugin_) {
        return;
    }
    if (entered_) {
        (void)plugin_->callI("plugin_on_exit");
        entered_ = false;
    }
    cdc::plugin_manager::Prerequisites::release(*plugin_);
    (void)plugin_->callI("plugin_deinit");
    // Drop the plugin's lockscreen quick-action and message handlers so no
    // registry holds a dangling pointer (the firmware's teardownPlugin does
    // the same).
    cdc::plugin_manager::clearLockscreenRegistrationFor(plugin_.get());
    // Same hooks and order as the firmware's teardownPlugin (minus the ble
    // and gpio sources the emulator does not compile). http/socket matter
    // since simulateDeepSleep(): without them adopted sockets and open HTTP
    // requests leak their fds and slot-pool entries across reload cycles.
    plg_msg_on_unload(plugin_.get());
    plg_ext_feature_on_unload(plugin_.get());
    plg_surface_on_unload(plugin_.get());
    plg_net_on_unload(plugin_.get());
    plg_http_on_unload(plugin_.get());
    plg_socket_on_unload(plugin_.get());
    plg_set_active_plugin(nullptr);
    cdc::plugin_manager::emulatorSetActivePlugin(nullptr);
    plugin_.reset();
    residency_ = Residency::Unloaded;
}

bool EmulatorCore::validateExports()
{
    static const char* kRequired[] = {"plugin_required_api_major",
                                      "plugin_required_api_minor",
                                      "plugin_init",
                                      "plugin_deinit",
                                      "plugin_on_enter",
                                      "plugin_on_exit"};
    bool ok = true;
    for (const char* name : kRequired) {
        if (!plugin_->hasExport(name)) {
            LOG_E(TAG, "required export missing: %s", name);
            ok = false;
        }
    }
    if (!ok) {
        return false;
    }
    int32_t major = 0;
    int32_t minor = 0;
    if (!plugin_->callI("plugin_required_api_major", {}, &major) ||
        !plugin_->callI("plugin_required_api_minor", {}, &minor)) {
        LOG_E(TAG, "plugin_required_api_* call failed");
        return false;
    }
    if (major != HOST_API_LEVEL_MAJOR || minor > HOST_API_LEVEL_MINOR) {
        LOG_E(TAG, "plugin requires host API %d.%d, emulator provides %s",
              (int)major, (int)minor, HOST_API_LEVEL_STR);
        return false;
    }
    LOG_I(TAG, "plugin %s loaded (requires API %d.%d, emulator level %s)",
          plugin_id_.c_str(), (int)major, (int)minor, HOST_API_LEVEL_STR);
    return true;
}

bool EmulatorCore::start()
{
    // The fake lockscreen is the permanent root view (depth 1), mirroring
    // the badge's system UI below the plugin; plugin views stack above it
    // and popping the last one lands back here (residency transition).
    auto& stack = cdc::ui::ViewStack::instance();
    if (stack.depth() == 0) {
        if (!lockscreen_) {
            lockscreen_.reset(new LockscreenView());
            lockscreen_->setOnUnlock(unlockTrampoline, this);
            lockscreen_->setOnBecameTop(lockscreenTopTrampoline, this);
        }
        stack.push(lockscreen_.get());
    }

    int32_t rc = 0;
    if (!plugin_->callI("plugin_init", {}, &rc) || rc != 0) {
        LOG_E(TAG, "plugin_init failed (rc=%d, trap=%s)", (int)rc,
              plugin_->lastTrapMessage());
        return false;
    }
    if (!runPrerequisites()) {
        return false;
    }
    if (!plugin_->callI("plugin_on_enter", {}, &rc) || rc != 0) {
        LOG_E(TAG, "plugin_on_enter failed (rc=%d, trap=%s)", (int)rc,
              plugin_->lastTrapMessage());
        return false;
    }
    entered_ = true;
    next_tick_ms_ = VirtualClock::instance().nowMs() + kTickIntervalMs;
    renderIfNeeded(true);
    return true;
}

bool EmulatorCore::runPrerequisites()
{
    using namespace cdc::plugin_manager;

    // --fail-prereq: force a declared prerequisite to fail so its error path
    // is testable off-device (the host environment otherwise satisfies every
    // prerequisite). Honors the manifest's on_fail policy like the firmware:
    // "abort" (default) refuses the start, anything else continues with a
    // warning.
    if (!options_.fail_prereq.empty()) {
        for (const auto& spec : manifest_->manifest.prerequisites) {
            if (spec.name != options_.fail_prereq) {
                continue;
            }
            const std::string onFail = spec.on_fail.empty() ? "abort" : spec.on_fail;
            LOG_W(TAG, "prerequisite '%s' failed (forced by --fail-prereq, "
                       "on_fail=%s)",
                  spec.name.c_str(), onFail.c_str());
            if (onFail == "abort") {
                LOG_E(TAG, "prerequisite '%s' aborted start of %s",
                      spec.name.c_str(), plugin_id_.c_str());
                return false;
            }
            return true;  // soft failure: continue like the firmware does
        }
        LOG_W(TAG, "--fail-prereq '%s' is not declared by %s - ignored",
              options_.fail_prereq.c_str(), plugin_id_.c_str());
    }

    std::string failedName;
    std::string onFail;
    const PrereqResult result = Prerequisites::walk(*plugin_, failedName, onFail);
    if (result == PrereqResult::HardFailed) {
        LOG_E(TAG, "prerequisite '%s' aborted start of %s", failedName.c_str(),
              plugin_id_.c_str());
        return false;
    }
    if (result == PrereqResult::SoftFailed) {
        LOG_W(TAG, "prerequisite '%s' soft-failed for %s, continuing",
              failedName.c_str(), plugin_id_.c_str());
    }
    return true;
}

void EmulatorCore::renderIfNeeded(bool force)
{
    auto& stack = cdc::ui::ViewStack::instance();
    if (stack.needsRender()) {
        stack.render(true);  // synchronous flush -> HostDisplay captures
    } else if (force) {
        HostDisplay::instance().captureFrame();
    }
}

void EmulatorCore::injectKey(char key, bool longPress)
{
    auto& stack = cdc::ui::ViewStack::instance();
    if (longPress) {
        // Same route the firmware's long-press keypad callback takes; the
        // ViewStack also publishes KEY_LONG_PRESS on the EventBus.
        stack.dispatchLongPress(key);
    } else {
        auto& keypad = HostKeypad::instance();
        keypad.injectKey(key, false);
        // Same path AppUi::ui_process takes: drain the key buffer into the
        // view stack (which publishes KEY_PRESSED on the EventBus); the
        // plugin sees the key via its views' callbacks or a subscription.
        for (;;) {
            const auto next = keypad.getNextKey();
            if (next == cdc::hal::Key::KEY_NONE) {
                break;
            }
            stack.dispatchKey(static_cast<char>(next));
        }
    }
    cdc::core::EventBus::instance().process();
    // Leaving the plugin (its last view popping onto the root lockscreen)
    // raises a deferred lock via the lockscreen's became-top hook; the WASM
    // frame that triggered the pop has unwound by now, so it is safe here.
    processPendingLock();
    renderIfNeeded();
}

void EmulatorCore::lock()
{
    if (!locked_) {
        pending_lock_ = true;
    }
}

void EmulatorCore::processPendingLock()
{
    if (!pending_lock_) {
        return;
    }
    pending_lock_ = false;
    if (locked_) {
        return;
    }
    locked_ = true;
    LOG_I(TAG, "plugin left the foreground - fake lockscreen");

    auto& stack = cdc::ui::ViewStack::instance();
    while (stack.hasModal()) {
        stack.hideModal();
    }
    // Collapse any remaining plugin views down to the lockscreen root.
    stack.popToDepth(1);

    if (plugin_) {
        if (manifest_->manifest.capabilities.background) {
            // Background resident: leave the view (on_exit) but keep the
            // instance loaded and ticking, like the badge's background slot.
            if (entered_) {
                (void)plugin_->callI("plugin_on_exit");
                entered_ = false;
            }
            residency_ = Residency::Background;
        } else {
            // Foreground-only plugins go through the full unload cycle so
            // deinit/reload paths are testable off-device.
            unloadPluginInstance();
        }
    }

    cdc::core::EventBus::instance().publish(cdc::core::EventType::SYSTEM_LOCK, 0);
    cdc::core::EventBus::instance().process();
    if (lockscreen_) {
        lockscreen_->markDirty();
    }
    renderIfNeeded(true);
}

void EmulatorCore::unlock()
{
    if (!locked_) {
        return;
    }
    locked_ = false;
    LOG_I(TAG, "unlocking - back into the plugin");

    cdc::core::EventBus::instance().publish(cdc::core::EventType::SYSTEM_UNLOCK, 0);
    cdc::core::EventBus::instance().process();

    int32_t rc = 0;
    if (residency_ == Residency::Unloaded) {
        // Full reload, exactly like starting the plugin on the badge again.
        if (!instantiatePlugin() || !start()) {
            LOG_E(TAG, "reload after unlock failed");
        }
        return;
    }
    residency_ = Residency::Foreground;
    if (plugin_ && (!plugin_->callI("plugin_on_enter", {}, &rc) || rc != 0)) {
        LOG_E(TAG, "plugin_on_enter after unlock failed (rc=%d)", (int)rc);
    }
    entered_ = true;
    renderIfNeeded(true);
}

bool EmulatorCore::injectEvent(const std::string& spec)
{
    using cdc::core::EventType;

    // "name" or "name:value" - names mirror cdc_core/EventBus.h EventType.
    static const struct {
        const char* name;
        EventType   type;
    } kEvents[] = {
        {"key_pressed", EventType::KEY_PRESSED},
        {"key_released", EventType::KEY_RELEASED},
        {"key_long_press", EventType::KEY_LONG_PRESS},
        {"usb_connected", EventType::POWER_USB_CONNECTED},
        {"usb_disconnected", EventType::POWER_USB_DISCONNECTED},
        {"charging", EventType::POWER_CHARGING},
        {"battery_low", EventType::POWER_BATTERY_LOW},
        {"battery_critical", EventType::POWER_BATTERY_CRITICAL},
        {"unlock", EventType::SYSTEM_UNLOCK},
        {"lock", EventType::SYSTEM_LOCK},
        {"sleep", EventType::SYSTEM_SLEEP},
        {"wake", EventType::SYSTEM_WAKE},
        {"sleep_incoming", EventType::SYSTEM_SLEEP_INCOMING},
        {"ble_connected", EventType::BLE_CONNECTED},
        {"ble_disconnected", EventType::BLE_DISCONNECTED},
        {"display_refresh", EventType::DISPLAY_REFRESH},  // value: 1 begin, 0 end
    };

    std::string name = spec;
    uint8_t     value = 0;
    const size_t colon = spec.find(':');
    if (colon != std::string::npos) {
        name = spec.substr(0, colon);
        value = static_cast<uint8_t>(atoi(spec.c_str() + colon + 1));
    }
    for (const auto& entry : kEvents) {
        if (name == entry.name) {
            LOG_I(TAG, "injecting event %s (value=%u)", entry.name, value);
            cdc::core::EventBus::instance().publish(entry.type, value);
            cdc::core::EventBus::instance().process();
            renderIfNeeded();
            return true;
        }
    }
    LOG_E(TAG, "unknown event '%s' (see EventBus.h for names)", name.c_str());
    return false;
}

void EmulatorCore::sendCmd(const std::string& payload)
{
    if (!cdc::plugin_manager::PluginManager::instance().dispatchCmd(
            plugin_id_, payload.c_str(), payload.size())) {
        LOG_W(TAG, "plugin_on_cmd not handled (export missing?)");
    }
    processPendingLock();
    renderIfNeeded();
}

void EmulatorCore::advance(int64_t ms)
{
    auto&         clock = VirtualClock::instance();
    const int64_t target = clock.nowMs() + ms;
    while (clock.nowMs() < target) {
        const int64_t step =
            (next_tick_ms_ - clock.nowMs()) < (target - clock.nowMs())
                ? (next_tick_ms_ - clock.nowMs())
                : (target - clock.nowMs());
        if (step > 0) {
            clock.advanceMs(step);
        }
        const int64_t now = clock.nowMs();
        if (now >= next_tick_ms_) {
            next_tick_ms_ += kTickIntervalMs;
            const uint64_t uptime = static_cast<uint64_t>(now);
            const int32_t  hi = static_cast<int32_t>(uptime >> 32);
            const int32_t  lo = static_cast<int32_t>(uptime & 0xFFFFFFFFu);
            // Foreground AND background residents keep ticking; only an
            // unloaded (locked, non-background) plugin gets no ticks.
            if (plugin_) {
                (void)plugin_->callI("plugin_on_tick", {lo, hi});
            }
            plg_msg_pump();
            plg_ext_feature_pump();
            plg_net_pump();
            auto& stack = cdc::ui::ViewStack::instance();
            stack.dispatchTick(static_cast<uint32_t>(now));
            // Same as AppUi::ui_process: fire the registered inactivity
            // action once the plugin-configured timeout elapses.
            if (stack.depth() > 0) {
                stack.checkInactivity(static_cast<uint32_t>(now));
            }
            cdc::core::EventBus::instance().process();
            processPendingLock();
        }
    }
    processPendingLock();
    renderIfNeeded();
    // Deliver a frame that was coalesced during the emulated panel-busy
    // window (realtime refresh mode; no-op otherwise).
    HostDisplay::instance().pump();
}

bool EmulatorCore::injectMessage(const std::string& file)
{
    std::string error;
    if (!msgInject(file, error)) {
        LOG_E(TAG, "msg-in %s failed: %s", file.c_str(), error.c_str());
        return false;
    }
    // The delivery stash is drained by plg_msg_pump on the next tick.
    advance(kTickIntervalMs);
    return true;
}

int EmulatorCore::runScripted()
{
    if (!start()) {
        return 3;
    }
    if (!options_.cmd.empty()) {
        sendCmd(options_.cmd);
    }
    if (!options_.msg_in.empty()) {
        (void)injectMessage(options_.msg_in);
    }
    if (!options_.keys.empty()) {
        std::stringstream keys(options_.keys);
        std::string       token;
        while (std::getline(keys, token, ',')) {
            if (token.empty()) {
                continue;
            }
            if (token[0] == '@') {
                // "@usb_disconnected" etc.: publish an EventBus event at this
                // point in the sequence instead of pressing a key.
                (void)injectEvent(token.substr(1));
            } else if (token == "L") {
                lock();  // not a badge key: scripted fake-lockscreen entry
            } else if (token == "U") {
                unlock();
            } else {
                const bool longPress = token.size() > 1 && token[1] == '!';
                injectKey(token[0], longPress);
            }
            // Small settle window between steps, as a human would produce.
            advance(kTickIntervalMs);
        }
    }
    if (options_.run_ticks > 0) {
        advance(options_.run_ticks * kTickIntervalMs);
    }
    if (options_.run_seconds > 0) {
        advance(options_.run_seconds * 1000);
    }
    if (options_.serve_seconds > 0) {
        serveWallclock(options_.serve_seconds);
    }
    renderIfNeeded(true);
    shutdown();
    return 0;
}

int EmulatorCore::serveWallclock(int64_t seconds)
{
    using clk = std::chrono::steady_clock;
    auto& vclock = VirtualClock::instance();
    const auto deadline = clk::now() + std::chrono::seconds(seconds);
    const auto midpoint = clk::now() + std::chrono::seconds(seconds / 2);
    bool sleep_done = false;
    std::printf("[emu] serving in real time for %llds\n", static_cast<long long>(seconds));

    while (clk::now() < deadline) {
        // Optional mid-run sleep simulation, once.
        if (!sleep_done && clk::now() >= midpoint &&
            (options_.sim_light_sleep || options_.sim_deep_sleep)) {
            sleep_done = true;
            if (options_.sim_deep_sleep) simulateDeepSleep();
            else simulateLightSleep();
        }

        vclock.advanceMs(kTickIntervalMs);
        const uint64_t uptime = static_cast<uint64_t>(vclock.nowMs());
        if (plugin_) {
            (void)plugin_->callI("plugin_on_tick",
                                 {static_cast<int32_t>(uptime & 0xFFFFFFFFu),
                                  static_cast<int32_t>(uptime >> 32)});
        }
        plg_msg_pump();
        plg_ext_feature_pump();
        plg_net_pump();
        cdc::ui::ViewStack::instance().dispatchTick(static_cast<uint32_t>(uptime));
        cdc::core::EventBus::instance().process();
        processPendingLock();
        std::this_thread::sleep_for(std::chrono::milliseconds(kTickIntervalMs));
    }
    return 0;
}

void EmulatorCore::simulateLightSleep()
{
    // The badge pauses tasks during light sleep and resumes on wake. A non-
    // blocking listener simply keeps accepting in the pump afterward; this
    // marks the boundary and lets the plugin observe a wake tick.
    std::printf("[emu] --- light sleep --- (listener should survive)\n");
    if (plugin_) {
        const uint64_t up = static_cast<uint64_t>(VirtualClock::instance().nowMs());
        (void)plugin_->callI("plugin_on_tick",
                             {static_cast<int32_t>(up & 0xFFFFFFFFu),
                              static_cast<int32_t>(up >> 32)});
    }
    plg_net_pump();
}

void EmulatorCore::simulateDeepSleep()
{
    // Deep sleep is a reboot: tear the plugin down and reload it. autoload +
    // set_resident(true) in plugin_init bring a resident service back up.
    std::printf("[emu] --- deep sleep (reboot) --- reloading plugin\n");
    unloadPluginInstance();
    if (load() && start()) {
        std::printf("[emu] plugin reloaded after deep sleep\n");
    } else {
        std::printf("[emu] plugin failed to reload after deep sleep\n");
    }
}

void EmulatorCore::shutdown()
{
    unloadPluginInstance();
    if (runtime_ready_) {
        cdc::plugin_manager::unregister_host_imports();
        wasm_runtime_destroy();
        runtime_ready_ = false;
    }
}

}  // namespace emu
