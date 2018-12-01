#!/bin/bash
#set -ex
#rm -f picorv32.v
#wget https://raw.githubusercontent.com/cliffordwolf/picorv32/master/picorv32.v
yosys attosoc.ys
../nextpnr-xc7 --json attosoc.json --xdl attosoc.xdl --pcf attosoc.pcf --freq 150
xdl -xdl2ncd attosoc.xdl
bitgen -w attosoc.ncd -g UnconstrainedPins:Allow
trce attosoc.ncd -v 10

netgen -sim -ofmt vhdl attosoc.ncd attosoc_pnr.vhd
ghdl -c -fexplicit --no-vital-checks --ieee=synopsys -Pxilinx-ise attosoc_tb.vhd attosoc_pnr.vhd -r testbench
