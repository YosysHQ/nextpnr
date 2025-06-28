set(SOURCES
    cells.cc
    constids.inc
    extra_data.h
    fasm.cc
    pack_carry.cc
    pack.cc
    pack_clocking.cc
    pack_dram.cc
    pack_dsp_xc7.cc
    pack.h
    pack_io.cc
    pins.cc
    pins.h
    xdc.cc
    xilinx.cc
    xilinx.h
    xilinx_place.cc
)

add_nextpnr_himbaechel_microarchitecture(${uarch}
    CORE_SOURCES ${SOURCES}
)

set(HIMBAECHEL_PRJXRAY_DB "" CACHE STRING
    "Path to a project x-ray database")
if (NOT HIMBAECHEL_PRJXRAY_DB AND NOT IMPORT_BBA_FILES)
    message(FATAL_ERROR "HIMBAECHEL_PRJXRAY_DB must be set to a prjxray database checkout")
endif()

set(ALL_HIMBAECHEL_XILINX_DEVICES xc7a100t xc7a200t xc7a50t xc7s50 xc7z010 xc7z020)
set(HIMBAECHEL_XILINX_DEVICES ${ALL_HIMBAECHEL_XILINX_DEVICES} CACHE STRING
    "Include support for these Xilinx devices (available: ${ALL_HIMBAECHEL_XILINX_DEVICES})")
if (HIMBAECHEL_XILINX_DEVICES STREQUAL "all")
    set(HIMBAECHEL_XILINX_DEVICES ${ALL_HIMBAECHEL_XILINX_DEVICES})
endif()
message(STATUS "Enabled Himbaechel-Xilinx devices: ${HIMBAECHEL_XILINX_DEVICES}")

foreach (device ${HIMBAECHEL_XILINX_DEVICES})
    add_bba_produce_command(
        TARGET  nextpnr-himbaechel-xilinx-bba
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/gen/xilinx_gen.py
            --xray ${HIMBAECHEL_PRJXRAY_DB}/artix7
            --device ${device}
            --bba ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba.new
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        INPUTS
            ${CMAKE_CURRENT_SOURCE_DIR}/gen/xilinx_gen.py
            ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
    )

    add_bba_compile_command(
        TARGET  nextpnr-himbaechel-xilinx-chipdb
        OUTPUT  himbaechel/xilinx/chipdb-${device}.bin
        INPUT   ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        MODE    binary
    )
endforeach()
