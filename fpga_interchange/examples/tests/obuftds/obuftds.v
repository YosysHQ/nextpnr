module top(
  input  wire [11:8] sw,

  output wire [1:0]  diff_p,
  output wire [1:0]  diff_n
);

wire [1:0] buf_i;
wire [1:0] buf_t;

OBUFTDS # (
  .IOSTANDARD("DIFF_SSTL135"),
  .SLEW("FAST")
) obuftds_0 (
  .I(buf_i[0]),
  .T(buf_t[0]),
  .O(diff_p[0]),
  .OB(diff_n[0])
);

OBUFTDS # (
  .IOSTANDARD("DIFF_SSTL135"),
  .SLEW("FAST")
) obuftds_1 (
  .I(buf_i[1]),
  .T(buf_t[1]),
  .O(diff_p[1]),
  .OB(diff_n[1])
);

assign buf_i[0] = sw[ 8];
assign buf_t[0] = sw[ 9];
assign buf_i[1] = sw[10];
assign buf_t[1] = sw[11];

endmodule

