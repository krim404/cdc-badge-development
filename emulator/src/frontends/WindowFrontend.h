/**
 * \file WindowFrontend.h
 * \brief Interactive SDL2 window showing the current frame, with the host
 *        keyboard mapped to the 12-key badge keypad (0-9, Y, N).
 *
 * Compiled only when SDL2 is available (EMULATOR_WITH_SDL2); platforms
 * without a self-containable window library ship headless-only (FR-031/032).
 * Time advances with the wall clock through EmulatorCore::advance(), so the
 * same deterministic machinery drives interactive runs.
 */
#pragma once

#include <cstdint>

#include "IFrontend.h"

namespace emu {

class Console;
class EmulatorCore;

class WindowFrontend : public IFrontend {
public:
    WindowFrontend();
    ~WindowFrontend() override;

    /// Open the window. False when no display/driver is available - the
    /// caller falls back to headless (FR-015).
    bool open();

    void onFrame(const uint8_t* frame, size_t len) override;

    /// Pump SDL events + wall-clock time into the core until quit.
    /// \param console Optional stdin console polled once per frame.
    /// \return process exit code.
    int run(EmulatorCore& core, Console* console = nullptr);

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace emu
