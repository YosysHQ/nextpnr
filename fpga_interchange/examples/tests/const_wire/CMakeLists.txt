add_interchange_test(
    name const_wire_basys3
    part xc7a35tcpg236-1
    package cpg236
    tcl run.tcl
    xdc wire.xdc
    sources wire.v
)

add_interchange_test(
    name const_wire_arty
    part xc7a35tcsg324-1
    package csg324
    tcl run.tcl
    xdc wire.xdc
    sources wire.v
)
