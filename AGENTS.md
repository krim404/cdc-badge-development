# Agent guide: cdc-badge-development

Shared instructions for AI coding agents (Claude Code, Codex, opencode) working
in this repository. `CLAUDE.md` points here; this is the single source.

## What this repo is

A trivial-to-start environment for building, flashing and debugging **Rust
WebAssembly plugins** for the CDC Badge. It contains **no firmware**; the
firmware and the plugin SDK are vendored read-only under `vendor/` as git
submodules. `host_api.h` is canonical in the firmware submodule - never fork or
edit it; always verify host API calls against
`vendor/cdc-badge-plugins/sdk/host_api.h`.

## The one command you need: `badge`

Run it with the project venv Python (the VS Code tasks do this). The `Setup`
task / `python scripts/setup.py` provisions everything first.

| Action | Command |
|--------|---------|
| New plugin | `python tools/badge.py new <name>` |
| Build | `python tools/badge.py build <name>` |
| Test (host) | `python tools/badge.py test <name>` |
| Flash + run + logs | `python tools/badge.py flash <name> --start --monitor` |
| Serial log | `python tools/badge.py monitor` |
| Manage | `python tools/badge.py list|start|stop|delete ...` |

Flashing and the serial monitor run **on the host** over USB (115200). If the
host cannot reach USB (e.g. a container), use the WebSerial webflasher in
`vendor/cdc-badge-plugins/webflasher` instead. Do not assume USB inside a container.

## How to work here (apply when generating or reviewing code)

- **Test-Driven Development is standard.** Write a failing host-side test
  (`badge test`), then implement, then keep it green. New plugins start
  test-ready. SDD-generated features include tests by default.
- **Follow `code-quality.md`**: DRY, KISS, single responsibility, clear names, no
  magic numbers, error handling. Read it before writing code.
- **Generated code is for beginners**: comment generously to explain *what* the
  code does, so a learner can follow each step (teaching exception to the usual
  "why over what" rule, documented in `code-quality.md`).
- **Guard against the common AI-agent mistakes** (see `code-quality.md`): verify
  every host API call against the vendored `host_api.h`, build and run tests
  before claiming done, prefer existing patterns, keep changes minimal, do not
  invent APIs/packages.
- **Docs describe the current state only** - no history, rationale, or migration notes.

## Where to look

- Plugin-dev skill: `.claude/skills/cdc-badge-plugin-dev/` (mirrored to
  `.agents/skills/`). It covers develop/test/flash/debug and the badge-specific pitfalls.
- Knowledge hub: `knowledge/index.md` (links into the vendored SDK, host-API
  reference, manifest schema, serial commands, and firmware specs).
- Spec-driven development: the `specify`/`plan`/`tasks`/`implement` commands are
  committed and active for all three agents - no install needed.

<!-- SPECKIT START -->
For additional context about technologies to be used, project structure,
shell commands, and other important information, read the current plan
<!-- SPECKIT END -->
