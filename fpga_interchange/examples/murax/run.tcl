yosys -import

read_verilog Murax.v
read_verilog basys3_toplevel.v

synth_xilinx -flatten -nolutram -nowidelut -nosrl -nocarry -nodsp

# opt_expr -undriven makes sure all nets are driven, if only by the $undef
# net.
opt_expr -undriven
opt_clean

setundef -zero -params

write_json build/murax.json
