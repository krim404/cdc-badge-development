#!/usr/bin/env python3
"""One-shot, cross-platform bootstrap for the CDC Badge plugin toolchain.

Run it the SAME way on macOS, Linux and Windows:

    python scripts/setup.py

It is deliberately written in Python (not a shell or PowerShell script) so a
single, testable codebase provisions every operating system. It is idempotent:
running it again only fills in what is missing.

What it does:
  1. Initialise the git submodules under vendor/ (the firmware + plugin SDK).
  2. Make sure rustup + the wasm32-unknown-unknown target are present.
  3. Make sure Binaryen's `wasm-opt` is available (downloads a pinned build if not).
  4. Create a local Python virtual environment (.venv) and install pyserial.
  5. Keep the assistant skill in sync between .claude/ and .agents/.

It installs nothing system-wide that a normal user setup would not: rustup is
per-user, the Python packages live in the project-local .venv, and wasm-opt is
dropped into the ignored .tools/ folder. Nothing here needs PowerShell.
"""

from __future__ import annotations

import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

# Pinned versions. wasm-opt matches the version the plugin CI builds against.
BINARYEN_VERSION = "version_119"
ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = ROOT / ".tools"
VENV_DIR = ROOT / ".venv"


def step(msg: str) -> None:
    """Print a visible progress line."""
    print(f"\n==> {msg}")


def run(cmd: list[str], **kw) -> subprocess.CompletedProcess:
    """Run a command, echoing it first so the student sees what happens."""
    print("   $", " ".join(cmd))
    return subprocess.run(cmd, check=True, **kw)


def have(tool: str) -> bool:
    """Return True if an executable is on PATH."""
    return shutil.which(tool) is not None


# --------------------------------------------------------------------------- #
# 1. Submodules                                                               #
# --------------------------------------------------------------------------- #
def init_submodules() -> None:
    step("Initialising vendored knowledge (git submodules)")
    # First level only: we need the SDK + firmware docs/tools, NOT the firmware's
    # heavy nested submodules (WAMR, libtropic, ...), some of which are private
    # or have very long paths that break Windows checkouts.
    run(["git", "submodule", "update", "--init",
         "vendor/cdc-badge-os", "vendor/cdc-badge-plugins"], cwd=str(ROOT))


# --------------------------------------------------------------------------- #
# 2. Rust toolchain                                                           #
# --------------------------------------------------------------------------- #
def ensure_rust() -> None:
    step("Checking the Rust toolchain")
    if not have("rustup"):
        # rustup is the official, per-user Rust installer and works on every OS.
        # On Windows the student installs it from EASY.md; here we only auto-install
        # on Unix where the one-liner is safe and non-interactive.
        if os.name == "nt":
            sys.exit("rustup is not installed. Install Rust from https://rustup.rs "
                     "(see EASY.md), then re-run this script.")
        run(["sh", "-c",
             "curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y"])
        # rustup drops cargo/rustup into ~/.cargo/bin for this session.
        os.environ["PATH"] = str(Path.home() / ".cargo" / "bin") + os.pathsep + os.environ["PATH"]
    # The pinned channel comes from vendor/cdc-badge-plugins/rust-toolchain.toml,
    # which rustup honours automatically inside a plugin directory. We only have
    # to make sure the WebAssembly target is installed.
    run(["rustup", "target", "add", "wasm32-unknown-unknown"])


# --------------------------------------------------------------------------- #
# 3. wasm-opt (Binaryen)                                                      #
# --------------------------------------------------------------------------- #
def binaryen_assets() -> list[str]:
    """Candidate Binaryen release asset names for this OS + CPU.

    Not every CPU has a dedicated asset for every release, so we list the
    preferred one first and fall back to the x86_64 build (which runs fine
    under Rosetta on Apple Silicon, for example).
    """
    sysname = platform.system()
    arm = platform.machine().lower() in ("arm64", "aarch64")
    v = BINARYEN_VERSION
    if sysname == "Darwin":
        return ([f"binaryen-{v}-arm64-macos.tar.gz"] if arm else []) + [f"binaryen-{v}-x86_64-macos.tar.gz"]
    if sysname == "Windows":
        return [f"binaryen-{v}-x86_64-windows.tar.gz"]
    return ([f"binaryen-{v}-aarch64-linux.tar.gz"] if arm else []) + [f"binaryen-{v}-x86_64-linux.tar.gz"]


