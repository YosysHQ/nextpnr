#!/usr/bin/env bash
set -ex
NAME=${1%.v}
yosys -p "synth_ice40 -nocarry -top io_wrapper; write_json ${NAME}.json" $1 io_wrapper.v
../../nextpnr-ice40 --json ${NAME}.json --pack --asc ${NAME}.asc
icebox_vlog -p test.pcf ${NAME}.asc > ${NAME}_out.v

yosys -p "rename top gate\
          read_verilog $1\
          rename top gold\
          hierarchy\
          proc\
          clk2fflogic\
          miter -equiv -flatten -ignore_gold_x -make_outputs -make_outcmp gold gate miter\
          sat -dump_vcd equiv_${NAME}.vcd -verify-no-timeout -timeout 20 -seq 10 -prove trigger 0 -prove-skip 1 -show-inputs -show-outputs miter" ${NAME}_out.v
