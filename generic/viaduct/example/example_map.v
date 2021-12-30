module \$lut (A, Y);
	parameter WIDTH = 0;
	parameter LUT = 0;
	input [WIDTH-1:0] A;
	output Y;

	localparam rep = 1<<(4-WIDTH);

	LUT4 #(.INIT({rep{LUT}})) _TECHMAP_REPLACE_ (.I(A), .F(Y));
endmodule

module  \$_DFF_P_ (input D, C, output Q); DFF  _TECHMAP_REPLACE_ (.D(D), .Q(Q), .CLK(C)); endmodule
