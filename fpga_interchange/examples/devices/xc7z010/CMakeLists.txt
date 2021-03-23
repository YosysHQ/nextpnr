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
    device_config ${PYTHON_INTERCHANGE_PATH}/test_data/series7_device_config.yaml
    test_package clg400
)
