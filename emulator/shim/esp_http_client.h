/**
 * \file esp_http_client.h (host shim)
 * \brief API-compatible subset of ESP-IDF's esp_http_client, backed by a host
 *        HTTP/HTTPS client over BSD sockets + mbedTLS
 *        (emulator/src/backends/HostHttpClient.cpp). Only the surface
 *        host_api_http.cpp uses is provided.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_http_client* esp_http_client_handle_t;

typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_PATCH,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_HEAD,
} esp_http_client_method_t;

typedef enum {
    HTTP_EVENT_ERROR = 0,
    HTTP_EVENT_ON_CONNECTED,
    HTTP_EVENT_HEADERS_SENT,
    HTTP_EVENT_ON_HEADER,
    HTTP_EVENT_ON_DATA,
    HTTP_EVENT_ON_FINISH,
    HTTP_EVENT_DISCONNECTED,
} esp_http_client_event_id_t;

typedef struct esp_http_client_event {
    esp_http_client_event_id_t event_id;
    esp_http_client_handle_t   client;
    void*                      data;
    int                        data_len;
    void*                      user_data;
    char*                      header_key;
    char*                      header_value;
} esp_http_client_event_t;

typedef esp_err_t (*http_event_handle_cb)(esp_http_client_event_t* evt);

typedef struct {
    const char*              url;
    esp_http_client_method_t method;
    int                      timeout_ms;
    http_event_handle_cb     event_handler;
    void*                    user_data;
    bool                     disable_auto_redirect;
    esp_err_t (*crt_bundle_attach)(void* conf);
    bool keep_alive_enable;
    int  buffer_size;
    int  buffer_size_tx;
} esp_http_client_config_t;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char* url);
esp_err_t esp_http_client_set_method(esp_http_client_handle_t client,
                                     esp_http_client_method_t method);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,
                                     const char* key, const char* value);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client,
                                         const char* data, int len);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
int       esp_http_client_get_status_code(esp_http_client_handle_t client);
int64_t   esp_http_client_get_content_length(esp_http_client_handle_t client);
int       esp_http_client_read(esp_http_client_handle_t client, char* buffer, int len);

#ifdef __cplusplus
}
#endif
