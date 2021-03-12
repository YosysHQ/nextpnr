function(create_rapidwright_device_db)
    set(options)
    set(oneValueArgs part)
    set(multiValueArgs)

    cmake_parse_arguments(
        create_rapidwright_device_db
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(part ${create_rapidwright_device_db_part})
    set(rapidwright_device_db ${CMAKE_CURRENT_BINARY_DIR}/${part}.device)
    add_custom_command(
        OUTPUT ${rapidwright_device_db}
        COMMAND
            RAPIDWRIGHT_PATH=${RAPIDWRIGHT_PATH}
            ${RAPIDWRIGHT_PATH}/scripts/invoke_rapidwright.sh
            com.xilinx.rapidwright.interchange.DeviceResourcesExample
            ${part}
        DEPENDS
            ${RAPIDWRIGHT_PATH}/scripts/invoke_rapidwright.sh
    )

    add_custom_target(rapidwright-${part}-device DEPENDS ${rapidwright_device_db})
    set_property(TARGET rapidwright-${part}-device PROPERTY LOCATION ${rapidwright_device_db})
endfunction()

function(generate_chipdb)
    set(options)
    set(oneValueArgs part)
    set(multiValueArgs)

    cmake_parse_arguments(
        generate_chipdb
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(part ${generate_chipdb_part})

    create_rapidwright_device_db(part ${part})

    set(rapidwright_device_target rapidwright-${part}-device)
    get_property(rapidwright_device_loc TARGET ${rapidwright_device_target} PROPERTY LOCATION)

    set(constraints_device ${CMAKE_CURRENT_BINARY_DIR}/${part}_constraints.device)
    add_custom_command(
        OUTPUT ${constraints_device}
        COMMAND
            python3 -mfpga_interchange.patch
                --schema_dir ${INTERCHANGE_SCHEMA_PATH}
                --schema device
                --patch_path constraints
                --patch_format yaml
                ${rapidwright_device_loc}
                ${PYTHON_INTERCHANGE_PATH}/test_data/series7_constraints.yaml
                ${constraints_device}
        DEPENDS
            ${rapidwright_device_target}
    )

    add_custom_target(constraints-${part}-device DEPENDS ${constraints_device})

    set(constraints_luts_device ${CMAKE_CURRENT_BINARY_DIR}/${part}_constraints_luts.device)
    add_custom_command(
        OUTPUT ${constraints_luts_device}
        COMMAND
            python3 -mfpga_interchange.patch
                --schema_dir ${INTERCHANGE_SCHEMA_PATH}
                --schema device
                --patch_path lutDefinitions
                --patch_format yaml
                ${constraints_device}
                ${PYTHON_INTERCHANGE_PATH}/test_data/series7_luts.yaml
                ${constraints_luts_device}
        DEPENDS
            ${constraints_device}
    )

    add_custom_target(constraints-luts-${part}-device DEPENDS ${constraints_luts_device})
    set_property(TARGET constraints-luts-${part}-device PROPERTY LOCATION ${constraints_luts_device})

    set(chipdb_bba ${chipdb_dir}/chipdb-${part}.bba)
    add_custom_command(
        OUTPUT ${chipdb_bba}
        COMMAND
            python3 -mfpga_interchange.nextpnr_emit
                --schema_dir ${INTERCHANGE_SCHEMA_PATH}
                --output_dir ${chipdb_dir}
                --bel_bucket_seeds ${PYTHON_INTERCHANGE_PATH}/test_data/series7_bel_buckets.yaml
                --device ${constraints_luts_device}
        COMMAND
            mv ${chipdb_dir}/chipdb.bba ${chipdb_bba}
        DEPENDS
            ${constraints_luts_device}
    )

    add_custom_target(chipdb-${part}-bba DEPENDS ${chipdb_bba})
    add_dependencies(chipdb-${family}-bbas chipdb-${part}-bba)
endfunction()

set(chipdb_dir ${CMAKE_CURRENT_BINARY_DIR}/${family}/chipdb)
file(MAKE_DIRECTORY ${chipdb_dir})

add_custom_target(chipdb-${family}-bbas)

add_subdirectory(${family}/examples/devices)
