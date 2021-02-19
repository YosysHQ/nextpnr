DESIGN := lut
DESIGN_TOP := top
PACKAGE := csg324

include ../template.mk

build/lut.json: lut.v | build
	yosys -c run.tcl
