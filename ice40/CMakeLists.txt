cmake_minimum_required(VERSION 3.5)
project(chipdb-ice40 NONE)

set(ALL_ICE40_DEVICES 384 1k 5k u4k 8k)
set(ICE40_DEVICES ${ALL_ICE40_DEVICES} CACHE STRING
    "Include support for these iCE40 devices (available: ${ALL_ICE40_DEVICES})")
message(STATUS "Enabled iCE40 devices: ${ICE40_DEVICES}")

if(DEFINED ICE40_CHIPDB)
    add_custom_target(chipdb-ice40-bbas ALL)
else()
    find_package(Python3 3.5 REQUIRED COMPONENTS Interpreter)

    # shared among all families
    set(SERIALIZE_CHIPDBS TRUE CACHE BOOL
        "Serialize device data preprocessing to minimize memory use")

    set(icestorm_default_install_prefix ${CMAKE_INSTALL_PREFIX})
    # for compatibility with old build scripts
    if(DEFINED ICEBOX_ROOT)
        message(WARNING "-DICEBOX_ROOT= is deprecated, use -DICESTORM_INSTALL_PREFIX=${ICEBOX_ROOT} instead")
        get_filename_component(dir ${ICEBOX_ROOT} DIRECTORY)
        get_filename_component(dir ${dir} DIRECTORY)
        set(icestorm_default_install_prefix ${dir})
    elseif(DEFINED ENV{ICESTORM_INSTALL_PREFIX})
        set(icestorm_default_install_prefix $ENV{ICESTORM_INSTALL_PREFIX})
    endif()
    set(ICESTORM_INSTALL_PREFIX ${icestorm_default_install_prefix} CACHE STRING
        "IceStorm install prefix")
    message(STATUS "IceStorm install prefix: ${ICESTORM_INSTALL_PREFIX}")

    if(NOT ICEBOX_DATADIR)
        set(ICEBOX_DATADIR ${ICESTORM_INSTALL_PREFIX}/share/icebox)
    endif()
    message(STATUS "icebox data directory: ${ICEBOX_DATADIR}")

    set(all_device_bbas)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/chipdb)
    foreach(device ${ICE40_DEVICES})
        if(NOT device IN_LIST ALL_ICE40_DEVICES)
            message(FATAL_ERROR "Device ${device} is not a supported iCE40 device")
        endif()

        if(device STREQUAL "5k")
            set(timing_opts
                --slow ${ICEBOX_DATADIR}/timings_up5k.txt)
        elseif(device STREQUAL "u4k")
            set(timing_opts
                --slow ${ICEBOX_DATADIR}/timings_u4k.txt)
        elseif(device STREQUAL "384")
            set(timing_opts
                --slow ${ICEBOX_DATADIR}/timings_lp384.txt)
        else()
            set(timing_opts
                --fast ${ICEBOX_DATADIR}/timings_hx${device}.txt
                --slow ${ICEBOX_DATADIR}/timings_lp${device}.txt)
        endif()

        set(device_bba chipdb/chipdb-${device}.bba)
        add_custom_command(
            OUTPUT ${device_bba}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/chipdb.py
                -p ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
                -g ${CMAKE_CURRENT_SOURCE_DIR}/gfx.h
                ${timing_opts}
                ${ICEBOX_DATADIR}/chipdb-${device}.txt
                > ${device_bba}.new
            # atomically update
            COMMAND ${CMAKE_COMMAND} -E rename ${device_bba}.new ${device_bba}
            DEPENDS
                ${CMAKE_CURRENT_SOURCE_DIR}/chipdb.py
                ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
                ${CMAKE_CURRENT_SOURCE_DIR}/gfx.h
                ${PREVIOUS_CHIPDB_TARGET}
            VERBATIM)
        list(APPEND all_device_bbas ${device_bba})
        if(SERIALIZE_CHIPDBS)
            set(PREVIOUS_CHIPDB_TARGET ${CMAKE_CURRENT_BINARY_DIR}/${device_bba})
        endif()
    endforeach()

    add_custom_target(chipdb-ice40-bbas ALL DEPENDS ${all_device_bbas})

    get_directory_property(has_parent PARENT_DIRECTORY)
    if(has_parent)
        set(ICE40_CHIPDB ${CMAKE_CURRENT_BINARY_DIR}/chipdb PARENT_SCOPE)
        # serialize chipdb build across multiple architectures
        set(PREVIOUS_CHIPDB_TARGET chipdb-ice40-bbas PARENT_SCOPE)
    else()
        message(STATUS "Build nextpnr with -DICE40_CHIPDB=${CMAKE_CURRENT_BINARY_DIR}/chipdb")
    endif()
endif()
