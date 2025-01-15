set(HIMBAECHEL_PRJXRAY_DB "" CACHE STRING
    "Path to a project x-ray database")
if (NOT HIMBAECHEL_PRJXRAY_DB)
    message(FATAL_ERROR "HIMBAECHEL_PRJXRAY_DB must be set to a prjxray database checkout")
endif()

add_custom_target(chipdb-himbaechel-xilinx)
foreach (target ${family_targets})
    add_dependencies(${target} chipdb-himbaechel-xilinx)
endforeach()

set(HIMBAECHEL_XILINX_DEVICES "" CACHE STRING
    "Include support for these Xilinx devices")
message(STATUS "Enabled Himbaechel-Xilinx devices: ${HIMBAECHEL_XILINX_DEVICES}")

foreach (device ${HIMBAECHEL_XILINX_DEVICES})
    add_bba_produce_command(
        COMMAND /usr/bin/pypy3 ${CMAKE_CURRENT_SOURCE_DIR}/gen/xilinx_gen.py
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
        TARGET  chipdb-himbaechel-xilinx
        OUTPUT  ${CMAKE_BINARY_DIR}/share/himbaechel/xilinx/chipdb-${device}.bin
        INPUT   ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        MODE    binary
    )
endforeach()

install(
    DIRECTORY ${CMAKE_BINARY_DIR}/share/himbaechel/xilinx
    DESTINATION share/nextpnr/himbaechel/xilinx
)
