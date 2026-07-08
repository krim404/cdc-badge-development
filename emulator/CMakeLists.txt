# CDC Badge off-device plugin emulator (spec 022).
#
# A host CMake target that compiles selected READ-ONLY vendored cdc-badge-os
# sources (drawing stack, view stack, plugin manager host-API bodies, WAMR)
# against the shim/ ESP-IDF headers and the host backends in src/backends/.
# The vendored firmware is consumed, never patched (research D3).
cmake_minimum_required(VERSION 3.20)
project(cdc-badge-emulator CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)  # firmware builds with gnu++17
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(VENDOR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/cdc-badge-os/components)
set(SHIM ${CMAKE_CURRENT_SOURCE_DIR}/shim)

if(NOT EXISTS ${VENDOR}/plugin_manager/src/WamrImports.cpp)
    message(FATAL_ERROR "vendor/cdc-badge-os submodule missing - run: "
                        "git submodule update --init vendor/cdc-badge-os && "
                        "git -C vendor/cdc-badge-os submodule update --init "
                        "components/Adafruit-GFX components/CalEPD "
                        "components/wamr_runtime/wasm-micro-runtime")
endif()

# --------------------------------------------------------------------------
# Third-party dependencies (pinned; verified against upstream releases)
# --------------------------------------------------------------------------
include(FetchContent)

# cJSON 1.7.x declares cmake_minimum_required(2.8), which CMake 4.x runners
# refuse outright; this floor keeps the fetched subprojects configurable.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# mbedTLS 3.6 LTS - matches the ESP-IDF 5.x mbedTLS API the firmware uses.
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_AS_SUBPROJECT ON)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG        mbedtls-3.6.6
    GIT_SHALLOW    TRUE)

# cJSON - the manifest parser the firmware gets from ESP-IDF's json component.
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UNINSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG        v1.7.19
    GIT_SHALLOW    TRUE)

# Monocypher - Ed25519 (SHA-512) for the software secure element; mbedTLS has
# no Ed25519 signing. Not a CMake project: compiled directly below.
FetchContent_Declare(monocypher
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG        4.0.3
    GIT_SHALLOW    TRUE)

# stb_image_write - single-header PNG writer (pinned commit).
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        31c1ad37456438565541f4919958214b6e762fb4
    GIT_SHALLOW    FALSE)

# LodePNG - compact PNG decoder for the host png_image shim.
FetchContent_Declare(lodepng
    GIT_REPOSITORY https://github.com/lvandeve/lodepng.git
    GIT_TAG        ed6fe5825c6a4fbb7f58ab35a4231c7543cd452a
    GIT_SHALLOW    FALSE)

# espressif/qrcode 0.2.0 - the QR engine behind host_qr_* / QrRaster (same
# pinned commit as the firmware's managed component).
FetchContent_Declare(idf_extra
    GIT_REPOSITORY https://github.com/espressif/idf-extra-components.git
    GIT_TAG        3117b1e6806738c0271bbd18df17f6d74ec66452
    GIT_SHALLOW    FALSE)

FetchContent_MakeAvailable(mbedtls cjson monocypher stb lodepng idf_extra)

