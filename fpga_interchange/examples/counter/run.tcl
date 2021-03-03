yosys -import

read_verilog counter.v

synth_xilinx -nolutram -nowidelut -nosrl -nocarry -nodsp
techmap -map ../remap.v

# opt_expr -undriven makes sure all nets are driven, if only by the $undef
# net.
opt_expr -undriven
opt_clean

setundef -zero -params

write_json build/counter.json
