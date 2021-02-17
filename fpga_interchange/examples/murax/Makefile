DESIGN := murax
DESIGN_TOP := toplevel
PACKAGE := cpg236

include ../template.mk

build/murax.json: Murax.v basys3_toplevel.v run.tcl | build
	yosys -c run.tcl
