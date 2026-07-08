/**
 * \file EmulatorCore.h
 * \brief Loads a plugin .wasm against the reused WAMR runtime + "cdc" host-API
 *        table and drives its lifecycle off-device (FR-001/FR-002).
 *
 * The core never boots firmware or the badge state machine: it owns exactly
 * one foreground Plugin (the reused firmware class), registers the reused
 * native table (WamrImports.cpp), validates the manifest with the reused
 * PluginManifest/CapabilityChecker, and steps
 * plugin_init -> plugin_on_enter -> {keys | ticks | cmd | events}* ->
 * plugin_on_exit -> plugin_deinit.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace cdc::plugin_manager {
class Plugin;
struct PluginManifest;
}  // namespace cdc::plugin_manager

namespace emu {

class LockscreenView;

struct EmulatorOptions {
    std::string wasm_path;
    std::string meta_path;
    std::string data_dir = ".emu-data";
    std::string frames_dir;    ///< write a PNG per committed frame
    std::string snapshot_dir;  ///< regression compare mode (FR-026)
    std::string keys;          ///< scripted keys "1,2,Y,N"; "5!" = long press,
                               ///< "@name[:value]" publishes an EventBus event
    std::string cmd;           ///< plugin command channel payload (plugin_on_cmd)
    std::string msg_in;        ///< inject this packet file after plugin_on_enter
    std::string fail_prereq;   ///< force this declared prerequisite to fail
    bool        headless = false;
    bool        offline = false;
    int64_t     run_seconds = -1;  ///< advance the clock deterministically
    int64_t     run_ticks = -1;    ///< advance in 50 ms plugin ticks
    int64_t     serve_seconds = -1;///< run in REAL time for this long (network servers)
    bool        sim_light_sleep = false; ///< pulse a light-sleep cycle during serve
    bool        sim_deep_sleep = false;  ///< reboot (unload+reload) during serve
};

class EmulatorCore {
public:
    explicit EmulatorCore(EmulatorOptions options);
    ~EmulatorCore();

    /// Manifest parse + capability validation + WAMR bring-up + module load.
    /// Reports declared-but-unavailable capabilities (FR-009/FR-037).
    bool load();

    /// plugin_required_api_* + plugin_init + plugin_on_enter (+ first render).
    bool start();

    /// Feed one key through the same path the firmware uses
    /// (ViewStack::dispatchKey); renders if the views marked themselves dirty.
    void injectKey(char key, bool longPress = false);

    /// Deliver the --cmd payload via plugin_on_cmd (FR-008 `cmd`).
    void sendCmd(const std::string& payload);

    /// Publish a named EventBus event ("usb_disconnected", "battery_low",
    /// ...; optional ":value" suffix) so plugin subscriptions are testable.
    /// \return false when the name is unknown.
    bool injectEvent(const std::string& spec);

    /// Inject an incoming badge-to-badge packet from a JSON file (the same
    /// format outbound sends produce under `<data>/msg/out/`). Wakes an
    /// unloaded handler plugin headless, like the badge does.
    /// \return false with a logged reason when no handler accepts it.
    bool injectMessage(const std::string& file);

    /// Advance the virtual clock by `ms`, firing plugin_on_tick every 50 ms
    /// and ViewStack ticks, exactly like the firmware's plg_tick task cadence.
    void advance(int64_t ms);

    /// Run the whole scripted session per options (keys/cmd/seconds/ticks).
    /// \return process exit code (0 = clean run).
    int runScripted();

    /// Run in REAL (wall-clock) time for `seconds`, ticking every 50 ms so
    /// network servers (host_net_* listeners) can accept real connections
    /// while the process lives. Optionally pulses a simulated sleep cycle.
    int serveWallclock(int64_t seconds);

    /// Simulate a light-sleep cycle: log the sleep/wake pair and run one tick
    /// plus a net pump afterwards. Sockets survive, matching light sleep on
    /// the badge. IPowerManager pre-sleep/wake callbacks are NOT fired yet.
    void simulateLightSleep();

    /// Simulate deep sleep: full plugin teardown then reload (a reboot), so
    /// autoload + set_resident bring a resident service back afterward.
    void simulateDeepSleep();

    /// plugin_on_exit + plugin_deinit + unload. Idempotent.
    void shutdown();

    /// Render pending view changes and capture the frame (FR-033).
    void renderIfNeeded(bool force = false);

    /// Fake lockscreen: leave the plugin like the badge does. With
    /// capabilities.background the plugin stays loaded and keeps ticking;
    /// otherwise it goes through the full unload cycle (on_exit, deinit,
    /// instance destroyed) and unlock() reloads it from scratch - so both
    /// residency paths are testable off-device. Publishes SYSTEM_LOCK.
    ///
    /// DEFERRED, like the firmware's requestStopActivePlugin: the request is
    /// frequently raised from inside a running plugin call (ui::pop in
    /// plugin_on_action pops onto the lockscreen), and destroying the WASM
    /// instance under its own live interpreter frame is a use-after-free.
    /// The transition runs at the next safe point (end of injectKey/advance).
    void lock();

    /// Y on the lockscreen (or the `unlock` console command): back into the
    /// plugin - re-enter for background residents, full reload otherwise.
    void unlock();

    bool isLocked() const { return locked_; }

    const cdc::plugin_manager::PluginManifest& manifest() const;

private:
    enum class Residency { Foreground, Background, Unloaded };

    bool initRuntime();
    bool validateExports();
    bool runPrerequisites();
    /// Create + load + validate the WASM instance (reused by unlock-reload).
    bool instantiatePlugin();
    /// on_exit + prerequisite release + deinit + destroy the instance,
    /// keeping the runtime alive (the badge's teardownPlugin equivalent).
    void unloadPluginInstance();
    /// Run a pending lock() transition; must only be called when no plugin
    /// WASM frame is on the stack.
    void processPendingLock();

    EmulatorOptions                              options_;
    std::unique_ptr<cdc::plugin_manager::Plugin> plugin_;
    std::unique_ptr<LockscreenView>              lockscreen_;
    std::string                                  plugin_id_;
    bool                                         runtime_ready_ = false;
    bool                                         entered_ = false;
    bool                                         locked_ = false;
    bool                                         pending_lock_ = false;
    Residency                                    residency_ = Residency::Foreground;
    int64_t                                      next_tick_ms_ = 0;

    struct ManifestHolder;
    std::unique_ptr<ManifestHolder> manifest_;
};

}  // namespace emu
