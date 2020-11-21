#!/usr/bin/env bash
set -ex
${YOSYS:yosys} -p "synth_machxo2 -json blinky.json" blinky.v
${NEXTPNR:-../../nextpnr-machxo2} --json blinky.json --write pnrblinky.json
${YOSYS:yosys} -p "read_verilog -lib +/machxo2/cells_sim.v; read_json pnrblinky.json; dump -o blinky.il; show -format png -prefix blinky"
