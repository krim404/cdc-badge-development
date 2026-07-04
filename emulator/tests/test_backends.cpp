/**
 * \file test_backends.cpp
 * \brief Host backend unit tests (T032): NVS round-trip + persistence, and
 *        the software secure element - keygen, externally verified P-256 /
 *        Ed25519 signatures (SC-004) and RMemHeader round-trips (FR-020).
 *
 * Plain asserts + exit codes keep the harness dependency-free; CTest counts
 * a non-zero exit as failure.
 */
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "backends/HostSecureElement.h"

#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"
#include "monocypher-ed25519.h"

extern "C" {
#include "esp_err.h"
#include "nvs.h"
}

namespace emu {
void hostNvsSetBaseDir(const std::string& dir);
}

namespace {

int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

void testNvsRoundTrip(const std::string& dir)
{
    emu::hostNvsSetBaseDir(dir);

    nvs_handle_t handle = 0;
    CHECK(nvs_open("plg_test", NVS_READWRITE, &handle) == ESP_OK);
    CHECK(nvs_set_u32(handle, "answer", 42) == ESP_OK);
    CHECK(nvs_set_str(handle, "name", "emulator") == ESP_OK);
    const uint8_t blob[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    CHECK(nvs_set_blob(handle, "blob", blob, sizeof(blob)) == ESP_OK);
    CHECK(nvs_commit(handle) == ESP_OK);
    nvs_close(handle);

    // Values persist across a re-open (file-backed, FR-018).
    CHECK(nvs_open("plg_test", NVS_READWRITE, &handle) == ESP_OK);
    uint32_t answer = 0;
    CHECK(nvs_get_u32(handle, "answer", &answer) == ESP_OK && answer == 42);
    char   name[16] = {};
    size_t nameLen = sizeof(name);
    CHECK(nvs_get_str(handle, "name", name, &nameLen) == ESP_OK &&
          strcmp(name, "emulator") == 0);
    uint8_t blobOut[4] = {};
    size_t  blobLen = sizeof(blobOut);
    CHECK(nvs_get_blob(handle, "blob", blobOut, &blobLen) == ESP_OK &&
          memcmp(blobOut, blob, 4) == 0);

    // Type mismatch and missing keys behave like the ESP-IDF contract.
    uint32_t wrongType = 0;
    CHECK(nvs_get_u32(handle, "name", &wrongType) == ESP_ERR_NVS_NOT_FOUND);
    CHECK(nvs_get_u32(handle, "missing", &wrongType) == ESP_ERR_NVS_NOT_FOUND);
    CHECK(nvs_erase_key(handle, "answer") == ESP_OK);
    CHECK(nvs_get_u32(handle, "answer", &answer) == ESP_ERR_NVS_NOT_FOUND);
    nvs_close(handle);
}

void testSecureElementP256(const std::string& dir)
{
    using cdc::hal::EccCurve;
    using cdc::hal::SeResult;
    auto& se = emu::HostSecureElement::instance();
    se.setBaseDir(dir);

    CHECK(se.eccGenerate(0, EccCurve::P256) == SeResult::OK);
    CHECK(se.eccSlotUsed(0));
    // Occupied slots refuse a second generate (firmware contract).
    CHECK(se.eccGenerate(0, EccCurve::P256) == SeResult::SLOT_OCCUPIED);
    // eccImport stays unsupported, as in the firmware (FR-022 scope).
    uint8_t fakeKey[32] = {1};
    CHECK(se.eccImport(1, fakeKey, EccCurve::P256) == SeResult::NOT_SUPPORTED);

    uint8_t  pub[65] = {};
    EccCurve curve;
    CHECK(se.eccGetPublicKey(0, pub, &curve) == SeResult::OK);
    CHECK(curve == EccCurve::P256 && pub[0] == 0x04);

    const uint8_t msg[] = "emulator signing test";
    uint8_t       sig[64] = {};
    size_t        sigLen = 0;
    CHECK(se.ecdsaSign(0, msg, sizeof(msg), sig, &sigLen) == SeResult::OK);
    CHECK(sigLen == 64);

    // External verification (SC-004): rebuild the public point and verify
    // R||S over SHA-256(msg) with mbedTLS acting as the third party.
    uint8_t hash[32];
    mbedtls_sha256(msg, sizeof(msg), hash, 0);
    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi       r, s;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    CHECK(mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0);
    CHECK(mbedtls_ecp_point_read_binary(&grp, &Q, pub, sizeof(pub)) == 0);
    CHECK(mbedtls_mpi_read_binary(&r, sig, 32) == 0);
    CHECK(mbedtls_mpi_read_binary(&s, sig + 32, 32) == 0);
    CHECK(mbedtls_ecdsa_verify(&grp, hash, sizeof(hash), &Q, &r, &s) == 0);
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_group_free(&grp);

    CHECK(se.eccDelete(0) == SeResult::OK);
    CHECK(!se.eccSlotUsed(0));
}

void testSecureElementEd25519(const std::string& dir)
{
    using cdc::hal::EccCurve;
    using cdc::hal::SeResult;
    auto& se = emu::HostSecureElement::instance();
    se.setBaseDir(dir);

    CHECK(se.eccGenerate(2, EccCurve::ED25519) == SeResult::OK);
    uint8_t  pub[32] = {};
    EccCurve curve;
    CHECK(se.eccGetPublicKey(2, pub, &curve) == SeResult::OK);
    CHECK(curve == EccCurve::ED25519);

    const uint8_t msg[] = "ed25519 emulator test";
    uint8_t       sig[64] = {};
    CHECK(se.eddsaSign(2, msg, sizeof(msg), sig) == SeResult::OK);
    // Independent verification via Monocypher's checker.
    CHECK(crypto_ed25519_check(sig, pub, msg, sizeof(msg)) == 0);
    // Curve mismatch is rejected.
    uint8_t sig2[64];
    size_t  sigLen = 0;
    CHECK(se.ecdsaSign(2, msg, sizeof(msg), sig2, &sigLen) ==
          SeResult::INVALID_PARAM);
    CHECK(se.eccDelete(2) == SeResult::OK);
}

void testRmemHeaderRoundTrip(const std::string& dir)
{
    using cdc::hal::SeResult;
    using RMemHeader = cdc::hal::ISecureElement::RMemHeader;
    auto& se = emu::HostSecureElement::instance();
    se.setBaseDir(dir);

    const uint8_t payload[] = {1, 2, 3, 4, 5};
    CHECK(se.rmemWriteWithHeader(100, 7, "test_rec", 0x02, payload,
                                 sizeof(payload)) == SeResult::OK);
    CHECK(se.rmemSlotUsed(100));

    RMemHeader header = {};
    uint8_t    out[16] = {};
    uint16_t   outLen = 0;
    CHECK(se.rmemReadWithHeader(100, &header, out, sizeof(out), &outLen) ==
          SeResult::OK);
    CHECK(header.moduleId == 7);
    CHECK(header.flags == 0x02);
    CHECK(strcmp(header.name, "test_rec") == 0);
    CHECK(header.payloadLen == sizeof(payload));
    CHECK(outLen == sizeof(payload) && memcmp(out, payload, sizeof(payload)) == 0);

    // The header layout must stay firmware-compatible: 24 packed bytes.
    static_assert(sizeof(RMemHeader) == 22,
                  "RMemHeader layout changed - check firmware compatibility");

    // Oversized writes are rejected (slot size cap).
    uint8_t big[500] = {};
    CHECK(se.rmemWrite(101, big, sizeof(big)) == SeResult::INVALID_PARAM);
    CHECK(se.rmemErase(100) == SeResult::OK);
    CHECK(!se.rmemSlotUsed(100));
}

void testHandEditedJson(const std::string& dir)
{
    // The JSON store is meant to be edited by hand between runs: a file
    // written with a text editor must load exactly like one the emulator
    // wrote itself.
    namespace fs = std::filesystem;
    const fs::path file = fs::path(dir) / "nvs" / "plg_edited.json";
    fs::create_directories(file.parent_path());
    std::ofstream out(file);
    out << R"({
  "url":    { "type": "str",  "value": "http://example.com/feed" },
  "count":  { "type": "u32",  "value": 7 },
  "big":    { "type": "u64",  "value": "18446744073709551615" },
  "secret": { "type": "blob", "value": "3q2+7w==" }
})";
    out.close();

