#!/usr/bin/env bash
set -ex
yosys -p "tcl ../synth/synth_machxo2.tcl 4 blinky.json" blinky.v
${NEXTPNR:-../../nextpnr-machxo2} --json blinky.json --write pnrblinky.json
yosys -p "read_verilog -lib ../synth/prims.v; read_json pnrblinky.json; dump -o blinky.il; show -format png -prefix blinky"
