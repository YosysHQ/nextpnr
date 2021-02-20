DESIGN := ff
DESIGN_TOP := top
PACKAGE := csg324

include ../template.mk

build/ff.json: ff.v | build
	yosys -c run.tcl
