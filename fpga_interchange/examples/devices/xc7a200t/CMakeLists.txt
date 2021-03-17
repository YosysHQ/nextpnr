generate_xc7_device_db(
    device xc7a200t
    part xc7a200tsbg484-1
    device_target xc7a200t_target
)

generate_chipdb(
    family ${family}
    device xc7a200t
    part xc7a200tsbg484-1
    device_target ${xc7a200t_target}
    bel_bucket_seeds ${PYTHON_INTERCHANGE_PATH}/test_data/series7_bel_buckets.yaml
    test_package sbg484
)
