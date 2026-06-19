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
import json
import os
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


def _is_real_cargo(path: str) -> bool:
    """True only if `path --version` reports cargo itself, not a proxy wrapper.

    Some installs leave a `cargo` on PATH that is really a rustup proxy (e.g.
    Homebrew's `~/.cargo/bin/cargo` execs `rustup "$@"`, so it prints `rustup ...`
    and breaks every cargo call). Such a binary must be ignored.
    """
    try:
        out = subprocess.run([path, "--version"], capture_output=True,
                             text=True, timeout=20)
    except (OSError, subprocess.SubprocessError):
        return False
    return out.returncode == 0 and out.stdout.strip().lower().startswith("cargo")


def cargo() -> str:
    exe = "cargo.exe" if sys.platform == "win32" else "cargo"
    # Ask rustup first: it returns the real toolchain binary, never a proxy shim.
    if shutil.which("rustup"):
        try:
            out = subprocess.run(["rustup", "which", "cargo"],
                                 capture_output=True, text=True, check=True)
            p = out.stdout.strip()
            if p and Path(p).exists() and _is_real_cargo(p):
                return p
        except (OSError, subprocess.SubprocessError):
            pass
    # Otherwise accept a PATH cargo, but only if it verifies as real cargo.
    on_path = shutil.which("cargo")
    if on_path and _is_real_cargo(on_path):
        return on_path
    return find("cargo", Path.home() / ".cargo" / "bin" / exe)


def rust_env() -> dict:
    """Environment for invoking cargo: put the toolchain's own bin dir on PATH so
    cargo can find its sibling rustc even when the ~/.cargo/bin shims are not set
    up (e.g. a Homebrew rustup). Works on every OS, no hardcoded paths."""
    env = os.environ.copy()
    cargo_dir = str(Path(cargo()).parent)
    env["PATH"] = cargo_dir + os.pathsep + env.get("PATH", "")
    return env


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
    # Copy the starter WITHOUT build artefacts, so the new plugin starts clean.
    shutil.copytree(STARTER, dst,
                    ignore=shutil.ignore_patterns("target", "Cargo.lock", "*.wasm"))
    # Rename the crate - targeted, not a blind replace that could hit substrings.
    cargo_toml = dst / "Cargo.toml"
    cargo_toml.write_text(cargo_toml.read_text().replace('name = "starter"', f'name = "{a.name}"'))
    lib_rs = dst / "src" / "lib.rs"
    if lib_rs.exists():
        lib_rs.write_text(lib_rs.read_text().replace('"starter"', f'"{a.name}"'))
    # Update the manifest id and display name as structured JSON (keeps "Starter"
    # from leaking into the on-device name).
    meta_path = dst / "meta.json"
    meta = json.loads(meta_path.read_text())
    meta["id"] = a.name
    names = meta.get("i18n", {}).get("meta", {}).get("name")
    if isinstance(names, dict):
        for lang in names:
            names[lang] = a.name
    meta_path.write_text(json.dumps(meta, indent=2) + "\n")
    print(f"created plugins/{a.name}")
    print(f"  - code:     plugins/{a.name}/src/lib.rs")
    print(f"  - manifest: plugins/{a.name}/meta.json  (set the name, description, author, icon and capabilities)")
    print(f"  then build it with: badge build {a.name}")


def cmd_build(a: argparse.Namespace) -> Path:
    """Compile to wasm and size-optimise with wasm-opt; return the artifact."""
    run = [cargo(), "build", "--release", "--target", WASM_TARGET,
           "--manifest-path", str(manifest(a.name))]
    print("   $", " ".join(run))
    subprocess.run(run, check=True, env=rust_env())
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
    subprocess.run(run, check=True, env=rust_env())


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
    # One clear completion line BEFORE the (blocking) monitor, so the job is
    # obviously finished even if the monitor is interrupted.
    done = "flashed and started" if a.start else "flashed"
    print(f"Plugin '{a.name}' {done}. Run 'badge monitor' to see its log.")
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
            # Let the line settle and drop any partial bytes already buffered, so
            # the first lines aren't garbled when attaching mid-transmission.
            time.sleep(0.2)
            ser.reset_input_buffer()
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


def cmd_info(a: argparse.Namespace) -> None:
    upload(["--info", a.name], a.pin, a.port)


def cmd_list(a: argparse.Namespace) -> None:
    upload(["--list"], a.pin, a.port)


def cmd_start(a: argparse.Namespace) -> None:
    upload(["--start", a.name], a.pin, a.port)
    print(f"-> start requested for '{a.name}' (see the badge reply above). "
          f"Run 'badge monitor' to watch its log.")


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
    # Accepted and ignored: monitor never authenticates, but the docs/agents may
    # pass --pin alongside other commands, so don't crash on it.
    sp.add_argument("--pin", help=argparse.SUPPRESS)
    sp.set_defaults(func=cmd_monitor)

    for verb, fn in (("info", cmd_info), ("list", cmd_list), ("start", cmd_start),
                     ("stop", cmd_stop), ("delete", cmd_delete)):
        sp = sub.add_parser(verb)
        if verb in ("info", "start", "delete"):
            sp.add_argument("name")
        with_serial(sp)
        sp.set_defaults(func=fn)
    return p


def reexec_in_venv() -> None:
    """Re-run under the project's .venv Python so pyserial (flash/monitor) is
    available no matter which Python invoked us."""
    venv_dir = ROOT / ".venv"
    py = venv_dir / ("Scripts" if sys.platform == "win32" else "bin") \
        / ("python.exe" if sys.platform == "win32" else "python")
    try:
        in_venv = Path(sys.prefix).resolve() == venv_dir.resolve()
    except OSError:
        in_venv = False
    if py.exists() and not in_venv:
        os.execv(str(py), [str(py), *sys.argv])


def main() -> None:
    reexec_in_venv()
    args = build_parser().parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
