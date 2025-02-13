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
    gfx.cc
    gfx.h
    globals.cc
    lpf.cc
    pack.cc
)

add_nextpnr_architecture(${family}
    CORE_SOURCES ${SOURCES}
    MAIN_SOURCE  main.cc
)

# Note that the four *X (MachXO) devices fail to import with prjtrellis commit 14ac883fa.
set(ALL_MACHXO2_DEVICES 256X 640X 1200X 2280X 256 640 1200 2000 4000 7000 1300 2100 4300 6900 9400 4300D 9400D)
set(MACHXO2_DEVICES 1200 6900 CACHE STRING
    "Include support for these MachXO2/XO3 devices (available: ${ALL_MACHXO2_DEVICES})")
message(STATUS "Enabled MachXO2/XO3 devices: ${MACHXO2_DEVICES}")

configure_file(machxo2_available.h.in ${CMAKE_CURRENT_BINARY_DIR}/machxo2_available.h)
target_sources(nextpnr-${family}-core PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/machxo2_available.h)
target_include_directories(nextpnr-${family}-core INTERFACE ${CMAKE_CURRENT_BINARY_DIR})
if (BUILD_GUI)
    target_include_directories(nextpnr-${family}-gui PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()

foreach (device ${MACHXO2_DEVICES})
    if (NOT device IN_LIST ALL_MACHXO2_DEVICES)
        message(FATAL_ERROR "Device ${device} is not a supported MachXO2/XO3 device")
    endif()

    add_bba_produce_command(
        TARGET  nextpnr-${family}-bba
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/facade_import.py
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
            ${CMAKE_CURRENT_SOURCE_DIR}/facade_import.py
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
