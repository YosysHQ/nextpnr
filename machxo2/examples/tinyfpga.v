// Modified from:
// https://github.com/tinyfpga/TinyFPGA-A-Series/tree/master/template_a2
// https://tinyfpga.com/a-series-guide.html used as a basis.

module top (
  (* LOC="13" *)
  inout pin1
);


  wire clk;

  OSCH #(
    .NOM_FREQ("16.63")
  ) internal_oscillator_inst (
    .STDBY(1'b0),
    .OSC(clk)
  );

  reg [23:0] led_timer;

  always @(posedge clk) begin
    led_timer <= led_timer + 1;
  end

  // left side of board
  assign pin1 = led_timer[23];
endmodule
