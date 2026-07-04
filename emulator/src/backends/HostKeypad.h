/**
 * \file HostKeypad.h
 * \brief Host implementation of cdc::hal::IKeypad: a key queue fed by --keys
 *        scripts and/or the interactive window frontend.
 *
 * Injected keys emit the same press/release callback sequence the TCA9535
 * driver produces, so views and plugin key handling behave as on device.
 * Long-press is delivered explicitly (scripted "5!" syntax or a window key
 *held past the threshold) rather than timed, keeping runs deterministic.
 */
#pragma once

#include <deque>
#include <mutex>

#include "cdc_hal/IKeypad.h"

namespace emu {

class HostKeypad : public cdc::hal::IKeypad {
public:
    static HostKeypad& instance();

    /// Inject a key press (from --keys or the window). Fires the registered
    /// callback (press + release) and buffers the key for polling consumers.
    void injectKey(char key, bool longPress = false);

    // IService
    bool init() override;
    bool start() override { return true; }
    void stop() override {}
    cdc::core::ServiceState getState() const override { return state_; }
    const char* getName() const override { return "HostKeypad"; }

    // IKeypad
    void poll() override {}
    bool isKeyPressed(cdc::hal::Key key) const override;
    cdc::hal::Key getNextKey() override;
    bool hasKey() const override;
    bool anyKeyDown() const override { return false; }
    void setCallback(cdc::hal::KeyCallback callback) override { callback_ = callback; }
    void setLongPressEnabled(bool enabled, uint32_t thresholdMs) override;
    void setLongPressCallback(LongPressCallback callback) override
    {
        longPressCallback_ = callback;
    }
    void setKeyRepeat(uint16_t initial_ms, uint16_t period_ms) override
    {
        repeat_initial_ms_ = initial_ms;
        repeat_period_ms_ = period_ms;
    }

    /// Repeat configuration the active view requested (IKeypad::setKeyRepeat).
    /// The window frontend polls this to re-inject held keys; a non-zero
    /// period suppresses long-press, exactly like the TCA9535 driver.
    uint16_t keyRepeatInitialMs() const { return repeat_initial_ms_; }
    uint16_t keyRepeatPeriodMs() const { return repeat_period_ms_; }
    void setPanicChordCallback(PanicChordCallback callback) override
    {
        (void)callback;  // the emulator has no lock screen to panic into
    }
    void prepareForSleep() override {}
    void recoverFromSleep() override {}
    void clearBuffer() override;

private:
    HostKeypad() = default;

    mutable std::mutex      mutex_;
    std::deque<char>        buffer_;
    cdc::hal::KeyCallback   callback_ = nullptr;
    LongPressCallback       longPressCallback_ = nullptr;
    bool                    longPressEnabled_ = false;
    uint16_t                repeat_initial_ms_ = 0;
    uint16_t                repeat_period_ms_ = 0;
    cdc::core::ServiceState state_ = cdc::core::ServiceState::UNINITIALIZED;
};

}  // namespace emu
