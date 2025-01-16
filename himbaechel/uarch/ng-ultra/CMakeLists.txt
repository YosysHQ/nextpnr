set(SOURCES
    bitstream.cc
    cells.cc
    constids.inc
    csv.cc
    extra_data.h
    location_map.cc
    location_map.h
    ng_ultra.cc
    ng_ultra.h
    pack.cc
    pack.h
)

set(TEST_SOURCES
    tests/lut_dff.cc
    tests/main.cc
)

add_nextpnr_himbaechel_microarchitecture(${uarch}
    CORE_SOURCES ${SOURCES}
    TEST_SOURCES ${TEST_SOURCES}
)

set(HIMBAECHEL_PRJBEYOND_DB "" CACHE STRING
    "Path to a Project Beyond database")
if (NOT HIMBAECHEL_PRJBEYOND_DB)
    message(FATAL_ERROR "HIMBAECHEL_PRJBEYOND_DB must be set to a prjbeyond database checkout")
endif()

set(ALL_HIMBAECHEL_NGULTRA_DEVICES ng-ultra)
set(HIMBAECHEL_NGULTRA_DEVICES ${ALL_HIMBAECHEL_NGULTRA_DEVICES} CACHE STRING
    "Include support for these NG-Ultra devices (available: ${ALL_HIMBAECHEL_NGULTRA_DEVICES})")
message(STATUS "Enabled Himbaechel-NG-Ultra devices: ${HIMBAECHEL_NGULTRA_DEVICES}")

foreach (device ${HIMBAECHEL_NGULTRA_DEVICES})
    if (NOT device IN_LIST ALL_HIMBAECHEL_NGULTRA_DEVICES)
        message(FATAL_ERROR "Device ${device} is not a supported NG-Ultra device")
    endif()

    string(TOUPPER ${device} device_upper)

    add_bba_produce_command(
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/gen/arch_gen.py
            --db ${HIMBAECHEL_PRJBEYOND_DB}
            --device ${device_upper}
            --bba ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba.new
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        INPUTS
            ${CMAKE_CURRENT_SOURCE_DIR}/gen/arch_gen.py
            ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
    )

    add_bba_compile_command(
        TARGET  nextpnr-himbaechel-ng-ultra-chipdb
        OUTPUT  himbaechel/ng-ultra/chipdb-${device}.bin
        INPUT   ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        MODE    binary
    )
endforeach()
