add_interchange_test(
    name ff_basys3
    device xc7a35t
    package cpg236
    tcl run.tcl
    xdc ff_basys3.xdc
    sources ff.v
)

add_interchange_test(
    name ff_arty
    device xc7a35t
    package csg324
    tcl run.tcl
    xdc ff_arty.xdc
    sources ff.v
)
