/**
 * \file HostHttpClient.cpp
 * \brief Host implementation of the esp_http_client shim: HTTP/1.1 over BSD
 *        sockets, HTTPS via mbedTLS. Serves host_api_http.cpp unchanged, so
 *        plugin HTTP requests reach the real internet (FR-035).
 *
 * Dev-tool trade-off: TLS certificates are NOT verified (the badge trusts
 * ESP-IDF's bundle; shipping and updating a CA store in a dev emulator is out
 * of proportion). A warning is logged once per run. --offline short-circuits
 * every request with the plugin-visible network-error contract (FR-036).
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

#include "HostNet.h"
#include "cdc_log.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"

extern "C" {
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
}

namespace {

constexpr const char* TAG = "HostHttp";
constexpr int         kMaxRedirects = 5;

struct Url {
    bool        https = false;
    std::string host;
    std::string port;
    std::string path;
};

bool parseUrl(const std::string& url, Url& out)
{
    std::string rest;
    if (url.rfind("https://", 0) == 0) {
        out.https = true;
        rest = url.substr(8);
    } else if (url.rfind("http://", 0) == 0) {
        out.https = false;
        rest = url.substr(7);
    } else {
        return false;
    }
    const size_t slash = rest.find('/');
    std::string  authority = slash == std::string::npos ? rest : rest.substr(0, slash);
    out.path = slash == std::string::npos ? "/" : rest.substr(slash);
    const size_t colon = authority.find(':');
    if (colon != std::string::npos) {
        out.host = authority.substr(0, colon);
        out.port = authority.substr(colon + 1);
    } else {
        out.host = authority;
        out.port = out.https ? "443" : "80";
    }
    return !out.host.empty();
}

/// One TCP (+ optional TLS) connection, mbedTLS-net based for portability.
class Connection {
public:
    ~Connection() { close(); }

    bool open(const Url& url, int timeout_ms)
    {
        mbedtls_net_init(&net_);
        if (mbedtls_net_connect(&net_, url.host.c_str(), url.port.c_str(),
                                MBEDTLS_NET_PROTO_TCP) != 0) {
            return false;
        }
        connected_ = true;
        (void)timeout_ms;
        if (!url.https) {
            return true;
        }

        static bool warned = false;
        if (!warned) {
            warned = true;
            LOG_W(TAG, "TLS certificate verification is DISABLED (dev-only "
                       "emulator; do not treat connections as authenticated)");
        }

        mbedtls_ssl_init(&ssl_);
        mbedtls_ssl_config_init(&conf_);
        mbedtls_entropy_init(&entropy_);
        mbedtls_ctr_drbg_init(&drbg_);
        tls_ = true;
        if (mbedtls_ctr_drbg_seed(&drbg_, mbedtls_entropy_func, &entropy_,
                                  nullptr, 0) != 0 ||
            mbedtls_ssl_config_defaults(&conf_, MBEDTLS_SSL_IS_CLIENT,
                                        MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
            return false;
        }
        mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_rng(&conf_, mbedtls_ctr_drbg_random, &drbg_);
        if (mbedtls_ssl_setup(&ssl_, &conf_) != 0 ||
            mbedtls_ssl_set_hostname(&ssl_, url.host.c_str()) != 0) {
            return false;
        }
        mbedtls_ssl_set_bio(&ssl_, &net_, mbedtls_net_send, mbedtls_net_recv,
                            nullptr);
        int rc;
        while ((rc = mbedtls_ssl_handshake(&ssl_)) != 0) {
            if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
                LOG_W(TAG, "TLS handshake with %s failed (-0x%04x)",
                      url.host.c_str(), (unsigned)-rc);
                return false;
            }
        }
        return true;
    }

    bool writeAll(const uint8_t* data, size_t len)
    {
        size_t off = 0;
        while (off < len) {
            int rc = tls_ ? mbedtls_ssl_write(&ssl_, data + off, len - off)
                          : mbedtls_net_send(&net_, data + off, len - off);
            if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
            if (rc <= 0) {
                return false;
            }
            off += static_cast<size_t>(rc);
        }
        return true;
    }

    int read(uint8_t* buf, size_t len)
    {
        for (;;) {
            int rc = tls_ ? mbedtls_ssl_read(&ssl_, buf, len)
                          : mbedtls_net_recv(&net_, buf, len);
            if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
            if (rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                return 0;
            }
            return rc;
        }
    }

    void close()
    {
        if (tls_) {
            mbedtls_ssl_close_notify(&ssl_);
            mbedtls_ssl_free(&ssl_);
            mbedtls_ssl_config_free(&conf_);
            mbedtls_ctr_drbg_free(&drbg_);
            mbedtls_entropy_free(&entropy_);
            tls_ = false;
        }
        if (connected_) {
            mbedtls_net_free(&net_);
            connected_ = false;
        }
    }

private:
    mbedtls_net_context      net_;
    mbedtls_ssl_context      ssl_;
    mbedtls_ssl_config       conf_;
    mbedtls_entropy_context  entropy_;
    mbedtls_ctr_drbg_context drbg_;
    bool                     tls_ = false;
    bool                     connected_ = false;
};

}  // namespace

struct esp_http_client {
    esp_http_client_config_t           config;
    std::string                        url;
    esp_http_client_method_t           method = HTTP_METHOD_GET;
    std::map<std::string, std::string> headers;
    std::string                        body;
    int                                status = 0;
    int64_t                            content_length = -1;
    std::vector<uint8_t>               response;
    size_t                             read_pos = 0;
};

namespace {

const char* methodName(esp_http_client_method_t method)
{
    switch (method) {
    case HTTP_METHOD_POST:
        return "POST";
    case HTTP_METHOD_PUT:
        return "PUT";
    case HTTP_METHOD_PATCH:
        return "PATCH";
    case HTTP_METHOD_DELETE:
        return "DELETE";
    case HTTP_METHOD_HEAD:
        return "HEAD";
    default:
        return "GET";
    }
}

void fireOnData(esp_http_client* c, const uint8_t* data, size_t len)
{
    if (!c->config.event_handler || len == 0) {
        return;
    }
    esp_http_client_event_t evt = {};
    evt.event_id = HTTP_EVENT_ON_DATA;
    evt.client = c;
    evt.data = const_cast<uint8_t*>(data);
    evt.data_len = static_cast<int>(len);
    evt.user_data = c->config.user_data;
    (void)c->config.event_handler(&evt);
}

/// Parse "HTTP/1.1 200 OK\r\nheaders\r\n\r\n" + body; handles Content-Length
/// and chunked transfer encoding (the two encodings that occur in practice).
bool performOnce(esp_http_client* c, const std::string& url, std::string& redirect)
{
    Url parsed;
    if (!parseUrl(url, parsed)) {
        LOG_W(TAG, "unsupported URL: %s", url.c_str());
        return false;
    }
    Connection conn;
    if (!conn.open(parsed, c->config.timeout_ms)) {
        LOG_W(TAG, "connect to %s:%s failed", parsed.host.c_str(),
              parsed.port.c_str());
        return false;
    }

    std::string request = std::string(methodName(c->method)) + " " + parsed.path +
                          " HTTP/1.1\r\nHost: " + parsed.host +
                          "\r\nConnection: close\r\nAccept-Encoding: identity\r\n";
    bool haveUa = false;
    for (const auto& [key, value] : c->headers) {
        request += key + ": " + value + "\r\n";
        if (strcasecmp(key.c_str(), "User-Agent") == 0) {
            haveUa = true;
        }
    }
    if (!haveUa) {
        request += "User-Agent: cdc-badge-emulator\r\n";
    }
    if (!c->body.empty() || c->method == HTTP_METHOD_POST ||
        c->method == HTTP_METHOD_PUT) {
        char lenHeader[48];
        snprintf(lenHeader, sizeof(lenHeader), "Content-Length: %zu\r\n",
                 c->body.size());
        request += lenHeader;
    }
    request += "\r\n";
    request += c->body;

    if (!conn.writeAll(reinterpret_cast<const uint8_t*>(request.data()),
                       request.size())) {
        return false;
    }

    // Read the full response (Connection: close), then split head/body.
    std::vector<uint8_t> raw;
    uint8_t              chunk[4096];
    for (;;) {
        const int n = conn.read(chunk, sizeof(chunk));
        if (n <= 0) {
            break;
        }
        raw.insert(raw.end(), chunk, chunk + n);
        if (raw.size() > 16 * 1024 * 1024) {
            LOG_W(TAG, "response exceeds 16 MiB - truncating");
            break;
        }
    }
    const std::string headText(reinterpret_cast<char*>(raw.data()),
                               raw.size() > 8192 ? 8192 : raw.size());
    const size_t      headEnd = headText.find("\r\n\r\n");
    if (headEnd == std::string::npos) {
        return false;
    }
    if (sscanf(headText.c_str(), "HTTP/%*d.%*d %d", &c->status) != 1) {
        return false;
    }

    // Location header for redirects.
    redirect.clear();
    if (c->status >= 300 && c->status < 400) {
        size_t pos = headText.find("\r\nLocation:");
        if (pos == std::string::npos) {
            pos = headText.find("\r\nlocation:");
        }
        if (pos != std::string::npos) {
            pos = headText.find(':', pos + 2) + 1;
            const size_t end = headText.find("\r\n", pos);
            redirect = headText.substr(pos, end - pos);
            redirect.erase(0, redirect.find_first_not_of(' '));
        }
    }

    const bool chunked = headText.find("chunked") != std::string::npos;
    std::vector<uint8_t> body(raw.begin() + static_cast<long>(headEnd + 4),
                              raw.end());
    if (chunked) {
        // De-chunk: <hex len>\r\n<data>\r\n ... 0\r\n\r\n
        std::vector<uint8_t> decoded;
        size_t               pos = 0;
        while (pos < body.size()) {
            size_t lineEnd = pos;
            while (lineEnd + 1 < body.size() &&
                   !(body[lineEnd] == '\r' && body[lineEnd + 1] == '\n')) {
                ++lineEnd;
            }
            const unsigned long chunkLen = strtoul(
                reinterpret_cast<const char*>(body.data()) + pos, nullptr, 16);
            if (chunkLen == 0) {
                break;
            }
            pos = lineEnd + 2;
            if (pos + chunkLen > body.size()) {
                break;
            }
            decoded.insert(decoded.end(), body.begin() + static_cast<long>(pos),
                           body.begin() + static_cast<long>(pos + chunkLen));
            pos += chunkLen + 2;
        }
        body.swap(decoded);
    }

    c->response = std::move(body);
    // The full body is buffered (and de-chunked) by the time perform()
    // returns, so the effective content length is always known - unlike
    // ESP-IDF's streaming client, which reports -1 for chunked transfers.
    // Reporting the real size is strictly more useful to plugins and avoids
    // (size_t)-1 blowing up the SDK's usize-based reader.
    c->content_length = static_cast<int64_t>(c->response.size());
    c->read_pos = 0;
    return true;
}

}  // namespace

extern "C" {

esp_err_t esp_crt_bundle_attach(void* conf)
{
    (void)conf;
    return ESP_OK;
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config)
{
    if (!config || !config->url) {
        return nullptr;
    }
    auto* client = new esp_http_client();
    client->config = *config;
    client->url = config->url;
    client->method = config->method;
    return client;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client)
{
    delete client;
    return ESP_OK;
}

esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char* url)
{
    if (!client || !url) {
        return ESP_ERR_INVALID_ARG;
    }
    client->url = url;
    return ESP_OK;
}

esp_err_t esp_http_client_set_method(esp_http_client_handle_t client,
                                     esp_http_client_method_t method)
{
    if (!client) {
        return ESP_ERR_INVALID_ARG;
    }
    client->method = method;
    return ESP_OK;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,
                                     const char* key, const char* value)
{
    if (!client || !key || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    client->headers[key] = value;
    return ESP_OK;
}

esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client,
                                         const char* data, int len)
{
    if (!client || (!data && len)) {
        return ESP_ERR_INVALID_ARG;
    }
    client->body.assign(data ? data : "", static_cast<size_t>(len));
    return ESP_OK;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t client)
{
    if (!client) {
        return ESP_ERR_INVALID_ARG;
    }
    if (emu::HostWifi::isOffline()) {
        LOG_W(TAG, "--offline: request to %s fails with a network error",
              client->url.c_str());
        return ESP_FAIL;
    }
    std::string url = client->url;
    for (int hop = 0; hop <= kMaxRedirects; ++hop) {
        std::string redirect;
        if (!performOnce(client, url, redirect)) {
            return ESP_FAIL;
        }
        if (redirect.empty() || client->config.disable_auto_redirect) {
            fireOnData(client, client->response.data(), client->response.size());
            return ESP_OK;
        }
        url = redirect;
    }
    LOG_W(TAG, "too many redirects for %s", client->url.c_str());
    return ESP_FAIL;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client)
{
    return client ? client->status : 0;
}

int64_t esp_http_client_get_content_length(esp_http_client_handle_t client)
{
    return client ? client->content_length : -1;
}

int esp_http_client_read(esp_http_client_handle_t client, char* buffer, int len)
{
    if (!client || !buffer || len <= 0) {
        return -1;
    }
    const size_t remaining = client->response.size() - client->read_pos;
    const size_t n = remaining < static_cast<size_t>(len) ? remaining
                                                          : static_cast<size_t>(len);
    memcpy(buffer, client->response.data() + client->read_pos, n);
    client->read_pos += n;
    return static_cast<int>(n);
}

}  // extern "C"
