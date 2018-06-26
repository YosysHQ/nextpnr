#!/usr/bin/env bash
set -ex
NAME=${1%.v}
yosys -p "synth_ice40 -top top; write_json ${NAME}.json" $1
../../nextpnr-ice40 --json ${NAME}.json --pcf test.pcf --asc ${NAME}.asc --verbose
icebox_vlog -p test.pcf -L ${NAME}.asc > ${NAME}_out.v
iverilog -o ${NAME}_sim.out ${NAME}_tb.v ${NAME}_out.v 
vvp ${NAME}_sim.out

