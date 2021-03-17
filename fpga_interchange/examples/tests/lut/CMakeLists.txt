add_interchange_test(
    name lut_basys3
    family ${family}
    device xc7a35t
    package cpg236
    tcl run.tcl
    xdc lut_basys3.xdc
    sources lut.v
)

add_interchange_test(
    name lut_arty
    family ${family}
    device xc7a35t
    package csg324
    tcl run.tcl
    xdc lut_arty.xdc
    sources lut.v
)
