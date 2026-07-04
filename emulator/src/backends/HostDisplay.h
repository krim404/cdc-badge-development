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

    EpdSpi*                 io_ = nullptr;
    Gdey029T94*             gdey_ = nullptr;
    FrameSink               sink_;
    uint16_t                backlight_ = 512;
    cdc::core::ServiceState state_ = cdc::core::ServiceState::UNINITIALIZED;
};

}  // namespace emu
