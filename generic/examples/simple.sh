#!/usr/bin/bash
set -ex
yosys -p "tcl ../synth/synth_generic.tcl 4 blinky.json" blinky.v
../../nextpnr-generic --pre-pack simple.py --pre-place simple_timing.py --json blinky.json --post-route bitstream.py