/**
 * \file HostNvs.cpp
 * \brief File-backed implementation of the ESP-IDF nvs_* C API (shim/nvs.h).
 *
 * One human-readable JSON file per namespace under `<data>/nvs/<ns>.json`
 * ({"key": {"type": "u32", "value": 42}, ...}; blobs are base64, u64 is a
 * decimal string to dodge JSON number precision). Mirrors nvs_* semantics:
 * typed entries, NOT_FOUND on missing key or type mismatch, required-buffer-
 * size protocol for str/blob reads. Edit the files freely between runs -
 * that is the point. Storage is dev-only and unencrypted.
 */
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "JsonStore.h"
#include "cJSON.h"

extern "C" {
#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
}

namespace fs = std::filesystem;

namespace emu {

/// Set by main() from --data; namespace files live in `<dir>/nvs/`.
void hostNvsSetBaseDir(const std::string& dir);

}  // namespace emu

namespace {

enum class EntryType : uint8_t {
    U8 = 1,
    I8,
    U16,
    U32,
    I32,
    U64,
    STR,
    BLOB,
};

struct Entry {
    EntryType            type;
    std::vector<uint8_t> data;
};

struct Namespace {
    std::map<std::string, Entry> entries;
    bool                         dirty = false;
};

struct Handle {
    std::string ns;
    bool        readonly = false;
};

std::mutex                            g_mutex;
std::string                           g_base_dir = ".emu-data";
std::map<std::string, Namespace>      g_namespaces;
std::map<nvs_handle_t, Handle>        g_handles;
nvs_handle_t                          g_next_handle = 1;

fs::path nsFile(const std::string& ns)
{
    return fs::path(g_base_dir) / "nvs" / (ns + ".json");
}

const char* typeName(EntryType type)
{
    switch (type) {
    case EntryType::U8:
        return "u8";
    case EntryType::I8:
        return "i8";
    case EntryType::U16:
        return "u16";
    case EntryType::U32:
        return "u32";
    case EntryType::I32:
        return "i32";
    case EntryType::U64:
        return "u64";
    case EntryType::STR:
        return "str";
    default:
        return "blob";
    }
}

bool typeFromName(const char* name, EntryType& out)
{
    static const struct {
        const char* name;
        EntryType   type;
    } kTypes[] = {
        {"u8", EntryType::U8},   {"i8", EntryType::I8},
        {"u16", EntryType::U16}, {"u32", EntryType::U32},
        {"i32", EntryType::I32}, {"u64", EntryType::U64},
        {"str", EntryType::STR}, {"blob", EntryType::BLOB},
    };
    for (const auto& t : kTypes) {
        if (name && std::strcmp(name, t.name) == 0) {
            out = t.type;
            return true;
        }
    }
    return false;
}

/// One entry -> its JSON "value". Scalars are numbers (u64: decimal string,
/// JSON numbers are doubles), strings plain, blobs base64.
cJSON* entryToJson(const Entry& entry)
{
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", typeName(entry.type));
    switch (entry.type) {
    case EntryType::STR: {
        // Stored with the trailing NUL; show without it.
        const std::string text(reinterpret_cast<const char*>(entry.data.data()),
                               entry.data.empty() ? 0 : entry.data.size() - 1);
        cJSON_AddStringToObject(obj, "value", text.c_str());
        break;
    }
    case EntryType::BLOB:
        cJSON_AddStringToObject(
            obj, "value",
            emu::base64Encode(entry.data.data(), entry.data.size()).c_str());
        break;
    case EntryType::U64: {
        uint64_t v = 0;
        std::memcpy(&v, entry.data.data(), sizeof(v));
        char text[24];
        snprintf(text, sizeof(text), "%" PRIu64, v);
        cJSON_AddStringToObject(obj, "value", text);
        break;
    }
    default: {
        int64_t v = 0;
        std::memcpy(&v, entry.data.data(),
                    entry.data.size() < 8 ? entry.data.size() : 8);
        if (entry.type == EntryType::I8) {
            v = static_cast<int8_t>(v);
        } else if (entry.type == EntryType::I32) {
            v = static_cast<int32_t>(static_cast<uint32_t>(v));
        }
        cJSON_AddNumberToObject(obj, "value", static_cast<double>(v));
        break;
    }
    }
    return obj;
}

template <typename T>
void scalarBytes(Entry& entry, T value)
{
    entry.data.assign(reinterpret_cast<const uint8_t*>(&value),
                      reinterpret_cast<const uint8_t*>(&value) + sizeof(T));
}

bool jsonToEntry(const cJSON* obj, Entry& entry)
{
    const cJSON* type = cJSON_GetObjectItemCaseSensitive(obj, "type");
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(obj, "value");
    if (!cJSON_IsString(type) || !value ||
        !typeFromName(type->valuestring, entry.type)) {
        return false;
    }
    switch (entry.type) {
    case EntryType::STR: {
        if (!cJSON_IsString(value)) {
            return false;
        }
        const char* text = value->valuestring;
        entry.data.assign(text, text + std::strlen(text) + 1);
        return true;
    }
    case EntryType::BLOB: {
        if (!cJSON_IsString(value)) {
            return false;
        }
        entry.data = emu::base64Decode(value->valuestring);
        return true;
    }
    case EntryType::U64: {
        // Accept both the canonical decimal string and a plain number.
        uint64_t v = 0;
        if (cJSON_IsString(value)) {
            v = strtoull(value->valuestring, nullptr, 10);
        } else if (cJSON_IsNumber(value)) {
            v = static_cast<uint64_t>(value->valuedouble);
        } else {
            return false;
        }
        scalarBytes(entry, v);
        return true;
    }
    default: {
        if (!cJSON_IsNumber(value)) {
            return false;
        }
        const int64_t v = static_cast<int64_t>(value->valuedouble);
        switch (entry.type) {
        case EntryType::U8:
            scalarBytes(entry, static_cast<uint8_t>(v));
            break;
        case EntryType::I8:
            scalarBytes(entry, static_cast<int8_t>(v));
            break;
        case EntryType::U16:
            scalarBytes(entry, static_cast<uint16_t>(v));
            break;
        case EntryType::U32:
            scalarBytes(entry, static_cast<uint32_t>(v));
            break;
        default:
            scalarBytes(entry, static_cast<int32_t>(v));
            break;
        }
        return true;
    }
    }
}

void loadNamespace(const std::string& ns)
{
    if (g_namespaces.count(ns)) {
        return;
    }
    Namespace& store = g_namespaces[ns];
    cJSON*     root = emu::jsonReadFile(nsFile(ns).string());
    if (!root) {
        return;
    }
    for (cJSON* item = root->child; item; item = item->next) {
        Entry entry;
        if (item->string && jsonToEntry(item, entry)) {
            store.entries[item->string] = std::move(entry);
        } else if (item->string) {
            fprintf(stderr, "W (HostNvs) %s: ignoring malformed entry '%s'\n",
                    nsFile(ns).string().c_str(), item->string);
        }
    }
    cJSON_Delete(root);
}

void saveNamespace(const std::string& ns)
{
    auto it = g_namespaces.find(ns);
    if (it == g_namespaces.end() || !it->second.dirty) {
        return;
    }
    cJSON* root = cJSON_CreateObject();
    for (const auto& [key, entry] : it->second.entries) {
        cJSON_AddItemToObject(root, key.c_str(), entryToJson(entry));
    }
    (void)emu::jsonWriteFile(nsFile(ns).string(), root);
    cJSON_Delete(root);
    it->second.dirty = false;
}

Namespace* storeFor(nvs_handle_t handle, bool forWrite, std::string* nsOut)
{
    auto it = g_handles.find(handle);
    if (it == g_handles.end()) {
        return nullptr;
    }
    if (forWrite && it->second.readonly) {
        return nullptr;
    }
    if (nsOut) {
        *nsOut = it->second.ns;
    }
    return &g_namespaces[it->second.ns];
}

template <typename T>
esp_err_t getScalar(nvs_handle_t handle, const char* key, T* out, EntryType type)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Namespace* store = storeFor(handle, false, nullptr);
    if (!store || !key || !out) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    auto it = store->entries.find(key);
    if (it == store->entries.end() || it->second.type != type ||
        it->second.data.size() != sizeof(T)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    std::memcpy(out, it->second.data.data(), sizeof(T));
    return ESP_OK;
}

template <typename T>
esp_err_t setScalar(nvs_handle_t handle, const char* key, T value, EntryType type)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Namespace* store = storeFor(handle, true, nullptr);
    if (!store || !key) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    Entry& entry = store->entries[key];
    entry.type = type;
    entry.data.assign(reinterpret_cast<const uint8_t*>(&value),
                      reinterpret_cast<const uint8_t*>(&value) + sizeof(T));
    store->dirty = true;
    return ESP_OK;
}

