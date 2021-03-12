add_interchange_test(
    name counter_basys3
    device xc7a50t
    package cpg236
    tcl run.tcl
    xdc counter.xdc
    sources counter.v
)

add_interchange_test(
    name counter_arty
    device xc7a50t
    package csg324
    tcl run.tcl
    xdc counter.xdc
    sources counter.v
)
