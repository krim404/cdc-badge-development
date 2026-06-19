# CLAUDE.md

This project's agent instructions live in [`AGENTS.md`](./AGENTS.md) so a single
file serves Claude Code, Codex and opencode. Read it first.

Claude Code specifics:
- The plugin-development skill is at `.agents/skills/cdc-badge-plugin-dev/`
  (mirrored to `.claude/skills/`) and triggers on plugin
  develop/build/test/flash/debug requests.
- The Spec Kit commands (`/speckit-specify`, `/speckit-plan`, `/speckit-tasks`,
  `/speckit-implement`, …) are committed under `.agents/skills/` (and
  `.claude/skills/`) and active out of the box.
