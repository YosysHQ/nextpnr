add_custom_target(chipdb-himbaechel-example)
foreach (target ${family_targets})
    add_dependencies(${target} chipdb-himbaechel-example)
endforeach()

set(ALL_HIMBAECHEL_EXAMPLE_DEVICES example)
set(HIMBAECHEL_EXAMPLE_DEVICES ${ALL_HIMBAECHEL_EXAMPLE_DEVICES} CACHE STRING
    "Include support for these Example devices (available: ${ALL_HIMBAECHEL_EXAMPLE_DEVICES})")
message(STATUS "Enabled Himbaechel-Example devices: ${HIMBAECHEL_EXAMPLE_DEVICES}")

foreach (device ${HIMBAECHEL_EXAMPLE_DEVICES})
    if (NOT device IN_LIST ALL_HIMBAECHEL_EXAMPLE_DEVICES)
        message(FATAL_ERROR "Device ${device} is not a supported Example device")
    endif()

    add_bba_produce_command(
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/example_arch_gen.py
            ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba.new
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        INPUTS
            ${CMAKE_CURRENT_SOURCE_DIR}/example_arch_gen.py
            ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/gfxids.inc
    )

    add_bba_compile_command(
        TARGET  chipdb-himbaechel-example
        OUTPUT  ${CMAKE_BINARY_DIR}/share/himbaechel/example/chipdb-${device}.bin
        INPUT   ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        MODE    binary
    )
endforeach()

install(
    DIRECTORY ${CMAKE_BINARY_DIR}/share/himbaechel/example
    DESTINATION share/nextpnr/himbaechel/example
)
