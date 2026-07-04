/**
 * \file JsonStore.h
 * \brief Shared helpers for the emulator's human-readable JSON persistence
 *        (NVS namespaces, R-Memory slots, SE keys). Files under --data are
 *        meant to be read, edited and diffed by developers.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct cJSON;

namespace emu {

/// Parse a JSON file. Returns nullptr when missing/invalid (caller logs).
/// The caller owns the result (cJSON_Delete).
cJSON* jsonReadFile(const std::string& path);

/// Pretty-print `root` to `path`, creating parent directories.
bool jsonWriteFile(const std::string& path, const cJSON* root);

std::string          base64Encode(const uint8_t* data, size_t len);
std::vector<uint8_t> base64Decode(const std::string& text);

}  // namespace emu
