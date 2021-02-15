find_package(TCL)
if(NOT ${TCL_FOUND})
    message(FATAL_ERROR "Tcl is required for FPGA interchange Arch.")
endif()

find_package(ZLIB REQUIRED)

add_subdirectory(3rdparty/fpga-interchange-schema/cmake/cxx_static)

foreach (target ${family_targets})
    target_include_directories(${target} PRIVATE ${TCL_INCLUDE_PATH})
    target_link_libraries(${target} PRIVATE ${TCL_LIBRARY})
    target_link_libraries(${target} PRIVATE fpga_interchange_capnp)
    target_link_libraries(${target} PRIVATE z)
endforeach()
