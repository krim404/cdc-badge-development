/**
 * \file HostSecureElement.h
 * \brief Software secure element: real crypto (mbedTLS P-256, Monocypher
 *        Ed25519), file-backed key + R-Memory storage.
 *
 * *** DEV-ONLY - NOT SECURE ***
 * Key material and R-Memory records are stored as plaintext files under
 * `<data>/se/` so tests can inspect them. This backend exists exclusively for
 * off-device plugin development; it provides NO hardware security and MUST
 * NOT be represented as doing so (Constitution IV scope note).
 *
 * It exposes and implements NO irreversible TROPIC01 operation: no pairing-key
 * write/invalidate, no monotonic counters, no I-Config/OTP (verified by the
 * safety test in emulator/tests/). eccImport stays unsupported, as in the
 * firmware's Tropic01Element.
 */
#pragma once

#include <string>

#include "cdc_hal/ISecureElement.h"

namespace emu {

class HostSecureElement : public cdc::hal::ISecureElement {
public:
    static HostSecureElement& instance();

    /// Root data dir; keys/records live in `<dir>/se/`.
    void setBaseDir(const std::string& dir) { base_dir_ = dir; }

    // IService
    bool init() override
    {
        state_ = cdc::core::ServiceState::INITIALIZED;
        return true;
    }
    bool start() override { return true; }
    void stop() override {}
    cdc::core::ServiceState getState() const override { return state_; }
    const char* getName() const override { return "HostSecureElement"; }

    // Session management - trivially satisfied off-device.
    bool sessionStart() override
    {
        session_ = true;
        return true;
    }
    void sessionEnd() override { session_ = false; }
    bool isSessionActive() const override { return session_; }
    void sleep() override {}

    // ECC key operations
    cdc::hal::SeResult eccGenerate(uint8_t slot, cdc::hal::EccCurve curve) override;
    cdc::hal::SeResult eccImport(uint8_t slot, const uint8_t* privKey,
                                 cdc::hal::EccCurve curve) override;
    cdc::hal::SeResult eccGetPublicKey(uint8_t slot, uint8_t* pubKey,
                                       cdc::hal::EccCurve* curve) override;
    cdc::hal::SeResult eccDelete(uint8_t slot) override;
    bool eccSlotUsed(uint8_t slot) const override;

    // Signing
    cdc::hal::SeResult ecdsaSign(uint8_t slot, const uint8_t* msg, size_t msgLen,
                                 uint8_t* sig, size_t* sigLen) override;
    cdc::hal::SeResult eddsaSign(uint8_t slot, const uint8_t* msg, size_t msgLen,
                                 uint8_t* sig) override;

    // R-Memory
    cdc::hal::SeResult rmemRead(uint16_t slot, uint8_t* data, uint16_t maxLen,
                                uint16_t* actualLen) override;
    cdc::hal::SeResult rmemWrite(uint16_t slot, const uint8_t* data,
                                 uint16_t len) override;
    cdc::hal::SeResult rmemErase(uint16_t slot) override;
    bool rmemSlotUsed(uint16_t slot) const override;
    cdc::hal::SeResult rmemWriteWithHeader(uint16_t slot, uint8_t moduleId,
                                           const char* name, uint8_t flags,
                                           const uint8_t* payload,
                                           uint16_t payloadLen) override;
    cdc::hal::SeResult rmemReadWithHeader(uint16_t slot, RMemHeader* headerOut,
                                          uint8_t* payloadOut, uint16_t payloadMax,
                                          uint16_t* payloadLenOut) override;

    // RNG - host PRNG (seedable for deterministic runs); "strict" succeeds
    // too, since off-device there is no hardware TRNG to insist on.
    bool getRandom(uint8_t* buffer, uint16_t size) override;
    bool getRandomStrict(uint8_t* buffer, uint16_t size) override;

    // Diagnostics - plausible fixed values.
    bool getChipId(uint8_t* serialNum, uint8_t size) override;
    bool getFwVersion(uint8_t riscvVer[4], uint8_t spectVer[4]) override;
    uint16_t getRmemSlotSize() const override { return RMEM_SLOT_SIZE_MAX; }

private:
    HostSecureElement() = default;

    std::string eccPath(uint8_t slot) const;
    std::string rmemPath(uint16_t slot) const;

    std::string             base_dir_ = ".emu-data";
    bool                    session_ = false;
    cdc::core::ServiceState state_ = cdc::core::ServiceState::UNINITIALIZED;
};

}  // namespace emu
