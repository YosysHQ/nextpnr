include(FindIceStorm)

set(SOURCES
    arch.cc
    archdefs.h
    arch.h
    arch_place.cc
    arch_pybindings.cc
    arch_pybindings.h
    bitstream.cc
    bitstream.h
    cells.cc
    cells.h
    chains.cc
    chains.h
    constids.inc
    delay.cc
    gfx.cc
    gfx.h
    pack.cc
    pcf.cc
    pcf.h
)

set(TEST_SOURCES
    tests/hx1k.cc
    tests/hx8k.cc
    tests/lp1k.cc
    tests/lp384.cc
    tests/lp8k.cc
    tests/main.cc
    tests/up5k.cc
)

add_nextpnr_architecture(${family}
    CORE_SOURCES ${SOURCES}
    TEST_SOURCES ${TEST_SOURCES}
    MAIN_SOURCE  main.cc
)

set(ALL_ICE40_DEVICES 384 1k 5k u4k 8k)
set(ICE40_DEVICES ${ALL_ICE40_DEVICES} CACHE STRING
    "Include support for these iCE40 devices (available: ${ALL_ICE40_DEVICES})")
message(STATUS "Enabled iCE40 devices: ${ICE40_DEVICES}")

foreach (device ${ICE40_DEVICES})
    if (NOT device IN_LIST ALL_ICE40_DEVICES)
        message(FATAL_ERROR "Device ${device} is not a supported iCE40 device")
    endif()

    if (device STREQUAL "5k")
        set(timing_opts
            --slow ${ICEBOX_DATADIR}/timings_up5k.txt)
    elseif (device STREQUAL "u4k")
        set(timing_opts
            --slow ${ICEBOX_DATADIR}/timings_u4k.txt)
    elseif (device STREQUAL "384")
        set(timing_opts
            --slow ${ICEBOX_DATADIR}/timings_lp384.txt)
    else()
        set(timing_opts
            --fast ${ICEBOX_DATADIR}/timings_hx${device}.txt
            --slow ${ICEBOX_DATADIR}/timings_lp${device}.txt)
    endif()

    add_bba_produce_command(
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/chipdb.py
            -p ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
            -g ${CMAKE_CURRENT_SOURCE_DIR}/gfx.h
            ${timing_opts}
            ${ICEBOX_DATADIR}/chipdb-${device}.txt
            > ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba.new
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        INPUTS
            ${CMAKE_CURRENT_SOURCE_DIR}/chipdb.py
            ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/gfx.h
    )

    add_bba_compile_command(
        TARGET  nextpnr-${family}-chipdb
        OUTPUT  ${family}/chipdb-${device}.bin
        INPUT   ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        MODE    ${BBASM_MODE}
    )
endforeach()
