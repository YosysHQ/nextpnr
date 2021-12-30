#!/usr/bin/env bash
set -ex
# Run synthesis
yosys -p "tcl synth_viaduct_example.tcl blinky.json" ../../examples/blinky.v
# Run PnR
${NEXTPNR:-../../../build/nextpnr-generic} --uarch example --json blinky.json
