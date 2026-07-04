/**
 * \file main.cpp
 * \brief cdc-badge-emulator entry point: parse args, load manifest + .wasm,
 *        run the plugin lifecycle interactively (window) or scripted
 *        (headless), with automatic headless fallback when no display is
 *        available (FR-015).
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include <chrono>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>

#include <io.h>
#define emu_dup _dup
#define emu_dup2 _dup2
#define emu_fileno _fileno
#else
#include <unistd.h>
#define emu_dup dup
#define emu_dup2 dup2
#define emu_fileno fileno
#endif

#include "EmulatorCore.h"
#include "backends/HostDisplay.h"
#include "backends/HostRtc.h"
#include "frontends/Console.h"
#include "frontends/PngFrontend.h"

#ifdef EMULATOR_WITH_SDL2
#include "frontends/WindowFrontend.h"
#endif

namespace {

void usage(const char* argv0)
{
    fprintf(stderr,
            "Usage: %s --wasm <file.wasm> --meta <meta.json> [options]\n"
            "\n"
            "Options:\n"
            "  --headless        run without a window (PNG/hash output only)\n"
            "  --keys <seq>      scripted keypad sequence, e.g. 1,2,Y,N\n"
            "                    (5! = long press, @name[:v] = EventBus event)\n"
            "  --fail-prereq <n> force a declared prerequisite to fail (error-path test)\n"
            "  --cmd <str|@file> feed the plugin command channel (plugin_on_cmd)\n"
            "  --msg-in <file>   inject an incoming message packet (JSON, same\n"
            "                    format sends write to <data>/msg/out/)\n"
            "  --frames <dir>    write a PNG per committed frame into <dir>\n"
            "  --snapshot <dir>  compare frames against reference hashes in <dir>\n"
            "  --data <dir>      base dir for NVS / vFAT sandbox / SE store\n"
            "  --offline         HTTP/socket fail cleanly; WiFi still reports on\n"
            "  --seconds <n>     advance the virtual clock by n seconds\n"
            "  --ticks <n>       advance the virtual clock by n 50 ms plugin ticks\n"
            "  --repl            force the interactive stdin console\n"
            "                    (default on when stdin is a TTY and the run is\n"
            "                    not scripted; type 'help' once running)\n",
            argv0);
}

bool readCmdArg(const std::string& value, std::string& out)
{
    if (!value.empty() && value[0] == '@') {
        std::ifstream in(value.substr(1), std::ios::binary);
        if (!in) {
            fprintf(stderr, "error: cannot read cmd file %s\n",
                    value.c_str() + 1);
            return false;
        }
        std::ostringstream buf;
        buf << in.rdbuf();
        out = buf.str();
        return true;
    }
    out = value;
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
#ifdef _WIN32
    // One-time Winsock bring-up for the plugin socket/HTTP backends.
    WSADATA wsaData;
    (void)WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    // Rescue the real stdout for the console, then send process stdout to
    // stderr: the reused firmware sources printf() drawing traces and refresh
    // stats to stdout, which would otherwise bury interactive console output.
    FILE* consoleOut = fdopen(emu_dup(emu_fileno(stdout)), "w");
    (void)emu_dup2(emu_fileno(stderr), emu_fileno(stdout));
    if (!consoleOut) {
        consoleOut = stderr;
    }

    emu::EmulatorOptions options;
    bool                 windowRequested = true;
    bool                 replForced = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: %s needs a value\n", name);
                exit(2);
            }
            return argv[++i];
        };
        if (arg == "--wasm") {
            options.wasm_path = next("--wasm");
        } else if (arg == "--meta") {
            options.meta_path = next("--meta");
        } else if (arg == "--headless") {
            windowRequested = false;
            options.headless = true;
        } else if (arg == "--keys") {
            options.keys = next("--keys");
        } else if (arg == "--fail-prereq") {
            options.fail_prereq = next("--fail-prereq");
        } else if (arg == "--msg-in") {
            options.msg_in = next("--msg-in");
        } else if (arg == "--cmd") {
            if (!readCmdArg(next("--cmd"), options.cmd)) {
                return 2;
            }
        } else if (arg == "--frames") {
            options.frames_dir = next("--frames");
        } else if (arg == "--snapshot") {
            options.snapshot_dir = next("--snapshot");
        } else if (arg == "--data") {
            options.data_dir = next("--data");
        } else if (arg == "--offline") {
            options.offline = true;
        } else if (arg == "--repl") {
            replForced = true;
        } else if (arg == "--seconds") {
            options.run_seconds = atoll(next("--seconds"));
        } else if (arg == "--ticks") {
            options.run_ticks = atoll(next("--ticks"));
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "error: unknown option %s\n", arg.c_str());
            usage(argv[0]);
            return 2;
        }
    }
    if (options.wasm_path.empty() || options.meta_path.empty()) {
        usage(argv[0]);
        return 2;
    }

    // Scripted inputs imply a deterministic, non-interactive run.
    const bool scripted = !options.keys.empty() || options.run_seconds > 0 ||
                          options.run_ticks > 0 || !options.snapshot_dir.empty();
    // The stdin console: forced via --repl, default-on for interactive runs
    // launched from a terminal (never during scripted/snapshot runs, which
    // must stay deterministic).
    const bool replWanted =
        replForced || (!scripted && emu::Console::stdinIsTty());

    emu::PngFrontend png(options.frames_dir, options.snapshot_dir);

    emu::EmulatorCore core(options);
    emu::Console      console(consoleOut);

#ifdef EMULATOR_WITH_SDL2
    if (windowRequested && !scripted) {
        emu::WindowFrontend window;
        if (window.open()) {
            emu::HostDisplay::instance().setFrameSink(
                [&](const uint8_t* frame, size_t len) {
                    png.onFrame(frame, len);
                    window.onFrame(frame, len);
                });
            emu::HostRtc::instance().useHostTime();
            if (!core.load()) {
                return 2;
            }
            if (!core.start()) {
                return 3;
            }
            if (replWanted) {
                console.start();
            }
            const int rc = window.run(core, replWanted ? &console : nullptr);
            const int snap = png.finish();
            return rc != 0 ? rc : snap;
        }
        fprintf(stderr, "I (emul) no display available - headless fallback\n");
    }
#else
    if (windowRequested && !scripted) {
        fprintf(stderr,
                "I (emul) built without a window frontend - headless run\n");
    }
#endif

    emu::HostDisplay::instance().setFrameSink(
        [&](const uint8_t* frame, size_t len) { png.onFrame(frame, len); });

    if (!core.load()) {
        return 2;
    }

    if (!scripted && replWanted) {
        // Headless-interactive: the console drives the session; wall-clock
        // time feeds the same deterministic advance machinery as the window.
        emu::HostRtc::instance().useHostTime();
        if (!core.start()) {
            return 3;
        }
        console.start();
        auto last = std::chrono::steady_clock::now();
        while (console.poll(core)) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - last)
                    .count();
            if (elapsed > 0) {
                core.advance(elapsed);
                last = now;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        core.shutdown();
        return png.finish();
    }

    const int rc = core.runScripted();
    const int snap = png.finish();
    return rc != 0 ? rc : snap;
}