add_library(lodepng_static STATIC ${lodepng_SOURCE_DIR}/lodepng.cpp)
target_include_directories(lodepng_static PUBLIC ${lodepng_SOURCE_DIR})
if(NOT MSVC)
    set_source_files_properties(${lodepng_SOURCE_DIR}/lodepng.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

add_library(esp_qrcode STATIC
    ${idf_extra_SOURCE_DIR}/qrcode/esp_qrcode_main.c
    ${idf_extra_SOURCE_DIR}/qrcode/esp_qrcode_wrapper.c
    ${idf_extra_SOURCE_DIR}/qrcode/qrcodegen.c)
target_include_directories(esp_qrcode PUBLIC
    ${idf_extra_SOURCE_DIR}/qrcode/include
    ${idf_extra_SOURCE_DIR}/qrcode)
target_include_directories(esp_qrcode PRIVATE ${SHIM})
# On-device the IDF log header chain pulls in stdlib.h; the shim's does not.
target_compile_options(esp_qrcode PRIVATE -include stdlib.h)

add_library(monocypher_ed25519 STATIC
    ${monocypher_SOURCE_DIR}/src/monocypher.c
    ${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c)
target_include_directories(monocypher_ed25519 PUBLIC
    ${monocypher_SOURCE_DIR}/src
    ${monocypher_SOURCE_DIR}/src/optional)

# --------------------------------------------------------------------------
# WAMR desktop build (research D7): same interpreter flags as the device,
# minus the ESP32-S3 PSRAM workarounds.
# --------------------------------------------------------------------------
if(WIN32)
    set(WAMR_BUILD_PLATFORM "windows")
    # We build with MinGW GCC (the vendored firmware is GCC-only code); WAMR's
    # hardware bound checks use MSVC SEH (__try) on Windows, so fall back to
    # the software bound checks - plugin-visible behaviour is identical.
    set(WAMR_DISABLE_HW_BOUND_CHECK 1)
elseif(APPLE)
    set(WAMR_BUILD_PLATFORM "darwin")
else()
    set(WAMR_BUILD_PLATFORM "linux")
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(WAMR_BUILD_TARGET "AARCH64")
else()
    set(WAMR_BUILD_TARGET "X86_64")
endif()

set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_MULTI_MODULE 0)
set(WAMR_BUILD_SHARED_MEMORY 0)
set(WAMR_BUILD_MINI_LOADER 0)
set(WAMR_BUILD_REF_TYPES 1)
set(WAMR_BUILD_BULK_MEMORY 1)
set(WAMR_BUILD_LIB_PTHREAD 0)
set(WAMR_BUILD_INSTRUCTION_METERING 1)

set(WAMR_ROOT_DIR ${VENDOR}/wamr_runtime/wasm-micro-runtime)
include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

# With Ninja + MinGW GCC the MINGW variable is not always set when WAMR's
# iwasm_common.cmake picks the invokeNative trampoline, leaving the symbol
# undefined at link time; add the MinGW GAS implementation explicitly.
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    enable_language(ASM)
    set(EMU_INVOKE_NATIVE
        ${IWASM_DIR}/common/arch/invokeNative_mingw_x64.s)
    list(FIND WAMR_RUNTIME_LIB_SOURCE ${EMU_INVOKE_NATIVE} emu_invoke_idx)
    if(emu_invoke_idx EQUAL -1)
        list(APPEND WAMR_RUNTIME_LIB_SOURCE ${EMU_INVOKE_NATIVE})
    endif()
endif()

add_library(wamr STATIC ${WAMR_RUNTIME_LIB_SOURCE})
target_include_directories(wamr PUBLIC
    ${WAMR_ROOT_DIR}/core/iwasm/include
    ${SHARED_DIR}/include
    ${IWASM_DIR}/common)

# --------------------------------------------------------------------------
# Reused vendored firmware sources (READ-ONLY)
# --------------------------------------------------------------------------
set(GFX ${VENDOR}/Adafruit-GFX)
set(CALEPD ${VENDOR}/CalEPD)
set(PLUGIN_MANAGER ${VENDOR}/plugin_manager)

set(VENDOR_SOURCES
    # Drawing core (portable) + panel driver (shimmed transport)
    ${GFX}/Adafruit_GFX.cpp
    ${GFX}/Print.cpp
    ${GFX}/WString.cpp
    ${GFX}/glcdfont.c
    ${GFX}/srdlib_noniso.c
    ${CALEPD}/epd.cpp
    ${CALEPD}/models/goodisplay/gdey029T94.cpp
    # Core utilities
    ${VENDOR}/cdc_core/src/EventBus.cpp
    ${VENDOR}/cdc_core/src/Cp437.cpp
    ${VENDOR}/cdc_core/src/TropicSlotMap.cpp
    # UI framework + views (the plugin-visible UI surface)
    ${VENDOR}/cdc_ui/src/ViewStack.cpp
    ${VENDOR}/cdc_ui/src/I18n.cpp
    ${VENDOR}/cdc_views/src/CanvasAnim.cpp
    ${VENDOR}/cdc_views/src/CanvasView.cpp
    ${VENDOR}/cdc_views/src/ColorPickerView.cpp
    ${VENDOR}/cdc_views/src/ConfirmView.cpp
    ${VENDOR}/cdc_views/src/ContextMenuView.cpp
    ${VENDOR}/cdc_views/src/DateInputView.cpp
    ${VENDOR}/cdc_views/src/Fonts.cpp
    ${VENDOR}/cdc_views/src/HtmlViewerHook.cpp
    ${VENDOR}/cdc_views/src/ImageView.cpp
    ${VENDOR}/cdc_views/src/InfoView.cpp
    ${VENDOR}/cdc_views/src/ListView.cpp
    ${VENDOR}/cdc_views/src/MarkdownParser.cpp
    ${VENDOR}/cdc_views/src/MarkdownView.cpp
    ${VENDOR}/cdc_views/src/MessageBox.cpp
    ${VENDOR}/cdc_views/src/PasswordT9View.cpp
    ${VENDOR}/cdc_views/src/PinEntryView.cpp
    ${VENDOR}/cdc_views/src/RenderHelpers.cpp
    ${VENDOR}/cdc_views/src/SliderView.cpp
    ${VENDOR}/cdc_views/src/T9InputView.cpp
    ${VENDOR}/cdc_views/src/TimeInputView.cpp
    ${VENDOR}/cdc_views/src/ToastView.cpp
    ${VENDOR}/cdc_views/src/QrRaster.cpp
    ${VENDOR}/cdc_views/src/MonoSurface.cpp
    # Image pipeline used by ImageView / host_fs image viewing
    ${VENDOR}/cdc_image/src/Dither.cpp
    ${VENDOR}/cdc_image/src/ImageDecoder.cpp
    ${VENDOR}/cdc_image/src/ImageFormat.cpp
    ${VENDOR}/cdc_image/src/JpegEncoder.cpp
    # vCard store backing host_vcard_* (NVS-shimmed)
    ${VENDOR}/mod_vcard/src/vcard_store.cpp
    # Vendored IJG libjpeg (decoder path of ImageDecoder)
    ${VENDOR}/libjpeg/jaricom.c ${VENDOR}/libjpeg/jcomapi.c
    ${VENDOR}/libjpeg/jdapimin.c ${VENDOR}/libjpeg/jdapistd.c
    ${VENDOR}/libjpeg/jdarith.c ${VENDOR}/libjpeg/jdatadst.c
    ${VENDOR}/libjpeg/jdatasrc.c ${VENDOR}/libjpeg/jdcoefct.c
    ${VENDOR}/libjpeg/jdcolor.c ${VENDOR}/libjpeg/jddctmgr.c
    ${VENDOR}/libjpeg/jdhuff.c ${VENDOR}/libjpeg/jdinput.c
    ${VENDOR}/libjpeg/jdmainct.c ${VENDOR}/libjpeg/jdmarker.c
    ${VENDOR}/libjpeg/jdmaster.c ${VENDOR}/libjpeg/jdmerge.c
    ${VENDOR}/libjpeg/jdpostct.c ${VENDOR}/libjpeg/jdsample.c
    ${VENDOR}/libjpeg/jdtrans.c ${VENDOR}/libjpeg/jerror.c
    ${VENDOR}/libjpeg/jidctflt.c ${VENDOR}/libjpeg/jidctfst.c
    ${VENDOR}/libjpeg/jidctint.c ${VENDOR}/libjpeg/jquant1.c
    ${VENDOR}/libjpeg/jquant2.c ${VENDOR}/libjpeg/jutils.c
    ${VENDOR}/libjpeg/jmemmgr.c ${VENDOR}/libjpeg/jmem_psram.c
    # libjpeg compressor path (host_surface_export_jpg)
    ${VENDOR}/libjpeg/jcapimin.c ${VENDOR}/libjpeg/jcapistd.c
    ${VENDOR}/libjpeg/jcarith.c ${VENDOR}/libjpeg/jccoefct.c
    ${VENDOR}/libjpeg/jccolor.c ${VENDOR}/libjpeg/jcdctmgr.c
    ${VENDOR}/libjpeg/jchuff.c ${VENDOR}/libjpeg/jcinit.c
    ${VENDOR}/libjpeg/jcmainct.c ${VENDOR}/libjpeg/jcmarker.c
    ${VENDOR}/libjpeg/jcmaster.c ${VENDOR}/libjpeg/jcparam.c
    ${VENDOR}/libjpeg/jcprepct.c ${VENDOR}/libjpeg/jcsample.c
    ${VENDOR}/libjpeg/jctrans.c ${VENDOR}/libjpeg/jfdctflt.c
    ${VENDOR}/libjpeg/jfdctfst.c ${VENDOR}/libjpeg/jfdctint.c
    # Plugin manager: manifest, capability checks, WASM module, host API
    ${PLUGIN_MANAGER}/src/Plugin.cpp
    ${PLUGIN_MANAGER}/src/PluginManifest.cpp
    ${PLUGIN_MANAGER}/src/CapabilityChecker.cpp
    ${PLUGIN_MANAGER}/src/Prerequisites.cpp
    ${PLUGIN_MANAGER}/src/PluginUiState.cpp
    ${PLUGIN_MANAGER}/src/WamrImports.cpp
    ${PLUGIN_MANAGER}/src/host_str_conv.cpp
    ${PLUGIN_MANAGER}/src/plugin_log_bridge.cpp
    # In-scope host-API families, reused unchanged (FR-008)
    ${PLUGIN_MANAGER}/src/host_api_canvas.cpp
    ${PLUGIN_MANAGER}/src/host_api_cmd.cpp
    ${PLUGIN_MANAGER}/src/host_api_crypto.cpp
    ${PLUGIN_MANAGER}/src/host_api_display.cpp
    ${PLUGIN_MANAGER}/src/host_api_event.cpp
    ${PLUGIN_MANAGER}/src/host_api_fs.cpp
    ${PLUGIN_MANAGER}/src/host_api_http.cpp
    ${PLUGIN_MANAGER}/src/host_api_i18n.cpp
    ${PLUGIN_MANAGER}/src/host_api_keypad.cpp
    ${PLUGIN_MANAGER}/src/host_api_lockscreen.cpp
    ${PLUGIN_MANAGER}/src/host_api_log.cpp
    ${PLUGIN_MANAGER}/src/host_api_msg.cpp
    ${PLUGIN_MANAGER}/src/host_api_net.cpp
    ${PLUGIN_MANAGER}/src/host_api_lifecycle.cpp
    ${PLUGIN_MANAGER}/src/host_api_nvs.cpp
    ${PLUGIN_MANAGER}/src/host_api_power.cpp
    ${PLUGIN_MANAGER}/src/host_api_se.cpp
    ${PLUGIN_MANAGER}/src/host_api_socket.cpp
    ${PLUGIN_MANAGER}/src/host_api_strings.cpp
    ${PLUGIN_MANAGER}/src/host_api_sysinfo.cpp
    ${PLUGIN_MANAGER}/src/host_api_time.cpp
    ${PLUGIN_MANAGER}/src/host_api_ui.cpp
    ${PLUGIN_MANAGER}/src/host_api_ui_views.cpp
    ${PLUGIN_MANAGER}/src/host_api_wifi.cpp
    # Host API 0.8 additions (pure API families, no hardware)
    ${PLUGIN_MANAGER}/src/host_api_qr.cpp
    ${PLUGIN_MANAGER}/src/host_api_image.cpp
    ${PLUGIN_MANAGER}/src/host_api_surface.cpp
    ${PLUGIN_MANAGER}/src/host_api_vcard.cpp
    ${PLUGIN_MANAGER}/src/host_api_ext_feature.cpp
)

# --------------------------------------------------------------------------
# Emulator sources
# --------------------------------------------------------------------------
set(EMULATOR_SOURCES
    src/EmulatorCore.cpp
    src/EpdSpiStub.cpp
    src/PluginManagerHost.cpp
    src/PluginStorageHost.cpp
    src/StubApis.cpp
    src/WamrImports64.cpp
    src/backends/HostCpuStats.cpp
    src/backends/HostDisplay.cpp
    src/backends/HostHttpClient.cpp
    src/backends/HostKeypad.cpp
    src/backends/HostMessageTransfer.cpp
    src/backends/HostNet.cpp
    src/backends/HostNvs.cpp
    src/backends/JsonStore.cpp
    src/backends/LockscreenView.cpp
    src/backends/HostPower.cpp
    src/backends/HostRtc.cpp
    src/backends/HostSecureElement.cpp
    src/backends/VirtualClock.cpp
    src/frontends/Console.cpp
    src/frontends/PngFrontend.cpp
    src/shim/CalepdPrintf.cpp
    src/shim/EspShim.cpp
    src/shim/FreeRtosShim.cpp
    src/shim/HostLog.cpp
    src/shim/PngShim.cpp
    src/shim/StdCompat.cpp
)

# Everything except main.cpp lives in a static lib so the tests link it too.
add_library(emulator_core STATIC ${EMULATOR_SOURCES} ${VENDOR_SOURCES})

add_executable(cdc-badge-emulator src/main.cpp)
target_link_libraries(cdc-badge-emulator PRIVATE emulator_core)

target_include_directories(emulator_core PUBLIC
    ${SHIM}                        # ESP-IDF shim headers FIRST
    ${GFX}
    ${CALEPD}/include
    ${VENDOR}/cdc_core/include
    ${VENDOR}/cdc_hal/include
    ${VENDOR}/cdc_image/include
    ${VENDOR}/libjpeg
    ${VENDOR}/cdc_log/include
    ${VENDOR}/cdc_msg/include
    ${VENDOR}/mod_vcard/include
    ${VENDOR}/cdc_os_ui/include
    ${VENDOR}/cdc_ui/include
    ${VENDOR}/cdc_views/include
    ${PLUGIN_MANAGER}/include
    ${PLUGIN_MANAGER}/src          # host_str_conv.h, HexUtil.h
    ${VENDOR}/../main              # tropic_slot_map.h (central slot map)
    ${stb_SOURCE_DIR}
    ${lodepng_SOURCE_DIR}
    ${cjson_SOURCE_DIR}
    src
)

target_compile_definitions(emulator_core PUBLIC
    APP_VERSION="emulator"
)
if(WIN32)
    # Keep windows.h from leaking macros (ERROR from wingdi breaks the
    # vendored ServiceState enum; min/max break std usage).
    target_compile_definitions(emulator_core PUBLIC
        WIN32_LEAN_AND_MEAN NOGDI NOMINMAX)
endif()

target_link_libraries(emulator_core PUBLIC
    wamr
    cjson
    esp_qrcode
    mbedtls
    mbedcrypto
    mbedx509
    monocypher_ed25519
    lodepng_static
)

find_package(Threads REQUIRED)
target_link_libraries(emulator_core PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(emulator_core PUBLIC ws2_32)
endif()

# Self-contained release binaries (FR-028/029): WAMR, mbedTLS, cJSON,
# Monocypher, LodePNG and stb are always linked statically; this additionally links
# libstdc++/libgcc statically so the binary runs on older distros/macOS
# without matching runtime packages.
option(EMULATOR_STATIC_RUNTIME "Statically link the C++ runtime (release artifacts)" OFF)
if(EMULATOR_STATIC_RUNTIME AND NOT MSVC AND NOT APPLE)
    target_link_options(cdc-badge-emulator PRIVATE -static-libstdc++ -static-libgcc)
endif()
if(EMULATOR_STATIC_RUNTIME AND MSVC)
    set_property(TARGET cdc-badge-emulator emulator_core wamr monocypher_ed25519
                 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Interactive window frontend (SDL2). Optional: without it the build is
# headless-only (FR-031/FR-032).
option(EMULATOR_WINDOW "Build the interactive SDL2 window frontend" ON)
if(EMULATOR_WINDOW)
    find_package(SDL2 QUIET)
    if(SDL2_FOUND)
        target_sources(emulator_core PRIVATE src/frontends/WindowFrontend.cpp)
        target_compile_definitions(emulator_core PUBLIC EMULATOR_WITH_SDL2)
        # On macOS prefer the static SDL2: a single self-contained binary
        # sidesteps the ad-hoc-signed-dylib library-validation hang that a
        # provenanced (CI-built) process hits when it dlopen()s a dylib whose
        # signature differs from the executable's.
        if(APPLE AND TARGET SDL2::SDL2-static)
            target_link_libraries(emulator_core PUBLIC SDL2::SDL2-static)
            message(STATUS "emulator: SDL2 window frontend enabled (static)")
        elseif(TARGET SDL2::SDL2)
            target_link_libraries(emulator_core PUBLIC SDL2::SDL2)
            message(STATUS "emulator: SDL2 window frontend enabled")
        else()
            target_include_directories(emulator_core PUBLIC ${SDL2_INCLUDE_DIRS})
            target_link_libraries(emulator_core PUBLIC ${SDL2_LIBRARIES})
            message(STATUS "emulator: SDL2 window frontend enabled")
        endif()
    else()
        message(STATUS "emulator: SDL2 not found - building headless-only")
    endif()
endif()

# Vendored firmware + WAMR generate warnings we must not fail on; keep the
# emulator's own sources honest instead.
if(NOT MSVC)
    set_source_files_properties(${VENDOR_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
    target_compile_options(emulator_core PRIVATE -Wall -Wextra)
    # stb single-header writer trips -Wextra; not our code.
    set_source_files_properties(src/frontends/PngFrontend.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")
    # CalEPD prints banners and always-0 refresh STATS to stdout; Adafruit-GFX
    # prints a "write(NN) Custom font" line per classic-font glyph. Reroute the
    # vendored firmware's printf into the debug log so stdout stays
    # machine-readable (must come after the blanket VENDOR_SOURCES property,
    # which it replaces).
    set_source_files_properties(
        ${CALEPD}/epd.cpp ${CALEPD}/models/goodisplay/gdey029T94.cpp
        ${GFX}/Adafruit_GFX.cpp
        PROPERTIES COMPILE_OPTIONS
        "-w;-include;${CMAKE_CURRENT_SOURCE_DIR}/src/shim/calepd_printf_redirect.h")
endif()

enable_testing()
add_subdirectory(tests)
