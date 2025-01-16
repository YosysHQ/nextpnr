include(FindTrellis)

add_library(chipdb-${family} OBJECT)
target_compile_definitions(chipdb-${family} PRIVATE NEXTPNR_NAMESPACE=nextpnr_${family})
target_include_directories(chipdb-${family} PRIVATE .)

foreach (family_target ${family_targets})
    target_link_libraries(${family_target} PRIVATE chipdb-${family})
endforeach()

# Note that the four *X (MachXO) devices fail to import with prjtrellis commit 14ac883fa.
set(ALL_MACHXO2_DEVICES 256X 640X 1200X 2280X 256 640 1200 2000 4000 7000 1300 2100 4300 6900 9400 4300D 9400D)
set(MACHXO2_DEVICES 1200 6900 CACHE STRING
    "Include support for these MachXO2/XO3 devices (available: ${ALL_MACHXO2_DEVICES})")
message(STATUS "Enabled MachXO2/XO3 devices: ${MACHXO2_DEVICES}")

configure_file(machxo2_available.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/machxo2_available.h)
target_sources(chipdb-${family} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated/machxo2_available.h)
target_include_directories(chipdb-${family} PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)

foreach (device ${MACHXO2_DEVICES})
    if (NOT device IN_LIST ALL_MACHXO2_DEVICES)
        message(FATAL_ERROR "Device ${device} is not a supported MachXO2/XO3 device")
    endif()

    add_bba_produce_command(
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
        TARGET  chipdb-${family}
        OUTPUT  ${CMAKE_BINARY_DIR}/share/${family}/chipdb-${device}.bin
        INPUT   ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        IDENT   ${family}/chipdb-${device}.bin
        MODE    ${BBASM_MODE}
    )
endforeach()
