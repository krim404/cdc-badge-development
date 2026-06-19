#!/usr/bin/env bash
# Thin convenience wrapper for macOS/Linux. It only forwards to the real,
# cross-platform Python bootstrap. Windows users run:  python scripts\setup.py
set -euo pipefail
cd "$(dirname "$0")/.."
exec python3 scripts/setup.py "$@"
