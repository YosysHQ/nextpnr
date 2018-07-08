module \$_DFF_P_ (input D, C, output Q);
	TRELLIS_SLICE #(
		.MODE("LOGIC"),
		.CLKMUX("CLK"),
		.CEMUX("1"),
		.REG0_SD("0"),
		.REG0_REGSET("RESET"),
		.SRMODE("LSR_OVER_CE"),
		.GSR("DISABLED")
	) _TECHMAP_REPLACE_ (
		.CLK(C),
		.M0(D),
		.Q0(Q)
	);
endmodule

module \$lut (A, Y);
	parameter WIDTH = 0;
	parameter LUT = 0;

	input [WIDTH-1:0] A;
	output Y;

	generate
		if (WIDTH == 1) begin
			TRELLIS_SLICE #(
				.MODE("LOGIC"),
				.LUT0_INITVAL(LUT)
			) _TECHMAP_REPLACE_ (
				.A0(A[0]),
				.F0(Y)
			);
		end
		if (WIDTH == 2) begin
			TRELLIS_SLICE #(
				.MODE("LOGIC"),
				.LUT0_INITVAL(LUT)
			) _TECHMAP_REPLACE_ (
				.A0(A[0]),
				.B0(A[1]),
				.F0(Y)
			);
		end
		if (WIDTH == 3) begin
			TRELLIS_SLICE #(
				.MODE("LOGIC"),
				.LUT0_INITVAL(LUT)
			) _TECHMAP_REPLACE_ (
				.A0(A[0]),
				.B0(A[1]),
				.C0(A[2]),
				.F0(Y)
			);
		end
		if (WIDTH == 4) begin
			TRELLIS_SLICE #(
				.MODE("LOGIC"),
				.LUT0_INITVAL(LUT)
			) _TECHMAP_REPLACE_ (
				.A0(A[0]),
				.B0(A[1]),
				.C0(A[2]),
				.D0(A[3]),
				.F0(Y)
			);
		end
	endgenerate
endmodule
