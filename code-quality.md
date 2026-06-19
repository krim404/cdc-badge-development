# Code quality: what good plugin code looks like

This guide is for two readers: **you**, the student, and the **AI assistant**,
which is told to follow it. It explains the standards we hold code to, how we
comment, and the mistakes AI coding tools make so you can catch them.

## The standards

- **KISS - Keep It Simple.** Prefer the simplest thing that works. Don't add
  abstraction, configuration or "flexibility" you don't need yet.
- **DRY - Don't Repeat Yourself.** If you copy-paste code and tweak it, extract a
  function with a parameter instead. Each piece of logic lives in one place.
- **Single Responsibility.** A function or module does one thing. If you can't
  describe it in one sentence, split it.
- **Clear names.** Names should say what something is or does. Good names remove
  the need for many comments. `greeting_for(user)` beats `g(u)`.
- **Small functions.** If a function is longer than a screen, it probably does
  too much. Break it up.
- **No magic numbers.** Give constants a name (`const MAX_RETRIES: u32 = 3;`)
  instead of sprinkling `3` around.
- **Handle errors.** Don't ignore a `Result`/return code. Decide what happens on
  failure and make it visible.
- **Test first (TDD).** Write a failing test (`badge test`), then write the code
  that makes it pass. Keep tests green. Put pure logic in plain functions so it
  is easy to test off-device.

## Work within the rails of the upstreams

The firmware and the SDK under `vendor/` are **read-only**. A plugin reaches the
badge ONLY through the host API and the capabilities it declares. That contract is
the rails - stay on them.

- **Never patch upstream to make your plugin work.** It is often the easy path:
  the firmware or `host_api.h` sits right there in `vendor/`, and adding a function
  or loosening a check would unblock you in a minute. Don't. A plugin that needs a
  patched firmware is not a plugin - it won't run on anyone else's badge (they run
  stock firmware), it breaks the moment the submodule updates, and it quietly
  defeats the security model that the capabilities exist to enforce.
- **If the host API seems not to do what you want**, in this order:
  1. **Look harder within the rails.** The API surface is large (see the skill's
     host-API map); there is usually an intended path. Re-read the SDK module and
     `host_api.h` before concluding it's impossible.
  2. **File a feature request upstream** (cdc-badge-os) to add the missing host API
     surface, and keep working within the rails until it lands.
  3. **Patching foreign code is the absolute last resort** - and even then it is a
     private fork, never something you can ship as a plugin.
- This applies doubly to AI assistants: they will happily "just patch the OS"
  because it is the shortest path. That is the wrong move here. Treat `host_api.h`
  as immutable and design within it.

This is not bureaucracy - it is what keeps every plugin portable, every badge
secure, and every upstream update safe.

## Comments

Two rules that seem to disagree - both are right, in their place:

- **Professional code: comment the _why_, not the _what_.** Good code is mostly
  self-documenting through clear names and small functions. Reserve comments for
  the reasoning, a non-obvious constraint, or a unit. A comment that just repeats
  the code adds noise.
- **Learning code (here): comment generously to explain the _what_.** Because
  this repo teaches beginners, the AI assistant and the examples comment more
  than production code would, so you can follow each step by reading the output.
  The `starter` plugin is written this way on purpose.

As you grow, lean toward the professional norm: let names carry the meaning and
keep comments for the things code can't say.

## Mistakes AI coding agents make (and how to catch them)

AI assistants are powerful but predictable in how they go wrong. Watch for these:

- **Hallucinated APIs / packages.** The agent invents a function or crate that
  doesn't exist. *Guard:* every host call is checked against
  `vendor/cdc-badge-plugins/sdk/host_api.h`; if it's not there, it isn't real.
  Don't add dependencies that aren't needed.
- **Security slips.** AI code disproportionately introduces vulnerabilities.
  *Guard:* never paste secrets into code; respect the badge's capability and GPIO
  restrictions; review anything touching keys, input parsing or the network.
- **Overcomplication.** It adds layers, traits and config you didn't ask for.
  *Guard:* prefer the simplest version; delete speculative code (KISS, YAGNI).
- **Ignoring existing patterns.** It writes a new style instead of matching the
  examples. *Guard:* read `vendor/cdc-badge-plugins/examples/` and follow them.
- **Silent scope creep.** It changes more than you asked. *Guard:* keep diffs
  small and on-topic; review every changed line.
- **"It works" without running it.** It claims success without building/testing.
  *Guard:* always `badge build` and `badge test` (and flash to the device) before
  believing it. The CI runs build + test on every push as a backstop.

If the assistant does any of these, point it back to this file and to the
`host_api.h` source of truth.
