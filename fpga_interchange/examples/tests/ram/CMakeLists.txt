add_interchange_test(
    name ram_basys3
    family ${family}
    device xc7a35t
    package cpg236
    tcl run.tcl
    xdc basys3.xdc
    sources ram.v
)

