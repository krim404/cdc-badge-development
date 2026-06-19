# CDC Badge Development

**The easy way to start building embedded software for the CDC Badge.** Write a
small Rust plugin, build it, flash it onto the badge, watch it run - on macOS,
Linux or Windows. This repository is built for two kinds of newcomer:

- **Classical developers** who want a ready-made toolchain and a simple command line.
- **Vibe-coding / spec-driven (SDD) developers** who want an AI assistant and a
  guided spec → plan → tasks → implement workflow, in Claude Code, Codex or opencode.

It contains **no firmware** - it bundles the tools, an AI assistant skill, and
the official firmware/plugin projects (as `vendor/` submodules) so everything
just works.

## ▶ Start here

**New to all this? Follow [`EASY.md`](./EASY.md)** - it walks an absolute
beginner from "nothing installed" to a running plugin, step by step (including
how to install VS Code on each OS).

In short:

1. Install VS Code, git and Python (see `EASY.md` for the exact per-OS command).
2. `git clone --recursive https://github.com/krim404/cdc-badge-development`
3. Open the folder in VS Code and run the **Setup** task (or `python scripts/setup.py`).
4. Build the example: **Build plugin** task, or `python tools/badge.py build starter`.

## Two paths

### Classical (no AI)

```sh
python tools/badge.py new   hello        # create a plugin
python tools/badge.py test  hello        # run host-side tests (write tests first!)
python tools/badge.py build hello        # compile to .wasm
python tools/badge.py flash hello --start --monitor   # upload, run, watch logs
```

If your computer can't reach the badge over USB, use the browser installer
(WebSerial) at <https://krim404.github.io/cdc-badge-plugins/> instead.

### Vibe / spec-driven (AI)

Open the repo in **Claude Code**, **Codex** or **opencode**. A ready-made skill
(`cdc-badge-plugin-dev`) guides developing, testing, flashing and debugging, and
the spec-driven-development commands (`/speckit-specify`, `/speckit-plan`, …) are
active out of the box - no setup. The AI follows the standards in
[`code-quality.md`](./code-quality.md), writes tests first, and comments code for
learners.

## What's inside

| Path | What |
|------|------|
| `scripts/setup.py` | One-command, cross-platform toolchain setup |
| `tools/badge.py` | The `badge` command: new/build/test/flash/monitor |
| `plugins/starter/` | A commented, test-ready example plugin |
| `.claude/skills/`, `.agents/skills/` | The AI plugin-dev skill + Spec Kit commands |
| `knowledge/index.md` | Links into the vendored SDK, host API and firmware docs |
| `vendor/` | The firmware and plugin SDK (git submodules, updatable) |
| `code-quality.md`, `EASY.md` | How to write good code; the beginner walkthrough |

## Official resources

- Firmware: <https://github.com/krim404/cdc-badge-os> · docs <https://krim404.github.io/cdc-badge-os/>
- Plugin SDK & examples: <https://github.com/krim404/cdc-badge-plugins>
- Host API reference: <https://krim404.github.io/cdc-badge-os/dev/host-api/> (Doxygen: <https://krim404.github.io/cdc-badge-os/api/host__api_8h.html>)
- Web installer: <https://krim404.github.io/cdc-badge-plugins/>
- Spec Kit (SDD): <https://github.com/github/spec-kit>
- Agents: [Claude Code](https://docs.anthropic.com/en/docs/claude-code) · [Codex](https://developers.openai.com/codex) · [opencode](https://opencode.ai/docs/)

## License

GPL-3.0 (matching the upstream projects).
