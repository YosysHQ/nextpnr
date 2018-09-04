#!/bin/bash
set -ex
rm -f picorv32.v
wget https://raw.githubusercontent.com/cliffordwolf/picorv32/master/picorv32.v
yosys picorv32.ys
../nextpnr-xc7 --json picorv32.json --xdl picorv32.xdl
#../nextpnr-ice40 --hx8k --asc picorv32.asc --json picorv32.json
#icetime -d hx8k -t picorv32.asc
