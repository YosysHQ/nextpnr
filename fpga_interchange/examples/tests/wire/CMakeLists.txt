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
    name wire_arty_35t
    family ${family}
    device xc7a35t
    package csg324
    tcl run.tcl
    xdc wire_arty.xdc
    sources wire.v
)

add_interchange_test(
    name wire_arty_100t
    family ${family}
    device xc7a100t
    package csg324
    tcl run.tcl
    xdc wire_arty.xdc
    sources wire.v
)

add_interchange_test(
    name wire_nexys_video
    family ${family}
    device xc7a200t
    package sbg484
    tcl run.tcl
    xdc wire_nexys_video.xdc
    sources wire.v
)

add_interchange_test(
    name wire_zybo
    family ${family}
    device xc7z010
    package clg400
    tcl run.tcl
    xdc wire_zybo.xdc
    sources wire.v
)
