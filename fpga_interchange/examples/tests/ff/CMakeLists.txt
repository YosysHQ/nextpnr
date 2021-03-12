add_interchange_test(
    name ff_basys3
    device xc7a50t
    package cpg236
    tcl run.tcl
    xdc ff.xdc
    sources ff.v
)

add_interchange_test(
    name ff_arty
    device xc7a50t
    package csg324
    tcl run.tcl
    xdc ff.xdc
    sources ff.v
)
