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
