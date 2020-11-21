#!/usr/bin/env bash
set -ex
${YOSYS:-yosys} -p "synth_machxo2 -json blinky.json" blinky.v
${NEXTPNR:-../../nextpnr-machxo2} --no-iobs --json blinky.json --write pnrblinky.json
${YOSYS:-yosys} -p "read_json blinky.json; write_verilog  -noattr -norename pnrblinky.v"
iverilog -o blinky_simtest ${CELLS_SIM:-`${YOSYS:yosys}-config --datdir/machxo2/cells_sim.v`} blinky_tb.v pnrblinky.v
vvp -N ./blinky_simtest
