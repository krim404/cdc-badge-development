/**
 * \file HostDisplay.h
 * \brief Host implementation of cdc::hal::IDisplay wrapping the host-built
 *        Gdey029T94 (reused CalEPD/Adafruit_GFX drawing core).
 *
 * Views draw directly on the Gdey029T94 returned by getNativeHandle(),
 * exactly as on device. flush()/flushSync() run a (no-op waveform) panel
 * update through the EpdSpi stub, capture the transmitted 4736-byte 1-bpp
 * frame (FR-014/FR-033) and hand it to the registered frame sink.
 */
#pragma once

#include <cstdint>
#include <functional>

#include "cdc_hal/IDisplay.h"

class EpdSpi;
class Gdey029T94;

namespace emu {

class HostDisplay : public cdc::hal::IDisplay {
public:
    /// Called with the captured panel-layout frame after every flush.
    using FrameSink = std::function<void(const uint8_t* frame, size_t len)>;

    static HostDisplay& instance();

    void setFrameSink(FrameSink sink) { sink_ = std::move(sink); }

    /// Capture the current buffer and feed the sink (used by EmulatorCore
    /// after lifecycle callbacks, FR-033).
    void captureFrame();

    /// Emulate the panel's refresh latency (interactive runs only): a flush
    /// starts a busy window sized by the refresh mode; flushes arriving while
    /// busy coalesce into one pending frame delivered when the window ends -
    /// mirroring the firmware's async render task, where ticks keep running
    /// but frames appear at most once per refresh. Scripted/snapshot runs
    /// leave this off and keep the immediate, deterministic behaviour.
    void setRealtimeRefresh(bool on) { realtime_ = on; }

    /// Deliver a coalesced pending frame once the busy window elapsed.
    /// Cheap no-op when realtime refresh is off or nothing is pending.
    void pump();

    // IService
    bool init() override;
    bool start() override { return true; }
    void stop() override {}
    cdc::core::ServiceState getState() const override { return state_; }
    const char* getName() const override { return "HostDisplay"; }

    // IDisplay
    void clear() override;
    void flush(cdc::hal::RefreshMode mode) override;
    void flushSync(cdc::hal::RefreshMode mode) override;
    bool isBusy() const override { return false; }
    uint16_t getWidth() const override { return 296; }
    uint16_t getHeight() const override { return 128; }
    void setBacklight(uint16_t level) override { backlight_ = level; }
    void saveBacklight() override {}
    uint16_t getBacklight() const override { return backlight_; }
    bool isBacklightOn() const override { return backlight_ > 0; }
    void backlightOn() override { backlight_ = 512; }
    void backlightOff() override { backlight_ = 0; }
    void* getNativeHandle() override;
    void showSplash(const char* subtitle) override;
    void drawPixel(int16_t x, int16_t y, uint16_t color) override;
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) override;
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override;
    void setCursor(int16_t x, int16_t y) override;
    void setTextColor(uint16_t color) override;
    void setTextSize(uint8_t size) override;
    void setFont(const void* font) override;
    void print(const char* text) override;
    void printf(const char* fmt, ...) override;

private:
    HostDisplay() = default;

    Gdey029T94& gdey();
    void flushInternal(cdc::hal::RefreshMode mode);
    /// Capture and feed the sink; when `invert` XOR the bytes first (the
    /// realtime "flash" cue that stands in for the e-paper's visible refresh).
    void emitFrame(bool invert);
    /// Start a realtime busy window for `mode`: publish DISPLAY_REFRESH and
    /// show the invert-flash cue for FAST/FULL, or capture normally otherwise.
    void beginRefresh(cdc::hal::RefreshMode mode, int64_t now);

    EpdSpi*                 io_ = nullptr;
    Gdey029T94*             gdey_ = nullptr;
    FrameSink               sink_;
    uint16_t                backlight_ = 512;
    cdc::core::ServiceState state_ = cdc::core::ServiceState::UNINITIALIZED;

    bool                  realtime_ = false;
    int64_t               busyUntilMs_ = 0;
    bool                  pending_ = false;
    cdc::hal::RefreshMode pendingMode_ = cdc::hal::RefreshMode::PARTIAL;
    /// True while a FAST/FULL window is open and its DISPLAY_REFRESH begin has
    /// been published but the matching end has not (paired in pump()).
    bool                  refreshBusyHeavy_ = false;
};

}  // namespace emu
