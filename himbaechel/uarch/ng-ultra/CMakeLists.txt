set(HIMBAECHEL_PRJBEYOND_DB "" CACHE STRING
    "Path to a Project Beyond database")
if (NOT HIMBAECHEL_PRJBEYOND_DB)
    message(FATAL_ERROR "HIMBAECHEL_PRJBEYOND_DB must be set to a prjbeyond database checkout")
endif()

add_custom_target(chipdb-himbaechel-ng-ultra)
foreach (target ${family_targets})
    add_dependencies(${target} chipdb-himbaechel-ng-ultra)
endforeach()

set(ALL_HIMBAECHEL_NGULTRA_DEVICES ng-ultra)
set(HIMBAECHEL_NGULTRA_DEVICES ${ALL_HIMBAECHEL_NGULTRA_DEVICES} CACHE STRING
    "Include support for these NG-Ultra devices (available: ${ALL_HIMBAECHEL_NGULTRA_DEVICES})")
message(STATUS "Enabled Himbaechel-NG-Ultra devices: ${HIMBAECHEL_NGULTRA_DEVICES}")

foreach (device ${HIMBAECHEL_NGULTRA_DEVICES})
    if (NOT device IN_LIST ALL_HIMBAECHEL_NGULTRA_DEVICES)
        message(FATAL_ERROR "Device ${device} is not a supported NG-Ultra device")
    endif()

    string(TOUPPER ${device} upcase_device)

    add_bba_produce_command(
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/gen/arch_gen.py
            --db ${HIMBAECHEL_PRJBEYOND_DB}
            --device ${upcase_device}
            --bba ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba.new
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        INPUTS
            ${CMAKE_CURRENT_SOURCE_DIR}/gen/arch_gen.py
            ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
    )

    add_bba_compile_command(
        TARGET  chipdb-himbaechel-ng-ultra
        OUTPUT  ${CMAKE_BINARY_DIR}/share/himbaechel/ng-ultra/chipdb-${device}.bin
        INPUT   ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        MODE    binary
    )
endforeach()

install(
    DIRECTORY ${CMAKE_BINARY_DIR}/share/himbaechel/ng-ultra
    DESTINATION share/nextpnr/himbaechel/ng-ultra
)
