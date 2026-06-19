#!/usr/bin/env python3
"""The `badge` command: one automated entry point for the whole plugin loop.

    python tools/badge.py new   <name>     # create a new, test-ready plugin
    python tools/badge.py build <name>     # compile to an optimised .wasm
    python tools/badge.py test  <name>     # run host-side unit tests (TDD)
    python tools/badge.py flash <name>     # build + upload to a connected badge
    python tools/badge.py monitor          # stream the badge's serial log
    python tools/badge.py list|start|stop|delete ...

Design notes for the curious student:
  * It is pure Python so it behaves the same on macOS, Linux and Windows.
  * Flashing and the serial monitor talk to the badge over USB-CDC at 115200.
    They reuse the firmware's well-tested `vendor/cdc-badge-os/tools/upload.py`
    instead of re-implementing the upload protocol.
  * Run it with the project virtual environment's Python (the VS Code tasks do
    this for you) so `pyserial` is available.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLUGINS = ROOT / "plugins"
DIST = ROOT / "dist"
STARTER = PLUGINS / "starter"
UPLOAD_PY = ROOT / "vendor" / "cdc-badge-os" / "tools" / "upload.py"
WASM_TARGET = "wasm32-unknown-unknown"
WASM_OPT_FLAGS = ["-Oz", "--enable-bulk-memory", "--enable-nontrapping-float-to-int"]
BINARYEN_VERSION = "version_119"
BAUD = 115200


def fail(msg: str) -> "NoReturn":  # type: ignore[name-defined]
    """Print a clear error and exit non-zero (so CI and agents notice)."""
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def find(tool: str, *fallbacks: Path) -> str:
    """Locate an executable on PATH, or at a known fallback path."""
    found = shutil.which(tool)
    if found:
        return found
    for fb in fallbacks:
        if fb.exists():
            return str(fb)
    fail(f"could not find {tool!r}. Run: python scripts/setup.py")


def cargo() -> str:
    exe = "cargo.exe" if sys.platform == "win32" else "cargo"
    on_path = shutil.which("cargo")
    if on_path:
        return on_path
    # A rustup-managed toolchain may not expose a cargo shim on PATH; ask rustup.
    try:
        out = subprocess.run(["rustup", "which", "cargo"],
                             capture_output=True, text=True, check=True)
        p = out.stdout.strip()
        if p and Path(p).exists():
            return p
    except Exception:  # noqa: BLE001 - fall through to the explicit fallback
        pass
    return find("cargo", Path.home() / ".cargo" / "bin" / exe)


def wasm_opt() -> str:
    exe = "wasm-opt.exe" if sys.platform == "win32" else "wasm-opt"
    local = ROOT / ".tools" / f"binaryen-{BINARYEN_VERSION}" / "bin" / exe
    return find("wasm-opt", local)


def manifest(name: str) -> Path:
    p = PLUGINS / name / "Cargo.toml"
    if not p.exists():
        fail(f"no plugin {name!r} in plugins/. Create one with: badge new {name}")
    return p


def upload(args: list[str], pin: str | None, port: str | None) -> None:
    """Invoke the firmware's upload.py with the current Python (has pyserial)."""
    if not UPLOAD_PY.exists():
        fail("vendor submodules missing. Run: python scripts/setup.py")
    cmd = [sys.executable, str(UPLOAD_PY), *args]
    if pin:
        cmd += ["--pin", pin]
    if port:
        cmd += ["--port", port]
    print("   $", " ".join(cmd))
    subprocess.run(cmd, check=True)


# --------------------------------------------------------------------------- #
# Commands                                                                    #
# --------------------------------------------------------------------------- #
def cmd_new(a: argparse.Namespace) -> None:
    """Copy the test-ready starter so every new plugin ships with a test."""
    dst = PLUGINS / a.name
    if dst.exists():
        fail(f"plugins/{a.name} already exists")
    if not STARTER.exists():
        fail("plugins/starter is missing (did the checkout complete?)")
    shutil.copytree(STARTER, dst)
    # Rename the crate and the manifest id from 'starter' to the chosen name.
    cargo_toml = dst / "Cargo.toml"
    cargo_toml.write_text(cargo_toml.read_text().replace("starter", a.name))
    meta = dst / "meta.json"
    meta.write_text(meta.read_text().replace("starter", a.name))
    print(f"created plugins/{a.name} (build it with: badge build {a.name})")


def cmd_build(a: argparse.Namespace) -> Path:
    """Compile to wasm and size-optimise with wasm-opt; return the artifact."""
    run = [cargo(), "build", "--release", "--target", WASM_TARGET,
           "--manifest-path", str(manifest(a.name))]
    print("   $", " ".join(run))
    subprocess.run(run, check=True)
    # A standalone plugin (no cargo workspace) puts its build output under its
    # own directory, next to its Cargo.toml - not the repo root.
    raw = PLUGINS / a.name / "target" / WASM_TARGET / "release" / f"{a.name}.wasm"
    if not raw.exists():
        fail(f"expected {raw} but it was not produced")
    DIST.mkdir(exist_ok=True)
    out = DIST / f"{a.name}.wasm"
    opt = [wasm_opt(), *WASM_OPT_FLAGS, str(raw), "-o", str(out)]
    print("   $", " ".join(opt))
    subprocess.run(opt, check=True)
    print(f"built {out}")
    return out


