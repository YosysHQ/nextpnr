find_package(TCL)
if(NOT ${TCL_FOUND})
    message(FATAL_ERROR "Tcl is required for FPGA interchange Arch.")
endif()

foreach (target ${family_targets})
    target_link_libraries(${target} LINK_PUBLIC ${TCL_LIBRARY})
    include_directories (${TCL_INCLUDE_PATH})
endforeach()
