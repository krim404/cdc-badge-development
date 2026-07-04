/**
 * \file EpdSpiCapture.h
 * \brief Frame capture hook of the no-op EpdSpi transport (EpdSpiStub.cpp).
 *
 * CalEPD's Gdey029T94::update() streams its 1-bpp buffer to the panel through
 * EpdSpi after the 0x24 (write RAM1) command, row-by-row from the highest
 * gate line down. The stub records exactly those bytes, so the captured frame
 * is byte-identical to what the physical panel receives (FR-014). The
 * vendored epdspi.h cannot grow members, hence the file-scope capture state
 * with this free-function API (one EpdSpi instance exists per emulator).
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace emu {

constexpr uint16_t kPanelWidth = 128;   // native portrait geometry
constexpr uint16_t kPanelHeight = 296;
constexpr size_t   kPanelBufferSize = (kPanelWidth * kPanelHeight) / 8;  // 4736

/// Bytes captured after the most recent 0x24 command, in panel transmission
/// order (gate line HEIGHT-1 first). Returns the number of valid bytes.
size_t epdCaptureSize();

/// Copy the capture into `out` (kPanelBufferSize bytes) converted back to the
/// firmware's buffer layout: out[y * (W/8) + x], y ascending.
/// Returns false when the last update transferred fewer bytes than a full
/// frame (nothing is copied then).
bool epdCaptureFrame(uint8_t* out);

}  // namespace emu