esp_err_t getBytes(nvs_handle_t handle, const char* key, void* out, size_t* len,
                   EntryType type)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Namespace* store = storeFor(handle, false, nullptr);
    if (!store || !key || !len) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    auto it = store->entries.find(key);
    if (it == store->entries.end() || it->second.type != type) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    const size_t needed = it->second.data.size();
    if (!out) {
        *len = needed;
        return ESP_OK;
    }
    if (*len < needed) {
        *len = needed;
        return ESP_ERR_INVALID_SIZE;
    }
    std::memcpy(out, it->second.data.data(), needed);
    *len = needed;
    return ESP_OK;
}

esp_err_t setBytes(nvs_handle_t handle, const char* key, const void* value,
                   size_t len, EntryType type)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Namespace* store = storeFor(handle, true, nullptr);
    if (!store || !key || (!value && len)) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    Entry& entry = store->entries[key];
    entry.type = type;
    const auto* bytes = static_cast<const uint8_t*>(value);
    entry.data.assign(bytes, bytes + len);
    store->dirty = true;
    return ESP_OK;
}

}  // namespace

namespace emu {

void hostNvsSetBaseDir(const std::string& dir)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_base_dir = dir;
}

}  // namespace emu

extern "C" {

esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { return ESP_OK; }

esp_err_t nvs_open(const char* ns, nvs_open_mode_t mode, nvs_handle_t* out)
{
    if (!ns || !out || std::strlen(ns) == 0 || std::strlen(ns) > 15) {
        return ESP_ERR_NVS_INVALID_NAME;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    loadNamespace(ns);
    const nvs_handle_t handle = g_next_handle++;
    g_handles[handle] = Handle{ns, mode == NVS_READONLY};
    *out = handle;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_handles.find(handle);
    if (it != g_handles.end()) {
        saveNamespace(it->second.ns);
        g_handles.erase(it);
    }
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string ns;
    if (!storeFor(handle, false, &ns)) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    saveNamespace(ns);
    return ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle_t h, const char* k, uint8_t* o) { return getScalar(h, k, o, EntryType::U8); }
esp_err_t nvs_get_i8(nvs_handle_t h, const char* k, int8_t* o) { return getScalar(h, k, o, EntryType::I8); }
esp_err_t nvs_get_u16(nvs_handle_t h, const char* k, uint16_t* o) { return getScalar(h, k, o, EntryType::U16); }
esp_err_t nvs_get_u32(nvs_handle_t h, const char* k, uint32_t* o) { return getScalar(h, k, o, EntryType::U32); }
esp_err_t nvs_get_i32(nvs_handle_t h, const char* k, int32_t* o) { return getScalar(h, k, o, EntryType::I32); }
esp_err_t nvs_get_u64(nvs_handle_t h, const char* k, uint64_t* o) { return getScalar(h, k, o, EntryType::U64); }

esp_err_t nvs_set_u8(nvs_handle_t h, const char* k, uint8_t v) { return setScalar(h, k, v, EntryType::U8); }
esp_err_t nvs_set_i8(nvs_handle_t h, const char* k, int8_t v) { return setScalar(h, k, v, EntryType::I8); }
esp_err_t nvs_set_u16(nvs_handle_t h, const char* k, uint16_t v) { return setScalar(h, k, v, EntryType::U16); }
esp_err_t nvs_set_u32(nvs_handle_t h, const char* k, uint32_t v) { return setScalar(h, k, v, EntryType::U32); }
esp_err_t nvs_set_i32(nvs_handle_t h, const char* k, int32_t v) { return setScalar(h, k, v, EntryType::I32); }
esp_err_t nvs_set_u64(nvs_handle_t h, const char* k, uint64_t v) { return setScalar(h, k, v, EntryType::U64); }

esp_err_t nvs_get_str(nvs_handle_t h, const char* k, char* out, size_t* len)
{
    return getBytes(h, k, out, len, EntryType::STR);
}

esp_err_t nvs_get_blob(nvs_handle_t h, const char* k, void* out, size_t* len)
{
    return getBytes(h, k, out, len, EntryType::BLOB);
}

esp_err_t nvs_set_str(nvs_handle_t h, const char* k, const char* value)
{
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    return setBytes(h, k, value, std::strlen(value) + 1, EntryType::STR);
}

esp_err_t nvs_set_blob(nvs_handle_t h, const char* k, const void* value, size_t len)
{
    return setBytes(h, k, value, len, EntryType::BLOB);
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char* key)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Namespace* store = storeFor(handle, true, nullptr);
    if (!store || !key) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    if (store->entries.erase(key) == 0) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    store->dirty = true;
    return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t handle)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    Namespace* store = storeFor(handle, true, nullptr);
    if (!store) {
        return ESP_ERR_NVS_INVALID_HANDLE;
    }
    store->entries.clear();
    store->dirty = true;
    return ESP_OK;
}

}  // extern "C"

