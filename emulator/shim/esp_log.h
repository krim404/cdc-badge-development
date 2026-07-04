/**
 * \file esp_log.h (host shim)
 * \brief ESP_LOGx -> stderr. Timestamps come from the virtual clock so log
 *        output lines up with plugin-visible time.
 */
#pragma once

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_LOG_NONE = 0,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE,
} esp_log_level_t;

uint32_t esp_log_timestamp(void);
void     esp_log_level_set(const char* tag, esp_log_level_t level);
esp_log_level_t esp_log_level_get(const char* tag);

#define ESP_LOG_LEVEL_LOCAL(level, tag, fmt, ...)                              \
    fprintf(stderr, "%c (%u) %s: " fmt "\n", "NEWIDV"[level],                  \
            (unsigned)esp_log_timestamp(), tag, ##__VA_ARGS__)

#define ESP_LOGE(tag, fmt, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_ERROR, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_WARN, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) ESP_LOG_LEVEL_LOCAL(ESP_LOG_INFO, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGV(tag, fmt, ...) ((void)0)

#ifdef __cplusplus
}
#endif
