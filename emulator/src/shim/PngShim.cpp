/**
 * \file PngShim.cpp
 * \brief png_image simplified-API subset over LodePNG (see shim/png.h).
 *
 * Supports exactly what ImageDecoder.cpp asks for: begin-read from memory,
 * finish-read into an 8-bit GRAY buffer with alpha composited over a solid
 * background. Gray conversion uses libpng's default coefficients
 * (0.212671 R + 0.715160 G + 0.072169 B) for closest parity.
 */
#include <cstdlib>
#include <cstring>

#include "lodepng.h"

extern "C" {
#include "png.h"
}

namespace {

struct Decoded {
    uint8_t*  rgba = nullptr;
    unsigned width = 0;
    unsigned height = 0;
};

}  // namespace

extern "C" {

int png_image_begin_read_from_memory(png_image* image, const void* memory,
                                     size_t size)
{
    if (!image || !memory || image->version != PNG_IMAGE_VERSION) {
        return 0;
    }
    unsigned width = 0;
    unsigned height = 0;
    uint8_t* rgba = nullptr;
    // Decode fully here; the simplified API only exposes begin/finish anyway.
    const unsigned error = lodepng_decode32(&rgba, &width, &height,
                                            static_cast<const uint8_t*>(memory),
                                            size);
    if (error != 0 || !rgba) {
        return 0;
    }
    auto* decoded = new Decoded{rgba, width, height};
    image->width = static_cast<uint32_t>(width);
    image->height = static_cast<uint32_t>(height);
    image->opaque = decoded;
    return 1;
}

int png_image_finish_read(png_image* image, const png_color* background,
                          void* buffer, int32_t row_stride, void* colormap)
{
    (void)colormap;
    if (!image || !image->opaque || !buffer || image->format != PNG_FORMAT_GRAY) {
        return 0;
    }
    auto*         decoded = static_cast<Decoded*>(image->opaque);
    const uint8_t bgR = background ? background->red : 255;
    const uint8_t bgG = background ? background->green : 255;
    const uint8_t bgB = background ? background->blue : 255;
    const size_t  stride = row_stride > 0 ? static_cast<size_t>(row_stride)
                                          : static_cast<size_t>(decoded->width);
    auto* out = static_cast<uint8_t*>(buffer);
    for (unsigned y = 0; y < decoded->height; ++y) {
        for (unsigned x = 0; x < decoded->width; ++x) {
            const uint8_t* px = decoded->rgba + (static_cast<size_t>(y) * decoded->width + x) * 4;
            const unsigned alpha = px[3];
            const unsigned r = (px[0] * alpha + bgR * (255 - alpha)) / 255;
            const unsigned g = (px[1] * alpha + bgG * (255 - alpha)) / 255;
            const unsigned b = (px[2] * alpha + bgB * (255 - alpha)) / 255;
            // libpng default rgb->gray coefficients, 15-bit fixed point.
            out[y * stride + x] =
                static_cast<uint8_t>((r * 6968u + g * 23434u + b * 2366u) >> 15);
        }
    }
    return 1;
}

void png_image_free(png_image* image)
{
    if (image && image->opaque) {
        auto* decoded = static_cast<Decoded*>(image->opaque);
        std::free(decoded->rgba);
        delete decoded;
        image->opaque = nullptr;
    }
}

}  // extern "C"
