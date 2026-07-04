#!/usr/bin/env python3
"""Badge-to-badge message tests (JSON packet link).

Roundtrip: instance A (mini_messenger) sends a message through the real
host_api_msg send path - the emulator writes it as a JSON packet under
<data>/msg/out/. That very file is then injected into instance B via
--msg-in, whose live handler renders the "New message" view (a new frame).

Wake-up: with the plugin locked out of the foreground (unloaded), injecting
a packet must load it headless (activateForMessageType) and deliver.

Usage: test_msg.py <emulator-binary>   (exit 77 = skip: wasm missing)
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WASM = ROOT / "dist" / "mini_messenger.wasm"
META = (ROOT / "vendor" / "cdc-badge-plugins" / "examples" / "mini_messenger" /
        "meta.json")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_msg.py <emulator-binary>")
        return 2
    binary = sys.argv[1]
    if not WASM.exists():
        print("SKIP: dist/mini_messenger.wasm not built")
        return 77

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)

        # --- Sender: drive the Send flow through the console (Send -> T9
        # paste -> confirm), which must write an outbound JSON packet.
        sender_data = work / "sender"
        proc = subprocess.run(
            [binary, "--wasm", str(WASM), "--meta", str(META), "--headless",
             "--repl", "--data", str(sender_data)],
            input="key Y\npaste roundtrip works\nkey Y\nadvance 200\nquit\n",
            capture_output=True, text=True, timeout=120)
        assert proc.returncode == 0, proc.stderr[-800:]
        outbox = sorted((sender_data / "msg" / "out").glob("*.json"))
        assert outbox, "send flow wrote no outbound packet"
        packet = outbox[0]
        print(f"sender ok: {packet.name}")

        # --- Receiver: inject that exact packet; the handler must fire and
        # render the message view (one frame beyond the main list).
        recv = work / "recv"
        proc = subprocess.run(
            [binary, "--wasm", str(WASM), "--meta", str(META), "--headless",
             "--msg-in", str(packet), "--seconds", "1",
             "--frames", str(recv / "frames"), "--data", str(recv / "data")],
            capture_output=True, text=True, timeout=120)
        assert proc.returncode == 0, proc.stderr[-800:]
        hashes = [line.split()[1] for line in
                  (recv / "frames" / "frames.txt").read_text().splitlines()]
        assert len(hashes) >= 2 and hashes[1] != hashes[0], \
            f"no message view rendered: {hashes}"
        print("receiver ok: injected packet rendered a new frame")

        # --- Wake-up: locked (plugin unloaded) + injection loads it headless.
        wake = work / "wake"
        proc = subprocess.run(
            [binary, "--wasm", str(WASM), "--meta", str(META), "--headless",
             "--repl", "--data", str(wake)],
            input=(f"lock\nmsg-in {packet}\nadvance 200\nquit\n"),
            capture_output=True, text=True, timeout=120)
        assert proc.returncode == 0, proc.stderr[-800:]
        assert "packet delivered" in proc.stdout, proc.stdout
        assert proc.stderr.count("msgr: init") == 2, \
            "expected a second headless init after the wake-up injection"
        print("wake-up ok: packet loaded the unloaded handler plugin headless")

    return 0


if __name__ == "__main__":
    sys.exit(main())
