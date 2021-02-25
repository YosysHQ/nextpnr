yosys -import

read_verilog wire.v

synth_xilinx -nolutram -nowidelut -nosrl -nocarry -nodsp

# opt_expr -undriven makes sure all nets are driven, if only by the $undef
# net.
opt_expr -undriven
opt_clean

setundef -zero -params

write_json build/wire.json
