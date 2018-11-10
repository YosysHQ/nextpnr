#!/bin/bash
set -ex
yosys blinky.ys
../nextpnr-xc7 --json blinky.json --pcf blinky.pcf --xdl blinky.xdl --freq 250
xdl -xdl2ncd blinky.xdl
trce blinky.ncd -v 10
bitgen -w blinky.ncd -g UnconstrainedPins:Allow
#icepack blinky.asc blinky.bin
#icebox_vlog blinky.asc > blinky_chip.v
#iverilog -o blinky_tb blinky_chip.v blinky_tb.v
#vvp -N ./blinky_tb
