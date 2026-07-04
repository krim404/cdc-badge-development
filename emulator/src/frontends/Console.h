/**
 * \file Console.h
 * \brief Interactive stdin console for a running emulator - the off-device
 *        counterpart of the badge's serial command interface.
 *
 * A reader thread collects complete lines from stdin; the emulator loop
 * (window or headless-interactive) polls and executes them synchronously, so
 * commands never race the plugin. Unknown input produces a clear error plus a
 * pointer to `help`. Command output goes to stdout; logs stay on stderr.
 */
#pragma once

#include <cstdio>
#include <memory>
#include <string>

namespace emu {

class EmulatorCore;

class Console {
public:
    /// \param out Stream for command output. main() hands us the rescued
    ///        original stdout: the process stdout is redirected to stderr
    ///        early so the vendored firmware's printf noise (CalEPD write
    ///        tracing, refresh stats) cannot bury the console.
    explicit Console(FILE* out = stdout);
    ~Console();

    /// Start the stdin reader thread. Call once.
    void start();

    /// Execute at most one pending input line against the core.
    /// \return false when the user asked to quit (quit/exit/EOF).
    bool poll(EmulatorCore& core);

    /// True when stdin is an interactive terminal (REPL default-on).
    static bool stdinIsTty();

private:
    /// Parse + run one line. Returns false on quit.
    bool execute(EmulatorCore& core, const std::string& line);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace emu
