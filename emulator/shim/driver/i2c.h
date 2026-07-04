/**
 * \file driver/i2c.h (host shim) - no-op; no I2C devices exist off-device.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int i2c_port_t;

#ifdef __cplusplus
}
#endif
