# EASY: from a blank computer to a running plugin

No prior experience needed. Follow the steps for your operating system. Lines
starting with `$` are commands you type into a terminal.

> **One note up front:** on **macOS/Linux** type `python3` in the commands below;
> on **Windows** type `python`. (Even simpler: use the VS Code **Tasks**, which
> work the same on every OS.)

## 1. Install the prerequisites

You need three things: **VS Code** (the editor), **git** (to download the code)
and **Python** (runs the setup). On Windows you also install **Rust**; on
macOS/Linux the setup installs Rust for you.

### macOS (using Homebrew)

If you don't have Homebrew, install it from <https://brew.sh>, then:

```sh
$ brew install --cask visual-studio-code
$ brew install git python
```

### Linux (Debian/Ubuntu)

```sh
$ sudo apt update
$ sudo apt install -y git python3 python3-venv curl
$ sudo snap install code --classic      # or get VS Code from https://code.visualstudio.com
```

### Windows (using winget, built into Windows 10/11)

Open **PowerShell** or **Terminal** and run (you only run these install lines;
nothing else here uses PowerShell):

```powershell
> winget install Microsoft.VisualStudio.Code Git.Git Python.Python.3.12 Rustlang.Rustup
```

Close and reopen the terminal afterwards so the new tools are on your PATH.

## 2. Get this project

```sh
$ git clone --recursive https://github.com/krim404/cdc-badge-development
```

`--recursive` also downloads the bundled firmware and SDK. (If you forget it, run
`git submodule update --init --recursive` inside the folder.)

## 3. Open it in VS Code and set up the toolchain

- Open the `cdc-badge-development` folder in VS Code. When it offers the
  recommended extensions, click **Install**.
- Run the setup once: press `Ctrl/Cmd+Shift+P` → **Tasks: Run Task** → **Setup**.
  (Or in a terminal: `python3 scripts/setup.py`.)

This installs the Rust WebAssembly toolchain, `wasm-opt`, and a local Python
environment. It can take a few minutes the first time. It also writes a
`.vscode/settings.json` that points VS Code's Rust support (rust-analyzer)
straight at the toolchain it just set up.

**If the Rust features (rust-analyzer) still show errors** like "cannot find
cargo/rustc" or "failed to load workspace", reload the editor: press
`Ctrl/Cmd+Shift+P` → **Developer: Reload Window**. If it persists, run **Setup**
again (it re-detects the toolchain) and reload once more.

## 4. Build the example

- Run Task → **Build plugin** (keep the default name `starter`), or:

```sh
$ python3 tools/badge.py build starter
```

You should see it produce `dist/starter.wasm`. 🎉 You just built embedded software.

## 5. Make your own plugin

### The classical way

```sh
$ python3 tools/badge.py new hello       # creates plugins/hello (with a test)
$ python3 tools/badge.py test hello      # run its tests - write tests first!
$ python3 tools/badge.py build hello     # compile
```

**Where you write code:** your plugin lives in **`plugins/hello/`** (named after
whatever you passed to `new`). Open **`plugins/hello/src/lib.rs`** in VS Code -
that is the file you edit. The pure logic at the top is what the tests cover; the
`#[cfg(target_arch = "wasm32")]` block is the on-badge behaviour. `meta.json` next
to it declares the plugin's name and capabilities.

Plug your badge into USB, then:

```sh
$ python3 tools/badge.py flash hello --start --monitor
```

It uploads, starts the plugin, and streams its log (`--monitor` keeps running
until you press Ctrl-C). No USB on your machine? Use the browser installer at
<https://krim404.github.io/cdc-badge-plugins/>.

### The AI way (vibe / spec-driven)

Open the project in **Claude Code**, **Codex** or **opencode** and just ask, for
example:

> Create a new plugin called `hello` that shows a "Hi!" toast when opened. Write
> a test first, build it, then flash it to my badge and show me the log.

The built-in `cdc-badge-plugin-dev` skill guides the assistant through the exact
commands above, writes a test first, comments the code for you, and reads the
serial log to confirm it works.

#### Spec-driven development (the full SDD loop)

For a bigger feature, drive it as a spec instead of one prompt. In **Claude
Code**, type these slash commands in order (each builds on the last):

```text
/speckit-constitution      (optional) set the project's ground rules
/speckit-specify   Build a stopwatch plugin with start/stop/reset on the keypad
/speckit-clarify           answer a few questions to sharpen the spec
/speckit-plan              produce the technical plan
/speckit-tasks             break the plan into an ordered task list
/speckit-analyze           (optional) cross-check spec ↔ plan ↔ tasks
/speckit-implement         build it, with tests, task by task
```