    nvs_handle_t handle = 0;
    CHECK(nvs_open("plg_edited", NVS_READONLY, &handle) == ESP_OK);
    char   url[64] = {};
    size_t urlLen = sizeof(url);
    CHECK(nvs_get_str(handle, "url", url, &urlLen) == ESP_OK &&
          strcmp(url, "http://example.com/feed") == 0);
    uint32_t count = 0;
    CHECK(nvs_get_u32(handle, "count", &count) == ESP_OK && count == 7);
    uint64_t big = 0;
    CHECK(nvs_get_u64(handle, "big", &big) == ESP_OK &&
          big == UINT64_C(18446744073709551615));
    uint8_t blob[4] = {};
    size_t  blobLen = sizeof(blob);
    const uint8_t expected[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    CHECK(nvs_get_blob(handle, "secret", blob, &blobLen) == ESP_OK &&
          memcmp(blob, expected, 4) == 0);
    nvs_close(handle);
}

}  // namespace

int main()
{
    const std::string dir =
        (std::filesystem::temp_directory_path() / "cdc-emu-test").string();
    std::filesystem::remove_all(dir);

    testNvsRoundTrip(dir);
    testHandEditedJson(dir);
    testSecureElementP256(dir);
    testSecureElementEd25519(dir);
    testRmemHeaderRoundTrip(dir);

    std::filesystem::remove_all(dir);
    if (g_failures) {
        fprintf(stderr, "%d check(s) FAILED\n", g_failures);
        return 1;
    }
    printf("all backend tests passed\n");
    return 0;
}
