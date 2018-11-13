#!/bin/bash
set -ex
yosys blinky.ys
../nextpnr-xc7 --json blinky.json --pcf blinky.pcf --xdl blinky.xdl --freq 150
xdl -xdl2ncd blinky.xdl
bitgen -w blinky.ncd -g UnconstrainedPins:Allow
trce blinky.ncd -v 10

#netgen -ofmt verilog -w blinky.ncd blinky_chip.v -tm blinky -insert_glbl true
#iverilog -o blinky_tb blinky_chip.v blinky_tb.v -y/opt/Xilinx/14.7/ISE_DS/ISE/verilog/src/simprims/
#vvp -N ./blinky_tb

#xdl -xdl2ncd blinky.xdl -nopips blinky_map.ncd
#par -w blinky_map.ncd blinky_par.ncd blinky.pcf
#bitgen -w blinky_par.ncd -g UnconstrainedPins:Allow
