set(SOURCES
    bitstream.cc
    ccf.cc
    cells.cc
    config.cc
    config.h
    constids.inc
    extra_data.h
    gatemate.cc
    gatemate.h
    gfx.cc
    gfxids.inc
    pack.cc
    pack_bram.cc
    pack_clocking.cc
    pack_cpe.cc
    pack_io.cc
    pack_mult.cc
    pack_serdes.cc
    pack.h
    pll.cc
    route_clock.cc
)

set(TEST_SOURCES
    tests/lut.cc
    tests/testing.cc
    tests/main.cc
)

add_nextpnr_himbaechel_microarchitecture(${uarch}
    CORE_SOURCES ${SOURCES}
    TEST_SOURCES ${TEST_SOURCES}
)

set(HIMBAECHEL_PEPPERCORN_PATH "" CACHE STRING
    "Path to a Project Peppercorn database")
if (NOT HIMBAECHEL_PEPPERCORN_PATH AND NOT IMPORT_BBA_FILES)
    message(FATAL_ERROR "HIMBAECHEL_PEPPERCORN_PATH must be set to a Project Peppercorn checkout")
endif()

#set(ALL_HIMBAECHEL_GATEMATE_DEVICES CCGM1A1 CCGM1A2 CCGM1A4 CCGM1A9 CCGM1A16 CCGM1A25)
set(ALL_HIMBAECHEL_GATEMATE_DEVICES CCGM1A1 CCGM1A2)
set(HIMBAECHEL_GATEMATE_DEVICES ${ALL_HIMBAECHEL_GATEMATE_DEVICES} CACHE STRING
    "Include support for these GateMate devices (available: ${ALL_HIMBAECHEL_GATEMATE_DEVICES})")
if (HIMBAECHEL_GATEMATE_DEVICES STREQUAL "all")
	set(HIMBAECHEL_GATEMATE_DEVICES ${ALL_HIMBAECHEL_GATEMATE_DEVICES})
endif()
message(STATUS "Enabled Himbaechel-GateMate devices: ${HIMBAECHEL_GATEMATE_DEVICES}")

foreach (device ${HIMBAECHEL_GATEMATE_DEVICES})
    if (NOT device IN_LIST ALL_HIMBAECHEL_GATEMATE_DEVICES)
        message(FATAL_ERROR "Device ${device} is not a supported GateMate device")
    endif()

    add_bba_produce_command(
        TARGET  nextpnr-himbaechel-gatemate-bba
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/gen/arch_gen.py
            --lib ${HIMBAECHEL_PEPPERCORN_PATH}/gatemate
            --device ${device}
            --bba ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba.new
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        INPUTS
            ${CMAKE_CURRENT_SOURCE_DIR}/gen/arch_gen.py
            ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/gfxids.inc
    )

    add_bba_compile_command(
        TARGET  nextpnr-himbaechel-gatemate-chipdb
        OUTPUT  himbaechel/gatemate/chipdb-${device}.bin
        INPUT   ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        MODE    binary
    )
endforeach()
