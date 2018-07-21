#!/bin/bash
set -ex
rm -f picorv32.v
wget https://raw.githubusercontent.com/cliffordwolf/picorv32/master/picorv32.v
yosys -p 'synth_ice40 -json picorv32.json -top top' picorv32.v picorv32_top.v
../nextpnr-ice40 --hx8k --asc picorv32.asc --json picorv32.json
icetime -d hx8k -t picorv32.asc
