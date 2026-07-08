#include "HostDisplay.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "../EpdSpiCapture.h"
#include "cdc_core/EventBus.h"
#include "goodisplay/gdey029T94.h"

namespace emu {

namespace {

/// A FAST/FULL waveform leaves the panel unreadable for its whole duration;
/// only those get a DISPLAY_REFRESH begin/end (PARTIAL is transparent).
bool isHeavy(cdc::hal::RefreshMode mode)
{
    return mode == cdc::hal::RefreshMode::FULL || mode == cdc::hal::RefreshMode::FAST;
}

void publishRefresh(uint8_t begin)
{
    cdc::core::EventBus::instance().publish(cdc::core::EventType::DISPLAY_REFRESH, begin);
}

/// Approximate GDEY029T94 refresh durations (the badge's async render task
/// keeps ticks running, but a new frame can appear at most once per refresh).
int64_t refreshDurationMs(cdc::hal::RefreshMode mode)
{
    switch (mode) {
    case cdc::hal::RefreshMode::FULL:
        return 1800;
    case cdc::hal::RefreshMode::FAST:
        return 700;
    case cdc::hal::RefreshMode::PARTIAL:
        return 250;
    case cdc::hal::RefreshMode::PARTIAL_LIGHT:
    default:
        return 120;
    }
}

/// Stronger waveform wins when coalescing (enum orders FULL..PARTIAL_LIGHT).
bool strongerRefresh(cdc::hal::RefreshMode a, cdc::hal::RefreshMode b)
{
    return static_cast<uint8_t>(a) < static_cast<uint8_t>(b);
}

int64_t wallMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

HostDisplay& HostDisplay::instance()
{
    static HostDisplay display;
    return display;
}

Gdey029T94& HostDisplay::gdey()
{
    if (!gdey_) {
        // Lazy construction mirrors the firmware's EpaperDisplay init order:
        // init -> rotation 1 (landscape 296x128) -> mono mode -> white screen.
        io_ = new EpdSpi();
        gdey_ = new Gdey029T94(*io_);
        gdey_->init(false);
        gdey_->setRotation(1);
        gdey_->setMonoMode(true);
        gdey_->fillScreen(EPD_WHITE);
    }
    return *gdey_;
}

bool HostDisplay::init()
{
    gdey();
    state_ = cdc::core::ServiceState::INITIALIZED;
    return true;
}

void HostDisplay::captureFrame()
{
    emitFrame(false);
}

void HostDisplay::emitFrame(bool invert)
{
    // A full update streams the entire mono buffer through the EpdSpi stub;
    // the waveform itself is a no-op, so this is cheap and side-effect-free.
    gdey().update();
    if (!sink_) {
        return;
    }
    uint8_t frame[kPanelBufferSize];
    if (epdCaptureFrame(frame)) {
        if (invert) {
            for (uint8_t& b : frame) {
                b = static_cast<uint8_t>(~b);
            }
        }
        sink_(frame, sizeof(frame));
    }
}

void HostDisplay::clear()
{
    gdey().fillScreen(EPD_WHITE);
}

void HostDisplay::flush(cdc::hal::RefreshMode mode)
{
    flushInternal(mode);
}

void HostDisplay::flushSync(cdc::hal::RefreshMode mode)
{
    // On the badge flushSync blocks the caller for the waveform; here both
    // entry points share the non-blocking busy model instead, because the
    // firmware's UI/tick loop keeps running during a refresh and the
    // emulator's single advance loop must not stall.
    flushInternal(mode);
}

void HostDisplay::beginRefresh(cdc::hal::RefreshMode mode, int64_t now)
{
    // Close any previous heavy window whose end pump() has not yet emitted.
    if (refreshBusyHeavy_) {
        publishRefresh(0);
        refreshBusyHeavy_ = false;
    }
    if (isHeavy(mode)) {
        // Pause plugins for the window and show the invert-flash cue in place
        // of the (invisible) e-paper waveform; the settled frame follows at
        // the window's end in pump().
        publishRefresh(1);
        refreshBusyHeavy_ = true;
        emitFrame(true);
    } else {
        captureFrame();
    }
    busyUntilMs_ = now + refreshDurationMs(mode);
}

void HostDisplay::flushInternal(cdc::hal::RefreshMode mode)
{
    if (!realtime_) {
        // Scripted/snapshot runs: refresh modes only affect the physical
        // waveform (ghosting management); off-device every flush is an
        // immediate, synchronous frame capture with no refresh events or cue
        // (snapshot baselines must stay pixel-exact).
        captureFrame();
        return;
    }
    const int64_t now = wallMs();
    if (now < busyUntilMs_) {
        // Panel busy: coalesce, keep the stronger waveform (same policy as
        // the firmware render task's pending-mode escalation).
        if (!pending_ || strongerRefresh(mode, pendingMode_)) {
            pendingMode_ = mode;
        }
        pending_ = true;
        return;
    }
    beginRefresh(mode, now);
}

void HostDisplay::pump()
{
    if (!realtime_) {
        return;
    }
    const int64_t now = wallMs();
    if (now < busyUntilMs_) {
        return;  // window still open
    }
    // Window elapsed: end a heavy refresh (release the pause, show the settled
    // frame) before starting any coalesced refresh that queued during it.
    if (refreshBusyHeavy_) {
        publishRefresh(0);
        refreshBusyHeavy_ = false;
        captureFrame();
    }
    if (!pending_) {
        return;
    }
    pending_ = false;
    beginRefresh(pendingMode_, now);
}

void* HostDisplay::getNativeHandle()
{
    return &gdey();
}

void HostDisplay::showSplash(const char* subtitle)
{
    auto& g = gdey();
    g.fillScreen(EPD_WHITE);
    g.setTextColor(EPD_BLACK);
    g.setCursor(10, 60);
    g.print("CDC Badge Emulator");
    if (subtitle) {
        g.setCursor(10, 80);
        g.print(subtitle);
    }
    captureFrame();
}

void HostDisplay::drawPixel(int16_t x, int16_t y, uint16_t color)
{
    gdey().drawPixel(x, y, color);
}

void HostDisplay::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                           uint16_t color)
{
    gdey().drawLine(x0, y0, x1, y1, color);
}

void HostDisplay::drawRect(int16_t x, int16_t y, int16_t w, int16_t h,
                           uint16_t color)
{
    gdey().drawRect(x, y, w, h, color);
}

void HostDisplay::fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                           uint16_t color)
{
    gdey().fillRect(x, y, w, h, color);
}

void HostDisplay::setCursor(int16_t x, int16_t y)
{
    gdey().setCursor(x, y);
}

void HostDisplay::setTextColor(uint16_t color)
{
    gdey().setTextColor(color);
}

void HostDisplay::setTextSize(uint8_t size)
{
    gdey().setTextSize(size);
}

void HostDisplay::setFont(const void* font)
{
    gdey().setFont(static_cast<const GFXfont*>(font));
}

void HostDisplay::print(const char* text)
{
    gdey().print(text);
}

void HostDisplay::printf(const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gdey().print(buf);
}

}  // namespace emu

namespace cdc::hal {

IDisplay* getDisplayInstance()
{
    return &emu::HostDisplay::instance();
}

void winkBacklight(uint8_t count, uint16_t period_ms)
{
    (void)count;
    (void)period_ms;
}

}  // namespace cdc::hal
