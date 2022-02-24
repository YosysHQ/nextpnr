#!/usr/bin/env bash
set -ex
# Run synthesis
yosys -p "tcl synth_okami.tcl blinky.json" ../../examples/blinky.v
# Run PnR
${NEXTPNR:-../../../build/nextpnr-generic} --uarch okami --json blinky.json --router router2
