include(FindIceStorm)

add_library(chipdb-${family} OBJECT)
target_compile_options(chipdb-${family} PRIVATE -w -g0 -O0)
target_compile_definitions(chipdb-${family} PRIVATE NEXTPNR_NAMESPACE=nextpnr_${family})
target_include_directories(chipdb-${family} PRIVATE .)

foreach (family_target ${family_targets})
    target_link_libraries(${family_target} PRIVATE chipdb-${family})
endforeach()

set(ALL_ICE40_DEVICES 384 1k 5k u4k 8k)
set(ICE40_DEVICES ${ALL_ICE40_DEVICES} CACHE STRING
    "Include support for these iCE40 devices (available: ${ALL_ICE40_DEVICES})")
message(STATUS "Enabled iCE40 devices: ${ICE40_DEVICES}")

foreach (device ${ICE40_DEVICES})
    if (NOT device IN_LIST ALL_ICE40_DEVICES)
        message(FATAL_ERROR "Device ${device} is not a supported iCE40 device")
    endif()

    if (device STREQUAL "5k")
        set(timing_opts
            --slow ${ICEBOX_DATADIR}/timings_up5k.txt)
    elseif (device STREQUAL "u4k")
        set(timing_opts
            --slow ${ICEBOX_DATADIR}/timings_u4k.txt)
    elseif (device STREQUAL "384")
        set(timing_opts
            --slow ${ICEBOX_DATADIR}/timings_lp384.txt)
    else()
        set(timing_opts
            --fast ${ICEBOX_DATADIR}/timings_hx${device}.txt
            --slow ${ICEBOX_DATADIR}/timings_lp${device}.txt)
    endif()

    add_bba_produce_command(
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/chipdb.py
            -p ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
            -g ${CMAKE_CURRENT_SOURCE_DIR}/gfx.h
            ${timing_opts}
            ${ICEBOX_DATADIR}/chipdb-${device}.txt
            > ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba.new
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/chipdb-${device}.bba
        INPUTS
            ${CMAKE_CURRENT_SOURCE_DIR}/chipdb.py
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
