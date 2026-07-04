/**
 * \file esp_crt_bundle.h (host shim)
 * \brief The host HTTP client uses the system trust store (or none, dev-only)
 *        instead of ESP-IDF's embedded certificate bundle; the attach hook is
 *        accepted and ignored.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_crt_bundle_attach(void* conf);

#ifdef __cplusplus
}
#endif
