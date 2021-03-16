function(add_interchange_test)
    # ~~~
    # add_interchange_test(
    #    name <name>
    #    family <family>
    #    device <common device>
    #    package <package>
    #    tcl <tcl>
    #    xdc <xdc>
    #    top <top name>
    #    sources <sources list>
    #    [techmap <techmap file>]
    # )
    #
    # Generates targets to run desired tests
    #
    # Targets generated:
    #   - test-fpga_interchange-<name>-json     : synthesis output
    #   - test-fpga_interchange-<name>-netlist  : interchange logical netlist
    #   - test-fpga_interchange-<name>-phys     : interchange physical netlist
    #   - test-fpga_interchange-<name>-dcp     : design checkpoint with RapidWright

    set(options)
    set(oneValueArgs name family device package tcl xdc top techmap)
    set(multiValueArgs sources)

    cmake_parse_arguments(
        add_interchange_test
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(name ${add_interchange_test_name})
    set(family ${add_interchange_test_family})
    set(device ${add_interchange_test_device})
    set(package ${add_interchange_test_package})
    set(top ${add_interchange_test_top})
    set(tcl ${CMAKE_CURRENT_SOURCE_DIR}/${add_interchange_test_tcl})
    set(xdc ${CMAKE_CURRENT_SOURCE_DIR}/${add_interchange_test_xdc})
    set(techmap ${CMAKE_CURRENT_SOURCE_DIR}/${add_interchange_test_techmap})

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
            TECHMAP=${techmap}
            yosys -c ${tcl}
        DEPENDS ${sources}
    )

    add_custom_target(test-${family}-${name}-json DEPENDS ${synth_json})

    # Logical Netlist
    get_property(device_target TARGET device-${device} PROPERTY DEVICE_TARGET)
    get_property(device_loc TARGET device-${device} PROPERTY DEVICE_LOC)

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

    # Logical Netlist YAML
    set(netlist_yaml ${CMAKE_CURRENT_BINARY_DIR}/${name}.netlist.yaml)
    add_custom_command(
        OUTPUT ${netlist_yaml}
        COMMAND
            ${PYTHON_EXECUTABLE} -mfpga_interchange.convert
                --schema_dir ${INTERCHANGE_SCHEMA_PATH}
                --schema logical
                --input_format capnp
                --output_format yaml
                ${netlist}
                ${netlist_yaml}
        DEPENDS
            ${netlist}
    )

    add_custom_target(test-${family}-${name}-netlist-yaml DEPENDS ${netlist_yaml})

    # Physical Netlist
    get_property(chipdb_bin_target TARGET device-${device} PROPERTY CHIPDB_BIN_TARGET)
    get_property(chipdb_bin_loc TARGET device-${device} PROPERTY CHIPDB_BIN_LOC)

    set(phys ${CMAKE_CURRENT_BINARY_DIR}/${name}.phys)
    add_custom_command(
        OUTPUT ${phys}
        COMMAND
            nextpnr-fpga_interchange
                --chipdb ${chipdb_bin_loc}
                --xdc ${xdc}
                --netlist ${netlist}
                --phys ${phys}
                --package ${package}
        DEPENDS
            ${netlist}
            ${chipdb_bin_target}
            ${chipdb_bin_loc}
    )

    add_custom_target(test-${family}-${name}-phys DEPENDS ${phys})

    # Physical Netlist YAML
    set(phys_yaml ${CMAKE_CURRENT_BINARY_DIR}/${name}.phys.yaml)
    add_custom_command(
        OUTPUT ${phys_yaml}
        COMMAND
            ${PYTHON_EXECUTABLE} -mfpga_interchange.convert
                --schema_dir ${INTERCHANGE_SCHEMA_PATH}
                --schema physical
                --input_format capnp
                --output_format yaml
                ${phys}
                ${phys_yaml}
        DEPENDS
            ${phys}
    )

    add_custom_target(test-${family}-${name}-phys-yaml DEPENDS ${phys_yaml})

    set(dcp ${CMAKE_CURRENT_BINARY_DIR}/${name}.dcp)
    add_custom_command(
        OUTPUT ${dcp}
        COMMAND
            RAPIDWRIGHT_PATH=${RAPIDWRIGHT_PATH}
            ${INVOKE_RAPIDWRIGHT}
            com.xilinx.rapidwright.interchange.PhysicalNetlistToDcp
            ${netlist} ${phys} ${xdc} ${dcp}
        DEPENDS
            ${INVOKE_RAPIDWRIGHT}
            ${phys}
            ${netlist}
    )

    add_custom_target(test-${family}-${name}-dcp DEPENDS ${dcp})
    add_dependencies(all-${family}-tests test-${family}-${name}-dcp)
endfunction()
