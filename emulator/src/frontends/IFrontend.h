/**
 * \file IFrontend.h
 * \brief Interchangeable renderers of the captured e-paper frame.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace emu {

class IFrontend {
public:
    virtual ~IFrontend() = default;

    /// A committed frame in panel-native 1-bpp layout (buffer[y*(W/8)+x],
    /// 128x296 portrait; bit set = white). Called on every display flush.
    virtual void onFrame(const uint8_t* frame, size_t len) = 0;
};

/// Convert the panel-native 1-bpp frame to 8-bit grayscale landscape
/// (296x128, row-major, 255 = white), matching the badge's visible
/// orientation (rotation 1). Both frontends share this mapping so the
/// window and the PNG can never diverge.
void frameToLandscapeGray(const uint8_t* frame, uint8_t* out296x128);

}  // namespace emu
