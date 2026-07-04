/**
 * \file EpdSpiStub.cpp
 * \brief No-op EpdSpi transport with frame capture (replaces CalEPD's
 *        epdspi.cpp on the host).
 *
 * The reused Gdey029T94 drawing core renders into its in-RAM 1-bpp buffer and
 * transfers it to the panel through EpdSpi. Off-device there is no panel:
 * commands are ignored, except that the byte stream following the 0x24
 * "write RAM1" command is recorded so HostDisplay can capture the exact frame
 * the panel would have shown (see EpdSpiCapture.h). The BUSY pin shim always
 * reads "idle", so CalEPD's _waitBusy() returns immediately.
 */
#include <cstring>
#include <vector>

#include "EpdSpiCapture.h"
#include "epdspi.h"

namespace {

constexpr uint8_t kCmdWriteRam1 = 0x24;

bool                 g_capturing = false;
std::vector<uint8_t> g_capture;

void captureAppend(const uint8_t* data, size_t len)
{
    if (g_capturing && g_capture.size() < emu::kPanelBufferSize) {
        const size_t room = emu::kPanelBufferSize - g_capture.size();
        g_capture.insert(g_capture.end(), data, data + (len < room ? len : room));
    }
}

}  // namespace

namespace emu {

size_t epdCaptureSize() { return g_capture.size(); }

bool epdCaptureFrame(uint8_t* out)
{
    if (g_capture.size() < kPanelBufferSize) {
        return false;
    }
    // Transmission order is gate line (kPanelHeight-1) down to 0, each line
    // kPanelWidth/8 bytes ascending; fold it back into buffer[y*(W/8)+x].
    const size_t lineBytes = kPanelWidth / 8;
    for (size_t row = 0; row < kPanelHeight; ++row) {
        const size_t y = kPanelHeight - 1 - row;
        std::memcpy(out + y * lineBytes, g_capture.data() + row * lineBytes,
                    lineBytes);
    }
    return true;
}

}  // namespace emu

void EpdSpi::init(uint8_t frequency, bool debug)
{
    (void)frequency;
    debug_enabled = debug;
    spi = nullptr;
}

void EpdSpi::cmd(const uint8_t cmd)
{
    if (cmd == kCmdWriteRam1) {
        g_capturing = true;
        g_capture.clear();
    } else {
        g_capturing = false;
    }
}

void EpdSpi::data(uint8_t data)
{
    captureAppend(&data, 1);
}

void EpdSpi::dataBuffer(uint8_t data)
{
    captureAppend(&data, 1);
}

void EpdSpi::data(const uint8_t* data, int len)
{
    if (data && len > 0) {
        captureAppend(data, static_cast<size_t>(len));
    }
}

void EpdSpi::dataVector(std::vector<uint8_t> _buffer)
{
    captureAppend(_buffer.data(), _buffer.size());
}

void EpdSpi::reset(uint8_t millis)
{
    (void)millis;
}
