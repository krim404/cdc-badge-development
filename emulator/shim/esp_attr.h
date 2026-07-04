/**
 * \file esp_attr.h (host shim) - placement attributes are meaningless on a
 * desktop, so they all expand to nothing.
 */
#pragma once

#define IRAM_ATTR
#define DRAM_ATTR
#define RTC_DATA_ATTR
#define RTC_IRAM_ATTR
#define EXT_RAM_BSS_ATTR
#define EXT_RAM_NOINIT_ATTR
#define WORD_ALIGNED_ATTR
#define FLAG_ATTR(TYPE)
