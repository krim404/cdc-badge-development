#!/usr/bin/env python3
"""Lockscreen residency tests (task: fake lockscreen).

Path A - foreground-only plugin (canvas_demo): leaving the plugin (N pops its
last view onto the root lockscreen) must run the FULL unload cycle, and
unlocking must reload from scratch - so the frame after unlock is
byte-identical to the very first frame.

Path B - background plugin (bg_probe fixture): locking keeps the instance
resident and ticking (NVS tick counter grows while locked), the lockscreen
quick-action reaches the resident plugin, and unlocking re-enters WITHOUT a
second plugin_init.

Usage: test_lockscreen.py <emulator-binary>   (exit 77 = skip: wasm missing)
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DIST = ROOT / "dist"
FIXTURE = Path(__file__).resolve().parent / "fixtures" / "bg_probe"


def build_bg_probe() -> Path:
    """Build the fixture with the badge CLI's own build helper."""
    wasm = DIST / "bg_probe.wasm"
    if wasm.exists():
        return wasm
    sys.path.insert(0, str(ROOT / "tools"))
    import badge  # noqa: E402  (the repo's CLI, reused as a library)

    return badge.build_wasm(FIXTURE, "bg_probe")


def run(binary: str, wasm: Path, meta: Path, keys: str, seconds: int,
        work: Path) -> tuple[list[str], Path]:
    frames_dir = work / "frames"
    data_dir = work / "data"
    proc = subprocess.run(
        [binary, "--wasm", str(wasm), "--meta", str(meta), "--headless",
         "--keys", keys, "--seconds", str(seconds),
         "--frames", str(frames_dir), "--data", str(data_dir)],
        capture_output=True, text=True, timeout=120)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        raise SystemExit(f"emulator exited {proc.returncode}")
    manifest = frames_dir / "frames.txt"
    hashes = [line.split()[1] for line in manifest.read_text().splitlines()]
    return hashes, data_dir


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_lockscreen.py <emulator-binary>")
        return 2
    binary = sys.argv[1]

    canvas_wasm = DIST / "canvas_demo.wasm"
    canvas_meta = (ROOT / "vendor" / "cdc-badge-plugins" / "examples" /
                   "canvas_demo" / "meta.json")
    if not canvas_wasm.exists():
        print("SKIP: dist/canvas_demo.wasm not built")
        return 77

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)

        # Path A: page 2 -> pop out (full unload) -> unlock reloads fresh.
        hashes, _ = run(binary, canvas_wasm, canvas_meta, "Y,N,Y", 0,
                        work / "a")
        assert len(hashes) == 4, f"expected 4 frames, got {len(hashes)}"
        assert hashes[3] == hashes[0], (
            "frame after unlock-reload must equal the first frame "
            f"({hashes[3]} != {hashes[0]})")
        assert hashes[2] not in (hashes[0], hashes[1]), \
            "lockscreen frame missing"
        print("path A ok: pop-out unloads, unlock reloads pixel-identically")

        # Path B: background resident keeps ticking while locked; the
        # quick-action reaches it; unlock re-enters without a second init.
        bg_wasm = build_bg_probe()
        _, data = run(binary, bg_wasm, FIXTURE / "meta.json", "L,1,U", 2,
                      work / "b")
        state = {
            key: entry["value"]
            for key, entry in json.loads(
                (data / "nvs" / "plg_bg_probe.json").read_text()).items()
        }
        assert state.get("inits") == 1, f"re-init happened: {state}"
        assert state.get("ticks", 0) > 10, f"no background ticks: {state}"
        assert state.get("quick") == 1, f"quick action not delivered: {state}"
        print(f"path B ok: background resident ticked while locked ({state})")

    return 0


if __name__ == "__main__":
    sys.exit(main())
