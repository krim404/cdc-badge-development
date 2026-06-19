# Plugin storage (nvs, fs, rmem)

Three persistence families, three trust levels. Pick by what you store: small
config keys → `nvs`; files / blobs → `fs`; secret bytes in secure memory → `rmem`.
Capability gating and the `Result`/`Option`/`Error` model are in
`capabilities-and-lifecycle.md`; sizing/quirks in `pitfalls.md`. All text is UTF-8.

| Family | Module | Capability | Scope |
|--------|--------|-----------|-------|
| NVS key-value | `nvs` | none (needs `nvs_namespace`) | private namespace |
| vFAT files | `fs` | `vfat` | private folder on the shared 2 MB partition |
| TROPIC01 R-Memory | `rmem` | `rmem: ["name"]` (needs `nvs_namespace`) | named slot, shared pool |

## NVS key-value

Module `nvs`. Source of truth: `vendor/cdc-badge-plugins/sdk/cdc-badge-plugin/src/nvs.rs`.
Always allowed, but the host routes every call into the plugin's own namespace, so
a `nvs_namespace` MUST be declared in `meta.json`: `plg_`/`plugin_` prefix,
`[a-z0-9_]`, ≤15 chars. No cross-plugin or firmware access.

```rust
nvs::get_blob(key: &str, max_len: usize) -> Option<Vec<u8>>;
nvs::set_blob(key: &str, value: &[u8]) -> Result<()>;
nvs::get_str(key: &str, max_len: usize) -> Option<String>;
nvs::set_str(key: &str, value: &str) -> Result<()>;
nvs::get_u32(key: &str) -> Option<u32>;
nvs::set_u32(key: &str, value: u32) -> Result<()>;
nvs::erase(key: &str) -> Result<()>;       // Ok also when already absent
nvs::erase_all() -> Result<()>;            // wipes only this plugin's namespace
nvs::list_keys(max_bytes: usize) -> Option<Vec<String>>;
```

Reads return `Option` (`None` = missing / unreadable / non-UTF-8); writes and
erases return `Result`.

```rust
// examples/gpio_mqtt + examples/news_feed (nvs.rs)
let url = nvs::get_str("url", 256).unwrap_or_default();
let _ = nvs::set_str("url", value.trim());
let _ = nvs::set_u32("angle_rad", 1);
```

## vFAT files

Module `fs`. Capability `vfat: true`. Source of truth:
`vendor/cdc-badge-plugins/sdk/cdc-badge-plugin/src/fs.rs`. Each plugin gets a
private folder on the shared plugins FAT partition; the host confines every path
to it. `name` is a bare filename (`[A-Za-z0-9._-]`, no path separators, no
leading dot).

```rust
fs::write(name: &str, data: &[u8]) -> Result<()>;        // create / overwrite
fs::write_str(name: &str, text: &str) -> Result<()>;
fs::read(name: &str, max_len: usize) -> Option<Vec<u8>>;
fs::read_str(name: &str, max_len: usize) -> Option<String>;
fs::remove(name: &str) -> Result<()>;                    // Err if missing
fs::size(name: &str) -> Option<usize>;                   // None if absent
fs::list(max_bytes: usize) -> Option<Vec<String>>;
fs::view(name: &str) -> Result<()>;                      // open in on-screen text viewer
```

`view` opens the file in the same scrollable text view the file explorer uses,
handy for a bundled readme.

```rust
// examples/sci_calc (fs.rs)
if fs::write_str("history.txt", &text).is_ok() {
    let _ = fs::view("history.txt");
}
```

## TROPIC01 R-Memory

Module `rmem`. Capability `rmem: ["slot_name", ...]` (each name 1-15 chars), and
a `nvs_namespace` must also be declared. Source of truth:
`vendor/cdc-badge-plugins/sdk/cdc-badge-plugin/src/rmem.rs`. Retained secure
memory addressed by name; only names listed in `capabilities.rmem` are reachable.
The pool is **shared**: multiple plugins declaring the same name share one
physical slot by design.

```rust
pub const RMEM_NAME_MAX: usize = 15;

rmem::read(name: &str, max_len: usize) -> Result<Vec<u8>>;  // Err if unknown/empty/denied
rmem::write(name: &str, data: &[u8]) -> Result<()>;         // first write allocates a slot
rmem::erase(name: &str) -> Result<()>;
rmem::is_used(name: &str) -> bool;                          // false on a bad name
rmem::slot_size() -> u16;                                   // header + payload bytes
```

Unlike `nvs`/`fs`, reads return `Result` (not `Option`). A write that cannot
allocate from the plugin pool fails with `HOST_ERR_RMEM_FULL` (`Error::RmemFull`).

```rust
// illustrative (SDK rmem.rs)
rmem::write("token", &secret)?;
if rmem::is_used("token") {
    let secret = rmem::read("token", rmem::slot_size() as usize)?;
}
```
