#!/usr/bin/env bash
set -ex
yosys -p "tcl ../synth/synth_generic.tcl 4 blinky.json" blinky.v
${NEXTPNR:-../../nextpnr-generic} --pre-pack simple.py --pre-place simple_timing.py --json blinky.json --post-route bitstream.py --write pnrblinky.json
yosys -p "read_verilog -lib ../synth/prims.v; read_json pnrblinky.json; dump -o blinky.il; show -format png -prefix blinky"
