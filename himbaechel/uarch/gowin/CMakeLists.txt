message(STATUS "Configuring Himbaechel-Gowin uarch")
cmake_minimum_required(VERSION 3.5)
project(himbaechel-gowin-chipdb NONE)

find_package(Python3 3.5 REQUIRED COMPONENTS Interpreter)
set(ALL_HIMBAECHEL_GOWIN_DEVICES GW1N-1 GW1NZ-1 GW1NS-2 GW1N-4 GW1N-9 GW1N-9C GW1NS-4 GW2A-18 GW2A-18C)
set(HIMBAECHEL_GOWIN_DEVICES "" CACHE STRING
    "Include support for these Gowin devices (available: ${ALL_HIMBAECHEL_GOWIN_DEVICES})")
message(STATUS "Enabled Himbaechel-Gowin devices: ${HIMBAECHEL_GOWIN_DEVICES}")

set(chipdb_binaries)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/chipdb)
foreach(device ${HIMBAECHEL_GOWIN_DEVICES})
    if(NOT device IN_LIST ALL_HIMBAECHEL_GOWIN_DEVICES)
		message(FATAL_ERROR "Device ${device} is not a supported Gowin device")
    endif()

	set(device_bba chipdb/chipdb-${device}.bba)
	set(device_bin chipdb/chipdb-${device}.bin)
	add_custom_command(
		OUTPUT ${device_bin}
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gowin_arch_gen.py -d ${device} -o ${device_bba}
		COMMAND bbasm ${BBASM_ENDIAN_FLAG} ${device_bba} ${device_bin}.new
        # atomically update
        COMMAND ${CMAKE_COMMAND} -E rename ${device_bin}.new ${device_bin}
		DEPENDS
			bbasm
			${CMAKE_CURRENT_SOURCE_DIR}/gowin_arch_gen.py
			${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
		VERBATIM)
	list(APPEND chipdb_binaries ${device_bin})
endforeach()

add_custom_target(chipdb-himbaechel-gowin ALL DEPENDS ${chipdb_binaries})
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/chipdb/ DESTINATION share/nextpnr/himbaechel/gowin
	    PATTERN "*.bba" EXCLUDE)
