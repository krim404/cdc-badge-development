//! Emulator test fixture: proves the background-residency path.
//!
//! Declares `capabilities.background = true`, counts every `plugin_on_tick`
//! into NVS (`plg_bg_probe:ticks`), and registers a lockscreen quick-action.
//! The lockscreen test locks the emulator, advances the clock, and asserts
//! the tick counter kept growing while the plugin was resident in the
//! background - then unlocks and expects a plain re-enter (no re-init).

#![no_std]

extern crate alloc;

use cdc_badge_plugin::{lockscreen, log, nvs, plugin_main, ui};

plugin_main!();

const TAG: &str = "bg_probe";
const ACTION_QUICK: u32 = 1;

#[no_mangle]
pub extern "C" fn plugin_init() -> i32 {
    // A fresh init resets the counter, so a full unload/reload is
    // distinguishable from a background re-enter in the persisted state.
    let _ = nvs::set_u32("inits", nvs::get_u32("inits").unwrap_or(0) + 1);
    let _ = nvs::set_u32("ticks", 0);
    let _ = lockscreen::register("quick", ACTION_QUICK);
    log::info(TAG, "init");
    0
}

#[no_mangle]
pub extern "C" fn plugin_deinit() -> i32 {
    lockscreen::unregister();
    log::info(TAG, "deinit");
    0
}

#[no_mangle]
pub extern "C" fn plugin_on_enter() -> i32 {
    ui::ListBuilder::new("BG Probe")
        .on_select(ACTION_QUICK)
        .item("running - N leaves, ticks keep counting", 0, ui::UI_ICON_NONE)
        .push();
    log::info(TAG, "enter");
    0
}

#[no_mangle]
pub extern "C" fn plugin_on_exit() -> i32 {
    log::info(TAG, "exit (still resident)");
    0
}

#[no_mangle]
pub extern "C" fn plugin_on_tick(_uptime_ms: u64) -> i32 {
    let ticks = nvs::get_u32("ticks").unwrap_or(0) + 1;
    let _ = nvs::set_u32("ticks", ticks);
    0
}

#[no_mangle]
pub extern "C" fn plugin_on_action(action_id: u32, _idx: u32, _user_data: u32) -> i32 {
    if action_id == ACTION_QUICK {
        log::info(TAG, "quick action fired");
        let _ = nvs::set_u32("quick", nvs::get_u32("quick").unwrap_or(0) + 1);
    }
    0
}
