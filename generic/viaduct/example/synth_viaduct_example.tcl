# Usage
# tcl synth_viaduct_example.tcl {out.json}

yosys read_verilog -lib [file dirname [file normalize $argv0]]/example_prims.v
yosys hierarchy -check -top top
yosys proc
yosys flatten
yosys tribuf -logic
yosys deminout
yosys synth -run coarse
yosys memory_map
yosys opt -full
yosys iopadmap -bits -inpad INBUF O:PAD -outpad OUTBUF I:PAD
yosys techmap -map +/techmap.v
yosys opt -fast
yosys dfflegalize -cell \$_DFF_P_ 0
yosys abc -lut 4 -dress
yosys clean
yosys techmap -map [file dirname [file normalize $argv0]]/example_map.v
yosys clean
yosys hierarchy -check
yosys stat

if {$argc > 0} { yosys write_json [lindex $argv 0] }
