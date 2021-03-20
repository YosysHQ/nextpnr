add_interchange_test(
    name counter_basys3
    family ${family}
    device xc7a35t
    package cpg236
    tcl run.tcl
    xdc counter_basys3.xdc
    sources counter.v
    techmap ../../remap.v
)

add_interchange_test(
    name counter_arty
    family ${family}
    device xc7a35t
    package csg324
    tcl run.tcl
    xdc counter_arty.xdc
    sources counter.v
    techmap ../../remap.v
)
