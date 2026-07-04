#!/usr/bin/env python3
"""Run the whole vendored plugin example set headlessly (T066, SC-007/SC-003).

PASS criteria per plugin: the emulator process exits on its own with a defined
exit code and never crashes (no signal, no timeout). A plugin that *chooses*
to abort in plugin_init because a declared capability is unavailable
off-device (grove_blink, gpio_mqtt) is a correct, honest run - the emulator
kept its contract.

Usage: python emulator/tests/run_examples.py [--offline]
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BADGE = ROOT / "tools" / "badge.py"

# name -> scripted input that exercises it a little.
EXAMPLES = {
    "hello_world": ["--keys", "Y", "--seconds", "1"],
    "canvas_demo": ["--keys", "Y,Y,Y,Y,N", "--seconds", "1"],
    "battery_widget": ["--seconds", "2"],
    "sci_calc": ["--keys", "1,2,3", "--seconds", "1"],
    "mini_messenger": ["--keys", "Y", "--seconds", "1"],
    "grove_blink": ["--seconds", "1"],   # aborts honestly: gpio unavailable
    "gpio_mqtt": ["--seconds", "1"],     # aborts honestly: gpio unavailable
    "news_feed": ["--offline", "--seconds", "2"],  # offline: deterministic error path
}


def main() -> int:
    extra = sys.argv[1:]
    failures: list[str] = []
    for name, args in EXAMPLES.items():
        cmd = [sys.executable, str(BADGE), "emulate", name, "--headless",
               "--data", str(ROOT / "build" / f"emu-example-{name}"), *args, *extra]
        print(f"=== {name} ===", flush=True)
        try:
            proc = subprocess.run(cmd, timeout=300)
        except subprocess.TimeoutExpired:
            failures.append(f"{name}: TIMEOUT (emulator hung)")
            continue
        if proc.returncode < 0:
            failures.append(f"{name}: CRASHED with signal {-proc.returncode}")
        elif proc.returncode not in (0, 2, 3, 4):
            failures.append(f"{name}: unexpected exit code {proc.returncode}")
        else:
            print(f"    ok (exit {proc.returncode})")
    if failures:
        print("\nFAILURES:")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print(f"\nall {len(EXAMPLES)} examples ran without an emulator crash")
    return 0


if __name__ == "__main__":
    sys.exit(main())
