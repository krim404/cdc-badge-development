/**
 * \file png.h (host shim)
 * \brief The subset of libpng's simplified API (png_image) that
 *        cdc_image/ImageDecoder.cpp uses, implemented over stb_image in
 *        emulator/src/shim/PngShim.cpp. On device this API comes from
 *        espressif/libpng (IDF component manager); vendoring zlib+libpng for
 *        one grayscale decode path is out of proportion for a dev tool. PNG
 *        decoding is lossless, so decoded gray values match libpng except for
 *        rounding in the RGB->gray conversion of colour images.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PNG_IMAGE_VERSION 1

#define PNG_FORMAT_GRAY 0
#define PNG_FORMAT_RGB 2
#define PNG_FORMAT_RGBA 3

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} png_color;

typedef struct png_image {
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t flags;
    uint32_t colormap_entries;
    uint32_t warning_or_error;
    char     message[64];
    void*    opaque;  /* host shim: decoded pixel stash between begin/finish */
} png_image;

int  png_image_begin_read_from_memory(png_image* image, const void* memory,
                                      size_t size);
int  png_image_finish_read(png_image* image, const png_color* background,
                           void* buffer, int32_t row_stride, void* colormap);
void png_image_free(png_image* image);

#ifdef __cplusplus
}
#endif
