#!/bin/bash
set -ex
yosys blinky.ys
../nextpnr-ice40 --json blinky.json --asc blinky.asc
icebox_vlog blinky.asc > blinky_chip.v
iverilog -o blinky_tb blinky_chip.v blinky_tb.v
./blinky_tb
