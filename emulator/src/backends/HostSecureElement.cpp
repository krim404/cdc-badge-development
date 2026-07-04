/**
 * \file HostSecureElement.cpp
 * \brief See HostSecureElement.h. DEV-ONLY, plaintext storage, not secure.
 *
 * Persistence is human-readable JSON so developers can inspect and edit the
 * store between runs:
 *   - `<data>/se/ecc_<slot>.json`:  {"curve": "p256"|"ed25519",
 *                                    "private_key": "<base64>"}
 *   - `<data>/se/rmem_<slot>.json`: {"module_id", "flags", "name",
 *                                    "payload": "<base64>"} for records
 *     written with the firmware RMemHeader convention, or {"raw": "<base64>"}
 *     for headerless writes. magic/checksum are reconstructed on read, so a
 *     hand-edited payload stays consistent (FR-020: the byte layout handed to
 *     plugins matches the firmware).
 */
#include "HostSecureElement.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "JsonStore.h"
#include "cJSON.h"

#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"
#include "monocypher-ed25519.h"

extern "C" {
#include "esp_random.h"
}

namespace fs = std::filesystem;

using cdc::hal::EccCurve;
using cdc::hal::SeResult;

namespace {

constexpr uint8_t kCurveP256 = 1;
constexpr uint8_t kCurveEd25519 = 0;
constexpr size_t  kPrivKeyLen = 32;

int hostRng(void* ctx, unsigned char* out, size_t len)
{
    (void)ctx;
    esp_fill_random(out, len);
    return 0;
}

constexpr uint8_t kRmemMagic = 0xCD;

using RMemHeader = cdc::hal::ISecureElement::RMemHeader;
constexpr uint8_t kRmemNameLen = cdc::hal::ISecureElement::RMEM_NAME_LEN;

/// XOR checksum over moduleId, flags, name and payload - the convention the
/// binary rmemWriteWithHeader used; reconstructed on every read so edited
/// JSON stays self-consistent.
uint8_t rmemChecksum(const RMemHeader& header, const uint8_t* payload)
{
    uint8_t checksum = header.moduleId ^ header.flags;
    for (size_t i = 0; i < kRmemNameLen; ++i) {
        checksum ^= static_cast<uint8_t>(header.name[i]);
    }
    for (uint16_t i = 0; i < header.payloadLen; ++i) {
        checksum ^= payload[i];
    }
    return checksum;
}

bool saveEccKey(const std::string& path, uint8_t curveByte,
                const uint8_t priv[32])
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "curve", curveByte == 1 ? "p256" : "ed25519");
    cJSON_AddStringToObject(root, "private_key",
                            emu::base64Encode(priv, 32).c_str());
    const bool ok = emu::jsonWriteFile(path, root);
    cJSON_Delete(root);
    return ok;
}

bool loadEccKey(const std::string& path, uint8_t& curveByte, uint8_t priv[32])
{
    cJSON* root = emu::jsonReadFile(path);
    if (!root) {
        return false;
    }
    const cJSON* curve = cJSON_GetObjectItemCaseSensitive(root, "curve");
    const cJSON* key = cJSON_GetObjectItemCaseSensitive(root, "private_key");
    bool ok = cJSON_IsString(curve) && cJSON_IsString(key);
    if (ok) {
        curveByte = std::strcmp(curve->valuestring, "p256") == 0 ? 1 : 0;
        const auto bytes = emu::base64Decode(key->valuestring);
        ok = bytes.size() == 32;
        if (ok) {
            std::memcpy(priv, bytes.data(), 32);
        }
    }
    cJSON_Delete(root);
    return ok;
}

/// Load an R-Memory slot as the exact byte image a plugin would read from
/// the chip: header-form records are re-serialised (RMemHeader + payload,
/// magic/checksum reconstructed), raw records decode verbatim.
bool loadRmemBytes(const std::string& path, std::vector<uint8_t>& out)
{
    cJSON* root = emu::jsonReadFile(path);
    if (!root) {
        return false;
    }
    const cJSON* raw = cJSON_GetObjectItemCaseSensitive(root, "raw");
    if (cJSON_IsString(raw)) {
        out = emu::base64Decode(raw->valuestring);
        cJSON_Delete(root);
        return true;
    }
    const cJSON* moduleId = cJSON_GetObjectItemCaseSensitive(root, "module_id");
    const cJSON* flags = cJSON_GetObjectItemCaseSensitive(root, "flags");
    const cJSON* name = cJSON_GetObjectItemCaseSensitive(root, "name");
    const cJSON* payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (!cJSON_IsNumber(moduleId) || !cJSON_IsString(name)) {
        cJSON_Delete(root);
        return false;
    }
    const std::vector<uint8_t> body =
        cJSON_IsString(payload) ? emu::base64Decode(payload->valuestring)
                                : std::vector<uint8_t>{};
    RMemHeader header = {};
    header.magic = kRmemMagic;
    header.moduleId = static_cast<uint8_t>(moduleId->valuedouble);
    header.flags = cJSON_IsNumber(flags)
                       ? static_cast<uint8_t>(flags->valuedouble)
                       : 0;
    std::strncpy(header.name, name->valuestring, kRmemNameLen - 1);
    header.payloadLen = static_cast<uint16_t>(body.size());
    header.checksum = rmemChecksum(header, body.data());

    out.resize(sizeof(RMemHeader) + body.size());
    std::memcpy(out.data(), &header, sizeof(header));
    if (!body.empty()) {
        std::memcpy(out.data() + sizeof(header), body.data(), body.size());
    }
    cJSON_Delete(root);
    return true;
}

