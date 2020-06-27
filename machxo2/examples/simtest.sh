#!/usr/bin/env bash
set -ex
yosys -p "tcl ../synth/synth_machxo2.tcl 4 blinky.json" blinky.v
${NEXTPNR:-../../nextpnr-machxo2} --no-iobs --json blinky.json --write pnrblinky.json
yosys -p "read_json pnrblinky.json; write_verilog -noattr -norename pnrblinky.v"
iverilog -o blinky_simtest ../synth/prims.v blinky_tb.v pnrblinky.v
vvp -N ./blinky_simtest
