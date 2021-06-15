add_interchange_group_test(
    name ram_nexus
    family ${family}
    board_list lifcl40evn
    tcl run_nexus.tcl
    sources ram_nexus.v
    techmap ../../remap_nexus.v
    skip_dcp
)