bool saveRmemRaw(const std::string& path, const uint8_t* data, uint16_t len)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "raw", emu::base64Encode(data, len).c_str());
    const bool ok = emu::jsonWriteFile(path, root);
    cJSON_Delete(root);
    return ok;
}

bool saveRmemRecord(const std::string& path, uint8_t moduleId, const char* name,
                    uint8_t flags, const uint8_t* payload, uint16_t payloadLen)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "module_id", moduleId);
    cJSON_AddNumberToObject(root, "flags", flags);
    cJSON_AddStringToObject(root, "name", name);
    cJSON_AddStringToObject(root, "payload",
                            emu::base64Encode(payload, payloadLen).c_str());
    const bool ok = emu::jsonWriteFile(path, root);
    cJSON_Delete(root);
    return ok;
}

/// Derive the P-256 public key (65-byte uncompressed) from a stored private key.
bool p256PublicKey(const uint8_t priv[kPrivKeyLen], uint8_t out[65])
{
    mbedtls_ecp_group grp;
    mbedtls_mpi       d;
    mbedtls_ecp_point Q;
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&Q);

    bool ok = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
              mbedtls_mpi_read_binary(&d, priv, kPrivKeyLen) == 0 &&
              mbedtls_ecp_mul(&grp, &Q, &d, &grp.G, hostRng, nullptr) == 0;
    if (ok) {
        size_t olen = 0;
        ok = mbedtls_ecp_point_write_binary(&grp, &Q, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                            &olen, out, 65) == 0 &&
             olen == 65;
    }
    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);
    return ok;
}

}  // namespace

