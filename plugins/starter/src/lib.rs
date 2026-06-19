//! Starter plugin for the CDC Badge.
//!
//! This file is written for newcomers, so it is commented generously to explain
//! WHAT each part does (see code-quality.md - learning code is commented more
//! heavily than production code on purpose).
//!
//! How it is organised:
//!   * "Pure logic" at the top: ordinary Rust with no hardware calls. Because it
//!     touches no badge APIs, we can unit-test it on a normal computer.
//!   * "Plugin glue" in the `plugin` module: the lifecycle hooks the badge calls.
//!     It is compiled ONLY for the badge (wasm) and calls the pure logic.
//!   * "Tests" at the bottom: run with `badge test starter` (i.e. `cargo test`).

// On the badge (wasm) we run without the standard library (`no_std`). On a normal
// computer we keep `std`, so host builds and `cargo test` work without a custom
// panic handler. Gating on the target (not on `test`) is important: `cargo test`
// also compiles the cdylib target without the `test` cfg.
#![cfg_attr(target_arch = "wasm32", no_std)]

/// Decide which greeting to show.
///
/// This is a plain function with no badge dependencies, which is exactly why it
/// can be tested on your computer. Keep your testable logic in functions like
/// this one.
///
/// \param name A name to greet; may be empty.
/// \return The greeting text to display.
pub fn greeting(name: &str) -> &str {
    if name.is_empty() {
        "Hello, Badge!"
    } else {
        name
    }
}

// --- Plugin glue: compiled only for the badge (wasm) ------------------------
#[cfg(target_arch = "wasm32")]
mod plugin {
    extern crate alloc;

    use cdc_badge_plugin::{plugin_main, ui, log};

    // Generates the API-level exports the firmware checks when it loads the plugin.
    plugin_main!();

    // A short tag that prefixes this plugin's log lines.
    const TAG: &str = "starter";

    /// Runs once when the plugin is loaded onto the badge.
    #[no_mangle]
    pub extern "C" fn plugin_init() -> i32 {
        log::info(TAG, "init");
        0
    }

    /// Runs once when the plugin is unloaded from the badge.
    #[no_mangle]
    pub extern "C" fn plugin_deinit() -> i32 {
        0
    }

    /// Runs every time the user opens the plugin. We show a short toast using
    /// the greeting from our pure, tested logic.
    #[no_mangle]
    pub extern "C" fn plugin_on_enter() -> i32 {
        ui::push_toast(crate::greeting(""), ui::UI_ICON_SUCCESS, 1500);
        0
    }

    /// Runs when the user leaves the plugin view.
    #[no_mangle]
    pub extern "C" fn plugin_on_exit() -> i32 {
        0
    }
}

// --- Host-side unit tests: this is the Test-Driven Development loop ----------
// Write a test first, watch it fail, then make it pass. Run them with:
//   badge test starter
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_name_falls_back_to_default() {
        assert_eq!(greeting(""), "Hello, Badge!");
    }

    #[test]
    fn a_name_is_used_as_is() {
        assert_eq!(greeting("Ada"), "Ada");
    }
}
