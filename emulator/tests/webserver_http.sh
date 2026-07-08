#!/usr/bin/env bash
# Acceptance gate: the webserver plugin serves its index.html over real TCP in
# the emulator. Proves the host_net_* listener + the emulator's real-time serve
# mode work end-to-end.
#
# Port 80 needs a free loopback, so this runs inside a user+net namespace
# (no root). Requires unprivileged user namespaces (default on most distros).
# Exit 77 = skip (CTest SKIP_RETURN_CODE): missing prerequisites, not a failure.
#
# Usage: emulator/tests/webserver_http.sh <emulator-bin> <webserver.wasm> <meta.json>
set -euo pipefail

EMU="${1:?emulator binary}"
WASM="${2:?webserver.wasm}"
META="${3:?meta.json}"

[ -x "$EMU" ] || { echo "SKIP: emulator binary not built"; exit 77; }
[ -f "$WASM" ] || { echo "SKIP: webserver.wasm not built"; exit 77; }
[ -f "$META" ] || { echo "SKIP: webserver meta.json missing"; exit 77; }
command -v unshare >/dev/null || { echo "SKIP: unshare not available"; exit 77; }
command -v curl >/dev/null || { echo "SKIP: curl not available"; exit 77; }
# Unprivileged user namespaces can be disabled (containers, hardened kernels).
unshare -rn true 2>/dev/null || { echo "SKIP: user namespaces unavailable"; exit 77; }

# Env vars survive into the namespaced shell; no fragile quote interpolation.
export EMU WASM META
unshare -rn bash -c '
  set -euo pipefail
  ip link set lo up 2>/dev/null || true
  log=$(mktemp)
  trap "rm -f \"$log\"" EXIT
  "$EMU" --wasm "$WASM" --meta "$META" --headless --serve 12 >"$log" 2>&1 &
  emu_pid=$!

  # Poll instead of a fixed sleep: fast when the server is up, tolerant on
  # slow CI, and a clear FAIL when it never comes up within the budget.
  body=""
  for _ in $(seq 1 40); do
    if ! kill -0 "$emu_pid" 2>/dev/null; then
      echo "FAIL: emulator exited early"; cat "$log"; exit 1
    fi
    body=$(curl -s -m 2 http://127.0.0.1:80/ || true)
    [ -n "$body" ] && break
    sleep 0.25
  done

  echo "$body" | grep -q "CDC Badge webserver" || {
    echo "FAIL: index.html not served"; cat "$log"; exit 1;
  }
  echo "PASS: webserver served index.html over TCP :80"
'
