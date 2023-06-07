cmake_minimum_required(VERSION 3.5)
project(chipdb-ecp5 NONE)

set(ALL_ECP5_DEVICES 25k 45k 85k)
set(ECP5_DEVICES ${ALL_ECP5_DEVICES} CACHE STRING
    "Include support for these ECP5 devices (available: ${ALL_ECP5_DEVICES})")
message(STATUS "Enabled ECP5 devices: ${ECP5_DEVICES}")

if(DEFINED ECP5_CHIPDB)
    add_custom_target(chipdb-ecp5-bbas ALL)
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

    IF(DEFINED ENV{TRELLIS_INSTALL_PREFIX})
        set(trellis_default_install_prefix $ENV{TRELLIS_INSTALL_PREFIX})
    ELSE()
        set(trellis_default_install_prefix ${CMAKE_INSTALL_PREFIX})
    ENDIF()

    set(TRELLIS_INSTALL_PREFIX ${trellis_default_install_prefix} CACHE STRING
        "Trellis install prefix")
    message(STATUS "Trellis install prefix: ${TRELLIS_INSTALL_PREFIX}")

    if(NOT TRELLIS_LIBDIR)
        # The pytrellis library isn't a normal shared library, but rather a native Python library;
        # it does not follow the normal platform conventions for shared libraries, so we can't just
        # use find_library() here. Instead, we emulate the useful parts of the find_library() logic
        # for use with find_path().
        set(pytrellis_paths)
        foreach(prefix_path ${CMAKE_PREFIX_PATH})
            list(APPEND pytrellis_paths ${prefix_path}/lib)
            if(CMAKE_LIBRARY_ARCHITECTURE)
                list(APPEND pytrellis_paths ${prefix_path}/lib/${CMAKE_LIBRARY_ARCHITECTURE})
            endif()
        endforeach()
        list(APPEND pytrellis_paths ${CMAKE_LIBRARY_PATH})
        if(NOT NO_CMAKE_SYSTEM_PATH)
            foreach(prefix_path ${CMAKE_SYSTEM_PREFIX_PATH})
                list(APPEND pytrellis_paths ${prefix_path}/lib)
                if(CMAKE_LIBRARY_ARCHITECTURE)
                    list(APPEND pytrellis_paths ${prefix_path}/lib/${CMAKE_LIBRARY_ARCHITECTURE})
                endif()
            endforeach()
            list(APPEND pytrellis_paths ${CMAKE_SYSTEM_LIBRARY_PATH})
        endif()
        message(STATUS "Searching for pytrellis in: ${pytrellis_paths}")

        if(WIN32)
            set(pytrellis_lib pytrellis.pyd)
        else()
            set(pytrellis_lib pytrellis${CMAKE_SHARED_MODULE_SUFFIX})
        endif()

        find_path(TRELLIS_LIBDIR ${pytrellis_lib}
            HINTS ${TRELLIS_INSTALL_PREFIX}/lib/${TRELLIS_PROGRAM_PREFIX}trellis
            PATHS ${pytrellis_paths}
            PATH_SUFFIXES ${TRELLIS_PROGRAM_PREFIX}trellis
            DOC "Location of the pytrellis library")
        if(NOT TRELLIS_LIBDIR)
            message(FATAL_ERROR "Failed to locate the pytrellis library")
        endif()
    endif()
    message(STATUS "Trellis library directory: ${TRELLIS_LIBDIR}")

    if(NOT TRELLIS_DATADIR)
        set(TRELLIS_DATADIR ${TRELLIS_INSTALL_PREFIX}/share/${TRELLIS_PROGRAM_PREFIX}trellis)
    endif()
    message(STATUS "Trellis data directory: ${TRELLIS_DATADIR}")

    set(all_device_bbas)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/chipdb)
    foreach(device ${ECP5_DEVICES})
        if(NOT device IN_LIST ALL_ECP5_DEVICES)
            message(FATAL_ERROR "Device ${device} is not a supported ECP5 device")
        endif()

        set(device_bba chipdb/chipdb-${device}.bba)
        add_custom_command(
            OUTPUT ${device_bba}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/trellis_import.py
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
                ${CMAKE_CURRENT_SOURCE_DIR}/trellis_import.py
                ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
                ${CMAKE_CURRENT_SOURCE_DIR}/gfx.h
                ${PREVIOUS_CHIPDB_TARGET}
            VERBATIM)
        list(APPEND all_device_bbas ${device_bba})
        if(SERIALIZE_CHIPDBS)
            set(PREVIOUS_CHIPDB_TARGET ${CMAKE_CURRENT_BINARY_DIR}/${device_bba})
        endif()
    endforeach()

    add_custom_target(chipdb-ecp5-bbas ALL DEPENDS ${all_device_bbas})

    get_directory_property(has_parent PARENT_DIRECTORY)
    if(has_parent)
        set(ECP5_CHIPDB ${CMAKE_CURRENT_BINARY_DIR}/chipdb PARENT_SCOPE)
        # serialize chipdb build across multiple architectures
        set(PREVIOUS_CHIPDB_TARGET chipdb-ecp5-bbas PARENT_SCOPE)
    else()
        message(STATUS "Build nextpnr with -DECP5_CHIPDB=${CMAKE_CURRENT_BINARY_DIR}/chipdb")
    endif()
endif()
