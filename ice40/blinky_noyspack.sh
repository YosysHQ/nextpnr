#!/bin/bash
set -ex
yosys blinky_nopack.ys
../nextpnr-ice40 --json blinky_nopack.json --asc blinky.asc --pack
icepack blinky.asc blinky.bin
icebox_vlog blinky.asc > blinky_chip.v
iverilog -o blinky_tb blinky_chip.v blinky_tb.v
vvp -N ./blinky_tb
