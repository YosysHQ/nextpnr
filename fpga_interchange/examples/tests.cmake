function(add_interchange_test)
    # ~~~
    # add_interchange_test(
    #    name <name>
    #    family <family>
    #    device <common device>
    #    package <package>
    #    tcl <tcl>
    #    xdc <xdc>
    #    sources <sources list>
    #    [top <top name>]
    #    [techmap <techmap file>]
    #    [output_fasm]
    #    [device_family <device family>]
    # )
    #
    # Generates targets to run desired tests
    #
    # Arguments:
    #   - name: test name. This must be unique and no other tests with the same
    #           name should exist
    #   - family: nextpnr architecture family (e.g. fpga_interchange)
    #   - device_family: common device name of a set of parts. E.g. xc7a35tcsg324-1 and xc7a35tcpg236-1
    #             share the same xc7a35t device prefix
    #   - device: common device name of a set of parts. E.g. xc7a35tcsg324-1 and xc7a35tcpg236-1
    #             share the same xc7a35t device prefix
    #   - package: package among the ones available for the device
    #   - tcl: tcl script used for synthesis
    #   - xdc: constraints file used in the physical netlist generation step
    #   - sources: list of HDL sources
    #   - top (optional): name of the top level module.
    #                     If not provided, "top" is assigned as top level module
    #   - techmap (optional): techmap file used during synthesis
    #   - output_fasm (optional): generates a fasm output
    #   - device_family (optional): this information is used during FASM generation, therfore it is
    #                               required only if the `output_fasm` option is enabled
    #
    # Targets generated:
    #   - test-fpga_interchange-<name>-json     : synthesis output
    #   - test-fpga_interchange-<name>-netlist  : interchange logical netlist
    #   - test-fpga_interchange-<name>-phys     : interchange physical netlist
    #   - test-fpga_interchange-<name>-dcp     : design checkpoint with RapidWright

    set(options skip_dcp output_fasm)
    set(oneValueArgs name family device package tcl xdc top techmap device_family)
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
    set(skip_dcp ${add_interchange_test_skip_dcp})
    set(output_fasm ${add_interchange_test_output_fasm})
    set(top ${add_interchange_test_top})
    set(tcl ${CMAKE_CURRENT_SOURCE_DIR}/${add_interchange_test_tcl})
    set(xdc ${CMAKE_CURRENT_SOURCE_DIR}/${add_interchange_test_xdc})
    set(techmap ${CMAKE_CURRENT_SOURCE_DIR}/${add_interchange_test_techmap})
    set(device_family ${add_interchange_test_device_family})

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
    set(synth_log ${CMAKE_CURRENT_BINARY_DIR}/${name}.json.log)
    add_custom_command(
        OUTPUT ${synth_json}
        COMMAND ${CMAKE_COMMAND} -E env
            SOURCES="${sources}"
            OUT_JSON=${synth_json}
            TECHMAP=${techmap}
            yosys -c ${tcl} -l ${synth_log}
        DEPENDS ${sources} ${techmap} ${tcl}
    )

    add_custom_target(test-${family}-${name}-json DEPENDS ${synth_json})

    # Logical Netlist
    get_property(device_target TARGET device-${device} PROPERTY DEVICE_TARGET)
    get_property(device_loc TARGET device-${device} PROPERTY DEVICE_LOC)

    set(netlist ${CMAKE_CURRENT_BINARY_DIR}/${name}.netlist)
    add_custom_command(
        OUTPUT ${netlist}
        COMMAND
            ${Python3_EXECUTABLE} -mfpga_interchange.yosys_json
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
            ${Python3_EXECUTABLE} -mfpga_interchange.convert
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
    set(phys_log ${CMAKE_CURRENT_BINARY_DIR}/${name}.phys.log)
    add_custom_command(
        OUTPUT ${phys}
        COMMAND
            nextpnr-fpga_interchange
                --chipdb ${chipdb_bin_loc}
                --xdc ${xdc}
                --netlist ${netlist}
                --phys ${phys}
                --package ${package}
                --log ${phys_log}
        DEPENDS
            nextpnr-fpga_interchange
            ${netlist}
            ${xdc}
            ${chipdb_bin_target}
            ${chipdb_bin_loc}
    )

    set(phys_verbose_log ${CMAKE_CURRENT_BINARY_DIR}/${name}.phys.verbose.log)
    add_custom_target(
        test-${family}-${name}-phys-verbose
        COMMAND
            nextpnr-fpga_interchange
                --chipdb ${chipdb_bin_loc}
                --xdc ${xdc}
                --netlist ${netlist}
                --phys ${phys}
                --package ${package}
                --verbose
                --log ${phys_verbose_log}
        DEPENDS
            ${netlist}
            ${xdc}
            ${chipdb_bin_target}
            ${chipdb_bin_loc}
    )

    add_custom_target(
        test-${family}-${name}-phys-verbose2
        COMMAND
            nextpnr-fpga_interchange
                --chipdb ${chipdb_bin_loc}
                --xdc ${xdc}
                --netlist ${netlist}
                --phys ${phys}
                --package ${package}
                --debug
        DEPENDS
            ${netlist}
            ${xdc}
            ${chipdb_bin_target}
            ${chipdb_bin_loc}
    )

    add_custom_target(
        test-${family}-${name}-phys-debug
        COMMAND gdb --args
            $<TARGET_FILE:nextpnr-fpga_interchange>
                --chipdb ${chipdb_bin_loc}
                --xdc ${xdc}
                --netlist ${netlist}
                --phys ${phys}
                --package ${package}
        DEPENDS
            ${netlist}
            ${xdc}
            ${chipdb_bin_target}
            ${chipdb_bin_loc}
    )

    add_custom_target(
        test-${family}-${name}-phys-valgrind
        COMMAND
            PYTHONMALLOC=malloc valgrind
            $<TARGET_FILE:nextpnr-fpga_interchange>
                --chipdb ${chipdb_bin_loc}
                --xdc ${xdc}
                --netlist ${netlist}
                --phys ${phys}
                --package ${package}
        DEPENDS
            ${netlist}
            ${xdc}
            ${chipdb_bin_target}
            ${chipdb_bin_loc}
    )

    if(PROFILER)
        add_custom_target(
            test-${family}-${name}-phys-profile
            COMMAND CPUPROFILE=${name}.prof
                    $<TARGET_FILE:nextpnr-fpga_interchange>
                    --chipdb ${chipdb_bin_loc}
                    --xdc ${xdc}
                    --netlist ${netlist}
                    --phys ${phys}
                    --package ${package}
            DEPENDS
                ${netlist}
                ${xdc}
                ${chipdb_bin_target}
                ${chipdb_bin_loc}
        )
    endif()

    add_custom_target(test-${family}-${name}-phys DEPENDS ${phys})

    # Physical Netlist YAML
    set(phys_yaml ${CMAKE_CURRENT_BINARY_DIR}/${name}.phys.yaml)
    add_custom_command(
        OUTPUT ${phys_yaml}
        COMMAND
            ${Python3_EXECUTABLE} -mfpga_interchange.convert
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

    set(last_target "")
    if(skip_dcp)
        set(last_target test-${family}-${name}-phys)
    else()
        set(dcp ${CMAKE_CURRENT_BINARY_DIR}/${name}.dcp)
        add_custom_command(
            OUTPUT ${dcp}
            COMMAND
                RAPIDWRIGHT_PATH=${RAPIDWRIGHT_PATH}
                ${INVOKE_RAPIDWRIGHT} ${JAVA_HEAP_SPACE}
                com.xilinx.rapidwright.interchange.PhysicalNetlistToDcp
                ${netlist} ${phys} ${xdc} ${dcp}
            DEPENDS
                ${INVOKE_RAPIDWRIGHT}
                ${phys}
                ${netlist}
        )

        add_custom_target(test-${family}-${name}-dcp DEPENDS ${dcp})
        set(last_target test-${family}-${name}-dcp)
    endif()

    add_dependencies(all-${device}-tests ${last_target})
    add_dependencies(all-${family}-tests ${last_target})

    if(output_fasm)
        if(NOT DEFINED device_family)
            message(FATAL_ERROR "If FASM output is enabled, the device family must be provided as well!")
        endif()

        # Output FASM target
        set(fasm ${CMAKE_CURRENT_BINARY_DIR}/${name}.fasm)
        add_custom_command(
            OUTPUT ${fasm}
            COMMAND
                ${Python3_EXECUTABLE} -mfpga_interchange.fasm_generator
                    --schema_dir ${INTERCHANGE_SCHEMA_PATH}
                    --family ${device_family}
                    ${device_loc}
                    ${netlist}
                    ${phys}
                    ${fasm}
            DEPENDS
                ${device_target}
                ${netlist}
                ${phys}
        )

        add_custom_target(test-${family}-${name}-fasm DEPENDS ${fasm} ${last_target})
        add_dependencies(all-${device}-tests test-${family}-${name}-fasm)
        add_dependencies(all-${family}-tests test-${family}-${name}-fasm)
    endif()
endfunction()

function(add_interchange_group_test)
    # ~~~
    # add_interchange_group_test(
    #    name <name>
    #    family <family>
    #    board_list <boards>
    #    xdc_list <xdc>
    #    tcl <tcl>
    #    sources <sources list>
    #    [top <top name>]
    #    [techmap <techmap file>]
    #    [skip_dcp]
    # )
    #
    # Generates targets to run desired tests over multiple devices.
    #
    # Arguments:
    #   - name: base test name. The real test name will be <name>_<board>
    #   - family: nextpnr architecture family (e.g. fpga_interchange)
    #   - board_list: list of boards, one for each test
    #   - tcl: tcl script used for synthesis
    #   - sources: list of HDL sources
    #   - top (optional): name of the top level module.
    #                     If not provided, "top" is assigned as top level module
    #   - techmap (optional): techmap file used during synthesis
    #
    # This function internally calls add_interchange_test to generate the various tests.
    #
    # Note: it is assumed that there exists an XDC file for each board, with the following naming
    #       convention: <board>.xdc

    set(options output_fasm skip_dcp)
    set(oneValueArgs name family tcl top techmap)
    set(multiValueArgs sources board_list)

    cmake_parse_arguments(
        add_interchange_group_test
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(name ${add_interchange_group_test_name})
    set(family ${add_interchange_group_test_family})
    set(top ${add_interchange_group_test_top})
    set(tcl ${add_interchange_group_test_tcl})
    set(techmap ${add_interchange_group_test_techmap})
    set(sources ${add_interchange_group_test_sources})
    set(output_fasm ${add_interchange_group_test_output_fasm})
    set(skip_dcp ${add_interchange_group_test_skip_dcp})

    set(output_fasm_arg "")
    if(output_fasm)
        set(output_fasm_arg "output_fasm")
    endif()

    set(skip_dcp_arg "")
    if(skip_dcp)
        set(skip_dcp_arg "skip_dcp")
    endif()

    if (NOT DEFINED top)
        # Setting default top value
        set(top "top")
    endif()

    foreach(board ${add_interchange_group_test_board_list})
        get_property(device_family TARGET board-${board} PROPERTY DEVICE_FAMILY)
        get_property(device TARGET board-${board} PROPERTY DEVICE)
        get_property(package TARGET board-${board} PROPERTY PACKAGE)

        add_interchange_test(
            name ${name}_${board}
            family ${family}
            device ${device}
            device_family ${device_family}
            package ${package}
            tcl ${tcl}
            xdc ${board}.xdc
            sources ${sources}
            top ${top}
            techmap ${techmap}
            ${output_fasm_arg}
            ${skip_dcp_arg}
        )
    endforeach()
endfunction()
