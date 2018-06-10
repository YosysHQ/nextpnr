module \$_DFF_P_ (input D, C, output Q);
	ICESTORM_LC #(
		.LUT_INIT(2),
		.NEG_CLK(0),
		.CARRY_ENABLE(0),
		.DFF_ENABLE(1),
		.SET_NORESET(0),
		.ASYNC_SR(0)
	) _TECHMAP_REPLACE_ (
		.I0(D),
		.CLK(C),
		.O(Q),

		.I1(),
		.I2(),
		.I3(),
		.CIN(),
		.CEN(),
		.SR(),
		.LO(),
		.COUT()
	);
endmodule

module \$lut (A, Y);
	parameter WIDTH = 0;
	parameter LUT = 0;

	input [WIDTH-1:0] A;
	output Y;

	generate
		if (WIDTH == 1) begin
			ICESTORM_LC #(
				.LUT_INIT(LUT),
				.NEG_CLK(0),
				.CARRY_ENABLE(0),
				.DFF_ENABLE(0),
				.SET_NORESET(0),
				.ASYNC_SR(0)
			) _TECHMAP_REPLACE_ (
				.I0(A[0]), .I1(), .I2(), .I3(), .O(Y),
				.CLK(), .CIN(), .CEN(), .SR(), .LO(), .COUT()
			);
		end
		if (WIDTH == 2) begin
			ICESTORM_LC #(
				.LUT_INIT(LUT),
				.NEG_CLK(0),
				.CARRY_ENABLE(0),
				.DFF_ENABLE(0),
				.SET_NORESET(0),
				.ASYNC_SR(0)
			) _TECHMAP_REPLACE_ (
				.I0(A[0]), .I1(A[1]), .I2(), .I3(), .O(Y),
				.CLK(), .CIN(), .CEN(), .SR(), .LO(), .COUT()
			);
		end
		if (WIDTH == 3) begin
			ICESTORM_LC #(
				.LUT_INIT(LUT),
				.NEG_CLK(0),
				.CARRY_ENABLE(0),
				.DFF_ENABLE(0),
				.SET_NORESET(0),
				.ASYNC_SR(0)
			) _TECHMAP_REPLACE_ (
				.I0(A[0]), .I1(A[1]), .I2(A[2]), .I3(), .O(Y),
				.CLK(), .CIN(), .CEN(), .SR(), .LO(), .COUT()
			);
		end
		if (WIDTH == 4) begin
			ICESTORM_LC #(
				.LUT_INIT(LUT),
				.NEG_CLK(0),
				.CARRY_ENABLE(0),
				.DFF_ENABLE(0),
				.SET_NORESET(0),
				.ASYNC_SR(0)
			) _TECHMAP_REPLACE_ (
				.I0(A[0]), .I1(A[1]), .I2(A[2]), .I3(A[3]), .O(Y),
				.CLK(), .CIN(), .CEN(), .SR(), .LO(), .COUT()
			);
		end
	endgenerate
endmodule
