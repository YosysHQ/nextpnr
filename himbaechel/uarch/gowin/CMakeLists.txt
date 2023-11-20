message(STATUS "Configuring Himbaechel-Gowin uarch")
cmake_minimum_required(VERSION 3.5)
project(himbaechel-gowin-chipdb NONE)

find_package(Python3 3.5 REQUIRED COMPONENTS Interpreter)
set(ALL_HIMBAECHEL_GOWIN_DEVICES GW1N-1 GW1NZ-1 GW1N-4 GW1N-9 GW1N-9C GW1NS-4 GW2A-18 GW2A-18C)
set(HIMBAECHEL_GOWIN_DEVICES "" CACHE STRING
    "Include support for these Gowin devices (available: ${ALL_HIMBAECHEL_GOWIN_DEVICES})")
if(HIMBAECHEL_GOWIN_DEVICES STREQUAL "all")
	set(HIMBAECHEL_GOWIN_DEVICES ${ALL_HIMBAECHEL_GOWIN_DEVICES})
endif()
message(STATUS "Enabled Himbaechel-Gowin devices: ${HIMBAECHEL_GOWIN_DEVICES}")

if(DEFINED ENV{APYCULA_INSTALL_PREFIX})
    set(apycula_default_install_prefix $ENV{APYCULA_INSTALL_PREFIX})
endif()
set(APYCULA_INSTALL_PREFIX ${apycula_default_install_prefix} CACHE STRING
    "Apycula install prefix (virtualenv directory)")
if(NOT APYCULA_INSTALL_PREFIX STREQUAL "")
	message(STATUS "Apycula install prefix: ${APYCULA_INSTALL_PREFIX}")
	set(apycula_Python3_EXECUTABLE ${APYCULA_INSTALL_PREFIX}/bin/python)
else()
	message(STATUS "Apycula install prefix: (using system Python)")
	set(apycula_Python3_EXECUTABLE ${Python3_EXECUTABLE})
endif()

set(chipdb_binaries)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/share/himbaechel/gowin)
foreach(device ${HIMBAECHEL_GOWIN_DEVICES})
    if(NOT device IN_LIST ALL_HIMBAECHEL_GOWIN_DEVICES)
		message(FATAL_ERROR "Device ${device} is not a supported Gowin device")
    endif()

	set(device_bba ${CMAKE_BINARY_DIR}/share/himbaechel/gowin/chipdb-${device}.bba)
	set(device_bin ${CMAKE_BINARY_DIR}/share/himbaechel/gowin/chipdb-${device}.bin)
	add_custom_command(
		OUTPUT ${device_bin}
		COMMAND ${apycula_Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gowin_arch_gen.py -d ${device} -o ${device_bba}
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
install(DIRECTORY ${CMAKE_BINARY_DIR}/share/himbaechel/gowin/ DESTINATION share/nextpnr/himbaechel/gowin
	    PATTERN "*.bba" EXCLUDE)
