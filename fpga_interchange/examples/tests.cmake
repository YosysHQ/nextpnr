function(add_interchange_test)
    # ~~~
    # add_interchange_test(
    #    name <name>
    #    part <part>
    #    part <package>
    #    tcl <tcl>
    #    xdc <xdc>
    #    top <top name>
    #    sources <sources list>
    # )
    # ~~~

    set(options)
    set(oneValueArgs name part package tcl xdc top)
    set(multiValueArgs sources)

    cmake_parse_arguments(
        add_interchange_test
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(name ${add_interchange_test_name})
    set(part ${add_interchange_test_part})
    set(package ${add_interchange_test_package})
    set(top ${add_interchange_test_top})
    set(tcl ${CMAKE_CURRENT_SOURCE_DIR}/${add_interchange_test_tcl})
    set(xdc ${CMAKE_CURRENT_SOURCE_DIR}/${add_interchange_test_xdc})

    set(sources)
    foreach(source ${add_interchange_test_sources})
        list(APPEND sources ${CMAKE_CURRENT_SOURCE_DIR}/${source})
    endforeach()

    if (NOT DEFINED top)
        # Setting default top value
        set(top "top")
    endif()

    # Synthesis
    set(synth_json ${CMAKE_CURRENT_BINARY_DIR}/${name}.json)
    add_custom_command(
        OUTPUT ${synth_json}
        COMMAND
            SOURCES=${sources}
            OUT_JSON=${synth_json}
            yosys -c ${tcl}
        DEPENDS ${sources}
    )

    add_custom_target(test-${family}-${name}-json DEPENDS ${synth_json})

    # Logical Netlist
    set(device_target constraints-luts-${part}-device)
    get_property(device_loc TARGET constraints-luts-${part}-device PROPERTY LOCATION)

    set(netlist ${CMAKE_CURRENT_BINARY_DIR}/${name}.netlist)
    add_custom_command(
        OUTPUT ${netlist}
        COMMAND
            python3 -mfpga_interchange.yosys_json
                --schema_dir ${INTERCHANGE_SCHEMA_PATH}
                --device ${device_loc}
                --top ${top}
                ${synth_json}
                ${netlist}
        DEPENDS
            ${synth_json}
            ${device_target}
    )

    add_custom_target(test-${family}-${name}-netlist DEPENDS ${netlist})

    set(chipdb_target chipdb-${part}-bba)

    # Physical Netlist
    set(phys ${CMAKE_CURRENT_BINARY_DIR}/${name}.phys)
    add_custom_command(
        OUTPUT ${phys}
        COMMAND
            nextpnr-fpga_interchange
                --chipdb ${chipdb_dir}/chipdb-${part}.bba
                --xdc ${xdc}
                --netlist ${netlist}
                --phys ${phys}
                --package ${package}
        DEPENDS
            ${netlist}
            ${chipdb_target}
    )

    add_custom_target(test-${family}-${name}-phys DEPENDS ${phys})

    set(dcp ${CMAKE_CURRENT_BINARY_DIR}/${name}.dcp)
    add_custom_command(
        OUTPUT ${dcp}
        COMMAND
            RAPIDWRIGHT_PATH=${RAPIDWRIGHT_PATH}
            ${RAPIDWRIGHT_PATH}/scripts/invoke_rapidwright.sh
            com.xilinx.rapidwright.interchange.PhysicalNetlistToDcp
            ${netlist} ${phys} ${xdc} ${dcp}
        DEPENDS
            ${phys}
            ${netlist}
    )

    add_custom_target(test-${family}-${name}-dcp DEPENDS ${dcp})
    add_dependencies(all-${family}-tests test-${family}-${name}-dcp)
endfunction()

add_custom_target(all-${family}-tests)
add_subdirectory(${family}/examples/tests)
