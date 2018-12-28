#!/bin/bash
set -ex
rm -f picorv32.v attosoc.v
wget https://raw.githubusercontent.com/cliffordwolf/picorv32/master/picorv32.v
wget https://raw.githubusercontent.com/SymbiFlow/prjtrellis/master/examples/picorv32_versa5g/attosoc.v
ln -sf firmware_slow.hex firmware.hex
yosys attosoc.ys
set +e
../nextpnr-xc7 --json attosoc.json --xdl attosoc.xdl --pcf attosoc.pcf --freq 125
set -e
xdl -xdl2ncd attosoc.xdl
bitgen -w attosoc.ncd -g UnconstrainedPins:Allow
trce attosoc.ncd -v 10
