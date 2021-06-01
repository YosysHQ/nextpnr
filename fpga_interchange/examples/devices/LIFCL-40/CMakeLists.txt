generate_nexus_device_db(
    device LIFCL-40
    device_target lifcl40_target
)

generate_chipdb(
    family ${family}
    device LIFCL-40
    part LIFCL-40-9BG400C
    device_target ${lifcl40_target}
    device_config ${PYTHON_INTERCHANGE_PATH}/test_data/nexus_device_config.yaml
    test_package CABGA400
)
