module top(input clk, cen, rst, ina, inb, output outa, outb, outc, outd);

wire temp0, temp1;

(* BEL="1_1_lc0" *)
SB_LUT4 #(
    .LUT_INIT(2'b01)
) lut0 (
    .I3(),
    .I2(),
    .I1(),
    .I0(ina),
    .O(temp0)
);


(* BEL="1_3_lc0" *)
SB_LUT4 #(
    .LUT_INIT(2'b01)
) lut1 (
    .I3(),
    .I2(),
    .I1(),
    .I0(inb),
    .O(temp1)
);

(* BEL="1_1_lc0" *)
SB_DFF ff0 (
    .C(clk),
    .D(temp1),
    .Q(outa)
);


(* BEL="1_1_lc7" *)
SB_DFF ff1 (
    .C(clk),
    .D(inb),
    .Q(outb)
);


(* BEL="1_6_lc7" *)
SB_DFF ff2 (
    .C(clk),
    .D(temp1),
    .Q(outc)
);


assign outd = 1'b0;

endmodule
