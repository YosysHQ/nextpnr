message(STATUS "Configuring Himbaechel-Gowin uarch")
cmake_minimum_required(VERSION 3.5)
project(himbaechel-gowin-chipdb NONE)

find_package(Python3 3.5 REQUIRED COMPONENTS Interpreter)
set(ALL_GOWIN_DEVICES GW1N-1 GW1NZ-1 GW1NS-2 GW1N-4 GW1N-9 GW1N-9C GW1NS-4 GW2A-18)
set(GOWIN_DEVICES ${ALL_GOWIN_DEVICES} CACHE STRING
    "Include support for these Gowin devices (available: ${ALL_GOWIN_DEVICES})")
message(STATUS "Enabled Himbaechel-Gowin devices: ${GOWIN_DEVICES}")

set(chipdb_binaries)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/chipdb)
foreach(device ${GOWIN_DEVICES})
    if(NOT device IN_LIST ALL_GOWIN_DEVICES)
		message(FATAL_ERROR "Device ${device} is not a supported Gowin device")
    endif()

	set(device_bba chipdb/chipdb-${device}.bba)
	set(device_bin chipdb/chipdb-${device}.bin)
	add_custom_command(
		OUTPUT ${device_bin}
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gowin_arch_gen.py -d ${device} -o ${device_bba}
		COMMAND bbasm ${BBASM_ENDIAN_FLAG} ${device_bba} ${device_bin}
		DEPENDS
			bbasm
			${CMAKE_CURRENT_SOURCE_DIR}/gowin_arch_gen.py
			${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
		VERBATIM)
	list(APPEND chipdb_binaries ${device_bin})
endforeach()

add_custom_target(chipdb-himbaechel-gowin ALL DEPENDS ${chipdb_binaries})
