add_interchange_group_test(
    name counter
    family ${family}
    board_list basys3 arty35t arty100t zybo
    tcl run_xilinx.tcl
    sources counter.v
    techmap ../../remap_xilinx.v
    output_fasm
)

add_interchange_group_test(
    name counter
    family ${family}
    board_list lifcl17 lifcl40evn
    tcl run_nexus.tcl
    sources counter.v
    techmap ../../remap_nexus.v
    skip_dcp
)
