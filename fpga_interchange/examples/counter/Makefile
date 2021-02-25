DESIGN := counter
DESIGN_TOP := top
PACKAGE := cpg236

include ../template.mk

build/counter.json: counter.v | build
	yosys -c run.tcl