def ensure_wasm_opt() -> None:
    step("Checking wasm-opt (Binaryen)")
    local = TOOLS_DIR / f"binaryen-{BINARYEN_VERSION}" / "bin" / ("wasm-opt.exe" if os.name == "nt" else "wasm-opt")
    if have("wasm-opt") or local.exists():
        print("   wasm-opt already available")
        return
    TOOLS_DIR.mkdir(exist_ok=True)
    base = f"https://github.com/WebAssembly/binaryen/releases/download/{BINARYEN_VERSION}"
    errors = []
    for asset in binaryen_assets():
        url = f"{base}/{asset}"
        print(f"   downloading {url}")
        # Use a temp DIRECTORY (not a held-open temp file): on Windows an open
        # file handle blocks reopening/deleting the same path (WinError 32).
        tmpdir = tempfile.mkdtemp()
        archive = os.path.join(tmpdir, "binaryen.tar.gz")
        try:
            urllib.request.urlretrieve(url, archive)
            with tarfile.open(archive) as tar:
                tar.extractall(TOOLS_DIR)
            print(f"   installed to {local}")
            return
        except Exception as exc:  # noqa: BLE001 - try the next candidate
            errors.append(f"{asset}: {exc}")
        finally:
            shutil.rmtree(tmpdir, ignore_errors=True)
    sys.exit("Could not fetch wasm-opt automatically:\n   " + "\n   ".join(errors) +
             f"\nInstall Binaryen {BINARYEN_VERSION} manually and put wasm-opt on PATH.")


# --------------------------------------------------------------------------- #
# 4. Python venv + host tooling                                               #
# --------------------------------------------------------------------------- #
def ensure_venv() -> None:
    step("Creating the Python virtual environment (.venv) for the badge tools")
    if not VENV_DIR.exists():
        run([sys.executable, "-m", "venv", str(VENV_DIR)])
    # Use `python -m pip` (not pip.exe): on Windows pip cannot replace its own
    # running executable, so calling pip.exe directly is fragile.
    py = VENV_DIR / ("Scripts" if os.name == "nt" else "bin") / ("python.exe" if os.name == "nt" else "python")
    # pyserial drives the upload/flash and the serial monitor (matches the
    # firmware repo's tools/requirements.txt pin).
    run([str(py), "-m", "pip", "install", "--quiet", "pyserial>=3.5"])


# --------------------------------------------------------------------------- #
# 5. Keep the assistant skill mirrored for every agent                        #
# --------------------------------------------------------------------------- #
def sync_skill() -> None:
    step("Mirroring the assistant skill for Claude Code / Codex / opencode")
    src = ROOT / ".claude" / "skills" / "cdc-badge-plugin-dev"
    dst = ROOT / ".agents" / "skills" / "cdc-badge-plugin-dev"
    if not src.exists():
        print("   (skill not present yet - skipping)")
        return
    # opencode reads both .claude/skills and .agents/skills; Codex reads
    # .agents/skills; Claude reads .claude/skills. One source, copied to the
    # second location so all three agents see an identical skill.
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    print(f"   {src.relative_to(ROOT)} -> {dst.relative_to(ROOT)}")


def main() -> None:
    print("CDC Badge development environment - host-native setup")
    print(f"Repository: {ROOT}")
    init_submodules()
    ensure_rust()
    ensure_wasm_opt()
    ensure_venv()
    sync_skill()
    step("Done. Next: open a plugin and run  python tools/badge.py build starter")
    print("   (see EASY.md for the full step-by-step walkthrough)")


if __name__ == "__main__":
    main()
