# Crypto & security

Three SDK families. **`crypto`** (software AEAD/hash/codecs) and **`random`** need
no capability; **`secure_element`** needs a named `ecc` capability. Sources of
truth: `vendor/cdc-badge-plugins/sdk/cdc-badge-plugin/src/{crypto,secure_element,random}.rs`
and the `host_*` C signatures in `host_api.h`. Open those for the exact bytes; the
signatures below are the Rust wrappers. No example plugin exercises these families,
so every snippet is illustrative.

Capability rules live in `capabilities-and-lifecycle.md`. For the persistent secret
store (R-Memory) see `storage.md`. The OS already ships a password vault and TOTP
generator (see the SKILL.md hardware section); do not rebuild those.

## crypto (no capability)

Software primitives backed by the firmware's mbedTLS: SHA-256, HMAC-SHA-256,
AES-256-GCM, and base32/base64/hex codecs. All fallible, returning `Result<T>`.

```rust
crypto::sha256(data: &[u8]) -> Result<[u8; 32]>;
crypto::hmac_sha256(key: &[u8], data: &[u8]) -> Result<[u8; 32]>;

// AES-256-GCM. key must be 32 bytes, iv 12 bytes, tag is 16 bytes.
crypto::aes_gcm_encrypt(key: &[u8], iv: &[u8], aad: &[u8], plaintext: &[u8])
    -> Result<crypto::GcmSealed>;        // GcmSealed { ciphertext: Vec<u8>, tag: [u8; 16] }
crypto::aes_gcm_decrypt(key: &[u8], iv: &[u8], aad: &[u8], ciphertext: &[u8], tag: &[u8])
    -> Result<Vec<u8>>;                  // Err when the tag fails to verify

crypto::base32_encode(data: &[u8]) -> Result<String>;   // RFC 4648, no padding
crypto::base32_decode(text: &str)  -> Result<Vec<u8>>;
crypto::base64_encode(data: &[u8]) -> Result<String>;   // standard alphabet, padded
crypto::base64_decode(text: &str)  -> Result<Vec<u8>>;
crypto::hex_encode(data: &[u8])    -> Result<String>;   // lowercase
crypto::hex_decode(text: &str)     -> Result<Vec<u8>>;  // case-insensitive
```

`aes_gcm_encrypt` / `aes_gcm_decrypt` return `Err(Error::InvalidArg)` on a wrong
key/iv/tag length before touching the hardware.

```rust
// illustrative (SDK crypto.rs), no demo plugin
let key = {
    let mut k = [0u8; 32];
    random::fill(&mut k)?;            // 32-byte key
    k
};
let mut iv = [0u8; 12];
random::fill(&mut iv)?;               // 12-byte nonce
let sealed = crypto::aes_gcm_encrypt(&key, &iv, b"", b"secret")?;
let plain  = crypto::aes_gcm_decrypt(&key, &iv, b"", &sealed.ciphertext, &sealed.tag)?;
```

## secure_element (capability: `ecc`)

TROPIC01 ECC keys addressed **by name**. Each name must be declared in the manifest
`capabilities.ecc: ["name", ...]` (names 1-`secure_element::ECC_NAME_MAX` = 1-15
chars); the host maps each declared name to a slot in a reserved plugin ECC pool and
persists the mapping. **Private keys never leave the chip** - only public keys and
signatures come back. The persistent secret store (R-Memory, named like ECC keys) is
documented in `storage.md`.

```rust
secure_element::Curve::P256        // public key 64 bytes
secure_element::Curve::Ed25519     // public key 32 bytes

secure_element::generate(name: &str, curve: Curve) -> Result<()>;  // claims a pool slot
secure_element::import(name: &str, priv_key: &[u8], curve: Curve) -> Result<()>;
        // currently rejected by the firmware (keys are generated on-chip)
secure_element::pubkey(name: &str, curve: Curve) -> Result<Vec<u8>>;  // 64 (P256) / 32 (Ed25519)
secure_element::delete(name: &str) -> Result<()>;   // erases the key, frees the slot
secure_element::exists(name: &str) -> bool;         // holds key material?

secure_element::ecdsa_sign(name: &str, msg: &[u8]) -> Result<[u8; 64]>;  // P-256
secure_element::eddsa_sign(name: &str, msg: &[u8]) -> Result<[u8; 64]>;  // Ed25519

secure_element::chip_id(max: usize) -> Result<Vec<u8>>;            // TROPIC01 serial blob
secure_element::fw_version() -> Result<([u8; 4], [u8; 4])>;        // (riscv, spect)
```

```rust
// illustrative (SDK secure_element.rs), no demo plugin
use cdc_badge_plugin::secure_element::{self as se, Curve};
if !se::exists("signing") {
    se::generate("signing", Curve::P256)?;       // "signing" must be in capabilities.ecc
}
let pk  = se::pubkey("signing", Curve::P256)?;   // 64 bytes
let sig = se::ecdsa_sign("signing", &crypto::sha256(b"payload")?)?;  // [u8; 64]
```

A denied call (name not in `capabilities.ecc`) returns `Error::NoCapability`; the
plugin keeps running. See `capabilities-and-lifecycle.md`.

## random (no capability)

```rust
random::fill(buf: &mut [u8]) -> Result<()>;          // may fall back to a software PRNG
random::fill_strict(buf: &mut [u8]) -> Result<()>;   // hardware TRNG only
random::u32() -> Result<u32>;                        // built on fill()
```

Use `fill` for general randomness (UI, nonces, jitter); it returns a software-PRNG
value when no hardware TRNG is available. Use `fill_strict` when only true hardware
entropy is acceptable - it returns `Err` rather than falling back. `u32` uses `fill`,
so it shares the same fallback behaviour.

```rust
// illustrative (SDK random.rs), no demo plugin
let mut nonce = [0u8; 12];
random::fill(&mut nonce)?;
let mut key = [0u8; 32];
random::fill_strict(&mut key)?;   // refuses without a hardware TRNG
```
