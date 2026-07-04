#include "JsonStore.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "cJSON.h"
#include "mbedtls/base64.h"

namespace fs = std::filesystem;

namespace emu {

cJSON* jsonReadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return nullptr;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string text = buf.str();
    cJSON* root = cJSON_ParseWithLength(text.c_str(), text.size());
    if (!root) {
        fprintf(stderr, "W (JsonStore) %s: invalid JSON - ignoring file\n",
                path.c_str());
    }
    return root;
}

bool jsonWriteFile(const std::string& path, const cJSON* root)
{
    char* text = cJSON_Print(root);
    if (!text) {
        return false;
    }
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const bool ok = static_cast<bool>(out << text << "\n");
    cJSON_free(text);
    return ok;
}

std::string base64Encode(const uint8_t* data, size_t len)
{
    if (len == 0) {
        return {};
    }
    size_t      needed = 0;
    (void)mbedtls_base64_encode(nullptr, 0, &needed, data, len);
    std::string out(needed, '\0');
    size_t      written = 0;
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(out.data()),
                              out.size(), &written, data, len) != 0) {
        return {};
    }
    out.resize(written);
    return out;
}

std::vector<uint8_t> base64Decode(const std::string& text)
{
    if (text.empty()) {
        return {};
    }
    size_t needed = 0;
    (void)mbedtls_base64_decode(nullptr, 0, &needed,
                                reinterpret_cast<const unsigned char*>(text.data()),
                                text.size());
    std::vector<uint8_t> out(needed);
    size_t               written = 0;
    if (mbedtls_base64_decode(out.data(), out.size(), &written,
                              reinterpret_cast<const unsigned char*>(text.data()),
                              text.size()) != 0) {
        return {};
    }
    out.resize(written);
    return out;
}

}  // namespace emu
