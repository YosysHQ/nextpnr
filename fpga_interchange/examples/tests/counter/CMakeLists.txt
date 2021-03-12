add_interchange_test(
    name counter_basys3
    device xc7a35t
    package cpg236
    tcl run.tcl
    xdc counter_basys3.xdc
    sources counter.v
)

add_interchange_test(
    name counter_arty
    device xc7a35t
    package csg324
    tcl run.tcl
    xdc counter_arty.xdc
    sources counter.v
)
