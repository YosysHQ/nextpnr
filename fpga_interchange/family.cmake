find_package(TCL)
if(NOT ${TCL_FOUND})
    message(FATAL_ERROR "Tcl is required for FPGA interchange Arch.")
endif()

find_package(ZLIB REQUIRED)

set(RAPIDWRIGHT_PATH $ENV{HOME}/RapidWright CACHE PATH "Path to RapidWright")
set(INVOKE_RAPIDWRIGHT ${RAPIDWRIGHT_PATH}/scripts/invoke_rapidwright.sh CACHE PATH "Path to RapidWright invocation script")
# FIXME: Make patch data available in the python package and remove this cached var
set(PYTHON_INTERCHANGE_PATH $ENV{HOME}/python-fpga-interchange CACHE PATH "Path to the FPGA interchange python library")
set(INTERCHANGE_SCHEMA_PATH $ENV{HOME}/fpga_interchange_schema CACHE PATH "Path to the FPGA interchange schema dir")

add_subdirectory(3rdparty/fpga-interchange-schema/cmake/cxx_static)

include(${family}/examples/chipdb.cmake)
include(${family}/examples/tests.cmake)

set(chipdb_dir ${CMAKE_CURRENT_BINARY_DIR}/${family}/chipdb)
file(MAKE_DIRECTORY ${chipdb_dir})

add_custom_target(all-${family}-tests)
add_custom_target(all-${family}-archcheck-tests)
add_subdirectory(${family}/examples/devices)
add_subdirectory(${family}/examples/tests)

foreach (target ${family_targets})
    target_include_directories(${target} PRIVATE ${TCL_INCLUDE_PATH})
    target_link_libraries(${target} PRIVATE ${TCL_LIBRARY})
    target_link_libraries(${target} PRIVATE fpga_interchange_capnp)
    target_link_libraries(${target} PRIVATE z)
    target_link_libraries(${target} PRIVATE profiler)
endforeach()

if(BUILD_GUI)
    target_link_libraries(gui_${family} fpga_interchange_capnp)
    target_link_libraries(gui_${family} z)
endif()
