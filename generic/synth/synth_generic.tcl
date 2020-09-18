# Usage
# tcl synth_generic.tcl {K} {out.json}

set LUT_K 4
if {$argc > 0} { set LUT_K [lindex $argv 0] }
yosys read_verilog -lib [file dirname [file normalize $argv0]]/prims.v
yosys hierarchy -check
yosys proc
yosys flatten
yosys tribuf -logic
yosys deminout
yosys synth -run coarse
yosys memory_map
yosys opt -full
yosys techmap -map +/techmap.v
yosys opt -fast
yosys dfflegalize -cell \$_DFF_P_ 0
yosys abc -lut $LUT_K -dress
yosys clean
yosys techmap -D LUT_K=$LUT_K -map [file dirname [file normalize $argv0]]/cells_map.v
yosys clean
yosys hierarchy -check
yosys stat

if {$argc > 1} { yosys write_json [lindex $argv 1] }
