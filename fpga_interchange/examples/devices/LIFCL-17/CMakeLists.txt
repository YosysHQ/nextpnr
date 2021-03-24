generate_nexus_device_db(
    device LIFCL-17
    part LIFCL-17-7SG72C
    device_target lifcl17_target
)

generate_chipdb(
    family ${family}
    device LIFCL-17
    part LIFCL-17-7SG72C
    device_target ${lifcl17_target}
    device_config ${PYTHON_INTERCHANGE_PATH}/test_data/nexus_device_config.yaml
    test_package QFN72
)
