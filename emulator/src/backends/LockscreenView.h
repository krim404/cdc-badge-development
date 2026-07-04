/**
 * \file LockscreenView.h
 * \brief Minimal fake lockscreen so residency semantics are testable
 *        off-device: leaving the plugin (or pressing L / `lock`) shows this
 *        view; Y unlocks straight back into the plugin - no PIN, no
 *        PinManager. Registered plugin quick-actions
 *        (host_lockscreen_register_action) are listed and can be triggered
 *        with their digit, exactly like the badge's lockscreen menu.
 */
#pragma once

#include "cdc_ui/IView.h"

namespace emu {

class LockscreenView : public cdc::ui::ViewBase {
public:
    using Callback = void (*)(void* ctx);

    void setOnUnlock(Callback cb, void* ctx)
    {
        on_unlock_ = cb;
        unlock_ctx_ = ctx;
    }

    /// Fired when the lockscreen becomes the visible top view again, i.e.
    /// the plugin popped (or was popped) off it - the "user left the
    /// plugin" moment EmulatorCore turns into the residency transition.
    void setOnBecameTop(Callback cb, void* ctx)
    {
        on_top_ = cb;
        top_ctx_ = ctx;
    }

    // IView
    void render(bool partial) override;
    cdc::ui::InputResult onKey(char key) override;
    void onEnter(void* context) override
    {
        (void)context;
        dirty_ = true;
    }
    void onExit() override {}
    void onResume() override
    {
        dirty_ = true;
        if (on_top_) {
            on_top_(top_ctx_);
        }
    }
    const char* getName() const override { return "EmuLockscreen"; }

private:
    Callback on_unlock_ = nullptr;
    void*    unlock_ctx_ = nullptr;
    Callback on_top_ = nullptr;
    void*    top_ctx_ = nullptr;
};

}  // namespace emu
