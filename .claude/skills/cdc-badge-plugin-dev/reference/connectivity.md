# Connectivity (networking & messaging)

Five families let a plugin reach the network and nearby badges: **wifi**, **http**,
**socket**, **ble**, **msg**. Each is gated by a manifest capability; see
`capabilities-and-lifecycle.md` for the full gating table and error model. Each
SDK module lives under `vendor/cdc-badge-plugins/sdk/cdc-badge-plugin/src/`; open
the `.rs` for the exact `host_*` C signatures. All text is UTF-8.

Layering: `http` and `socket` both run **over WiFi**, so WiFi must be up first.
WiFi may be absent on a given badge; bring it up (or declare the
`wifi_connected` prerequisite) before any HTTP/socket call. For copy-ready
patterns see `codebook.md`; for footguns see `pitfalls.md`.

## wifi (module `wifi`, capability `wifi`)

Usually not called directly: put `wifi_connected` in the manifest prerequisites
and the host brings the link up before `plugin_on_enter`. Use these only to drop
and re-acquire WiFi mid-session.

```rust
wifi::request(timeout_ms: u32) -> Result<()>;   // ask host to bring WiFi up
wifi::release();                                 // release the reservation
wifi::is_connected() -> bool;
wifi::ssid() -> Option<String>;                  // current network SSID
wifi::ip() -> Option<String>;                    // dotted-decimal IPv4
wifi::rssi() -> i8;                              // dBm
wifi::mac() -> Option<[u8; 6]>;                  // station MAC
wifi::start_scan() -> Result<()>;               // async scan
wifi::scan_done() -> bool;
wifi::scan_results(max: usize) -> Result<Vec<ScanResult>>;
// ScanResult { ssid: String, bssid: [u8; 6], rssi: i8, channel: u8, auth_mode: u8 }
```

```rust
// illustrative (SDK wifi.rs)
wifi::request(10_000)?;
if wifi::is_connected() {
    let here = wifi::ssid().unwrap_or_default();
    // ... use http/socket ...
}
wifi::release();
```

## http (module `http`, capability `http`)

Builder over the host's chunked transport. Build a `Request`, optionally set
headers and a body, `perform()` to send and get the status, then read the body.
The handle auto-closes on `Drop`. Runs over WiFi.

```rust
http::GET   = 0;  http::POST = 1;  http::PUT = 2;  http::DELETE = 3;

http::Request::open(method: u8, url: &str, timeout_ms: u32) -> Result<Request>;
req.header(key: &str, value: &str) -> Result<()>;
req.body(body: &[u8]) -> Result<()>;          // host adds Content-Length
req.perform() -> Result<i32>;                 // returns HTTP status code
req.content_length() -> usize;                // 0 if unknown/chunked
req.read_to_string() -> Result<String>;       // streams ~1 KiB chunks, UTF-8
```

Supported methods: `GET`, `POST`, `PUT`, `DELETE`. See `examples/news_feed` and
`plugins/home_assistant` for real HTTP usage.

```rust
// illustrative (SDK http.rs)
let req = http::Request::open(http::GET, "https://example.com/feed.json", 8000)?;
req.header("Accept", "application/json")?;
let status = req.perform()?;
if status == 200 {
    let body = req.read_to_string()?;
}   // handle closed on drop
```

## socket (module `socket`, capability `socket`)

Transport-level TCP/UDP client sockets; build your own framing on top (e.g.
MQTT). Both are connected to one remote endpoint; a UDP socket fixes its peer at
connect time. Handles auto-close on `Drop`. Requires an active network.

```rust
socket::TcpStream::connect(host: &str, port: u16, timeout_ms: u32) -> Result<TcpStream>;
tcp.write(data: &[u8], timeout_ms: u32) -> Result<usize>;
tcp.read(out: &mut [u8], timeout_ms: u32) -> Result<usize>;   // 0 = EOF

socket::UdpSocket::connect(host: &str, port: u16, timeout_ms: u32) -> Result<UdpSocket>;
udp.write(data: &[u8], timeout_ms: u32) -> Result<usize>;
udp.read(out: &mut [u8], timeout_ms: u32) -> Result<usize>;
```

`examples/gpio_mqtt` builds a protocol on top of `TcpStream`.

```rust
// illustrative (SDK socket.rs)
let s = socket::TcpStream::connect("10.0.0.5", 1883, 5000)?;
s.write(b"hello", 2000)?;
let mut buf = [0u8; 256];
let n = s.read(&mut buf, 2000)?;   // socket closed on drop
```

## ble (module `ble`, capability `ble`)

GATT **peripheral** (publish one service) and **central** (scan, connect, talk to
a peer). Plugins use this API only, never NimBLE directly. Inbound events
(writes, reads, notifications, discovery results) fire the `action_id` the plugin
passed; the `plugin_on_action` handler then pulls the payload with the matching
`consume_*`. Only one BLE connection exists at a time (central and peripheral
share it). Served service UUIDs must be declared in the manifest's
`ble_service_uuids`. GATT limits: `MAX_CHARS_PER_SERVICE = 6`,
`MAX_REGISTERED_SERVICES = 7`. Reserved system UUIDs (FIDO, HID, vCard, Nordic
UART, GPG, the SIG 16-bit range) are refused.

