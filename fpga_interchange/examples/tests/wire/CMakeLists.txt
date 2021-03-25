add_interchange_group_test(
    name wire
    family ${family}
    board_list basys3 arty35t zybo arty100t nexys_video
    tcl run.tcl
    sources wire.v
)
