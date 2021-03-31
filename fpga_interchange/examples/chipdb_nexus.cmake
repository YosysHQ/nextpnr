function(create_prjoxide_device_db)
    # ~~~
    # create_prjoxide_device_db(
    #    device <common device>
    #    output_target <output device target>
    # )
    # ~~~
    #
    # Generates a device database from Project Oxide
    #
    # If output_target is specified, the output_target_name variable
    # is set to the generated output_device_file target.
    #
    # Arguments:
    #   - device: common device name of a set of parts. E.g. LIFCL-17
    #   - output_target: variable name that will hold the output device target for the parent scope
    #
    # Targets generated:
    #   - prjoxide-<device>-device
    set(options)
    set(oneValueArgs device output_target)
    set(multiValueArgs)

    cmake_parse_arguments(
        create_prjoxide_device_db
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(device ${create_prjoxide_device_db_device})
    set(output_target ${create_prjoxide_device_db_output_target})
    set(prjoxide_device_db ${CMAKE_CURRENT_BINARY_DIR}/${device}.device)
    add_custom_command(
        OUTPUT ${prjoxide_device_db}
        COMMAND
            ${PRJOXIDE_PREFIX}/bin/prjoxide
            interchange-export
            ${device}
            ${prjoxide_device_db}
        DEPENDS
            ${PRJOXIDE_PREFIX}/bin/prjoxide
    )

    add_custom_target(prjoxide-${device}-device DEPENDS ${prjoxide_device_db})
    set_property(TARGET prjoxide-${device}-device PROPERTY LOCATION ${prjoxide_device_db})

    if (DEFINED output_target)
        set(${output_target} prjoxide-${device}-device PARENT_SCOPE)
    endif()

endfunction()

function(generate_nexus_device_db)
    # ~~~
    # generate_nexus_device_db(
    #    device <common device>
    #    device_target <variable name for device target>
    # )
    # ~~~
    #
    # Generates a chipdb BBA file, starting from a Project Oxide device database.
    # Patches applied:
    #   - primitive library from Yosys
    #
    # Arguments:
    #   - device: common device name of a set of parts. E.g. LIFCL-17
    #   - device_target: variable name that will hold the output device target for the parent scope
    set(options)
    set(oneValueArgs device device_target)
    set(multiValueArgs)

    cmake_parse_arguments(
        generate_nexus_device_db
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(device ${generate_nexus_device_db_device})
    set(device_target ${generate_nexus_device_db_device_target})

    create_prjoxide_device_db(
        device ${device}
        output_target prjoxide_device
    )

    # Add primitive library
    patch_device_with_prim_lib(
        device ${device}
        yosys_script synth_nexus
        input_device ${prjoxide_device}
        output_target prjoxide_prims_device
    )

    if(DEFINED device_target)
        set(${device_target} ${prjoxide_prims_device} PARENT_SCOPE)
    endif()
endfunction()
