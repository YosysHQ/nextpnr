add_interchange_test(
    name wire_basys3
    device xc7a50t
    package cpg236
    tcl run.tcl
    xdc wire.xdc
    sources wire.v
)

add_interchange_test(
    name wire_arty
    device xc7a50t
    package csg324
    tcl run.tcl
    xdc wire.xdc
    sources wire.v
)
