cmake_minimum_required(VERSION 3.5)
project(chipdb-gowin NONE)

set(ALL_GOWIN_DEVICES GW1N-1 GW1N-4 GW1N-9 GW1N-9C GW1NS-2 GW1NS-4)
set(GOWIN_DEVICES ${ALL_GOWIN_DEVICES} CACHE STRING
    "Include support for these Gowin devices (available: ${ALL_GOWIN_DEVICES})")
message(STATUS "Enabled Gowin devices: ${GOWIN_DEVICES}")

find_program (GOWIN_BBA_EXECUTABLE gowin_bba)
message(STATUS "gowin_bba executable: ${GOWIN_BBA_EXECUTABLE}")

if(DEFINED GOWIN_CHIPDB)
    add_custom_target(chipdb-gowin-bbas ALL)
else()
    # shared among all families
    set(SERIALIZE_CHIPDBS TRUE CACHE BOOL
        "Serialize device data preprocessing to minimize memory use")

    set(all_device_bbas)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/chipdb)
    foreach(device ${GOWIN_DEVICES})
        if(NOT device IN_LIST ALL_GOWIN_DEVICES)
            message(FATAL_ERROR "Device ${device} is not a supported Gowin device")
        endif()

        set(device_bba chipdb/chipdb-${device}.bba)
        add_custom_command(
            OUTPUT ${device_bba}
            COMMAND ${GOWIN_BBA_EXECUTABLE} -d ${device} -i ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc -o ${device_bba}.new
            # atomically update
            COMMAND ${CMAKE_COMMAND} -E rename ${device_bba}.new ${device_bba}
            DEPENDS
                ${GOWIN_BBA_EXECUTABLE}
                ${PREVIOUS_CHIPDB_TARGET}
                ${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
            VERBATIM)
        list(APPEND all_device_bbas ${device_bba})
        if(SERIALIZE_CHIPDBS)
            set(PREVIOUS_CHIPDB_TARGET ${CMAKE_CURRENT_BINARY_DIR}/${device_bba})
        endif()
    endforeach()

    add_custom_target(chipdb-gowin-bbas ALL DEPENDS ${all_device_bbas})

    get_directory_property(has_parent PARENT_DIRECTORY)
    if(has_parent)
        set(GOWIN_CHIPDB ${CMAKE_CURRENT_BINARY_DIR}/chipdb PARENT_SCOPE)
        # serialize chipdb build across multiple architectures
        set(PREVIOUS_CHIPDB_TARGET chipdb-gowin-bbas PARENT_SCOPE)
    else()
        message(STATUS "Build nextpnr with -DGOWIN_CHIPDB=${CMAKE_CURRENT_BINARY_DIR}/chipdb")
    endif()
endif()
