cmake_minimum_required(VERSION 3.5)
project(chipdb-nexus NONE)

set(ALL_NEXUS_FAMILIES LIFCL)

# NOTE: Unlike iCE40 and ECP5; one database can cover all densities of a given family

set(NEXUS_FAMILIES ${ALL_NEXUS_FAMILIES} CACHE STRING
    "Include support for these Nexus families (available: ${ALL_NEXUS_FAMILIES})")
message(STATUS "Enabled Nexus families: ${NEXUS_FAMILIES}")

if(DEFINED NEXUS_CHIPDB)
    add_custom_target(chipdb-nexus-bbas ALL)
else()
    # shared among all families
    IF(DEFINED ENV{OXIDE_INSTALL_PREFIX})
        set(oxide_default_install_prefix $ENV{OXIDE_INSTALL_PREFIX})
    ELSE()
        set(oxide_default_install_prefix ${CMAKE_INSTALL_PREFIX})
    ENDIF()
    set(OXIDE_INSTALL_PREFIX "${oxide_default_install_prefix}" CACHE STRING
        "prjoxide install prefix")
    message(STATUS "prjoxide install prefix: ${OXIDE_INSTALL_PREFIX}")

    set(all_device_bbas)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/chipdb)
    foreach(subfamily ${NEXUS_FAMILIES})
        if(NOT subfamily IN_LIST ALL_NEXUS_FAMILIES)
            message(FATAL_ERROR "${subfamily} is not a supported Nexus family")
        endif()

        set(family_bba chipdb/chipdb-${subfamily}.bba)
        set(PRJOXIDE_TOOL ${OXIDE_INSTALL_PREFIX}/bin/prjoxide)
        add_custom_command(
            OUTPUT ${family_bba}
            COMMAND 
                ${PRJOXIDE_TOOL} bba-export ${subfamily} ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc ${family_bba}.new
            # atomically update
            COMMAND ${CMAKE_COMMAND} -E rename ${family_bba}.new ${family_bba}
            DEPENDS
                ${PRJOXIDE_TOOL}
                ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
                ${CMAKE_CURRENT_SOURCE_DIR}/bba_version.inc
                ${PREVIOUS_CHIPDB_TARGET}
            VERBATIM)
        list(APPEND all_device_bbas ${family_bba})
        if(SERIALIZE_CHIPDBS)
            set(PREVIOUS_CHIPDB_TARGET ${CMAKE_CURRENT_BINARY_DIR}/${family_bba})
        endif()
    endforeach()

    add_custom_target(chipdb-nexus-bbas ALL DEPENDS ${all_device_bbas})

    get_directory_property(has_parent PARENT_DIRECTORY)
    if(has_parent)
        set(NEXUS_CHIPDB ${CMAKE_CURRENT_BINARY_DIR}/chipdb PARENT_SCOPE)
        # serialize chipdb build across multiple architectures
        set(PREVIOUS_CHIPDB_TARGET chipdb-nexus-bbas PARENT_SCOPE)
    else()
        message(STATUS "Build nextpnr with -DNEXUS_CHIPDB=${CMAKE_CURRENT_BINARY_DIR}/chipdb")
    endif()
endif()
