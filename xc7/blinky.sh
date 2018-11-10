#!/bin/bash
set -ex
yosys blinky.ys
../nextpnr-xc7 --json blinky.json --pcf blinky.pcf --xdl blinky.xdl --freq 250
xdl -xdl2ncd blinky.xdl
bitgen -w blinky.ncd -g UnconstrainedPins:Allow
trce blinky.ncd -v 10
netgen -ofmt verilog -w blinky.ncd blinky_chip.v -tm blinky
iverilog -o blinky_tb blinky_chip.v blinky_tb.v -y/opt/Xilinx/14.7/ISE_DS/ISE/verilog/src/simprims/ -insert_glbl true
vvp -N ./blinky_tb
