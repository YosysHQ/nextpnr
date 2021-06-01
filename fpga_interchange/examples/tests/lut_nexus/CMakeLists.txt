add_interchange_test(
    name lut_nexus
    family ${family}
    device LIFCL-17
    package QFN72
    tcl run.tcl
    xdc empty.xdc
    sources lut.v
    skip_dcp
)

add_interchange_test(
    name lut_nexus40
    family ${family}
    device LIFCL-40
    package CABGA400
    tcl run.tcl
    xdc empty.xdc
    sources lut.v
    skip_dcp
)
