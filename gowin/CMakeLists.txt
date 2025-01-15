include(FindApycula)

add_library(chipdb-${family} OBJECT)
target_compile_options(chipdb-${family} PRIVATE -w -g0 -O0)
target_compile_definitions(chipdb-${family} PRIVATE NEXTPNR_NAMESPACE=nextpnr_${family})
target_include_directories(chipdb-${family} PRIVATE .)

foreach (family_target ${family_targets})
    target_link_libraries(${family_target} PRIVATE chipdb-${family})
endforeach()

set(ALL_GOWIN_DEVICES GW1N-1 GW1NZ-1 GW1N-4 GW1N-9 GW1N-9C GW1NS-2 GW1NS-4 GW2A-18)
set(GOWIN_DEVICES ${ALL_GOWIN_DEVICES} CACHE STRING
    "Include support for these Gowin devices (available: ${ALL_GOWIN_DEVICES})")
message(STATUS "Enabled Gowin devices: ${GOWIN_DEVICES}")

foreach (device ${GOWIN_DEVICES})
    if (NOT device IN_LIST ALL_GOWIN_DEVICES)
        message(FATAL_ERROR "Device ${device} is not a supported Gowin device")
    endif()

    add_bba_produce_command(
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
        TARGET  chipdb-${family}
        OUTPUT  ${CMAKE_BINARY_DIR}/share/${family}/chipdb-${device}.bin
        INPUT   ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        IDENT   ${family}/chipdb-${device}.bin
        MODE    ${BBASM_MODE}
    )
endforeach()
