add_interchange_test(
    name lut_basys3
    device xc7a50t
    package cpg236
    tcl run.tcl
    xdc lut.xdc
    sources lut.v
)

add_interchange_test(
    name lut_arty
    device xc7a50t
    package csg324
    tcl run.tcl
    xdc lut.xdc
    sources lut.v
)
