#!/bin/bash
set -ex
rm -f picorv32.v
wget https://raw.githubusercontent.com/cliffordwolf/picorv32/master/picorv32.v
yosys picorv32.ys
../nextpnr-xc7 --json picorv32.json --xdl picorv32.xdl --pcf picorv32.pcf --freq 150
xdl -xdl2ncd picorv32.xdl
#bitgen -w blinky.ncd -g UnconstrainedPins:Allow
trce picorv32.ncd -v 10
