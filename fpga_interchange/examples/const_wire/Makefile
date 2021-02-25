DESIGN := wire
DESIGN_TOP := top
PACKAGE := csg324

include ../template.mk

build/wire.json: wire.v | build
	yosys -c run.tcl