State:

```rust
ble::is_enabled() -> bool;
ble::mac() -> Option<[u8; 6]>;
ble::device_name() -> Option<String>;
ble::rssi() -> i8;                              // dBm, 0 when idle
```

Property flags: `PROP_READ` 0x02, `PROP_WRITE_NO_RSP` 0x04, `PROP_WRITE` 0x08,
`PROP_NOTIFY` 0x10, `PROP_INDICATE` 0x20.

Peripheral (GATT server):

```rust
ble::CharDef::new(uuid: [u8; 16], properties: u8, write_action_id: u32) -> CharDef;
ble::register_service(uuid: [u8; 16], chars: &mut [CharDef]) -> Result<u32>; // 1-6 chars
ble::unregister_service(service_handle: u32) -> Result<()>;
ble::notify(char_handle: u32, data: &[u8]) -> Result<()>;
ble::indicate(char_handle: u32, data: &[u8]) -> Result<()>;
ble::consume_write(char_handle: u32, buf: &mut [u8]) -> Result<usize>;
```

`register_service` fills each `chars[i].char_handle`. A write fires the
characteristic's `write_action_id` with `idx` = char handle, `user_data` =
connection handle; pull it with `consume_write`.

Central:

```rust
ble::scan_start(duration_ms: u32) -> Result<()>;
ble::scan_done() -> bool;
ble::scan_results(max: usize) -> Result<Vec<ScanResult>>;
// ScanResult { addr: [u8; 6], addr_type: u8, rssi: i8, name: String }
ble::connect(addr: [u8; 6], addr_type: u8) -> Result<()>;   // 0 public, 1 random
ble::conn_handle() -> u32;                                   // 0 when idle
ble::disconnect(conn: u32) -> Result<()>;
ble::discover(conn: u32, uuid: [u8; 16], action_id: u32) -> Result<()>;
ble::consume_discovery(max: usize) -> Result<Vec<RemoteChar>>;
// RemoteChar { uuid: [u8; 16], value_handle: u16, properties: u8 }
ble::read_char(conn: u32, value_handle: u16, action_id: u32) -> Result<()>;
ble::consume_read(buf: &mut [u8]) -> Result<usize>;
ble::write_char(conn: u32, value_handle: u16, data: &[u8], with_response: bool) -> Result<()>;
ble::subscribe(conn: u32, cccd_handle: u16, action_id: u32) -> Result<()>;
ble::consume_notification(buf: &mut [u8]) -> Result<Option<(u16, usize)>>;  // (value_handle, len)
```

`connect` completion arrives as a `BLE_CONNECTED` event; read it with
`conn_handle`. See `plugins/ble_scanner` for central scanning.

```rust
// illustrative (SDK ble.rs) - peripheral
let mut chars = [ble::CharDef::new(my_uuid, ble::PROP_NOTIFY | ble::PROP_WRITE, ACT_WRITE)];
let svc = ble::register_service(svc_uuid, &mut chars)?;
ble::notify(chars[0].char_handle, b"hi")?;
```

## msg (module `msg`, capability `ble` + non-empty `message_types`)

Badge-to-badge MIME-typed transfer with ephemeral numeric-comparison pairing.
Register the MIME types you receive; a completed inbound transfer (after the local
user consented) fires the registered `action_id`, then pull the bytes with
`consume`. To send, hand a typed payload to the interactive senders, which open
the firmware-owned peer picker and consent/progress UI. Payload bytes are opaque;
text MIME types are UTF-8.

Constants: `PAYLOAD_MAX = 4096`, `MIME_MAX = 64`, `FLAG_PERSIST = 0x01`
(remember the verified pairing for the session; dropped on reboot).

```rust
msg::register_handler(mime_type: &str, action_id: u32) -> Result<()>;
msg::unregister_handler(mime_type: &str) -> Result<()>;
msg::consume(max_len: usize) -> Option<Received>;          // Received { mime: String, data: Vec<u8> }
msg::consume_text(max_len: usize) -> Option<(String, String)>;  // (mime, text)

msg::send_interactive(mime_type: &str, data: &[u8]) -> Result<()>;
msg::send_interactive_with(mime_type: &str, data: &[u8], flags: u32) -> Result<()>;
msg::send_text_interactive(text: &str) -> Result<()>;       // sends text/plain
msg::send_text_interactive_with(text: &str, flags: u32) -> Result<()>;
msg::send(addr: [u8; 6], addr_type: u8, mime_type: &str, data: &[u8]) -> Result<()>;
msg::send_with(addr: [u8; 6], addr_type: u8, mime_type: &str, data: &[u8], flags: u32) -> Result<()>;
```

`examples/mini_messenger` is the real reference for `msg`.

```rust
// illustrative (SDK msg.rs)
msg::register_handler("text/plain", ACT_RECV)?;
// in plugin_on_action(ACT_RECV, ...):
if let Some((mime, text)) = msg::consume_text(msg::PAYLOAD_MAX) { /* ... */ }
// sending:
msg::send_text_interactive_with("ping", msg::FLAG_PERSIST)?;
```
