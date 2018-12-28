#!/bin/bash
set -ex
yosys blinky.ys
../nextpnr-xc7 --json blinky.json --pcf blinky.pcf --xdl blinky.xdl --freq 125
xdl -xdl2ncd blinky.xdl
bitgen -w blinky.ncd -g UnconstrainedPins:Allow
