module INV(input I, output O);

LUT1 #(.INIT(2'b01)) _TECHMAP_REPLACE_ (.I0(I), .O(O));

endmodule

module BUF(input I, output O);

LUT1 #(.INIT(2'b10)) _TECHMAP_REPLACE_ (.I0(I), .O(O));

endmodule

module CARRY4(
  output [3:0] CO,
  output [3:0] O,
  input        CI,
  input        CYINIT,
  input  [3:0] DI, S
);
  parameter _TECHMAP_CONSTMSK_CI_ = 1;
  parameter _TECHMAP_CONSTVAL_CI_ = 1'b0;
  parameter _TECHMAP_CONSTMSK_CYINIT_ = 1;
  parameter _TECHMAP_CONSTVAL_CYINIT_ = 1'b0;

  localparam [0:0] IS_CI_ZERO = (
      _TECHMAP_CONSTMSK_CI_ == 1 && _TECHMAP_CONSTVAL_CI_ == 0 &&
      _TECHMAP_CONSTMSK_CYINIT_ == 1 && _TECHMAP_CONSTVAL_CYINIT_ == 0);
  localparam [0:0] IS_CI_ONE = (
      _TECHMAP_CONSTMSK_CI_ == 1 && _TECHMAP_CONSTVAL_CI_ == 0 &&
      _TECHMAP_CONSTMSK_CYINIT_ == 1 && _TECHMAP_CONSTVAL_CYINIT_ == 1);

  /* Workaround for LUT-thrus, add LUTs explicitly to reduce site_routing
   * time consumption
   */
  wire S0, S1, S2, S3;
  LUT1 #(.INIT(2'b10)) LUT_S0 (.I0(S[0]), .O(S0));
  LUT1 #(.INIT(2'b10)) LUT_S1 (.I0(S[1]), .O(S1));
  LUT1 #(.INIT(2'b10)) LUT_S2 (.I0(S[2]), .O(S2));
  LUT1 #(.INIT(2'b10)) LUT_S3 (.I0(S[3]), .O(S3));


  if(IS_CI_ZERO || IS_CI_ONE) begin
    CARRY4 _TECHMAP_REPLACE_ (
        .CYINIT(CYINIT),
        .CO(CO),
        .O(O),
        .DI(DI),
        .S({S0, S1, S2, S3}),
    );
  end else begin
    CARRY4 _TECHMAP_REPLACE_ (
        .CI(CI),
        .CYINIT(CYINIT),
        .CO(CO),
        .O(O),
        .DI(DI),
        .S({S0, S1, S2, S3}),
    );
  end
endmodule
