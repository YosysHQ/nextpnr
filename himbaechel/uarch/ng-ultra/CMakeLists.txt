message(STATUS "Configuring Himbaechel-NG-ULTRA uarch")
cmake_minimum_required(VERSION 3.5)
project(himbaechel-ng-ultra-chipdb NONE)

set(ALL_HIMBAECHEL_NGULTRA_DEVICES ng-ultra)
set(HIMBAECHEL_NGULTRA_DEVICES "" CACHE STRING
    "Include support for these NG-Ultra devices (available: ${ALL_HIMBAECHEL_NGULTRA_DEVICES})")
message(STATUS "Enabled Himbaechel-NG-Ultra devices: ${HIMBAECHEL_NGULTRA_DEVICES}")
set(HIMBAECHEL_PRJBEYOND_DB "" CACHE STRING
    "Path to a Project Beyond database")

set(chipdb_binaries)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/share/himbaechel/ng-ultra)
foreach(device ${HIMBAECHEL_NGULTRA_DEVICES})
	if("${HIMBAECHEL_PRJBEYOND_DB}" STREQUAL "")
		message(SEND_ERROR "HIMBAECHEL_PRJBEYOND_DB must be set to a prjbeyond database checkout")
	endif()

	set(device_bba ${CMAKE_BINARY_DIR}/share/himbaechel/ng-ultra/chipdb-${device}.bba)
	set(device_bin ${CMAKE_BINARY_DIR}/share/himbaechel/ng-ultra/chipdb-${device}.bin)
	string(TOUPPER ${device} upcase_device)
	add_custom_command(
		OUTPUT ${device_bin}
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gen/arch_gen.py --db ${HIMBAECHEL_PRJBEYOND_DB} --device ${upcase_device} --bba ${device_bba}
		COMMAND bbasm ${BBASM_ENDIAN_FLAG} ${device_bba} ${device_bin}.new
        # atomically update
        COMMAND ${CMAKE_COMMAND} -E rename ${device_bin}.new ${device_bin}
		DEPENDS
			bbasm
			${CMAKE_CURRENT_SOURCE_DIR}/gen/arch_gen.py
			${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
		VERBATIM)
	list(APPEND chipdb_binaries ${device_bin})
endforeach()

add_custom_target(chipdb-himbaechel-ng-ultra ALL DEPENDS ${chipdb_binaries})
install(DIRECTORY ${CMAKE_BINARY_DIR}/share/himbaechel/ng-ultra/ DESTINATION share/nextpnr/himbaechel/ng-ultra
	    PATTERN "*.bba" EXCLUDE)
