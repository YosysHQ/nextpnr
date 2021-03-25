generate_xc7_device_db(
    device xc7a100t
    part xc7a100tcsg324-1
    device_target xc7a100t_target
)

generate_chipdb(
    family ${family}
    device xc7a100t
    part xc7a100tcsg324-1
    device_target ${xc7a100t_target}
    device_config ${PYTHON_INTERCHANGE_PATH}/test_data/series7_device_config.yaml
    test_package csg324
)
