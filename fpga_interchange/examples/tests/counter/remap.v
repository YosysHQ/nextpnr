module INV(input I, output O);

LUT1 #(.INIT(2'b01)) _TECHMAP_REPLACE_ (.I0(I), .O(O));

endmodule

module BUF(input I, output O);

LUT1 #(.INIT(2'b10)) _TECHMAP_REPLACE_ (.I0(I), .O(O));

endmodule
