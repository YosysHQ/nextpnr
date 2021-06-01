add_board(
    name arty35t
    device_family xc7
    device xc7a35t
    package csg324
)

add_board(
    name arty100t
    device_family xc7
    device xc7a100t
    package csg324
)

add_board(
    name nexys_video
    device_family xc7
    device xc7a200t
    package sbg484
)

add_board(
    name basys3
    device_family xc7
    device xc7a35t
    package cpg236
)

add_board(
    name zybo
    device_family xc7
    device xc7z010
    package clg400
)

# This isn't a real board, all the real boards currently available use the LIFCL-40 but the LIFCL-17 speeds up runtime for testing
add_board(
    name lifcl17
    device_family nexus
    device LIFCL-17
    package QFN72
)

add_board(
    name lifcl40evn
    device_family nexus
    device LIFCL-40
    package CABGA400
)
