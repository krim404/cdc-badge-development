#!/usr/bin/env python3
"""Snapshot test driver: run the emulator headlessly with a scripted key
sequence against reference frame hashes. Exits 77 (CTest SKIP_RETURN_CODE)
when the plugin .wasm has not been built. Replaces the earlier CMake-script
driver, whose skip mechanism needed CMake >= 3.29.

Usage: run_snapshot.py <emulator> <wasm> <meta> <keys> <snapshot-dir>
       <work-dir> [seconds]
"""

import shutil
import subprocess
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 7:
        print(__doc__)
        return 2
    emulator, wasm, meta, keys, snapshot, work = sys.argv[1:7]
    seconds = sys.argv[7] if len(sys.argv) > 7 else None

    if not Path(wasm).exists():
        print(f"SKIP: {wasm} not built (run: badge build <plugin>)")
        return 77

    shutil.rmtree(work, ignore_errors=True)
    cmd = [emulator, "--wasm", wasm, "--meta", meta, "--headless",
           "--keys", keys, "--snapshot", snapshot,
           "--data", str(Path(work) / "data")]
    if seconds:
        cmd += ["--seconds", seconds]
    rc = subprocess.run(cmd, timeout=300).returncode
    if rc != 0:
        print(f"snapshot run failed (exit {rc})")
        return 1
    print("snapshot run passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
