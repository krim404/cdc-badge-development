/**
 * \file driver/gpio.h (host shim) - no-op GPIO. The emulator has no pins; the
 * gpio host-API family is stubbed at the host_api level, this shim only lets
 * reused sources (CalEPD transport paths) compile.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int gpio_num_t;

typedef enum {
    GPIO_MODE_DISABLE = 0,
    GPIO_MODE_INPUT,
    GPIO_MODE_OUTPUT,
    GPIO_MODE_OUTPUT_OD,
    GPIO_MODE_INPUT_OUTPUT_OD,
    GPIO_MODE_INPUT_OUTPUT,
} gpio_mode_t;

typedef enum {
    GPIO_PULLUP_ONLY = 0,
    GPIO_PULLDOWN_ONLY,
    GPIO_PULLUP_PULLDOWN,
    GPIO_FLOATING,
} gpio_pull_mode_t;

typedef enum {
    GPIO_PULLUP_DISABLE = 0,
    GPIO_PULLUP_ENABLE,
} gpio_pullup_t;

typedef enum {
    GPIO_PULLDOWN_DISABLE = 0,
    GPIO_PULLDOWN_ENABLE,
} gpio_pulldown_t;

typedef enum {
    GPIO_INTR_DISABLE = 0,
} gpio_int_type_t;

typedef struct {
    uint64_t        pin_bit_mask;
    gpio_mode_t     mode;
    gpio_pullup_t   pull_up_en;
    gpio_pulldown_t pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;

static inline esp_err_t gpio_config(const gpio_config_t* cfg)
{
    (void)cfg;
    return ESP_OK;
}
static inline esp_err_t gpio_set_direction(gpio_num_t pin, gpio_mode_t mode)
{
    (void)pin;
    (void)mode;
    return ESP_OK;
}
static inline esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level)
{
    (void)pin;
    (void)level;
    return ESP_OK;
}
static inline int gpio_get_level(gpio_num_t pin)
{
    (void)pin;
    return 0;
}
static inline esp_err_t gpio_set_pull_mode(gpio_num_t pin, gpio_pull_mode_t pull)
{
    (void)pin;
    (void)pull;
    return ESP_OK;
}
static inline esp_err_t gpio_reset_pin(gpio_num_t pin)
{
    (void)pin;
    return ESP_OK;
}

#ifdef __cplusplus
}
#endif
