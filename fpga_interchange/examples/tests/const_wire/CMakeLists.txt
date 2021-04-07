add_interchange_group_test(
    name const_wire
    family ${family}
    board_list basys3 arty35t arty100t
    tcl run.tcl
    sources wire.v
)
