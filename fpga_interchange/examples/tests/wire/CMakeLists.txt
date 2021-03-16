add_interchange_test(
    name wire_basys3
    family ${family}
    device xc7a35t
    package cpg236
    tcl run.tcl
    xdc wire_basys3.xdc
    sources wire.v
)

add_interchange_test(
    name wire_arty
    family ${family}
    device xc7a35t
    package csg324
    tcl run.tcl
    xdc wire_arty.xdc
    sources wire.v
)
