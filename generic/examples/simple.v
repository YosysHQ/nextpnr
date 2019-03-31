(* blackbox *)
module SLICE_LUT4(
	input I0, I1, I2, I3,
	input CLK,
	output Q
);
parameter INIT = 16'h0000;
parameter FF_USED = 1'b0;
endmodule

module top(input a, output q);

SLICE_LUT4 sl_i(.I0(a), .Q(q));

endmodule