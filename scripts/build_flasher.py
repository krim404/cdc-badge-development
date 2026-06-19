#!/usr/bin/env python3
"""Build a personal web flasher site from this repo's plugins.

Discovers every plugin under `plugins/` EXCEPT the `starter` test plugin (the one
that only proves the repo builds), compiles each to wasm, and stages a
self-contained web flasher into `_site/`: the WebSerial installer reused from the
vendored plugin repo plus a `catalog.json` with relative URLs. A GitHub Actions
workflow deploys `_site/` to GitHub Pages, giving the developer a shareable link
that installs their own plugins onto a CDC Badge straight from the browser.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLUGINS = ROOT / "plugins"
DIST = ROOT / "dist"
SITE = ROOT / "_site"
WEBFLASHER = ROOT / "vendor" / "cdc-badge-plugins" / "webflasher"
# The starter is the build-smoke-test plugin; never offer it in the flasher.
EXCLUDE = {"starter"}


def english(field) -> str:
    """Pick the English string from an i18n field (dict) or pass a plain value."""
    if isinstance(field, dict):
        return field.get("en") or next(iter(field.values()), "")
    return str(field)


def discover() -> list[Path]:
    """Plugin directories to offer: have a manifest + crate, and aren't excluded."""
    if not PLUGINS.is_dir():
        return []
    return sorted(
        d for d in PLUGINS.iterdir()
        if d.is_dir() and d.name not in EXCLUDE
        and (d / "meta.json").is_file() and (d / "Cargo.toml").is_file()
    )


def stage_webflasher() -> None:
    """Copy the reused WebSerial installer UI into the site."""
    if not WEBFLASHER.is_dir():
        sys.exit("webflasher UI missing - run: python scripts/setup.py (submodules)")
    for f in WEBFLASHER.rglob("*"):
        if f.is_file():
            dst = SITE / f.relative_to(WEBFLASHER)
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(f, dst)


def catalog_entry(pdir: Path, wasm: Path) -> dict:
    """Build one catalog entry with relative (same-origin) artifact URLs."""
    name = pdir.name
    manifest = json.loads((pdir / "meta.json").read_text(encoding="utf-8"))
    i18n_meta = manifest.get("i18n", {}).get("meta", {})
    entry = {
        "id": name,
        "version": manifest.get("version", "0.0.0"),
        "author": manifest.get("author", ""),
        "name": english(i18n_meta.get("name", name)),
        "description": english(i18n_meta.get("description", "")),
        "icon": manifest.get("icon", "info"),
        "host_api_level_min": manifest.get("host_api_level_min", "0.5"),
        "linear_memory_kb": manifest.get("linear_memory_kb", 64),
        "capabilities": manifest.get("capabilities", {}),
        "prerequisites": manifest.get("prerequisites", {}),
        "wasm_size_kb": (wasm.stat().st_size + 1023) // 1024,
        "wasm_url": f"./{name}.wasm",
        "meta_url": f"./{name}.meta.json",
    }
    shutil.copy2(wasm, SITE / f"{name}.wasm")
    shutil.copy2(pdir / "meta.json", SITE / f"{name}.meta.json")
    lang = pdir / f"{name}.lang.json"
    if lang.is_file():
        shutil.copy2(lang, SITE / f"{name}.lang.json")
        entry["lang_url"] = f"./{name}.lang.json"
    return entry


def main() -> None:
    SITE.mkdir(parents=True, exist_ok=True)
    stage_webflasher()

    plugins = []
    for pdir in discover():
        name = pdir.name
        subprocess.run([sys.executable, str(ROOT / "tools" / "badge.py"), "build", name], check=True)
        wasm = DIST / f"{name}.wasm"
        if not wasm.is_file():
            print(f"warning: {name} produced no wasm - skipped", file=sys.stderr)
            continue
        plugins.append(catalog_entry(pdir, wasm))

    catalog = {"catalog_version": 1, "release_version": "local", "plugins": plugins}
    (SITE / "catalog.json").write_text(json.dumps(catalog, indent=2) + "\n", encoding="utf-8")
    print(f"Staged {len(plugins)} plugin(s) into {SITE} (excluded: {', '.join(sorted(EXCLUDE))}).")
    if not plugins:
        print("No shareable plugins yet - add one with `badge new <name>` and push.")


if __name__ == "__main__":
    main()
