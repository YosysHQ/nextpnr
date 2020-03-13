// LUT and DFF are combined to a GENERIC_SLICE

module LUT #(
	parameter K = 4,
	parameter [2**K-1:0] INIT = 0
) (
	input [K-1:0] I,
	output Q
);
	wire [K-1:0] I_pd;

	genvar ii;
	generate
		for (ii = 0; ii < K; ii = ii + 1'b1)
			assign I_pd[ii] = (I[ii] === 1'bz) ? 1'b0 : I[ii];
	endgenerate

	assign Q = INIT[I_pd];
endmodule

module DFF (
	input CLK, D,
	output reg Q
);
	initial Q = 1'b0;
	always @(posedge CLK)
		Q <= D;
endmodule

module GENERIC_SLICE #(
	parameter K = 4,
	parameter [2**K-1:0] INIT = 0,
	parameter FF_USED = 1'b0
) (
	input CLK,
	input [K-1:0] I,
	output F,
	output Q
);
	wire f_wire;
	
	LUT #(.K(K), .INIT(INIT)) lut_i(.I(I), .Q(f_wire));

	DFF dff_i(.CLK(CLK), .D(f_wire), .Q(Q));

	assign F = f_wire;
endmodule

module GENERIC_IOB #(
	parameter INPUT_USED = 1'b0,
	parameter OUTPUT_USED = 1'b0,
	parameter ENABLE_USED = 1'b0
) (
	inout PAD,
	input I, EN,
	output O
);
	generate if (OUTPUT_USED && ENABLE_USED)
		assign PAD = EN ? I : 1'bz;
	else if (OUTPUT_USED)
		assign PAD = I;
	endgenerate

	generate if (INPUT_USED)
		assign O = PAD;
	endgenerate
endmodule
