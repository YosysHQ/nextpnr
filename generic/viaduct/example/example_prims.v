module LUT4 #(
    parameter [15:0] INIT = 0
) (
    input [3:0] I,
    output F
);
    wire [7:0] s3 = I[3] ?     INIT[15:8] :     INIT[7:0];
    wire [3:0] s2 = I[2] ?       s3[ 7:4] :       s3[3:0];
    wire [1:0] s1 = I[1] ?       s2[ 3:2] :       s2[1:0];
    assign F = I[0] ? s1[1] : s1[0];
endmodule

module DFF (
    input CLK, D,
    output reg Q
);
    initial Q = 1'b0;
    always @(posedge CLK)
        Q <= D;
endmodule

module INBUF (
    input PAD,
    output O,
);
    assign O = PAD;
endmodule

module OUTBUF (
    output PAD,
    input I,
);
    assign PAD = I;
endmodule

