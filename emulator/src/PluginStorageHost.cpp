/**
 * \file PluginStorageHost.cpp
 * \brief Host definitions of the PluginStorage statics the reused sources
 *        call. The vfat partition becomes a plain host directory (the --data
 *        base dir); the active plugin's .wasm is served from wherever the CLI
 *        resolved it (dist/, a direct path), no copy needed.
 */
#include <filesystem>
#include <string>

#include "plugin_manager/PluginStorage.h"

namespace fs = std::filesystem;

namespace cdc::plugin_manager {

namespace {

std::string g_base = ".emu-data";
std::string g_active_id;
std::string g_active_wasm;
std::string g_active_lang;

}  // namespace

/// Emulator-side hooks (declared in EmulatorCore.h).
void emulatorSetStorageBase(const std::string& dir)
{
    g_base = dir;
}

void emulatorSetActiveBinary(const std::string& id, const std::string& wasmPath,
                             const std::string& langPath)
{
    g_active_id = id;
    g_active_wasm = wasmPath;
    g_active_lang = langPath;
}

bool PluginStorage::mount()
{
    std::error_code ec;
    fs::create_directories(fs::path(g_base) / "plugins", ec);
    return !ec;
}

void PluginStorage::unmount() {}

const char* PluginStorage::basePath()
{
    // Matches the firmware's VFS prefix role: everything vfat-related lives
    // under this directory on the host.
    static std::string cached;
    cached = g_base;
    return cached.c_str();
}

std::string PluginStorage::binaryPath(const std::string& id)
{
    if (id == g_active_id && !g_active_wasm.empty()) {
        return g_active_wasm;
    }
    return wasmPath(id);
}

std::string PluginStorage::wasmPath(const std::string& id)
{
    if (id == g_active_id && !g_active_wasm.empty()) {
        return g_active_wasm;
    }
    return (fs::path(g_base) / "plugins" / (id + ".wasm")).string();
}

std::string PluginStorage::aotPath(const std::string& id)
{
    return (fs::path(g_base) / "plugins" / (id + ".aot")).string();
}

std::string PluginStorage::metaPath(const std::string& id)
{
    return (fs::path(g_base) / "plugins" / (id + ".meta")).string();
}

std::string PluginStorage::langPath(const std::string& id)
{
    if (id == g_active_id && !g_active_lang.empty()) {
        return g_active_lang;
    }
    return (fs::path(g_base) / "plugins" / (id + ".lang")).string();
}

std::string PluginStorage::disabledPath(const std::string& id)
{
    return (fs::path(g_base) / "plugins" / (id + ".disabled")).string();
}

bool PluginStorage::isDisabled(const std::string& id)
{
    std::error_code ec;
    return fs::exists(disabledPath(id), ec);
}

bool PluginStorage::setDisabled(const std::string& id, bool disabled)
{
    std::error_code ec;
    if (disabled) {
        std::filesystem::path p = disabledPath(id);
        fs::create_directories(p.parent_path(), ec);
        FILE* f = fopen(p.string().c_str(), "wb");
        if (f) {
            fclose(f);
        }
        return f != nullptr;
    }
    fs::remove(disabledPath(id), ec);
    return true;
}

std::vector<std::string> PluginStorage::listPluginIds()
{
    std::vector<std::string> ids;
    if (!g_active_id.empty()) {
        ids.push_back(g_active_id);
    }
    return ids;
}

bool PluginStorage::stats(uint64_t& free_bytes, uint64_t& total_bytes)
{
    std::error_code ec;
    const auto info = fs::space(g_base, ec);
    if (ec) {
        return false;
    }
    free_bytes = info.available;
    total_bytes = info.capacity;
    return true;
}

/* USB Mass Storage block access: meaningless off-device. */
uint16_t PluginStorage::blockSize() { return 0; }
uint64_t PluginStorage::blockTotalBytes() { return 0; }
bool PluginStorage::blockRead(uint32_t, uint32_t, void*, uint32_t) { return false; }
bool PluginStorage::blockWrite(uint32_t, uint32_t, const void*, uint32_t)
{
    return false;
}
void PluginStorage::setHostActive(bool) {}
bool PluginStorage::hostActive() { return false; }
void PluginStorage::remountIfPending() {}
void PluginStorage::protectSystemDir() {}

}  // namespace cdc::plugin_manager
