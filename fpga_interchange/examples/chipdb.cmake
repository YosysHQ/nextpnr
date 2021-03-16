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
            ${INVOKE_RAPIDWRIGHT}
            com.xilinx.rapidwright.interchange.DeviceResourcesExample
            ${part}
        DEPENDS
            ${INVOKE_RAPIDWRIGHT}
    )

    add_custom_target(rapidwright-${device}-device DEPENDS ${rapidwright_device_db})
    set_property(TARGET rapidwright-${device}-device PROPERTY LOCATION ${rapidwright_device_db})

    if (DEFINED output_target)
        set(${output_target} rapidwright-${device}-device PARENT_SCOPE)
    endif()
endfunction()

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

    if (DEFINED output_target)
        set(${output_target} ${patch_name}-${device}-device PARENT_SCOPE)
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
    # The final device target is output in the device_target variable to use in the parent scope

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

function(generate_chipdb)
    # ~~~
    # generate_chipdb(
    #    family <family>
    #    device <common device>
    #    part <part>
    #    device_target <device target>
    #    bel_bucket_seeds <bel bucket seeds>
    #    package <package>
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
    # Targets generated:
    #   - chipdb-${device}-bba
    #   - chipdb-${device}-bin
    #   - device-${device}
    #
    # The device-${device} target contains properties to get the interchange device as well
    # as the binary chipdb

    set(options)
    set(oneValueArgs family device part device_target bel_bucket_seeds package)
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
    set(bel_bucket_seeds ${generate_chipdb_bel_bucket_seeds})
    set(package ${generate_chipdb_package})

    get_target_property(device_loc ${device_target} LOCATION)
    set(chipdb_bba ${CMAKE_CURRENT_BINARY_DIR}/chipdb.bba)
    add_custom_command(
        OUTPUT ${chipdb_bba}
        COMMAND
            ${PYTHON_EXECUTABLE} -mfpga_interchange.nextpnr_emit
                --schema_dir ${INTERCHANGE_SCHEMA_PATH}
                --output_dir ${CMAKE_CURRENT_BINARY_DIR}
                --bel_bucket_seeds ${bel_bucket_seeds}
                --device ${device_loc}
        DEPENDS
            ${bel_bucket_seeds}
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
    set(test_data_source ${CMAKE_CURRENT_SOURCE_DIR}/test_data.yaml)
    set(test_data_binary ${CMAKE_CURRENT_BINARY_DIR}/test_data.yaml)
    add_custom_command(
        OUTPUT ${test_data_binary}
        COMMAND
            ${CMAKE_COMMAND} -E create_symlink
            ${test_data_source}
            ${test_data_binary}
        DEPENDS
            ${test_data_source}
    )

    add_custom_target(
        chipdb-${device}-bin-check
        COMMAND
            nextpnr-fpga_interchange
                --chipdb ${chipdb_bin}
                --package ${package}
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
                --package ${package}
                --run ${PROJECT_SOURCE_DIR}/python/check_arch_api.py
        DEPENDS
            ${chipdb_bin}
            chipdb-${device}-bin
            ${test_data_binary}
    )

add_dependencies(all-${family}-archcheck-tests chipdb-${device}-bin-check-test-data chipdb-${device}-bin-check)
endfunction()

