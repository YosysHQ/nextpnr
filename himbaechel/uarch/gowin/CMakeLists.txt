include(FindApycula)

set(SOURCES
    constids.inc
    cst.cc
    cst.h
    globals.cc
    globals.h
    gowin.cc
    gowin.h
    gowin_utils.cc
    gowin_utils.h
    pack.cc
    pack.h
)

add_nextpnr_himbaechel_microarchitecture(${uarch}
    CORE_SOURCES ${SOURCES}
)

set(ALL_HIMBAECHEL_GOWIN_DEVICES GW1N-1 GW1NZ-1 GW1N-4 GW1N-9 GW1N-9C GW1NS-4 GW2A-18 GW2A-18C)
set(HIMBAECHEL_GOWIN_DEVICES ${ALL_HIMBAECHEL_GOWIN_DEVICES} CACHE STRING
    "Include support for these Gowin devices (available: ${ALL_HIMBAECHEL_GOWIN_DEVICES})")
if (HIMBAECHEL_GOWIN_DEVICES STREQUAL "all")
	set(HIMBAECHEL_GOWIN_DEVICES ${ALL_HIMBAECHEL_GOWIN_DEVICES})
endif()
message(STATUS "Enabled Himbaechel-Gowin devices: ${HIMBAECHEL_GOWIN_DEVICES}")

foreach (device ${HIMBAECHEL_GOWIN_DEVICES})
    if (NOT device IN_LIST ALL_HIMBAECHEL_GOWIN_DEVICES)
        message(FATAL_ERROR "Device ${device} is not a supported Gowin device")
    endif()

    add_bba_produce_command(
        COMMAND ${apycula_Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/gowin_arch_gen.py
            -d ${device}
            -o ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba.new
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        INPUTS
            ${CMAKE_CURRENT_SOURCE_DIR}/gowin_arch_gen.py
            ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
    )

    add_bba_compile_command(
        TARGET  nextpnr-himbaechel-gowin-chipdb
        OUTPUT  himbaechel/gowin/chipdb-${device}.bin
        INPUT   ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        MODE    binary
    )
endforeach()
