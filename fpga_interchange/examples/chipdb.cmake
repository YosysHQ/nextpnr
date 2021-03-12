function(create_rapidwright_device_db)
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
            ${RAPIDWRIGHT_PATH}/scripts/invoke_rapidwright.sh
            com.xilinx.rapidwright.interchange.DeviceResourcesExample
            ${part}
        DEPENDS
            ${RAPIDWRIGHT_PATH}/scripts/invoke_rapidwright.sh
    )

    add_custom_target(rapidwright-${device}-device DEPENDS ${rapidwright_device_db})
    set_property(TARGET rapidwright-${device}-device PROPERTY LOCATION ${rapidwright_device_db})
endfunction()

function(create_patched_device_db)
    set(options)
    set(oneValueArgs device)
    set(multiValueArgs)

    cmake_parse_arguments(
        create_patched_device_db
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    set(device ${create_patched_device_db_device})

    get_property(rapidwright_device_loc TARGET rapidwright-${device}-device PROPERTY LOCATION)

    set(constraints_device ${CMAKE_CURRENT_BINARY_DIR}/${device}_constraints.device)
    add_custom_command(
        OUTPUT ${constraints_device}
        COMMAND
            ${PYTHON_EXECUTABLE} -mfpga_interchange.patch
                --schema_dir ${INTERCHANGE_SCHEMA_PATH}
                --schema device
                --patch_path constraints
                --patch_format yaml
                ${rapidwright_device_loc}
                ${PYTHON_INTERCHANGE_PATH}/test_data/series7_constraints.yaml
                ${constraints_device}
        DEPENDS
            rapidwright-${device}-device
    )

    add_custom_target(constraints-${device}-device DEPENDS ${constraints_device})

    set(constraints_luts_device ${CMAKE_CURRENT_BINARY_DIR}/${device}_constraints_luts.device)
    add_custom_command(
        OUTPUT ${constraints_luts_device}
        COMMAND
            ${PYTHON_EXECUTABLE} -mfpga_interchange.patch
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

    add_custom_target(constraints-luts-${device}-device DEPENDS ${constraints_luts_device})
    set_property(TARGET constraints-luts-${device}-device PROPERTY LOCATION ${constraints_luts_device})
endfunction()

function(generate_chipdb)
    set(options)
    set(oneValueArgs device part)
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

    create_rapidwright_device_db(
        device ${device}
        part ${part}
    )
    create_patched_device_db(device ${device})

    get_property(constrained_luts_device_loc TARGET constraints-luts-${device}-device PROPERTY LOCATION)
    set(chipdb_bba ${chipdb_dir}/chipdb-${device}.bba)
    add_custom_command(
        OUTPUT ${chipdb_bba}
        COMMAND
           ${PYTHON_EXECUTABLE} -mfpga_interchange.nextpnr_emit
                --schema_dir ${INTERCHANGE_SCHEMA_PATH}
                --output_dir ${CMAKE_CURRENT_BINARY_DIR}
                --bel_bucket_seeds ${PYTHON_INTERCHANGE_PATH}/test_data/series7_bel_buckets.yaml
                --device ${constrained_luts_device_loc}
        COMMAND
            mv ${CMAKE_CURRENT_BINARY_DIR}/chipdb.bba ${chipdb_bba}
        DEPENDS
            constraints-luts-${device}-device
    )

    add_custom_target(chipdb-${device}-bba DEPENDS ${chipdb_bba})
    add_dependencies(chipdb-${family}-bbas chipdb-${device}-bba)
endfunction()

set(chipdb_dir ${CMAKE_CURRENT_BINARY_DIR}/${family}/chipdb)
file(MAKE_DIRECTORY ${chipdb_dir})

add_custom_target(chipdb-${family}-bbas)

add_subdirectory(${family}/examples/devices)
