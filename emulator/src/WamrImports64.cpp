/**
 * \file WamrImports64.cpp
 * \brief 64-bit-safe overrides for the "cdc" natives that pass structs with
 *        embedded pointers across the WASM boundary.
 *
 * The firmware's WamrImports.cpp casts plugin linear memory directly to
 * `ui_item_t`, whose `const char* label` is 4 bytes on the 32-bit ESP32 but
 * 8 bytes on a 64-bit host. Reusing those wrappers off-device therefore reads
 * the item array with a 16-byte stride over a 12-byte-stride WASM layout,
 * misreads the label offsets, and WAMR's failed address validation leaves a
 * pending "out of bounds memory access" exception that traps the plugin.
 *
 * WAMR resolves natives from the most recently registered table first, so
 * registering this table AFTER register_host_imports() shadows exactly the
 * affected symbols (list push/replace/update/insert + context menu) with
 * wrappers that read the plugin's true 32-bit `#[repr(C)]` layout. Everything
 * else continues to hit the reused firmware table.
 */
#include <cstdint>
#include <cstring>

#include "cdc_views/ContextMenuView.h"
#include "cdc_views/ListView.h"
#include "plugin_manager/host_api.h"

extern "C" {
#include "wasm_export.h"
}

namespace {

/// The SDK's `#[repr(C)] UiItem` as laid out in wasm32 linear memory:
/// label offset (4) + icon (1) + icon_disabled (1) + padding (2) + id (4).
struct Wasm32UiItem {
    uint32_t label_off;
    uint8_t  icon;
    uint8_t  icon_disabled;
    uint8_t  pad[2];
    uint32_t item_id;
};
static_assert(sizeof(Wasm32UiItem) == 12, "wasm32 ui_item_t layout drifted");

/// Convert one wasm-layout item to a host ui_item_t with a translated label.
/// Mirrors the firmware wrapper's defensive behaviour: an invalid label
/// offset degrades to "" (and, exactly as on device, leaves the WAMR
/// exception pending so a genuinely corrupt plugin still traps).
ui_item_t toNative(wasm_module_inst_t inst, const Wasm32UiItem& item)
{
    ui_item_t native = {};
    native.icon = item.icon;
    native.icon_disabled = item.icon_disabled != 0;
    native.item_id = item.item_id;
    if (item.label_off != 0 &&
        wasm_runtime_validate_app_str_addr(inst, item.label_off)) {
        native.label = static_cast<const char*>(
            wasm_runtime_addr_app_to_native(inst, item.label_off));
    } else {
        native.label = "";
    }
    return native;
}

int32_t listCall(wasm_exec_env_t exec_env, const char* title, const void* items,
                 uint32_t count, uint32_t sel, uint32_t menu, bool replace)
{
    if (count == 0 || !items) {
        return replace ? host_ui_replace_list(title, nullptr, 0, sel, menu)
                       : host_ui_push_list(title, nullptr, 0, sel, menu);
    }
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    if (count > cdc::ui::ListView::MAX_ITEMS) {
        count = cdc::ui::ListView::MAX_ITEMS;
    }
    if (!wasm_runtime_validate_native_addr(
            inst, const_cast<void*>(items),
            static_cast<uint64_t>(count) * sizeof(Wasm32UiItem))) {
        return HOST_ERR_INVALID_ARG;
    }
    const auto* wasmItems = static_cast<const Wasm32UiItem*>(items);
    auto* native = new ui_item_t[count];
    for (uint32_t i = 0; i < count; ++i) {
        native[i] = toNative(inst, wasmItems[i]);
    }
    const int32_t rc =
        replace
            ? host_ui_replace_list(title, native, static_cast<uint16_t>(count),
                                   sel, menu)
            : host_ui_push_list(title, native, static_cast<uint16_t>(count), sel,
                                menu);
    delete[] native;
    return rc;
}

int32_t w64_host_ui_push_list(wasm_exec_env_t exec_env, const char* title,
                              const void* items, uint32_t count, uint32_t sel,
                              uint32_t menu)
{
    return listCall(exec_env, title, items, count, sel, menu, false);
}

int32_t w64_host_ui_replace_list(wasm_exec_env_t exec_env, const char* title,
                                 const void* items, uint32_t count, uint32_t sel,
                                 uint32_t menu)
{
    return listCall(exec_env, title, items, count, sel, menu, true);
}

int32_t w64_host_ui_push_context_menu(wasm_exec_env_t exec_env, const char* title,
                                      const void* items, uint32_t count,
                                      uint32_t sel)
{
    if (count == 0 || !items) {
        return host_ui_push_context_menu(title, nullptr, 0, sel);
    }
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    constexpr uint32_t kMaxItems = cdc::ui::ContextMenuView::MAX_ITEMS;
    if (count > kMaxItems) {
        return HOST_ERR_INVALID_ARG;
    }
    if (!wasm_runtime_validate_native_addr(
            inst, const_cast<void*>(items),
            static_cast<uint64_t>(count) * sizeof(Wasm32UiItem))) {
        return HOST_ERR_INVALID_ARG;
    }
    const auto* wasmItems = static_cast<const Wasm32UiItem*>(items);
    ui_item_t native[kMaxItems];
    for (uint32_t i = 0; i < count; ++i) {
        native[i] = toNative(inst, wasmItems[i]);
    }
    return host_ui_push_context_menu(title, native, static_cast<uint16_t>(count),
                                     sel);
}

int32_t itemCall(wasm_exec_env_t exec_env, uint32_t index, const void* item,
                 bool insert)
{
    if (!item) {
        return HOST_ERR_INVALID_ARG;
    }
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
    if (!wasm_runtime_validate_native_addr(inst, const_cast<void*>(item),
                                           sizeof(Wasm32UiItem))) {
        return HOST_ERR_INVALID_ARG;
    }
    Wasm32UiItem wasmItem;
    std::memcpy(&wasmItem, item, sizeof(wasmItem));
    const ui_item_t native = toNative(inst, wasmItem);
    return insert ? host_ui_insert_list_item(static_cast<uint16_t>(index), &native)
                  : host_ui_update_list_item(static_cast<uint16_t>(index), &native);
}

int32_t w64_host_ui_update_list_item(wasm_exec_env_t exec_env, uint32_t index,
                                     const void* item)
{
    return itemCall(exec_env, index, item, false);
}

int32_t w64_host_ui_insert_list_item(wasm_exec_env_t exec_env, uint32_t index,
                                     const void* item)
{
    return itemCall(exec_env, index, item, true);
}

/* Same symbol names and signatures as the firmware table; resolution order
 * (newest-first) makes these win. */
NativeSymbol s_overrides[] = {
    {"host_ui_push_list", (void*)w64_host_ui_push_list, "($*~ii)i", nullptr},
    {"host_ui_replace_list", (void*)w64_host_ui_replace_list, "($*~ii)i", nullptr},
    {"host_ui_push_context_menu", (void*)w64_host_ui_push_context_menu, "($*~i)i", nullptr},
    {"host_ui_update_list_item", (void*)w64_host_ui_update_list_item, "(i*)i", nullptr},
    {"host_ui_insert_list_item", (void*)w64_host_ui_insert_list_item, "(i*)i", nullptr},
};

}  // namespace

namespace emu {

/// Register after cdc::plugin_manager::register_host_imports(). No-op on
/// 32-bit hosts, where the firmware wrappers are already layout-correct.
bool registerHostImportOverrides()
{
    if (sizeof(void*) == 4) {
        return true;
    }
    const uint32_t n = sizeof(s_overrides) / sizeof(s_overrides[0]);
    return wasm_runtime_register_natives("cdc", s_overrides, n);
}

}  // namespace emu
