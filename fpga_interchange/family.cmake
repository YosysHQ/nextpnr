find_package(TCL)
if(NOT ${TCL_FOUND})
    message(FATAL_ERROR "Tcl is required for FPGA interchange Arch.")
endif()

find_package(ZLIB REQUIRED)

set(RAPIDWRIGHT_PATH $ENV{HOME}/RapidWright CACHE PATH "Path to RapidWright")
set(INVOKE_RAPIDWRIGHT "${RAPIDWRIGHT_PATH}/scripts/invoke_rapidwright.sh" CACHE PATH "Path to RapidWright invocation script")
set(JAVA_HEAP_SPACE "-Xmx8g" CACHE STRING "Heap space reserved for Java")
set(PRJOXIDE_PREFIX $ENV{HOME}/.cargo CACHE PATH "prjoxide install prefix")

# FIXME: Make patch data available in the python package and remove this cached var
set(PYTHON_INTERCHANGE_PATH $ENV{HOME}/python-fpga-interchange CACHE PATH "Path to the FPGA interchange python library")
set(INTERCHANGE_SCHEMA_PATH ${PROJECT_SOURCE_DIR}/3rdparty/fpga-interchange-schema/interchange CACHE PATH "Path to the FPGA interchange schema dir")

add_subdirectory(3rdparty/fpga-interchange-schema/cmake/cxx_static)

include(${family}/examples/chipdb.cmake)
include(${family}/examples/boards.cmake)
include(${family}/examples/tests.cmake)

set(chipdb_dir ${CMAKE_CURRENT_BINARY_DIR}/${family}/chipdb)
file(MAKE_DIRECTORY ${chipdb_dir})

add_custom_target(all-${family}-tests)
add_custom_target(all-${family}-archcheck-tests)
add_subdirectory(${family}/examples/devices)
add_subdirectory(${family}/examples/boards)
add_subdirectory(${family}/examples/tests)

set(PROTOS lookahead.capnp)
set(CAPNP_SRCS)
set(CAPNP_HDRS)
find_package(CapnProto REQUIRED)
foreach (proto ${PROTOS})
    capnp_generate_cpp(CAPNP_SRC CAPNP_HDR fpga_interchange/${proto})
    list(APPEND CAPNP_HDRS ${CAPNP_HDR})
    list(APPEND CAPNP_SRCS ${CAPNP_SRC})
endforeach()

add_library(extra_capnp STATIC ${CAPNP_SRCS})
target_link_libraries(extra_capnp PRIVATE CapnProto::capnp)

target_include_directories(extra_capnp INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/fpga_interchange)

foreach (target ${family_targets})
    target_include_directories(${target} PRIVATE ${TCL_INCLUDE_PATH})
    target_link_libraries(${target} PRIVATE ${TCL_LIBRARY})
    target_link_libraries(${target} PRIVATE fpga_interchange_capnp)
    target_link_libraries(${target} PRIVATE extra_capnp)
    target_link_libraries(${target} PRIVATE z)
endforeach()

if(BUILD_GUI)
    target_link_libraries(gui_${family} fpga_interchange_capnp)
    target_link_libraries(gui_${family} extra_capnp)
    target_link_libraries(gui_${family} z)
endif()
