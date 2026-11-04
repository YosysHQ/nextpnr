set(SOURCES
    constids.inc
    gfxids.inc
    cyclone2.cc
)

add_nextpnr_himbaechel_microarchitecture(${uarch}
    CORE_SOURCES ${SOURCES}
)

set(HIMBAECHEL_CYCLONE2_DB "" CACHE STRING
    "Path to the db/ directory of a cyclone2-re checkout")
if (NOT HIMBAECHEL_CYCLONE2_DB AND NOT IMPORT_BBA_FILES)
    message(FATAL_ERROR "HIMBAECHEL_CYCLONE2_DB must be set to the db/ directory of a cyclone2-re checkout")
endif()

set(ALL_HIMBAECHEL_CYCLONE2_DEVICES EP2C35F672C6)
set(HIMBAECHEL_CYCLONE2_DEVICES ${ALL_HIMBAECHEL_CYCLONE2_DEVICES} CACHE STRING
    "Include support for these Cyclone II devices (available: ${ALL_HIMBAECHEL_CYCLONE2_DEVICES})")
if (HIMBAECHEL_CYCLONE2_DEVICES STREQUAL "all")
    set(HIMBAECHEL_CYCLONE2_DEVICES ${ALL_HIMBAECHEL_CYCLONE2_DEVICES})
endif()
message(STATUS "Enabled Himbaechel-Cyclone2 devices: ${HIMBAECHEL_CYCLONE2_DEVICES}")

foreach (device ${HIMBAECHEL_CYCLONE2_DEVICES})
    if (NOT device IN_LIST ALL_HIMBAECHEL_CYCLONE2_DEVICES)
        message(FATAL_ERROR "Device ${device} is not a supported Cyclone II device")
    endif()

    add_bba_produce_command(
        TARGET  nextpnr-himbaechel-cyclone2-bba
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/gen/arch_gen.py
            --db ${HIMBAECHEL_CYCLONE2_DB}
            --device ${device}
            --bba ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba.new
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        INPUTS
            ${CMAKE_CURRENT_SOURCE_DIR}/gen/arch_gen.py
            ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
            ${HIMBAECHEL_CYCLONE2_DB}/${device}/chipdb.txt.xz
    )

    add_bba_compile_command(
        TARGET  nextpnr-himbaechel-cyclone2-chipdb
        OUTPUT  himbaechel/cyclone2/chipdb-${device}.bin
        INPUT   ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        MODE    binary
    )
endforeach()
