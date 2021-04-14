add_interchange_group_test(
    name ff
    family ${family}
    board_list basys3 arty35t arty100t
    tcl run.tcl
    sources ff.v
    output_fasm
)
