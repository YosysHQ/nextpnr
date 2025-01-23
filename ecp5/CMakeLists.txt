if (NOT IMPORT_BBA_FILES)
    include(FindTrellis)
endif()

set(SOURCES
    arch.cc
    archdefs.h
    arch.h
    arch_place.cc
    arch_pybindings.cc
    arch_pybindings.h
    baseconfigs.cc
    bitstream.cc
    bitstream.h
    cells.cc
    cells.h
    config.cc
    config.h
    constids.inc
    dcu_bitstream.h
    gfx.cc
    gfx.h
    globals.cc
    globals.h
    iotypes.inc
    lpf.cc
    pack.cc
    pio.cc
    pio.h
)

add_nextpnr_architecture(${family}
    CORE_SOURCES ${SOURCES}
    MAIN_SOURCE  main.cc
)

set(ALL_ECP5_DEVICES 25k 45k 85k)
set(ECP5_DEVICES ${ALL_ECP5_DEVICES} CACHE STRING
    "Include support for these ECP5 devices (available: ${ALL_ECP5_DEVICES})")
message(STATUS "Enabled ECP5 devices: ${ECP5_DEVICES}")

foreach (device ${ECP5_DEVICES})
    if (NOT device IN_LIST ALL_ECP5_DEVICES)
        message(FATAL_ERROR "Device ${device} is not a supported ECP5 device")
    endif()

    add_bba_produce_command(
        TARGET  nextpnr-${family}-bba
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/trellis_import.py
            -L ${TRELLIS_LIBDIR}
            -L ${TRELLIS_DATADIR}/util/common
            -L ${TRELLIS_DATADIR}/timing/util
            -p ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
            -g ${CMAKE_CURRENT_SOURCE_DIR}/gfx.h
            ${device}
            > ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba.new
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        INPUTS
            ${CMAKE_CURRENT_SOURCE_DIR}/trellis_import.py
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
