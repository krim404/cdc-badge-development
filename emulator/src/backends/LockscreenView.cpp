#include "LockscreenView.h"

#include <cstdio>

#include "cdc_hal/IDisplay.h"
#include "goodisplay/gdey029T94.h"
#include "plugin_manager/LockscreenRegistry.h"
#include "plugin_manager/Plugin.h"
#include "plugin_manager/PluginManager.h"

namespace emu {

void LockscreenView::render(bool partial)
{
    (void)partial;
    auto* display = cdc::hal::getDisplayInstance();
    if (!display) {
        return;
    }
    auto* gfx = static_cast<Gdey029T94*>(display->getNativeHandle());
    if (!gfx) {
        return;
    }

    gfx->fillScreen(EPD_WHITE);
    gfx->setTextColor(EPD_BLACK);
    gfx->setTextSize(2);
    gfx->setCursor(10, 18);
    gfx->print("LOCKED");
    gfx->setTextSize(1);
    gfx->setCursor(10, 44);
    gfx->print("Emulator fake lockscreen");

    // Plugin quick-actions, selectable by digit like the badge's menu.
    cdc::plugin_manager::LockscreenRegistration items[8];
    const uint8_t count = cdc::plugin_manager::collectLockscreenItems(items, 8);
    int16_t y = 62;
    for (uint8_t i = 0; i < count; ++i) {
        auto* plugin =
            static_cast<cdc::plugin_manager::Plugin*>(items[i].plugin);
        const char* label = items[i].label_key;
        if (plugin) {
            if (const char* translated = plugin->trKey(items[i].label_key)) {
                label = translated;
            }
        }
        char line[48];
        snprintf(line, sizeof(line), "[%u] %s", i + 1, label);
        gfx->setCursor(10, y);
        gfx->print(line);
        y += 12;
    }

    gfx->setCursor(10, 120);
    gfx->print("[Y] unlock");
    display->flushSync(cdc::hal::RefreshMode::PARTIAL);
    dirty_ = false;
}

cdc::ui::InputResult LockscreenView::onKey(char key)
{
    if (key == 'Y') {
        if (on_unlock_) {
            on_unlock_(unlock_ctx_);
        }
        return cdc::ui::InputResult::CONSUMED;
    }
    if (key >= '1' && key <= '8') {
        cdc::plugin_manager::LockscreenRegistration items[8];
        const uint8_t count =
            cdc::plugin_manager::collectLockscreenItems(items, 8);
        const uint8_t index = static_cast<uint8_t>(key - '1');
        if (index < count) {
            // Same dispatch the badge lockscreen menu performs.
            auto* plugin =
                static_cast<cdc::plugin_manager::Plugin*>(items[index].plugin);
            cdc::plugin_manager::PluginManager::instance().dispatchActionTo(
                plugin, items[index].action_id, 0, 0);
            return cdc::ui::InputResult::CONSUMED;
        }
    }
    return cdc::ui::InputResult::IGNORED;
}

}  // namespace emu