def cmd_test(a: argparse.Namespace) -> None:
    """Run the plugin's host-side unit tests (the TDD loop)."""
    run = [cargo(), "test", "--manifest-path", str(manifest(a.name))]
    print("   $", " ".join(run))
    subprocess.run(run, check=True)


def cmd_flash(a: argparse.Namespace) -> None:
    """Build if needed, upload to the badge, optionally start and watch logs."""
    wasm = DIST / f"{a.name}.wasm"
    if not wasm.exists() or a.rebuild:
        cmd_build(a)
    meta = PLUGINS / a.name / "meta.json"
    upload(["--wasm", str(wasm), "--meta", str(meta)], a.pin, a.port)
    if a.start:
        # --start is a separate upload.py invocation (its own argument group).
        upload(["--start", a.name], a.pin, a.port)
    if a.monitor:
        _monitor(a.port, seconds=None, until=None)


def _monitor(port: str | None, seconds: int | None, until: str | None) -> None:
    """Stream decoded serial lines until interrupted / timed out / matched."""
    try:
        import serial  # pyserial, installed in .venv by setup.py
        from serial.tools import list_ports
    except ImportError:
        fail("pyserial missing. Run: python scripts/setup.py "
             "(and use the .venv Python to run this command)")
    if not port:
        candidates = [p.device for p in list_ports.comports()
                      if "usbmodem" in p.device or "ACM" in p.device
                      or "USB" in p.device or p.device.upper().startswith("COM")]
        if not candidates:
            fail("no serial port found - is the badge connected? Pass --port to override.")
        port = candidates[0]
    print(f"monitoring {port} at {BAUD} baud (Ctrl-C to stop)")
    try:
        with serial.Serial(port, BAUD, timeout=1) as ser:
            import time
            start = time.monotonic()
            while True:
                line = ser.readline().decode(errors="replace").rstrip()
                if line:
                    print(line)
                if until and until in line:
                    return
                if seconds and (time.monotonic() - start) > seconds:
                    return
    except KeyboardInterrupt:
        print("\nstopped")
    except serial.SerialException as exc:
        fail(f"serial port busy or unavailable: {exc}. "
             "Close any other monitor (only one program can hold the port).")


def cmd_monitor(a: argparse.Namespace) -> None:
    _monitor(a.port, a.seconds, a.until)


def cmd_list(a: argparse.Namespace) -> None:
    upload(["--list"], a.pin, a.port)


def cmd_start(a: argparse.Namespace) -> None:
    upload(["--start", a.name], a.pin, a.port)


def cmd_stop(a: argparse.Namespace) -> None:
    upload(["--stop"], a.pin, a.port)


def cmd_delete(a: argparse.Namespace) -> None:
    upload(["--delete", a.name], a.pin, a.port)


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="badge", description="CDC Badge plugin toolbelt")
    sub = p.add_subparsers(dest="cmd", required=True)

    def with_serial(sp: argparse.ArgumentParser) -> None:
        sp.add_argument("--port", help="serial port (auto-detected if omitted)")
        sp.add_argument("--pin", help="badge PIN for AUTH, if required")

    sp = sub.add_parser("new", help="create a new test-ready plugin")
    sp.add_argument("name")
    sp.set_defaults(func=cmd_new)

    sp = sub.add_parser("build", help="compile a plugin to an optimised .wasm")
    sp.add_argument("name")
    sp.set_defaults(func=cmd_build)

    sp = sub.add_parser("test", help="run a plugin's host-side unit tests")
    sp.add_argument("name")
    sp.set_defaults(func=cmd_test)

    sp = sub.add_parser("flash", help="build + upload a plugin to the badge")
    sp.add_argument("name")
    sp.add_argument("--start", action="store_true", help="start the plugin after upload")
    sp.add_argument("--monitor", action="store_true", help="stream logs after flashing")
    sp.add_argument("--rebuild", action="store_true", help="force a rebuild first")
    with_serial(sp)
    sp.set_defaults(func=cmd_flash)

    sp = sub.add_parser("monitor", help="stream the badge serial log")
    sp.add_argument("--seconds", type=int, help="stop after N seconds (for scripts/agents)")
    sp.add_argument("--until", help="stop when a line contains this text")
    sp.add_argument("--port", help="serial port (auto-detected if omitted)")
    sp.set_defaults(func=cmd_monitor)

    for verb, fn in (("list", cmd_list), ("start", cmd_start),
                     ("stop", cmd_stop), ("delete", cmd_delete)):
        sp = sub.add_parser(verb)
        if verb in ("start", "delete"):
            sp.add_argument("name")
        with_serial(sp)
        sp.set_defaults(func=fn)
    return p


def main() -> None:
    args = build_parser().parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