What each does, in plain terms:

- `/speckit-specify <your idea>` - turns your description into a written spec.
- `/speckit-clarify` - asks you a few targeted questions and records the answers.
- `/speckit-plan` - decides the technical approach and structure.
- `/speckit-tasks` - produces a checklist of small, ordered steps.
- `/speckit-implement` - writes the code and tests to satisfy the tasks.

You type these literally, starting with `/`. In **Codex** and **opencode** the
same commands are available (they are committed in the repo); invoke them the way
your tool runs commands. Tests are included by default, and the code follows
`code-quality.md`.

## 6. Share it with friends (your own web flasher)

When you're happy with a plugin, let others install it from the browser - no
toolchain needed:

1. One-time: turn on GitHub Pages - open your repo's **Settings → Pages**, then
   under **Build and deployment** set **Source** to **GitHub Actions**.
2. Push your work to `main` (`git add -A && git commit -m "my plugin" && git push`).
3. GitHub builds a personal web flasher and publishes it. Find the link in the
   **Actions → deploy-flasher** run (or under Settings → Pages), e.g.
   `https://<your-username>.github.io/cdc-badge-development/`.
4. Send that link to a friend. They open it in Chrome/Edge, plug in their badge,
   and click install.

Your plugins are offered automatically; the `starter` test plugin is left out.

## What the badge is (and what fits)

The badge is an **ESP32-S3** with a small **2.9" e-paper screen (296×128)**, a
**12-key keypad** (with **T9 text entry** - there is a built-in T9 input view for
typing), **BLE + USB**, a **TROPIC01 secure element** (it is a crypto badge -
secrets and keys can live on-chip, not in plain flash), ~8 MB PSRAM (but tight
internal RAM), and a small **2 MB** storage area shared by all plugins. That
shapes what is fun to build:

- E-paper redraws are slow (~0.1-0.3 s) and can ghost - **don't animate**; redraw
  only when a value actually changes, at most about once a second.
- Plugins run as WebAssembly on a small chip - **keep the work light**.
- The network is modest - a small fetch now and then is fine, big downloads are not.

## Ideas to try

Small things that fit the hardware well **and that the badge does not already do**
(it already has TOTP, a password vault, a lock-screen clock and vCard contact
exchange built in - so build something new):

- **Name tag** - show your name in big letters. It is a conference badge, after
  all. Pure display, perfect for e-paper.
- **Counter / scorekeeper** - +/- on the keypad, one redraw per change.
- **Dice, coin flip or random picker** - press a key, draw the result once (no animation).
- **Magic 8-ball / decision maker** - ask, press a key, reveal a random answer.
- **Pocket flashcards / quiz** - flip question→answer with a key; ship a fixed set
  or type your own on the T9 keypad. Handy for studying.
- **Tip splitter or unit converter** - type numbers on the T9 keypad, show the result.
- **Pomodoro / countdown timer** - redraw once a minute, never every tick.
- **Badge-to-badge sticker or score swap** - send a custom payload (a high score,
  a short note typed on T9, a tiny drawn icon) to a nearby badge over BLE with the
  message-transfer API (`host_msg_*` / cdc_msg): the two badges confirm a matching
  number, then the payload arrives. (Ask the AI: *"make a plugin that sends a short
  text to another badge."*)
- **Quote / weather of the day** - one WiFi fetch, show the text, redraw once.

Not a good fit: fast animation, high-frame-rate games, or anything that redraws
constantly - the e-paper cannot keep up.

## Stuck?

- **`python: command not found`**: on macOS/Linux use `python3` (Windows: `python`).
- **Rust errors in VS Code** ("cannot find cargo/rustc", "failed to load workspace"):
  run **Setup**, then **Developer: Reload Window** (see step 3). Setup wires
  rust-analyzer to the toolchain via `.vscode/settings.json`.
- **Serial port "busy"**: close any open monitor - only one program can hold the port.
- **Badge asks for a PIN**: add `--pin <pin>` to the **flash** command. The
  `monitor` command needs no PIN.
- **`flash --monitor` never returns**: that is expected - it streams the log until
  Ctrl-C. For a bounded read (scripts/agents) use `monitor --seconds N` or
  `monitor --until "<text>"`.
- See `code-quality.md` for how to write clean code and what mistakes to avoid.
