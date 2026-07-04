#include "WindowFrontend.h"

#include <SDL.h>

#include <cstdio>
#include <cstring>

#include "../EmulatorCore.h"
#include "../EpdSpiCapture.h"
#include "../backends/HostKeypad.h"
#include "Console.h"
#include "cdc_log.h"

namespace emu {

namespace {

constexpr const char* TAG = "Window";
constexpr int         kFrameWidth = 296;
constexpr int         kFrameHeight = 128;
constexpr int         kScale = 3;
constexpr uint32_t    kLongPressMs = 800;

/// Keyboard -> 12-key keypad. Number row and numpad map to 0-9; Y/Enter =
/// KEY_YES, N/Escape/Backspace = KEY_NO; arrow keys map to the badge's
/// navigation digits (Up=2, Left=4, Right=6, Down=8).
char mapKey(SDL_Keycode code)
{
    if (code >= SDLK_0 && code <= SDLK_9) {
        return static_cast<char>('0' + (code - SDLK_0));
    }
    if (code >= SDLK_KP_1 && code <= SDLK_KP_9) {
        return static_cast<char>('1' + (code - SDLK_KP_1));
    }
    switch (code) {
    case SDLK_KP_0:
        return '0';
    case SDLK_UP:
        return '2';
    case SDLK_LEFT:
        return '4';
    case SDLK_RIGHT:
        return '6';
    case SDLK_DOWN:
        return '8';
    case SDLK_y:
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return 'Y';
    case SDLK_n:
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE:
        return 'N';
    default:
        return 0;
    }
}

}  // namespace

struct WindowFrontend::Impl {
    SDL_Window*   window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture*  texture = nullptr;
    uint8_t       gray[kFrameWidth * kFrameHeight] = {};
    bool          dirty = false;

    // Pending key for hold-to-long-press / key-repeat detection.
    char     heldKey = 0;
    uint32_t heldSinceMs = 0;
    bool     longFired = false;
    bool     downInjected = false;  // key already delivered on key-down (repeat mode)
    uint32_t repeatCount = 0;       // repeats already injected for this hold
};

WindowFrontend::WindowFrontend() : impl_(new Impl()) {}

WindowFrontend::~WindowFrontend()
{
    if (impl_->texture) {
        SDL_DestroyTexture(impl_->texture);
    }
    if (impl_->renderer) {
        SDL_DestroyRenderer(impl_->renderer);
    }
    if (impl_->window) {
        SDL_DestroyWindow(impl_->window);
    }
    SDL_Quit();
    delete impl_;
}

bool WindowFrontend::open()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        LOG_W(TAG, "SDL init failed (%s) - falling back to headless",
              SDL_GetError());
        return false;
    }
    impl_->window = SDL_CreateWindow(
        "CDC Badge Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kFrameWidth * kScale, kFrameHeight * kScale, SDL_WINDOW_SHOWN);
    if (!impl_->window) {
        LOG_W(TAG, "no window available (%s) - falling back to headless",
              SDL_GetError());
        return false;
    }
    impl_->renderer = SDL_CreateRenderer(impl_->window, -1, 0);
    impl_->texture =
        SDL_CreateTexture(impl_->renderer, SDL_PIXELFORMAT_RGB24,
                          SDL_TEXTUREACCESS_STREAMING, kFrameWidth, kFrameHeight);
    return impl_->renderer && impl_->texture;
}

void WindowFrontend::onFrame(const uint8_t* frame, size_t len)
{
    (void)len;
    frameToLandscapeGray(frame, impl_->gray);
    impl_->dirty = true;
}

int WindowFrontend::run(EmulatorCore& core, Console* console)
{
    uint32_t lastMs = SDL_GetTicks();
    bool     quit = false;
    while (!quit) {
        if (console && !console->poll(core)) {
            break;
        }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                quit = true;
                break;
            case SDL_KEYDOWN: {
                if (event.key.repeat) {
                    break;
                }
                if (event.key.keysym.sym == SDLK_l) {
                    core.lock();  // L = fake lockscreen (Y unlocks)
                    break;
                }
                const char key = mapKey(event.key.keysym.sym);
                if (key) {
                    impl_->heldKey = key;
                    impl_->heldSinceMs = SDL_GetTicks();
                    impl_->longFired = false;
                    impl_->repeatCount = 0;
                    // In repeat mode the device buffers the key on key-down
                    // (repeat suppresses long-press, so there is nothing to
                    // wait for); otherwise short-vs-long resolves on key-up.
                    impl_->downInjected =
                        emu::HostKeypad::instance().keyRepeatPeriodMs() > 0;
                    if (impl_->downInjected) {
                        core.injectKey(key, false);
                    }
                }
                break;
            }
            case SDL_KEYUP: {
                const char key = mapKey(event.key.keysym.sym);
                if (key && key == impl_->heldKey) {
                    if (!impl_->longFired && !impl_->downInjected) {
                        core.injectKey(key, false);
                    }
                    impl_->heldKey = 0;
                }
                break;
            }
            default:
                break;
            }
        }

        if (impl_->heldKey) {
            const uint32_t heldMs = SDL_GetTicks() - impl_->heldSinceMs;
            const uint16_t repInitial = emu::HostKeypad::instance().keyRepeatInitialMs();
            const uint16_t repPeriod = emu::HostKeypad::instance().keyRepeatPeriodMs();
            if (repPeriod > 0 && impl_->downInjected) {
                // View requested key repeat (IKeypad::setKeyRepeat): re-inject
                // the held key on its schedule; repeat suppresses long-press,
                // exactly like the TCA9535 driver.
                while (heldMs >= static_cast<uint32_t>(repInitial) +
                                     impl_->repeatCount * repPeriod) {
                    core.injectKey(impl_->heldKey, false);
                    ++impl_->repeatCount;
                }
            } else if (!impl_->longFired && !impl_->downInjected &&
                       heldMs >= kLongPressMs) {
                // Hold past the threshold fires a long press, like the real keypad.
                core.injectKey(impl_->heldKey, true);
                impl_->longFired = true;
            }
        }

        // Wall clock drives the same deterministic advance machinery.
        const uint32_t nowMs = SDL_GetTicks();
        if (nowMs > lastMs) {
            core.advance(nowMs - lastMs);
            lastMs = nowMs;
        }

        if (impl_->dirty) {
            impl_->dirty = false;
            uint8_t rgb[kFrameWidth * kFrameHeight * 3];
            for (size_t i = 0; i < sizeof(impl_->gray); ++i) {
                // Slight warm tint so the window reads as e-paper, not LCD.
                const uint8_t v = impl_->gray[i] ? 0xF4 : 0x10;
                rgb[i * 3 + 0] = v;
                rgb[i * 3 + 1] = v;
                rgb[i * 3 + 2] = impl_->gray[i] ? 0xE8 : 0x10;
            }
            SDL_UpdateTexture(impl_->texture, nullptr, rgb, kFrameWidth * 3);
            SDL_RenderClear(impl_->renderer);
            SDL_RenderCopy(impl_->renderer, impl_->texture, nullptr, nullptr);
            SDL_RenderPresent(impl_->renderer);
        }

        SDL_Delay(10);
    }
    core.shutdown();
    return 0;
}

}  // namespace emu
