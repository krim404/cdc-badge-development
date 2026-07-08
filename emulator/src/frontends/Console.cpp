#include "Console.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <io.h>
#define EMU_ISATTY() _isatty(_fileno(stdin))
#else
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#define EMU_ISATTY() isatty(STDIN_FILENO)
#endif

#include "../EmulatorCore.h"
#include "../EpdSpiCapture.h"
#include "../backends/HostDisplay.h"
#include "../backends/HostNet.h"
#include "../backends/VirtualClock.h"
#include "IFrontend.h"

#include "cdc_core/Cp437.h"
#include "cdc_ui/ViewStack.h"
#include "cdc_views/T9InputView.h"
#include "plugin_manager/PluginManifest.h"

#include "stb_image_write.h"

namespace emu {

namespace {

constexpr const char* kHelp =
    "Emulator console - the off-device serial-in. Commands:\n"
    "  help                 show this help\n"
    "  key <k>[!]           press badge key 0-9/Y/N ('!' = long press)\n"
    "  keys <seq>           run a sequence like --keys (1,2,Y,@battery_low)\n"
    "  paste <text>         paste text into the open T9/password editor\n"
    "  cmd <text>           deliver <text> via plugin_on_cmd\n"
    "  event <name>[:v]     publish an EventBus event (see --help for names)\n"
    "  advance <ms>         advance the virtual clock deterministically\n"
    "  screenshot <file>    write the current frame as PNG\n"
    "  offline <on|off>     toggle the network-error mode (WiFi stays on)\n"
    "  status               plugin id, uptime, network mode\n"
    "  lock | unlock        enter/leave the fake lockscreen (Y also unlocks)\n"
    "  msg-in <file>        inject an incoming message packet (JSON)\n"
    "  ble-in <file>        (planned: inject a BLE packet - not yet)\n"
    "  quit | exit          leave the emulator\n";

bool isBadgeKey(char key)
{
    return (key >= '0' && key <= '9') || key == 'Y' || key == 'N';
}

/// Run one --keys style token ("5", "5!", "@battery_low").
void runKeyToken(FILE* out, EmulatorCore& core, const std::string& token)
{
    if (token.empty()) {
        return;
    }
    if (token[0] == '@') {
        (void)core.injectEvent(token.substr(1));
    } else {
        const char key = static_cast<char>(toupper(token[0]));
        if (!isBadgeKey(key)) {
            fprintf(out, "error: '%s' is not a badge key (0-9, Y, N)\n", token.c_str());
            return;
        }
        core.injectKey(key, token.size() > 1 && token[1] == '!');
    }
    core.advance(50);
}

bool pasteIntoEditor(FILE* out, EmulatorCore& core, const std::string& text)
{
    // The T9 editor (and its PasswordT9View subclass) is a regular pushed
    // view; appendRaw() is the same entry the firmware's PASTE serial
    // command uses.
    auto* t9 = dynamic_cast<cdc::ui::T9InputView*>(
        cdc::ui::ViewStack::instance().current());
    if (!t9) {
        fprintf(out, "error: no T9/password editor is open\n");
        return false;
    }
    // The host terminal delivers UTF-8, but the editor buffer and the display
    // pipeline are CP437. On the badge the serial reader converts each line
    // before dispatch (SerialCmd); mirror that here for the paste payload.
    const std::string cp437 = cdc::core::cp437::fromUtf8(text.c_str());
    const uint16_t appended = t9->appendRaw(cp437.c_str());
    t9->markDirty();
    core.renderIfNeeded();
    fprintf(out, "pasted %u byte(s)\n", appended);
    return true;
}

bool writeScreenshot(FILE* out, const std::string& path)
{
    HostDisplay::instance().captureFrame();  // refresh the capture buffer
    uint8_t frame[kPanelBufferSize];
    if (!epdCaptureFrame(frame)) {
        fprintf(out, "error: no frame captured yet\n");
        return false;
    }
    uint8_t gray[296 * 128];
    frameToLandscapeGray(frame, gray);
    if (!stbi_write_png(path.c_str(), 296, 128, 1, gray, 296)) {
        fprintf(out, "error: cannot write %s\n", path.c_str());
        return false;
    }
    fprintf(out, "wrote %s\n", path.c_str());
    return true;
}

}  // namespace

struct Console::Impl {
    FILE*                   out = stdout;
    std::thread             reader;
    std::mutex              mutex;
    std::deque<std::string> lines;
    std::atomic<bool>       eof{false};
    std::atomic<bool>       stop{false};
    bool                    started = false;
};

Console::Console(FILE* out) : impl_(new Impl())
{
    impl_->out = out ? out : stdout;
}

Console::~Console()
{
#ifdef _WIN32
    // The reader thread blocks in getline(); it ends with the process.
    if (impl_->started) {
        impl_->reader.detach();
    }
#else
    // The poll()-based reader notices the stop flag within one timeout slice
    // and can be joined. Never detach a thread blocked in C-stdio input: it
    // holds the stdin FILE lock, and glibc's exit() -> _IO_flush_all() then
    // deadlocks on it (emulator kept running after the window was closed).
    impl_->stop = true;
    if (impl_->started && impl_->reader.joinable()) {
        impl_->reader.join();
    }
#endif
}

bool Console::stdinIsTty()
{
    return EMU_ISATTY() != 0;
}

void Console::start()
{
    if (impl_->started) {
        return;
    }
    impl_->started = true;
    FILE* out = impl_->out;
    fprintf(out, "emulator console ready - type 'help' for commands\n");
    fflush(out);
    Impl* impl = impl_.get();
#ifdef _WIN32
    impl_->reader = std::thread([impl]() {
        std::string line;
        while (std::getline(std::cin, line)) {
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->lines.push_back(line);
        }
        impl->eof = true;
    });
#else
    // Raw poll()/read() reader: unlike getline(std::cin) it never sits inside
    // C stdio holding the stdin FILE lock, so process teardown cannot deadlock
    // and the thread can be stopped and joined (see ~Console).
    impl_->reader = std::thread([impl]() {
        std::string acc;
        char        buf[256];
        while (!impl->stop) {
            struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
            const int     r = ::poll(&pfd, 1, 200);
            if (r < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (r == 0) {
                continue;
            }
            const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) {
                break;  // EOF or error
            }
            acc.append(buf, static_cast<size_t>(n));
            size_t nl;
            while ((nl = acc.find('\n')) != std::string::npos) {
                std::string line = acc.substr(0, nl);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                acc.erase(0, nl + 1);
                std::lock_guard<std::mutex> lock(impl->mutex);
                impl->lines.push_back(std::move(line));
            }
        }
        if (!impl->stop && !acc.empty()) {
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->lines.push_back(std::move(acc));
        }
        impl->eof = true;
    });
#endif
}

bool Console::poll(EmulatorCore& core)
{
    std::string line;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->lines.empty()) {
            return !impl_->eof;
        }
        line = std::move(impl_->lines.front());
        impl_->lines.pop_front();
    }
    return execute(core, line);
}

