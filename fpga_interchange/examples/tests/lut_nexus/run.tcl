yosys -import

read_verilog $::env(SOURCES)

synth_nexus -noccu2 -nobram -nolutram -nowidelut

# opt_expr -undriven makes sure all nets are driven, if only by the $undef
# net.
opt_expr -undriven
opt_clean

setundef -zero -params

write_json $::env(OUT_JSON)
