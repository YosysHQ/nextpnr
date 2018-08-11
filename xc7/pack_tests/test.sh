#!/usr/bin/env bash
set -ex
NAME=${1%.v}
yosys -p "synth_ice40 -nocarry -top top; write_json ${NAME}.json" $1
../../nextpnr-ice40 --json ${NAME}.json --pcf test.pcf --asc ${NAME}.asc
icebox_vlog -p test.pcf ${NAME}.asc > ${NAME}_out.v

yosys -p "read_verilog +/ice40/cells_sim.v;\
          rename chip gate;\
          read_verilog $1;\
          rename top gold;\
          hierarchy;\
          proc;\
          clk2fflogic;\
          miter -equiv -flatten -ignore_gold_x -make_outputs -make_outcmp gold gate miter;\
          sat -dump_vcd equiv_${NAME}.vcd -verify-no-timeout -timeout 60 -seq 50 -prove trigger 0 -prove-skip 1 -show-inputs -show-outputs miter" ${NAME}_out.v
