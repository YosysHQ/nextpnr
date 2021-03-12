function(add_interchange_test)
    # ~~~
    # add_interchange_test(
    #    name <name>
    #    device <common device>
    #    package <package>
    #    tcl <tcl>
    #    xdc <xdc>
    #    top <top name>
    #    sources <sources list>
    # )
    #
    # Generates targets to run desired tests
    #
    # Targets generated:
    #   - test-fpga_interchange-<name>-json     : synthesis output
    #   - test-fpga_interchange-<name>-netlist  : interchange logical netlist
    #   - test-fpga_interchange-<name>-phys     : interchange physical netlist
    #   - test-fpga_interchange-<name>-phys     : design checkpoint with RapidWright

    set(options)
    set(oneValueArgs name device package tcl xdc top)
    set(multiValueArgs sources)

    cmake_parse_arguments(
        add_interchange_test
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(name ${add_interchange_test_name})
    set(device ${add_interchange_test_device})
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
    set(device_target constraints-luts-${device}-device)
    get_property(device_loc TARGET constraints-luts-${device}-device PROPERTY LOCATION)

    set(netlist ${CMAKE_CURRENT_BINARY_DIR}/${name}.netlist)
    add_custom_command(
        OUTPUT ${netlist}
        COMMAND
            ${PYTHON_EXECUTABLE} -mfpga_interchange.yosys_json
                --schema_dir ${INTERCHANGE_SCHEMA_PATH}
                --device ${device_loc}
                --top ${top}
                ${synth_json}
                ${netlist}
        DEPENDS
            ${synth_json}
            ${device_target}
            ${device_loc}
    )

    add_custom_target(test-${family}-${name}-netlist DEPENDS ${netlist})

    set(chipdb_target chipdb-${device}-bba)

    # Physical Netlist
    set(phys ${CMAKE_CURRENT_BINARY_DIR}/${name}.phys)
    add_custom_command(
        OUTPUT ${phys}
        COMMAND
            nextpnr-fpga_interchange
                --chipdb ${chipdb_dir}/chipdb-${device}.bba
                --xdc ${xdc}
                --netlist ${netlist}
                --phys ${phys}
                --package ${package}
        DEPENDS
            ${netlist}
            ${chipdb_target}
            ${chipdb_dir}/chipdb-${device}.bba
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
