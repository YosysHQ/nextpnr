#!/bin/bash
set -ex
yosys blinky_sim.ys
../nextpnr-xc7 --json blinky.json --pcf blinky.pcf --xdl blinky.xdl --freq 125
xdl -xdl2ncd blinky.xdl
trce blinky.ncd -v 10
netgen -sim -ofmt vhdl blinky.ncd -w blinky_pnr.vhd
ghdl -c -fexplicit --no-vital-checks --ieee=synopsys -Pxilinx-ise blinky_tb.vhd blinky_pnr.vhd -r testbench
