add_interchange_group_test(
    name wire
    family ${family}
    board_list basys3 arty35t zybo arty100t nexys_video
    tcl run.tcl
    sources wire.v
    output_fasm
)

add_interchange_group_test(
    name wire
    family ${family}
    board_list lifcl40evn
    tcl run_nexus.tcl
    sources wire.v
    skip_dcp
)
