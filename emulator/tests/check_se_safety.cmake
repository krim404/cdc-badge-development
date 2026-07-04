# SE safety test (T067, FR-022/SC-008): the software secure element must not
# expose or implement ANY irreversible TROPIC01 operation. Fails when a
# forbidden token appears in the emulator sources (pairing-key write or
# invalidate, monotonic counters, I-Config/OTP writes).
#
# Run: cmake -DEMULATOR_SRC=<emulator dir> -P check_se_safety.cmake

if(NOT EMULATOR_SRC)
    message(FATAL_ERROR "pass -DEMULATOR_SRC=<emulator source dir>")
endif()

set(FORBIDDEN
    lt_pairing_key_write
    lt_pairing_key_invalidate
    lt_mcounter_init
    lt_mcounter_update
    lt_i_config_write
    pairing_key_invalidate
    mcounter_update
    i_config_write
)

file(GLOB_RECURSE SOURCES
    ${EMULATOR_SRC}/src/*.cpp ${EMULATOR_SRC}/src/*.h
    ${EMULATOR_SRC}/shim/*.h)

set(FAILED FALSE)
foreach(source ${SOURCES})
    file(READ ${source} content)
    foreach(token ${FORBIDDEN})
        string(FIND "${content}" "${token}" hit)
        if(NOT hit EQUAL -1)
            message(SEND_ERROR "irreversible-operation token '${token}' found in ${source}")
            set(FAILED TRUE)
        endif()
    endforeach()
endforeach()

if(FAILED)
    message(FATAL_ERROR "SE safety check FAILED")
endif()
message(STATUS "SE safety check passed: no irreversible operation is exposed")
