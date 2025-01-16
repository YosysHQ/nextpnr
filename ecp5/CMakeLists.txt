include(FindTrellis)

add_library(chipdb-${family} OBJECT)
target_compile_definitions(chipdb-${family} PRIVATE NEXTPNR_NAMESPACE=nextpnr_${family})
target_include_directories(chipdb-${family} PRIVATE .)

foreach (family_target ${family_targets})
    target_link_libraries(${family_target} PRIVATE chipdb-${family})
endforeach()

set(ALL_ECP5_DEVICES 25k 45k 85k)
set(ECP5_DEVICES ${ALL_ECP5_DEVICES} CACHE STRING
    "Include support for these ECP5 devices (available: ${ALL_ECP5_DEVICES})")
message(STATUS "Enabled ECP5 devices: ${ECP5_DEVICES}")

foreach (device ${ECP5_DEVICES})
    if (NOT device IN_LIST ALL_ECP5_DEVICES)
        message(FATAL_ERROR "Device ${device} is not a supported ECP5 device")
    endif()

    add_bba_produce_command(
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
        TARGET  chipdb-${family}
        OUTPUT  ${CMAKE_BINARY_DIR}/share/${family}/chipdb-${device}.bin
        INPUT   ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        IDENT   ${family}/chipdb-${device}.bin
        MODE    ${BBASM_MODE}
    )
endforeach()
