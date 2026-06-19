# EASY: from a blank computer to a running plugin

No prior experience needed. Follow the steps for your operating system. Lines
starting with `$` are commands you type into a terminal.

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
  (Or in a terminal: `python scripts/setup.py`.)

This installs the Rust WebAssembly toolchain, `wasm-opt`, and a local Python
environment. It can take a few minutes the first time.

## 4. Build the example

- Run Task → **Build plugin** (keep the default name `starter`), or:

```sh
$ python tools/badge.py build starter
```

You should see it produce `dist/starter.wasm`. 🎉 You just built embedded software.

## 5. Make your own plugin

### The classical way

```sh
$ python tools/badge.py new hello       # creates plugins/hello (with a test)
$ python tools/badge.py test hello      # run its tests - write tests first!
$ python tools/badge.py build hello     # compile
```

Plug your badge into USB, then:

```sh
$ python tools/badge.py flash hello --start --monitor
```

It uploads, starts the plugin, and shows its log. (No USB on your machine? Use
the browser installer at <https://krim404.github.io/cdc-badge-plugins/>.)

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

## Stuck?

- Serial port "busy": close any open monitor - only one program can use the port.
- Badge asks for a PIN: add `--pin <pin>` to the flash/monitor command.
- See `code-quality.md` for how to write clean code and what mistakes to avoid.
