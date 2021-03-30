function(create_rapidwright_device_db)
    # ~~~
    # create_rapidwright_device_db(
    #    device <common device>
    #    part <part>
    #    output_target <output device target>
    # )
    # ~~~
    #
    # Generates a device database from RapidWright
    #
    # If output_target is specified, the output_target_name variable
    # is set to the generated output_device_file target.
    #
    # Arguments:
    #   - device: common device name of a set of parts. E.g. xc7a35tcsg324-1 and xc7a35tcpg236-1
    #             share the same xc7a35t device prefix
    #   - part: one among the parts available for a given device
    #   - output_target: variable name that will hold the output device target for the parent scope
    #
    # Targets generated:
    #   - rapidwright-<device>-device

    set(options)
    set(oneValueArgs device part output_target)
    set(multiValueArgs)

    cmake_parse_arguments(
        create_rapidwright_device_db
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(device ${create_rapidwright_device_db_device})
    set(part ${create_rapidwright_device_db_part})
    set(output_target ${create_rapidwright_device_db_output_target})
    set(rapidwright_device_db ${CMAKE_CURRENT_BINARY_DIR}/${part}.device)
    add_custom_command(
        OUTPUT ${rapidwright_device_db}
        COMMAND
            RAPIDWRIGHT_PATH=${RAPIDWRIGHT_PATH}
            ${INVOKE_RAPIDWRIGHT} ${JAVA_HEAP_SPACE}
            com.xilinx.rapidwright.interchange.DeviceResourcesExample
            ${part}
        DEPENDS
            ${INVOKE_RAPIDWRIGHT}
    )

    add_custom_target(rapidwright-${device}-device DEPENDS ${rapidwright_device_db})
    set_property(TARGET rapidwright-${device}-device PROPERTY LOCATION ${rapidwright_device_db})

    add_custom_target(rapidwright-${device}-device-yaml
        COMMAND
            ${PYTHON_EXECUTABLE} -mfpga_interchange.convert
                --schema_dir ${INTERCHANGE_SCHEMA_PATH}
                --schema device
                --input_format capnp
                --output_format yaml
                ${rapidwright_device_db}
                ${rapidwright_device_db}.yaml
        DEPENDS ${rapidwright_device_db})

    if (DEFINED output_target)
        set(${output_target} rapidwright-${device}-device PARENT_SCOPE)
    endif()
endfunction()


function(generate_xc7_device_db)
    # ~~~
    # generate_xc7_device_db(
    #    device <common device>
    #    part <part>
    #    device_target <variable name for device target>
    # )
    # ~~~
    #
    # Generates a chipdb BBA file, starting from a RapidWright device database which is then patched.
    # Patches applied:
    #   - constraints patch
    #   - luts patch
    #
    # Arguments:
    #   - device: common device name of a set of parts. E.g. xc7a35tcsg324-1 and xc7a35tcpg236-1
    #             share the same xc7a35t device prefix
    #   - part: one among the parts available for a given device
    #   - device_target: variable name that will hold the output device target for the parent scope

    set(options)
    set(oneValueArgs device part device_target)
    set(multiValueArgs)

    cmake_parse_arguments(
        create_rapidwright_device_db
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(device ${create_rapidwright_device_db_device})
    set(part ${create_rapidwright_device_db_part})
    set(device_target ${create_rapidwright_device_db_device_target})

    create_rapidwright_device_db(
        device ${device}
        part ${part}
        output_target rapidwright_device
    )

    # Generate constraints patch
    create_patched_device_db(
        device ${device}
        patch_name constraints
        patch_path constraints
        patch_format yaml
        patch_data ${PYTHON_INTERCHANGE_PATH}/test_data/series7_constraints.yaml
        input_device ${rapidwright_device}
        output_target constraints_device
    )

    # Generate lut constraints patch
    create_patched_device_db(
        device ${device}
        patch_name constraints-luts
        patch_path lutDefinitions
        patch_format yaml
        patch_data ${PYTHON_INTERCHANGE_PATH}/test_data/series7_luts.yaml
        input_device ${constraints_device}
        output_target constraints_luts_device
    )

    if(DEFINED device_target)
        set(${device_target} ${constraints_luts_device} PARENT_SCOPE)
    endif()
endfunction()
