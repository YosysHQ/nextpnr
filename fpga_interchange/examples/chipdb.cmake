function(create_rapidwright_device_db)
    # ~~~
    # create_rapidwright_device_db(
    #    device <common device>
    #    part <part>
    # )
    # ~~~
    #
    # Generates a device database from RapidWright
    #
    # Targets generated:
    #   - rapidwright-<device>-device

    set(options)
    set(oneValueArgs device part)
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
    set(rapidwright_device_db ${CMAKE_CURRENT_BINARY_DIR}/${part}.device)
    add_custom_command(
        OUTPUT ${rapidwright_device_db}
        COMMAND
            RAPIDWRIGHT_PATH=${RAPIDWRIGHT_PATH}
            ${INVOKE_RAPIDWRIGHT}
            com.xilinx.rapidwright.interchange.DeviceResourcesExample
            ${part}
        DEPENDS
            ${INVOKE_RAPIDWRIGHT}
    )

    add_custom_target(rapidwright-${device}-device DEPENDS ${rapidwright_device_db})
    set_property(TARGET rapidwright-${device}-device PROPERTY LOCATION ${rapidwright_device_db})
endfunction()

function(create_patched_device_db)
    # ~~~
    # create_patched_device_db(
    #    device <common device>
    #    patch_name <patch_name>
    #    patch_path <patch_path>
    #    patch_format <patch_format>
    #    patch_data <patch_data>
    #    input_device <input_device target>
    # )
    # ~~~
    #
    # Generates a patched device database starting from an input device
    #
    # Targets generated:
    #   - <patch_name>-<device>-device

    set(options)
    set(oneValueArgs device patch_name patch_path patch_format patch_data input_device)
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

    get_property(input_device_loc TARGET ${input_device} PROPERTY LOCATION)
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
            ${input_device}
            ${input_device_loc}
    )

    add_custom_target(${patch_name}-${device}-device DEPENDS ${output_device_file})
    set_property(TARGET ${patch_name}-${device}-device PROPERTY LOCATION ${output_device_file})
endfunction()

function(generate_chipdb)
    # ~~~
    # create_patched_device_db(
    #    device <common device>
    #    part <part>
    # )
    # ~~~
    #
    # Generates a chipdb BBA file, starting from a RapidWright device database which is then patched.
    # Patches applied:
    #   - constraints patch
    #   - luts patch
    #
    # The chipdb file is moved to the <nextpnr-root>/build/fpga_interchange/chipdb/ directory
    #
    # Targets generated:
    #   - chipdb-<device>-bba

    set(options)
    set(oneValueArgs device part bel_bucket_seeds)
    set(multiValueArgs)

    cmake_parse_arguments(
        generate_chipdb
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(device ${generate_chipdb_device})
    set(part ${generate_chipdb_part})
    set(bel_bucket_seeds ${generate_chipdb_bel_bucket_seeds})

    create_rapidwright_device_db(
        device ${device}
        part ${part}
    )

    # Generate constraints patch
    create_patched_device_db(
        device ${device}
        patch_name constraints
        patch_path constraints
        patch_format yaml
        patch_data ${PYTHON_INTERCHANGE_PATH}/test_data/series7_constraints.yaml
        input_device rapidwright-${device}-device
    )

    # Generate lut constraints patch
    create_patched_device_db(
        device ${device}
        patch_name constraints-luts
        patch_path lutDefinitions
        patch_format yaml
        patch_data ${PYTHON_INTERCHANGE_PATH}/test_data/series7_luts.yaml
        input_device constraints-${device}-device
    )

    get_property(constraints_luts_device_loc TARGET constraints-luts-${device}-device PROPERTY LOCATION)
    set(chipdb_bba ${chipdb_dir}/chipdb-${device}.bba)
    add_custom_command(
        OUTPUT ${chipdb_bba}
        COMMAND
           ${PYTHON_EXECUTABLE} -mfpga_interchange.nextpnr_emit
                --schema_dir ${INTERCHANGE_SCHEMA_PATH}
                --output_dir ${CMAKE_CURRENT_BINARY_DIR}
                --bel_bucket_seeds ${bel_bucket_seeds}
                --device ${constraints_luts_device_loc}
        COMMAND
            mv ${CMAKE_CURRENT_BINARY_DIR}/chipdb.bba ${chipdb_bba}
        DEPENDS
            constraints-luts-${device}-device
            ${constraints_luts_device_loc}
    )

    add_custom_target(chipdb-${device}-bba DEPENDS ${chipdb_bba})
    add_dependencies(chipdb-${family}-bbas chipdb-${device}-bba)
endfunction()

set(chipdb_dir ${CMAKE_CURRENT_BINARY_DIR}/${family}/chipdb)
file(MAKE_DIRECTORY ${chipdb_dir})

add_custom_target(chipdb-${family}-bbas)

add_subdirectory(${family}/examples/devices)
