#!/usr/bin/env python3
"""Console `paste` encoding test (UTF-8 -> CP437).

The host terminal delivers console lines as UTF-8, but the T9 editor buffer
and the display pipeline are CP437. The firmware's serial reader converts
incoming UTF-8 to CP437 before dispatching commands (SerialCmd), so the
emulator's `paste` must do the same: "äöü" is 6 UTF-8 bytes but must land as
3 CP437 bytes in the editor.

Usage: test_paste.py <emulator-binary>   (exit 77 = skip: wasm missing)
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
        print("usage: test_paste.py <emulator-binary>")
        return 2
    binary = sys.argv[1]
    if not WASM.exists():
        print("SKIP: dist/mini_messenger.wasm not built")
        return 77

    with tempfile.TemporaryDirectory() as tmp:
        # key Y opens the Send flow's T9 editor; each umlaut must be converted
        # to its single CP437 byte (ä=0x84, ö=0x94, ü=0x81), not appended as a
        # raw two-byte UTF-8 sequence.
        proc = subprocess.run(
            [binary, "--wasm", str(WASM), "--meta", str(META), "--headless",
             "--repl", "--data", str(Path(tmp) / "data")],
            input="key Y\npaste äöü\nquit\n",
            capture_output=True, text=True, timeout=120)
        assert proc.returncode == 0, proc.stderr[-800:]
        assert "pasted 3 byte(s)" in proc.stdout, \
            f"umlauts not converted to CP437: {proc.stdout}"
        # stdout is machine-readable frontend output; the CalEPD driver's
        # banner/STATS printf noise must stay off it (debug log only).
        assert "STATS" not in proc.stdout and "CalEPD" not in proc.stdout, \
            f"CalEPD driver noise leaked to stdout: {proc.stdout}"
        print("paste ok: 3 UTF-8 umlauts landed as 3 CP437 bytes")

    return 0


if __name__ == "__main__":
    sys.exit(main())
