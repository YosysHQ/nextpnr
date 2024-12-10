message(STATUS "Configuring Himbaechel-GateMate uarch")
cmake_minimum_required(VERSION 3.5)
project(himbaechel-gatemate-chipdb NONE)

set(HIMBAECHEL_PEPPERCORN_PATH "" CACHE STRING
    "Path to a Project Peppercorn database scripts")

set(ALL_HIMBAECHE_GATEMATE_DEVICES CCGM1A1 CCGM1A2 CCGM1A4 CCGM1A9 CCGM1A16 CCGM1A25)
set(HIMBAECHEL_GATEMATE_DEVICES "" CACHE STRING
    "Include support for these GateMate devices (available: ${ALL_HIMBAECHE_GATEMATE_DEVICES})")
message(STATUS "Enabled Himbaechel-GateMate devices: ${HIMBAECHEL_GATEMATE_DEVICES}")

set(chipdb_binaries)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/share/himbaechel/gatemate)
foreach(device ${HIMBAECHEL_GATEMATE_DEVICES})
	set(device_bba ${CMAKE_BINARY_DIR}/share/himbaechel/gatemate/chipdb-${device}.bba)
	set(device_bin ${CMAKE_BINARY_DIR}/share/himbaechel/gatemate/chipdb-${device}.bin)
	if("${HIMBAECHEL_PEPPERCORN_PATH}" STREQUAL "")
		message(SEND_ERROR "HIMBAECHEL_PEPPERCORN_PATH must be set to a Project Peppercorn checkout")
	endif()
	add_custom_command(
		OUTPUT ${device_bin}
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gen/arch_gen.py --device ${device} --bba ${device_bba} --lib ${HIMBAECHEL_PEPPERCORN_PATH}/gatemate
		COMMAND bbasm ${BBASM_ENDIAN_FLAG} ${device_bba} ${device_bin}.new
        # atomically update
        COMMAND ${CMAKE_COMMAND} -E rename ${device_bin}.new ${device_bin}
		DEPENDS
			bbasm
			${CMAKE_CURRENT_SOURCE_DIR}/gen/arch_gen.py
			${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
			${CMAKE_CURRENT_SOURCE_DIR}/gfxids.inc
		VERBATIM)
	list(APPEND chipdb_binaries ${device_bin})
endforeach()

add_custom_target(chipdb-himbaechel-gatemate ALL DEPENDS ${chipdb_binaries})
install(DIRECTORY ${CMAKE_BINARY_DIR}/share/himbaechel/gatemate/ DESTINATION share/nextpnr/himbaechel/gatemate
	    PATTERN "*.bba" EXCLUDE)