namespace emu {

HostSecureElement& HostSecureElement::instance()
{
    static HostSecureElement se;
    return se;
}

std::string HostSecureElement::eccPath(uint8_t slot) const
{
    char name[32];
    snprintf(name, sizeof(name), "ecc_%u.json", slot);
    return (fs::path(base_dir_) / "se" / name).string();
}

std::string HostSecureElement::rmemPath(uint16_t slot) const
{
    char name[32];
    snprintf(name, sizeof(name), "rmem_%u.json", slot);
    return (fs::path(base_dir_) / "se" / name).string();
}

SeResult HostSecureElement::eccGenerate(uint8_t slot, EccCurve curve)
{
    if (slot >= ECC_SLOT_COUNT) {
        return SeResult::INVALID_PARAM;
    }
    if (eccSlotUsed(slot)) {
        return SeResult::SLOT_OCCUPIED;
    }
    uint8_t priv[kPrivKeyLen];
    const uint8_t curveByte =
        (curve == EccCurve::ED25519) ? kCurveEd25519 : kCurveP256;
    if (curve == EccCurve::P256) {
        // Generate a valid scalar via mbedTLS so d is guaranteed in range.
        mbedtls_ecp_group grp;
        mbedtls_mpi       d;
        mbedtls_ecp_point Q;
        mbedtls_ecp_group_init(&grp);
        mbedtls_mpi_init(&d);
        mbedtls_ecp_point_init(&Q);
        const bool ok =
            mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
            mbedtls_ecp_gen_keypair(&grp, &d, &Q, hostRng, nullptr) == 0 &&
            mbedtls_mpi_write_binary(&d, priv, kPrivKeyLen) == 0;
        mbedtls_ecp_point_free(&Q);
        mbedtls_mpi_free(&d);
        mbedtls_ecp_group_free(&grp);
        if (!ok) {
            return SeResult::ERROR;
        }
    } else {
        // Ed25519: any 32 random bytes are a valid seed.
        esp_fill_random(priv, kPrivKeyLen);
    }
    return saveEccKey(eccPath(slot), curveByte, priv) ? SeResult::OK
                                                      : SeResult::ERROR;
}

SeResult HostSecureElement::eccImport(uint8_t slot, const uint8_t* privKey,
                                      EccCurve curve)
{
    // Unsupported, exactly as in the firmware's Tropic01Element.
    (void)slot;
    (void)privKey;
    (void)curve;
    return SeResult::NOT_SUPPORTED;
}

SeResult HostSecureElement::eccGetPublicKey(uint8_t slot, uint8_t* pubKey,
                                            EccCurve* curve)
{
    if (slot >= ECC_SLOT_COUNT || !pubKey) {
        return SeResult::INVALID_PARAM;
    }
    uint8_t curveByte = 0;
    uint8_t priv[kPrivKeyLen];
    if (!loadEccKey(eccPath(slot), curveByte, priv)) {
        return SeResult::SLOT_EMPTY;
    }
    if (curveByte == kCurveP256) {
        if (!p256PublicKey(priv, pubKey)) {
            return SeResult::ERROR;
        }
        if (curve) {
            *curve = EccCurve::P256;
        }
    } else {
        uint8_t secret[64];
        crypto_ed25519_key_pair(secret, pubKey, priv);  // wipes the seed copy
        crypto_wipe(secret, sizeof(secret));
        if (curve) {
            *curve = EccCurve::ED25519;
        }
    }
    return SeResult::OK;
}

SeResult HostSecureElement::eccDelete(uint8_t slot)
{
    if (slot >= ECC_SLOT_COUNT) {
        return SeResult::INVALID_PARAM;
    }
    std::error_code ec;
    fs::remove(eccPath(slot), ec);
    return SeResult::OK;
}

bool HostSecureElement::eccSlotUsed(uint8_t slot) const
{
    if (slot >= ECC_SLOT_COUNT) {
        return false;
    }
    std::error_code ec;
    return fs::exists(eccPath(slot), ec);
}

SeResult HostSecureElement::ecdsaSign(uint8_t slot, const uint8_t* msg,
                                      size_t msgLen, uint8_t* sig, size_t* sigLen)
{
    if (slot >= ECC_SLOT_COUNT || !msg || !sig) {
        return SeResult::INVALID_PARAM;
    }
    uint8_t curveByte = 0;
    uint8_t priv[kPrivKeyLen];
    if (!loadEccKey(eccPath(slot), curveByte, priv)) {
        return SeResult::SLOT_EMPTY;
    }
    if (curveByte != kCurveP256) {
        return SeResult::INVALID_PARAM;
    }

    // The interface contract: hash internally, emit raw R||S (64 bytes).
    uint8_t hash[32];
    mbedtls_sha256(msg, msgLen, hash, 0);

    mbedtls_ecp_group grp;
    mbedtls_mpi       d, r, s;
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    const bool ok =
        mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_mpi_read_binary(&d, priv, kPrivKeyLen) == 0 &&
        mbedtls_ecdsa_sign(&grp, &r, &s, &d, hash, sizeof(hash), hostRng,
                           nullptr) == 0 &&
        mbedtls_mpi_write_binary(&r, sig, 32) == 0 &&
        mbedtls_mpi_write_binary(&s, sig + 32, 32) == 0;

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);

    if (!ok) {
        return SeResult::ERROR;
    }
    if (sigLen) {
        *sigLen = 64;
    }
    return SeResult::OK;
}

SeResult HostSecureElement::eddsaSign(uint8_t slot, const uint8_t* msg,
                                      size_t msgLen, uint8_t* sig)
{
    if (slot >= ECC_SLOT_COUNT || !msg || !sig) {
        return SeResult::INVALID_PARAM;
    }
    uint8_t curveByte = 0;
    uint8_t priv[kPrivKeyLen];
    if (!loadEccKey(eccPath(slot), curveByte, priv)) {
        return SeResult::SLOT_EMPTY;
    }
    if (curveByte != kCurveEd25519) {
        return SeResult::INVALID_PARAM;
    }
    uint8_t secret[64];
    uint8_t pub[32];
    crypto_ed25519_key_pair(secret, pub, priv);
    crypto_ed25519_sign(sig, secret, msg, msgLen);
    crypto_wipe(secret, sizeof(secret));
    return SeResult::OK;
}

SeResult HostSecureElement::rmemRead(uint16_t slot, uint8_t* data,
                                     uint16_t maxLen, uint16_t* actualLen)
{
    if (slot >= RMEM_SLOT_COUNT || !data) {
        return SeResult::INVALID_PARAM;
    }
    std::vector<uint8_t> record;
    if (!loadRmemBytes(rmemPath(slot), record)) {
        return SeResult::SLOT_EMPTY;
    }
    const uint16_t n =
        static_cast<uint16_t>(record.size() < maxLen ? record.size() : maxLen);
    std::memcpy(data, record.data(), n);
    if (actualLen) {
        *actualLen = static_cast<uint16_t>(record.size());
    }
    return SeResult::OK;
}

