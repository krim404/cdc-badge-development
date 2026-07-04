#include "HostDisplay.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "../EpdSpiCapture.h"
#include "goodisplay/gdey029T94.h"

namespace emu {

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
    // A full update streams the entire mono buffer through the EpdSpi stub;
    // the waveform itself is a no-op, so this is cheap and side-effect-free.
    gdey().update();
    if (!sink_) {
        return;
    }
    uint8_t frame[kPanelBufferSize];
    if (epdCaptureFrame(frame)) {
        sink_(frame, sizeof(frame));
    }
}

void HostDisplay::clear()
{
    gdey().fillScreen(EPD_WHITE);
}

void HostDisplay::flush(cdc::hal::RefreshMode mode)
{
    // Refresh modes only affect the physical waveform (ghosting management);
    // off-device every flush is an immediate, synchronous frame capture.
    (void)mode;
    captureFrame();
}

void HostDisplay::flushSync(cdc::hal::RefreshMode mode)
{
    (void)mode;
    captureFrame();
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
