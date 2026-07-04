/**
 * \file sdkconfig.h (host shim)
 * \brief Build-config defines the reused sources test. Pin numbers mirror the
 *        badge's hw_config values but are never driven on the host.
 */
#pragma once

#define CONFIG_IDF_TARGET_ESP32S3 1

/* E-paper wiring (GDEY029T94-FL03 on the badge; unused electrically here). */
#define CONFIG_EINK_SPI_MOSI 40
#define CONFIG_EINK_SPI_CLK  39
#define CONFIG_EINK_SPI_CS   41
#define CONFIG_EINK_DC       45
#define CONFIG_EINK_RST      46
#define CONFIG_EINK_BUSY     42
