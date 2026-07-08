# Host unit + snapshot tests (CTest).
#
# Unit tests link emulator_core directly. Integration/snapshot tests drive the
# emulator binary with a built plugin .wasm; they SKIP (return 77) when
# dist/canvas_demo.wasm has not been built yet (CI builds it first via
# `badge build canvas_demo`). Family-coverage notes: see coverage_notes.md.

add_executable(test_backends test_backends.cpp)
target_link_libraries(test_backends PRIVATE emulator_core)
add_test(NAME backends COMMAND test_backends)

# T067: no irreversible TROPIC01 operation may appear in the emulator.
add_test(NAME se_safety
    COMMAND ${CMAKE_COMMAND}
            -DEMULATOR_SRC=${CMAKE_CURRENT_SOURCE_DIR}/..
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_se_safety.cmake)

# T019/T027/T043/T044: scripted keys drive the lifecycle; frames must match
# the committed reference hashes byte-for-byte (deterministic clock, FR-034).
set(CANVAS_WASM ${CMAKE_CURRENT_SOURCE_DIR}/../../dist/canvas_demo.wasm)
set(CANVAS_META
    ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/cdc-badge-plugins/examples/canvas_demo/meta.json)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Snapshot tests run through a small Python driver (exit 77 = skip when the
# .wasm has not been built; the CMake-script variant needed CMake >= 3.29).
add_test(NAME canvas_demo_snapshot
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.py
            $<TARGET_FILE:cdc-badge-emulator>
            ${CANVAS_WASM} ${CANVAS_META} Y,Y
            ${CMAKE_CURRENT_SOURCE_DIR}/snapshots/canvas_demo
            ${CMAKE_CURRENT_BINARY_DIR}/canvas_demo_run)
set_tests_properties(canvas_demo_snapshot PROPERTIES SKIP_RETURN_CODE 77)

# Guards the 64-bit ui_item_t translation (WamrImports64.cpp): the list view
# must render the plugin's labels, not garbage from a stride mismatch.
add_test(NAME mini_messenger_snapshot
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/run_snapshot.py
            $<TARGET_FILE:cdc-badge-emulator>
            ${CMAKE_CURRENT_SOURCE_DIR}/../../dist/mini_messenger.wasm
            ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/cdc-badge-plugins/examples/mini_messenger/meta.json
            Y
            ${CMAKE_CURRENT_SOURCE_DIR}/snapshots/mini_messenger
            ${CMAKE_CURRENT_BINARY_DIR}/mini_messenger_run
            1)
set_tests_properties(mini_messenger_snapshot PROPERTIES SKIP_RETURN_CODE 77)

# Fake-lockscreen residency: full unload/reload for foreground plugins,
# stay-resident-and-tick for background plugins (bg_probe fixture).
if(Python3_FOUND)
    add_test(NAME lockscreen_residency
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_lockscreen.py
                $<TARGET_FILE:cdc-badge-emulator>)
    set_tests_properties(lockscreen_residency PROPERTIES SKIP_RETURN_CODE 77)

    # Badge-to-badge messaging over JSON packet files: send -> outbox file ->
    # inject into a second instance -> handler fires (incl. headless wake-up).
    add_test(NAME msg_roundtrip
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_msg.py
                $<TARGET_FILE:cdc-badge-emulator>)
    set_tests_properties(msg_roundtrip PROPERTIES SKIP_RETURN_CODE 77)

    # Console `paste` must convert host-terminal UTF-8 to CP437 like the
    # firmware serial reader does (umlauts land as single CP437 bytes).
    add_test(NAME paste_encoding
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_paste.py
                $<TARGET_FILE:cdc-badge-emulator>)
    set_tests_properties(paste_encoding PROPERTIES SKIP_RETURN_CODE 77)
endif()

# host_net_* listener end-to-end: the webserver plugin serves index.html over
# real TCP inside a user+net namespace. Skips (77) without unshare/userns,
# curl, or a built dist/webserver.wasm. The shell script is Linux/macOS-only;
# Windows coverage comes from compile-time checks in the emulator job.
if(NOT WIN32)
    add_test(NAME webserver_http
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/webserver_http.sh
                $<TARGET_FILE:cdc-badge-emulator>
                ${CMAKE_CURRENT_SOURCE_DIR}/../../dist/webserver.wasm
                ${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/cdc-badge-plugins/plugins/webserver/meta.json)
    set_tests_properties(webserver_http PROPERTIES SKIP_RETURN_CODE 77)
endif()