SeResult HostSecureElement::rmemWrite(uint16_t slot, const uint8_t* data,
                                      uint16_t len)
{
    if (slot >= RMEM_SLOT_COUNT || !data || len > getRmemSlotSize()) {
        return SeResult::INVALID_PARAM;
    }
    return saveRmemRaw(rmemPath(slot), data, len) ? SeResult::OK : SeResult::ERROR;
}

SeResult HostSecureElement::rmemErase(uint16_t slot)
{
    if (slot >= RMEM_SLOT_COUNT) {
        return SeResult::INVALID_PARAM;
    }
    std::error_code ec;
    fs::remove(rmemPath(slot), ec);
    return SeResult::OK;
}

bool HostSecureElement::rmemSlotUsed(uint16_t slot) const
{
    if (slot >= RMEM_SLOT_COUNT) {
        return false;
    }
    std::error_code ec;
    return fs::exists(rmemPath(slot), ec);
}

SeResult HostSecureElement::rmemWriteWithHeader(uint16_t slot, uint8_t moduleId,
                                                const char* name, uint8_t flags,
                                                const uint8_t* payload,
                                                uint16_t payloadLen)
{
    if (!name || (!payload && payloadLen)) {
        return SeResult::INVALID_PARAM;
    }
    if (sizeof(RMemHeader) + payloadLen > getRmemSlotSize()) {
        return SeResult::INVALID_PARAM;
    }
    char safeName[kRmemNameLen] = {};
    std::strncpy(safeName, name, kRmemNameLen - 1);
    return saveRmemRecord(rmemPath(slot), moduleId, safeName, flags, payload,
                          payloadLen)
               ? SeResult::OK
               : SeResult::ERROR;
}

SeResult HostSecureElement::rmemReadWithHeader(uint16_t slot, RMemHeader* headerOut,
                                               uint8_t* payloadOut,
                                               uint16_t payloadMax,
                                               uint16_t* payloadLenOut)
{
    std::vector<uint8_t> record;
    if (slot >= RMEM_SLOT_COUNT) {
        return SeResult::INVALID_PARAM;
    }
    if (!loadRmemBytes(rmemPath(slot), record) ||
        record.size() < sizeof(RMemHeader)) {
        return SeResult::SLOT_EMPTY;
    }
    RMemHeader header;
    std::memcpy(&header, record.data(), sizeof(header));
    const uint16_t available =
        static_cast<uint16_t>(record.size() - sizeof(RMemHeader));
    const uint16_t payloadLen =
        header.payloadLen < available ? header.payloadLen : available;
    if (headerOut) {
        *headerOut = header;
    }
    if (payloadOut) {
        const uint16_t n = payloadLen < payloadMax ? payloadLen : payloadMax;
        std::memcpy(payloadOut, record.data() + sizeof(RMemHeader), n);
    }
    if (payloadLenOut) {
        *payloadLenOut = payloadLen;
    }
    return SeResult::OK;
}

bool HostSecureElement::getRandom(uint8_t* buffer, uint16_t size)
{
    if (!buffer) {
        return false;
    }
    esp_fill_random(buffer, size);
    return true;
}

bool HostSecureElement::getRandomStrict(uint8_t* buffer, uint16_t size)
{
    return getRandom(buffer, size);
}

bool HostSecureElement::getChipId(uint8_t* serialNum, uint8_t size)
{
    if (!serialNum || size == 0) {
        return false;
    }
    static const uint8_t kChipId[] = {'E', 'M', 'U', 'L', 'A', 'T', 'O', 'R'};
    for (uint8_t i = 0; i < size; ++i) {
        serialNum[i] = kChipId[i % sizeof(kChipId)];
    }
    return true;
}

bool HostSecureElement::getFwVersion(uint8_t riscvVer[4], uint8_t spectVer[4])
{
    // Index 3 = major, 2 = minor, 1 = patch, 0 = build (chip convention).
    const uint8_t version[4] = {0, 0, 0, 2};
    if (riscvVer) {
        std::memcpy(riscvVer, version, 4);
    }
    if (spectVer) {
        std::memcpy(spectVer, version, 4);
    }
    return true;
}

}  // namespace emu

namespace cdc::hal {

ISecureElement* getSecureElementInstance()
{
    return &emu::HostSecureElement::instance();
}

}  // namespace cdc::hal
