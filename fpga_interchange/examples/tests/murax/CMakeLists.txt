add_interchange_test(
    name murax_basys3
    family ${family}
    device xc7a35t
    package cpg236
    top toplevel
    tcl run.tcl
    xdc murax.xdc
    sources Murax.v basys3_toplevel.v
    techmap ../../remap.v
)