bool Console::execute(EmulatorCore& core, const std::string& line)
{
    FILE* out = impl_->out;
    std::istringstream in(line);
    std::string        verb;
    in >> verb;
    if (verb.empty()) {
        return true;
    }
    std::string rest;
    std::getline(in, rest);
    if (!rest.empty() && rest[0] == ' ') {
        rest.erase(0, 1);
    }

    if (verb == "help" || verb == "?") {
        fputs(kHelp, out);
    } else if (verb == "quit" || verb == "exit") {
        fprintf(out, "bye\n");
        return false;
    } else if (verb == "key") {
        if (rest.empty()) {
            fprintf(out, "error: usage: key <0-9|Y|N>[!]\n");
        } else {
            runKeyToken(out, core, rest);
        }
    } else if (verb == "keys") {
        if (rest.empty()) {
            fprintf(out, "error: usage: keys <seq> (e.g. 1,2,Y,@battery_low)\n");
        } else {
            std::stringstream seq(rest);
            std::string       token;
            while (std::getline(seq, token, ',')) {
                runKeyToken(out, core, token);
            }
        }
    } else if (verb == "paste") {
        if (rest.empty()) {
            fprintf(out, "error: usage: paste <text>\n");
        } else {
            pasteIntoEditor(out, core, rest);
        }
    } else if (verb == "cmd") {
        if (rest.empty()) {
            fprintf(out, "error: usage: cmd <text>\n");
        } else {
            core.sendCmd(rest);
        }
    } else if (verb == "event") {
        if (rest.empty() || !core.injectEvent(rest)) {
            fprintf(out, "error: usage: event <name>[:value] - e.g. event battery_low\n");
        }
    } else if (verb == "advance") {
        const long ms = rest.empty() ? 0 : atol(rest.c_str());
        if (ms <= 0) {
            fprintf(out, "error: usage: advance <milliseconds>\n");
        } else {
            core.advance(ms);
            fprintf(out, "advanced %ld ms (uptime %u ms)\n", ms,
                   VirtualClock::instance().nowMs());
        }
    } else if (verb == "screenshot") {
        if (rest.empty()) {
            fprintf(out, "error: usage: screenshot <file.png>\n");
        } else {
            writeScreenshot(out, rest);
        }
    } else if (verb == "offline") {
        if (rest == "on") {
            HostWifi::setOffline(true);
            fprintf(out, "network errors ON (WiFi still reports connected)\n");
        } else if (rest == "off") {
            HostWifi::setOffline(false);
            fprintf(out, "network errors OFF (real internet)\n");
        } else {
            fprintf(out, "error: usage: offline <on|off>\n");
        }
    } else if (verb == "status") {
        fprintf(out, "plugin:  %s\nuptime:  %u ms\nnetwork: %s\n",
               core.manifest().id.c_str(), VirtualClock::instance().nowMs(),
               HostWifi::isOffline() ? "offline (forced errors)" : "online");
    } else if (verb == "lock") {
        core.lock();
        // The console runs outside any WASM frame, so the deferred residency
        // transition can complete right away (advance(0) hits the safe point).
        core.advance(0);
        fprintf(out, "locked (fake lockscreen; 'unlock' or key Y returns)\n");
    } else if (verb == "unlock") {
        core.unlock();
        fprintf(out, "unlocked\n");
    } else if (verb == "msg-in") {
        if (rest.empty()) {
            fprintf(out, "error: usage: msg-in <packet.json>\n");
        } else if (core.injectMessage(rest)) {
            fprintf(out, "packet delivered\n");
        } else {
            fprintf(out, "error: packet not delivered (see log)\n");
        }
    } else if (verb == "ble-in") {
        fprintf(out, "error: 'ble-in' is not implemented yet (BLE is out of scope)\n");
    } else {
        fprintf(out, "error: unknown command '%s' - try 'help'\n", verb.c_str());
    }
    fflush(out);
    return true;
}

}  // namespace emu
