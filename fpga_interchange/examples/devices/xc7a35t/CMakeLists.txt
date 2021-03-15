generate_xc7_device_db(
    device xc7a35t
    part xc7a35tcsg324-1
)

generate_chipdb(
    device xc7a35t
    part xc7a35tcsg324-1
    device_target ${device_target}
    bel_bucket_seeds ${PYTHON_INTERCHANGE_PATH}/test_data/series7_bel_buckets.yaml
    package csg324
)
