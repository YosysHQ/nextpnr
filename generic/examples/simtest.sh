#!/usr/bin/env bash
set -ex
yosys -p "tcl ../synth/synth_generic.tcl 4 blinky.json" blinky.v
${NEXTPNR:-../../nextpnr-generic} --no-iobs --pre-pack simple.py --pre-place simple_timing.py --json blinky.json --post-route bitstream.py --write pnrblinky.json
yosys -p "read_json pnrblinky.json; write_verilog -noattr -norename pnrblinky.v"
iverilog -o blinky_simtest ../synth/prims.v blinky_tb.v pnrblinky.v
vvp -N ./blinky_simtest
