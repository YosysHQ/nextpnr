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
  localparam [0:0] IS_CYINIT_FABRIC = _TECHMAP_CONSTMSK_CYINIT_ == 0;

  if(IS_CYINIT_FABRIC) begin
    CARRY4 _TECHMAP_REPLACE_ (
        .CO(CO),
        .CYINIT(CYINIT),
        .O(O),
        .DI(DI),
        .S(S),
    );
  end else if(IS_CI_ZERO || IS_CI_ONE) begin
    CARRY4 _TECHMAP_REPLACE_ (
        .CO(CO),
        .CYINIT(CYINIT),
        .O(O),
        .DI(DI),
        .S(S),
    );
  end else begin
    CARRY4 _TECHMAP_REPLACE_ (
        .CO(CO),
        .CYINIT(CYINIT),
        .O(O),
        .DI(DI),
        .S(S),
        .CI(CI)
    );
  end
endmodule