/* --- Entry iteration ----------------------------------------------------- */

namespace {

nvs_type_t publicType(EntryType type)
{
    switch (type) {
    case EntryType::U8:
        return NVS_TYPE_U8;
    case EntryType::I8:
        return NVS_TYPE_I8;
    case EntryType::U16:
        return NVS_TYPE_U16;
    case EntryType::U32:
        return NVS_TYPE_U32;
    case EntryType::I32:
        return NVS_TYPE_I32;
    case EntryType::U64:
        return NVS_TYPE_U64;
    case EntryType::STR:
        return NVS_TYPE_STR;
    default:
        return NVS_TYPE_BLOB;
    }
}

}  // namespace

struct nvs_opaque_iterator_t {
    std::vector<nvs_entry_info_t> entries;
    size_t                        pos = 0;
};

extern "C" {

esp_err_t nvs_entry_find(const char* part_name, const char* namespace_name,
                         nvs_type_t type, nvs_iterator_t* output_iterator)
{
    (void)part_name;
    if (!namespace_name || !output_iterator) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    loadNamespace(namespace_name);
    auto* it = new nvs_opaque_iterator_t();
    for (const auto& [key, entry] : g_namespaces[namespace_name].entries) {
        const nvs_type_t entryType = publicType(entry.type);
        if (type != NVS_TYPE_ANY && type != entryType) {
            continue;
        }
        nvs_entry_info_t info = {};
        snprintf(info.namespace_name, sizeof(info.namespace_name), "%s",
                 namespace_name);
        snprintf(info.key, sizeof(info.key), "%s", key.c_str());
        info.type = entryType;
        it->entries.push_back(info);
    }
    if (it->entries.empty()) {
        delete it;
        *output_iterator = nullptr;
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *output_iterator = it;
    return ESP_OK;
}

esp_err_t nvs_entry_next(nvs_iterator_t* iterator)
{
    if (!iterator || !*iterator) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_opaque_iterator_t* it = *iterator;
    if (++it->pos >= it->entries.size()) {
        delete it;
        *iterator = nullptr;
        return ESP_ERR_NVS_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t nvs_entry_info(const nvs_iterator_t iterator, nvs_entry_info_t* out_info)
{
    if (!iterator || !out_info || iterator->pos >= iterator->entries.size()) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_info = iterator->entries[iterator->pos];
    return ESP_OK;
}

void nvs_release_iterator(nvs_iterator_t iterator)
{
    delete iterator;
}

}  // extern "C"
