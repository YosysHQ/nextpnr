cmake_minimum_required(VERSION 3.5)
project(chipdb-machxo2 NONE)

set(ALL_MACHXO2_DEVICES 256X 640X 1200X 2280X 256 640 1200 2000 4000 7000)
set(MACHXO2_DEVICES 1200 CACHE STRING
    "Include support for these MachXO2 devices (available: ${ALL_MACHXO2_DEVICES})")
message(STATUS "Enabled MachXO2 devices: ${MACHXO2_DEVICES}")

if(DEFINED MACHXO2_CHIPDB)
    add_custom_target(chipdb-machxo2-bbas ALL)
else()
    find_package(Python3 3.5 REQUIRED COMPONENTS Interpreter)

    # shared among all families
    set(SERIALIZE_CHIPDBS TRUE CACHE BOOL
        "Serialize device data preprocessing to minimize memory use")

    set(TRELLIS_PROGRAM_PREFIX "" CACHE STRING
        "Trellis name prefix")
    if(TRELLIS_PROGRAM_PREFIX)
        message(STATUS "Trellis program prefix: ${TRELLIS_PROGRAM_PREFIX}")
    endif()

    set(TRELLIS_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX} CACHE STRING
        "Trellis install prefix")
    message(STATUS "Trellis install prefix: ${TRELLIS_INSTALL_PREFIX}")

    if(NOT DEFINED TRELLIS_LIBDIR)
        if(WIN32)
            set(pytrellis_lib pytrellis.pyd)
        else()
            set(pytrellis_lib pytrellis${CMAKE_SHARED_LIBRARY_SUFFIX})
        endif()
        find_path(TRELLIS_LIBDIR ${pytrellis_lib}
            HINTS ${TRELLIS_INSTALL_PREFIX}/lib/${TRELLIS_PROGRAM_PREFIX}trellis
            PATHS ${CMAKE_SYSTEM_LIBRARY_PATH} ${CMAKE_LIBRARY_PATH}
            PATH_SUFFIXES ${TRELLIS_PROGRAM_PREFIX}trellis
            DOC "Location of the pytrellis library")
        if(NOT TRELLIS_LIBDIR)
            message(FATAL_ERROR "Failed to locate the pytrellis library")
        endif()
    endif()
    message(STATUS "Trellis library directory: ${TRELLIS_LIBDIR}")

    if(NOT DEFINED TRELLIS_DATADIR)
        set(TRELLIS_DATADIR ${TRELLIS_INSTALL_PREFIX}/share/${TRELLIS_PROGRAM_PREFIX}trellis)
    endif()
    message(STATUS "Trellis data directory: ${TRELLIS_DATADIR}")

    set(all_device_bbas)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/chipdb)
    foreach(device ${MACHXO2_DEVICES})
        if(NOT device IN_LIST ALL_MACHXO2_DEVICES)
            message(FATAL_ERROR "Device ${device} is not a supported MachXO2 device")
        endif()

        set(device_bba chipdb/chipdb-${device}.bba)
        add_custom_command(
            OUTPUT ${device_bba}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/facade_import.py
                -L ${TRELLIS_LIBDIR}
                -L ${TRELLIS_DATADIR}/util/common
                -L ${TRELLIS_DATADIR}/timing/util
                -p ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
                -g ${CMAKE_CURRENT_SOURCE_DIR}/gfx.h
                ${device}
                > ${device_bba}.new
            # atomically update
            COMMAND ${CMAKE_COMMAND} -E rename ${device_bba}.new ${device_bba}
            DEPENDS
                ${CMAKE_CURRENT_SOURCE_DIR}/facade_import.py
                ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
                ${CMAKE_CURRENT_SOURCE_DIR}/gfx.h
                ${PREVIOUS_CHIPDB_TARGET}
            VERBATIM)
        list(APPEND all_device_bbas ${device_bba})
        if(SERIALIZE_CHIPDBS)
            set(PREVIOUS_CHIPDB_TARGET ${CMAKE_CURRENT_BINARY_DIR}/${device_bba})
        endif()
    endforeach()

    add_custom_target(chipdb-machxo2-bbas ALL DEPENDS ${all_device_bbas})

    get_directory_property(has_parent PARENT_DIRECTORY)
    if(has_parent)
        set(MACHXO2_CHIPDB ${CMAKE_CURRENT_BINARY_DIR}/chipdb PARENT_SCOPE)
        # serialize chipdb build across multiple architectures
        set(PREVIOUS_CHIPDB_TARGET chipdb-machxo2-bbas PARENT_SCOPE)
    else()
        message(STATUS "Build nextpnr with -DMACHXO2_CHIPDB=${CMAKE_CURRENT_BINARY_DIR}")
    endif()
endif()
