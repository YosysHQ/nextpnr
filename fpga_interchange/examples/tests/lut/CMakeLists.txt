add_interchange_group_test(
    name lut
    family ${family}
    board_list basys3 arty35t arty100t
    tcl run.tcl
    sources lut.v
    output_fasm
)
