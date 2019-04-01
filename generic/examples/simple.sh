#!/usr/bin/bash
set -ex
yosys -p "tcl ../synth/synth_generic.tcl 4 blinky.json" blinky.v
../../nextpnr-generic --pre-pack simple.py --json blinky.json --post-route report.py