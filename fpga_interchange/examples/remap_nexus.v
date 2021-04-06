// Inverter support is still a TODO
module INV(input A, output Z);
	LUT4 #(.INIT("0x5555")) _TECHMAP_REPLACE_ (.A(A), .B(1'b1), .C(1'b1), .D(1'b1), .Z(Z));
endmodule
