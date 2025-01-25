if (NOT IMPORT_BBA_FILES)
    include(FindApycula)
endif()

set(SOURCES
    arch.cc
    archdefs.h
    arch.h
    arch_pybindings.cc
    arch_pybindings.h
    cells.cc
    cells.h
    constids.inc
    cst.cc
    cst.h
    gfx.cc
    gfx.h
    globals.cc
    globals.h
    pack.cc
)

add_nextpnr_architecture(${family}
    CORE_SOURCES ${SOURCES}
    MAIN_SOURCE  main.cc
)

set(ALL_GOWIN_DEVICES GW1N-1 GW1NZ-1 GW1N-4 GW1N-9 GW1N-9C GW1NS-2 GW1NS-4 GW2A-18)
set(GOWIN_DEVICES ${ALL_GOWIN_DEVICES} CACHE STRING
    "Include support for these Gowin devices (available: ${ALL_GOWIN_DEVICES})")
message(STATUS "Enabled Gowin devices: ${GOWIN_DEVICES}")

foreach (device ${GOWIN_DEVICES})
    if (NOT device IN_LIST ALL_GOWIN_DEVICES)
        message(FATAL_ERROR "Device ${device} is not a supported Gowin device")
    endif()

    add_bba_produce_command(
        TARGET  nextpnr-${family}-bba
        COMMAND ${GOWIN_BBA_EXECUTABLE}
            -d ${device}
            -i ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
            -o ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba.new
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        INPUTS
            ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
    )

    add_bba_compile_command(
        TARGET  nextpnr-${family}-chipdb
        OUTPUT  ${family}/chipdb-${device}.bin
        INPUT   ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        MODE    ${BBASM_MODE}
    )
endforeach()
