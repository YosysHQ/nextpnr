include(${family}/examples/chipdb_xilinx.cmake)
include(${family}/examples/chipdb_nexus.cmake)

function(create_patched_device_db)
    # ~~~
    # create_patched_device_db(
    #    device <common device>
    #    patch_name <patch_name>
    #    patch_path <patch_path>
    #    patch_format <patch_format>
    #    patch_data <patch_data>
    #    input_device <input device target>
    #    output_target <output device target>
    # )
    # ~~~
    #
    # Generates a patched device database starting from an input device
    #
    # If output_target is specified, the variable named as the output_target
    # parameter value is set to the generated output_device_file target.
    #
    # Arguments:
    #   - device: common device name of a set of parts. E.g. xc7a35tcsg324-1 and xc7a35tcpg236-1
    #             share the same xc7a35t device prefix.
    #   - patch_name: name of the patch which determines the target name
    #   - patch_path: patch_path argument for the fpga_interchange.patch call
    #   - patch_format: patch_format argument for the fpga_interchange.patch call
    #   - patch_data: path to the patch_data required for the fpga_interchange.patch call
    #   - input_device: target for the device that needs to be patched
    #   - output_target: variable name that will hold the output device target for the parent scope
    #
    # Targets generated:
    #   - <patch_name>-<device>-device

    set(options)
    set(oneValueArgs device patch_name patch_path patch_format patch_data input_device output_target)
    set(multiValueArgs)

    cmake_parse_arguments(
        create_patched_device_db
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(device ${create_patched_device_db_device})
    set(patch_name ${create_patched_device_db_patch_name})
    set(patch_path ${create_patched_device_db_patch_path})
    set(patch_format ${create_patched_device_db_patch_format})
    set(patch_data ${create_patched_device_db_patch_data})
    set(input_device ${create_patched_device_db_input_device})
    set(output_target ${create_patched_device_db_output_target})

    get_target_property(input_device_loc ${input_device} LOCATION)
    set(output_device_file ${CMAKE_CURRENT_BINARY_DIR}/${device}_${patch_name}.device)
    add_custom_command(
        OUTPUT ${output_device_file}
        COMMAND
            ${PYTHON_EXECUTABLE} -mfpga_interchange.patch
                --schema_dir ${INTERCHANGE_SCHEMA_PATH}
                --schema device
                --patch_path ${patch_path}
                --patch_format ${patch_format}
                ${input_device_loc}
                ${patch_data}
                ${output_device_file}
        DEPENDS
            ${patch_data}
            ${input_device}
            ${input_device_loc}
    )

    add_custom_target(${patch_name}-${device}-device DEPENDS ${output_device_file})
    set_property(TARGET ${patch_name}-${device}-device PROPERTY LOCATION ${output_device_file})

    add_custom_target(${patch_name}-${device}-device-yaml
        COMMAND
            ${PYTHON_EXECUTABLE} -mfpga_interchange.convert
                --schema_dir ${INTERCHANGE_SCHEMA_PATH}
                --schema device
                --input_format capnp
                --output_format yaml
                ${output_device_file}
                ${output_device_file}.yaml
        DEPENDS ${output_device_file})

    if (DEFINED output_target)
        set(${output_target} ${patch_name}-${device}-device PARENT_SCOPE)
    endif()
endfunction()

function(patch_device_with_prim_lib)
    # ~~~
    # patch_device_with_prim_lib(
    #    device <common device>
    #    yosys_script <yosys script>
    #    input_device <input device target>
    #    output_target <output device target>
    # )
    # ~~~
    #
    # Patches an input device with a primitive library from Yosys
    #
    # If output_target is specified, the variable named as the output_target
    # parameter value is set to the generated output_device_file target.
    #
    # Arguments:
    #   - device: common device name of a set of parts. E.g. xc7a35tcsg324-1 and xc7a35tcpg236-1
    #             share the same xc7a35t device prefix.
    #   - yosys_script: yosys script to produce cell library
    #   - input_device: target for the device that needs to be patched
    #   - output_target: variable name that will hold the output device target for the parent scope
    #
    # Targets generated:
    #   - prims-<device>-device

    set(options)
    set(oneValueArgs device yosys_script input_device output_target)
    set(multiValueArgs)

    cmake_parse_arguments(
        patch_device_with_prim_lib
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(device ${patch_device_with_prim_lib_device})
    set(yosys_script ${patch_device_with_prim_lib_yosys_script})
    set(input_device ${patch_device_with_prim_lib_input_device})
    set(output_target ${patch_device_with_prim_lib_output_target})

    get_target_property(input_device_loc ${input_device} LOCATION)
    set(output_device_file ${CMAKE_CURRENT_BINARY_DIR}/${device}_prim_lib.device)
    set(output_json_file ${CMAKE_CURRENT_BINARY_DIR}/${device}_prim_lib.json)

    add_custom_command(
        OUTPUT ${output_json_file}
        COMMAND
            yosys -p '${yosys_script}\; write_json ${output_json_file}'
    )

    add_custom_command(
        OUTPUT ${output_device_file}
        COMMAND
            ${PYTHON_EXECUTABLE} -mfpga_interchange.add_prim_lib
                --schema_dir ${INTERCHANGE_SCHEMA_PATH}
                ${input_device_loc}
                ${output_json_file}
                ${output_device_file}
        DEPENDS
            ${input_device}
            ${input_device_loc}
            ${output_json_file}
    )

    add_custom_target(prims-${device}-device DEPENDS ${output_device_file})
    set_property(TARGET prims-${device}-device PROPERTY LOCATION ${output_device_file})

    if (DEFINED output_target)
        set(${output_target} prims-${device}-device PARENT_SCOPE)
    endif()
endfunction()

function(generate_chipdb)
    # ~~~
    # generate_chipdb(
    #    family <family>
    #    device <common device>
    #    part <part>
    #    device_target <device target>
    #    device_config <device config>
    #    test_package <test_package>
    # )
    # ~~~
    #
    # Generates a chipdb BBA file, starting from a device database.
    #
    # The chipdb binary file is directly generated to the
    # <nextpnr-root>/build/fpga_interchange/chipdb/ directory.
    #
    # The package argument is only used to run the architecture check target.
    #
    # Arguments:
    #   - family: nextpnr architecture family (e.g. fpga_interchange)
    #   - device: common device name of a set of parts. E.g. xc7a35tcsg324-1 and xc7a35tcpg236-1
    #             share the same xc7a35t device prefix
    #   - part: one among the parts available for a given device
    #   - device_target: target for the device from which the chipdb is generated
    #   - device_config: path to the device configYAML file
    #       This file specifies some nextpnr specific data, such as BEL bucket
    #       seeds and global BEL names.
    #   - test_package: package among the ones available for the device. This is used for architecture
    #                   testing only
    #
    # Targets generated:
    #   - chipdb-${device}-bba
    #   - chipdb-${device}-bin
    #   - device-${device}
    #
    # The device-${device} target contains properties to get the interchange device as well
    # as the binary chipdb

    set(options)
    set(oneValueArgs family device part device_target device_config test_package)
    set(multiValueArgs)

    cmake_parse_arguments(
        generate_chipdb
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(family ${generate_chipdb_family})
    set(device ${generate_chipdb_device})
    set(part ${generate_chipdb_part})
    set(device_target ${generate_chipdb_device_target})
    set(device_config ${generate_chipdb_device_config})
    set(test_package ${generate_chipdb_test_package})

    get_target_property(device_loc ${device_target} LOCATION)
    set(chipdb_bba ${CMAKE_CURRENT_BINARY_DIR}/chipdb.bba)
    add_custom_command(
        OUTPUT ${chipdb_bba}
        COMMAND
            ${PYTHON_EXECUTABLE} -mfpga_interchange.nextpnr_emit
                --schema_dir ${INTERCHANGE_SCHEMA_PATH}
                --output_dir ${CMAKE_CURRENT_BINARY_DIR}
                --device_config ${device_config}
                --device ${device_loc}
        DEPENDS
            ${device_config}
            ${device_target}
            ${device_loc}
    )

    add_custom_target(chipdb-${device}-bba DEPENDS ${chipdb_bba})

    set(chipdb_bin ${chipdb_dir}/chipdb-${device}.bin)
    add_custom_command(
        OUTPUT ${chipdb_bin}
        COMMAND
            bbasm -l ${chipdb_bba} ${chipdb_bin}
        DEPENDS
            chipdb-${device}-bba
            ${chipdb_bba}
            bbasm
    )

    add_custom_target(chipdb-${device}-bin DEPENDS ${chipdb_bin})

    # Setting device target properties
    add_custom_target(device-${device})
    set_target_properties(
        device-${device}
        PROPERTIES
            DEVICE_LOC ${device_loc}
            DEVICE_TARGET ${device_target}
            CHIPDB_BIN_LOC ${chipdb_bin}
            CHIPDB_BIN_TARGET chipdb-${device}-bin
    )

    # Generate architecture check target
    add_custom_target(
        chipdb-${device}-bin-check
        COMMAND
            nextpnr-fpga_interchange
                --chipdb ${chipdb_bin}
                --package ${test_package}
                --test
        DEPENDS
            ${chipdb_bin}
            chipdb-${device}-bin
    )

    add_custom_target(
        chipdb-${device}-bin-check-verbose
        COMMAND
            nextpnr-fpga_interchange
                --chipdb ${chipdb_bin}
                --package ${test_package}
                --test --verbose
        DEPENDS
            ${chipdb_bin}
            chipdb-${device}-bin
    )

    add_custom_target(
        chipdb-${device}-bin-check-verbose2
        COMMAND
            nextpnr-fpga_interchange
                --chipdb ${chipdb_bin}
                --package ${test_package}
                --test --debug
        DEPENDS
            ${chipdb_bin}
            chipdb-${device}-bin
    )

    add_custom_target(
        chipdb-${device}-bin-check-debug
        COMMAND gdb --args
            $<TARGET_FILE:nextpnr-fpga_interchange>
                --chipdb ${chipdb_bin}
                --package ${test_package}
                --test
        DEPENDS
            ${chipdb_bin}
            chipdb-${device}-bin
    )

    add_custom_target(
        chipdb-${device}-bin-check-test-data
        COMMAND
            nextpnr-fpga_interchange
                --chipdb ${chipdb_bin}
                --package ${test_package}
                --run ${PROJECT_SOURCE_DIR}/python/check_arch_api.py
        DEPENDS
            ${chipdb_bin}
            chipdb-${device}-bin
            ${test_data_binary}
        WORKING_DIRECTORY
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_dependencies(all-${family}-archcheck-tests chipdb-${device}-bin-check-test-data chipdb-${device}-bin-check)

    # All tests targets for this device are added to this target
    add_custom_target(
        all-${device}-tests
        DEPENDS
            chipdb-${device}-bin-check-test-data
            chipdb-${device}-bin-check
    )
endfunction()

