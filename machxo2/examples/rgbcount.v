// Modified from:
// https://github.com/tinyfpga/TinyFPGA-A-Series/tree/master/template_a2
// https://tinyfpga.com/a-series-guide.html used as a basis.

module top (
  (* LOC="21" *)
  inout pin6,
  (* LOC="26" *)
  inout pin9_jtgnb,
  (* LOC="27" *)
  inout pin10_sda,
);
  wire clk;

  OSCH #(
    .NOM_FREQ("2.08")
  ) internal_oscillator_inst (
    .STDBY(1'b0),
    .OSC(clk)
  );

  reg [23:0] led_timer;

  always @(posedge clk) begin
    led_timer <= led_timer + 1;
  end

  // left side of board
  assign pin9_jtgnb = led_timer[23];
  assign pin10_sda = led_timer[22];
  assign pin6 = led_timer[21];

endmodule
