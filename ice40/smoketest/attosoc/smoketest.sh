#!/usr/bin/env bash
set -ex
yosys -q -p 'synth_ice40 -json attosoc.json -top attosoc' attosoc.v picorv32.v
$NEXTPNR --hx8k --json attosoc.json --pcf attosoc.pcf --asc attosoc.asc --freq 50
icetime -tmd hx8k -c 50 attosoc.asc
icebox_vlog -L -l -p attosoc.pcf -c -n attosoc attosoc.asc > attosoc_pnr.v
iverilog -o attosoc_pnr_tb attosoc_pnr.v attosoc_tb.v `yosys-config --datdir/ice40/cells_sim.v`
vvp attosoc_pnr_tb
diff output.txt golden.txt
