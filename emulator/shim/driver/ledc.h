/**
 * \file driver/ledc.h (host shim) - no-op PWM (backlight/beeper have no host
 * equivalent; the gpio/pwm host-API family is stubbed).
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { LEDC_LOW_SPEED_MODE = 0 } ledc_mode_t;
typedef enum { LEDC_TIMER_0 = 0, LEDC_TIMER_1, LEDC_TIMER_2, LEDC_TIMER_3 } ledc_timer_t;
typedef enum { LEDC_CHANNEL_0 = 0, LEDC_CHANNEL_1, LEDC_CHANNEL_2 } ledc_channel_t;
typedef enum { LEDC_TIMER_8_BIT = 8, LEDC_TIMER_10_BIT = 10, LEDC_TIMER_13_BIT = 13 } ledc_timer_bit_t;

typedef struct {
    ledc_mode_t      speed_mode;
    ledc_timer_bit_t duty_resolution;
    ledc_timer_t     timer_num;
    uint32_t         freq_hz;
    int              clk_cfg;
} ledc_timer_config_t;

typedef struct {
    int            gpio_num;
    ledc_mode_t    speed_mode;
    ledc_channel_t channel;
    int            intr_type;
    ledc_timer_t   timer_sel;
    uint32_t       duty;
    int            hpoint;
} ledc_channel_config_t;

static inline esp_err_t ledc_timer_config(const ledc_timer_config_t* cfg)
{
    (void)cfg;
    return ESP_OK;
}
static inline esp_err_t ledc_channel_config(const ledc_channel_config_t* cfg)
{
    (void)cfg;
    return ESP_OK;
}
static inline esp_err_t ledc_set_duty(ledc_mode_t mode, ledc_channel_t ch, uint32_t duty)
{
    (void)mode;
    (void)ch;
    (void)duty;
    return ESP_OK;
}
static inline esp_err_t ledc_update_duty(ledc_mode_t mode, ledc_channel_t ch)
{
    (void)mode;
    (void)ch;
    return ESP_OK;
}
static inline esp_err_t ledc_stop(ledc_mode_t mode, ledc_channel_t ch, uint32_t idle_level)
{
    (void)mode;
    (void)ch;
    (void)idle_level;
    return ESP_OK;
}

#ifdef __cplusplus
}
#endif
