#!/usr/bin/env bash
set -ex
export PYTHONPATH=$(dirname $0)
K=$(python3 -c "import simple_config; print(simple_config.K)")
yosys -p "tcl ../synth/synth_generic.tcl $K blinky.json" blinky.v
${NEXTPNR:-../../nextpnr-generic} --pre-pack simple.py --pre-place simple_timing.py --json blinky.json --post-route bitstream.py --write pnrblinky.json
yosys -p "read_verilog -lib ../synth/prims.v; read_json pnrblinky.json; dump -o blinky.il; show -format png -prefix blinky"
