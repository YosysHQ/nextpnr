module top;
	wire x, y;
	(*keep*)
	LUT4 lut_0(.A(x), .B(x), .C(x), .D(x), .Z(y));
	(*keep*)
	LUT4 lut_1(.A(y), .B(y), .C(y), .D(y), .Z(x));
endmodule