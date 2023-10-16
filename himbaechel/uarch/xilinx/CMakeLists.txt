message(STATUS "Configuring Xilinx uarch")
cmake_minimum_required(VERSION 3.5)
project(himbaechel-xilinx-chipdb NONE)

set(HIMBAECHEL_XILINX_DEVICES "" CACHE STRING
    "Include support for these Xilinx devices via himbaechel")
set(HIMBAECHEL_PRJXRAY_DB "" CACHE STRING
    "Path to a project x-ray database")
message(STATUS "Enabled Himbaechel-Xilinx devices: ${HIMBAECHEL_XILINX_DEVICES}")


set(chipdb_binaries)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/share/himbaechel/xilinx)
foreach(device ${HIMBAECHEL_XILINX_DEVICES})
	if("${HIMBAECHEL_PRJXRAY_DB}" STREQUAL "")
		message(SEND_ERROR "HIMBAECHEL_PRJXRAY_DB must be set to a prjxray database checkout")
	endif()

	set(device_bba ${CMAKE_BINARY_DIR}/share/himbaechel/xilinx/chipdb-${device}.bba)
	set(device_bin ${CMAKE_BINARY_DIR}/share/himbaechel/xilinx/chipdb-${device}.bin)
	add_custom_command(
		OUTPUT ${device_bin}
		COMMAND pypy3 ${CMAKE_CURRENT_SOURCE_DIR}/gen/xilinx_gen.py --xray ${HIMBAECHEL_PRJXRAY_DB}/artix7 --device ${device} --bba ${device_bba}
		COMMAND bbasm ${BBASM_ENDIAN_FLAG} ${device_bba} ${device_bin}.new
        # atomically update
        COMMAND ${CMAKE_COMMAND} -E rename ${device_bin}.new ${device_bin}
		DEPENDS
			bbasm
			${CMAKE_CURRENT_SOURCE_DIR}/gen/xilinx_gen.py
			${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
		VERBATIM)
	list(APPEND chipdb_binaries ${device_bin})
endforeach()

add_custom_target(chipdb-himbaechel-xilinx ALL DEPENDS ${chipdb_binaries})
install(DIRECTORY ${CMAKE_BINARY_DIR}/share/himbaechel/xilinx/ DESTINATION share/nextpnr/himbaechel/xilinx
	    PATTERN "*.bba" EXCLUDE)
