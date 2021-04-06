add_interchange_group_test(
    name counter
    family ${family}
    board_list basys3 arty35t arty100t
    tcl run.tcl
    sources counter.v
    techmap ../../remap.v
)
