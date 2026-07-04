/**
 * \file driver/spi_master.h (host shim) - types only; the EpdSpi transport is
 * replaced by EpdSpiStub.cpp, so no SPI function is ever executed.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* spi_device_handle_t;
typedef void* spi_host_device_t_ptr;

typedef enum {
    SPI1_HOST = 0,
    SPI2_HOST = 1,
    SPI3_HOST = 2,
} spi_host_device_t;

typedef struct {
    int mosi_io_num;
    int miso_io_num;
    int sclk_io_num;
    int quadwp_io_num;
    int quadhd_io_num;
    int max_transfer_sz;
} spi_bus_config_t;

typedef struct {
    uint8_t  command_bits;
    uint8_t  address_bits;
    uint8_t  dummy_bits;
    uint8_t  mode;
    uint16_t duty_cycle_pos;
    uint16_t cs_ena_pretrans;
    uint8_t  cs_ena_posttrans;
    int      clock_speed_hz;
    int      input_delay_ns;
    int      spics_io_num;
    uint32_t flags;
    int      queue_size;
    void (*pre_cb)(void*);
    void (*post_cb)(void*);
} spi_device_interface_config_t;

typedef struct spi_transaction_t {
    uint32_t    flags;
    uint16_t    cmd;
    uint64_t    addr;
    size_t      length;
    size_t      rxlength;
    void*       user;
    const void* tx_buffer;
    void*       rx_buffer;
} spi_transaction_t;

static inline esp_err_t spi_bus_initialize(spi_host_device_t host,
                                           const spi_bus_config_t* cfg, int dma)
{
    (void)host;
    (void)cfg;
    (void)dma;
    return ESP_OK;
}
static inline esp_err_t spi_bus_add_device(spi_host_device_t host,
                                           const spi_device_interface_config_t* cfg,
                                           spi_device_handle_t* out)
{
    (void)host;
    (void)cfg;
    if (out) {
        *out = 0;
    }
    return ESP_OK;
}
static inline esp_err_t spi_device_transmit(spi_device_handle_t dev,
                                            spi_transaction_t* txn)
{
    (void)dev;
    (void)txn;
    return ESP_OK;
}
static inline esp_err_t spi_device_polling_transmit(spi_device_handle_t dev,
                                                    spi_transaction_t* txn)
{
    (void)dev;
    (void)txn;
    return ESP_OK;
}

#ifdef __cplusplus
}
#endif
