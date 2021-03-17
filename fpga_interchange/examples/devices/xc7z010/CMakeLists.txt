generate_xc7_device_db(
    device xc7z010
    part xc7z010clg400-1
    device_target xc7z010_target
)

generate_chipdb(
    family ${family}
    device xc7z010
    part xc7z010clg400-1
    device_target ${xc7z010_target}
    bel_bucket_seeds ${PYTHON_INTERCHANGE_PATH}/test_data/series7_bel_buckets.yaml
    test_package clg400
)
