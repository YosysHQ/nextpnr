message(STATUS "Configuring Himbaechel-Example uarch")
cmake_minimum_required(VERSION 3.5)
project(himbaechel-example-chipdb NONE)

set(ALL_HIMBAECHEL_EXAMPLE_DEVICES example)
set(HIMBAECHEL_EXAMPLE_DEVICES "" CACHE STRING
    "Include support for these Example devices (available: ${ALL_HIMBAECHEL_EXAMPLE_DEVICES})")
message(STATUS "Enabled Himbaechel-Example devices: ${HIMBAECHEL_EXAMPLE_DEVICES}")

set(chipdb_binaries)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/share/himbaechel/example)
foreach(device ${HIMBAECHEL_EXAMPLE_DEVICES})
	set(device_bba ${CMAKE_BINARY_DIR}/share/himbaechel/example/chipdb-${device}.bba)
	set(device_bin ${CMAKE_BINARY_DIR}/share/himbaechel/example/chipdb-${device}.bin)
	add_custom_command(
		OUTPUT ${device_bin}
		COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/example_arch_gen.py ${device_bba}
		COMMAND bbasm ${BBASM_ENDIAN_FLAG} ${device_bba} ${device_bin}.new
        # atomically update
        COMMAND ${CMAKE_COMMAND} -E rename ${device_bin}.new ${device_bin}
		DEPENDS
			bbasm
			${CMAKE_CURRENT_SOURCE_DIR}/example_arch_gen.py
			${CMAKE_CURRENT_SOURCE_DIR}/constids.inc
		VERBATIM)
	list(APPEND chipdb_binaries ${device_bin})
endforeach()

add_custom_target(chipdb-himbaechel-example ALL DEPENDS ${chipdb_binaries})
install(DIRECTORY ${CMAKE_BINARY_DIR}/share/himbaechel/example/ DESTINATION share/nextpnr/himbaechel/example
	    PATTERN "*.bba" EXCLUDE)
