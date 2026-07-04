/**
 * \file esp_random.h (host shim)
 * \brief Host RNG. Seedable for deterministic regression runs; defaults to a
 *        fixed seed so identical runs produce identical frames (FR-034).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t esp_random(void);
void     esp_fill_random(void* buf, size_t len);

#ifdef __cplusplus
}
#endif
