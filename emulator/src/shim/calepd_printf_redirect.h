/**
 * \file calepd_printf_redirect.h
 * \brief Reroutes the vendored firmware's printf diagnostics.
 *
 * Force-included (-include) into selected vendored translation units, see
 * CMakeLists.txt. CalEPD prints banners and per-refresh STATS straight to
 * stdout (off-device the timing numbers are always 0, since the virtual clock
 * does not advance inside a synchronous update()); Adafruit-GFX prints a
 * "write(NN) Custom font" line per classic-font glyph. stdout must stay clean
 * for machine-readable frontend output (FRAME lines, PNG data). The
 * function-like macro only rewrites call sites, so declarations and members
 * keep their names.
 */
#pragma once

// Pull in the real declarations first; their include guards then keep the
// macro below from mangling any later stdio declaration.
#ifdef __cplusplus
#include <cstdio>
extern "C"
#else
#include <stdio.h>
#endif
int emu_calepd_printf(const char* fmt, ...);

#define printf(...) emu_calepd_printf(__VA_ARGS__)
