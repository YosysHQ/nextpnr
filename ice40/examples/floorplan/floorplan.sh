#!/usr/bin/env bash
set -ex
yosys -p "synth_ice40 -top top -json floorplan.json" floorplan.v
../../../nextpnr-ice40 --up5k --json floorplan.json --pcf icebreaker.pcf --asc floorplan.asc --ignore-loops --pre-place floorplan.py
icepack floorplan.asc floorplan.bin
iceprog floorplan.bin
