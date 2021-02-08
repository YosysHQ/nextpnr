// Modified from:
// https://github.com/tinyfpga/TinyFPGA-A-Series/tree/master/template_a2

module top (
  (* LOC="13" *)
  output pin1,
  (* LOC="21" *)
  input clk
);

  reg [23:0] led_timer;

  always @(posedge clk) begin
    led_timer <= led_timer + 1;
  end

  // left side of board
  assign pin1 = led_timer[23];
endmodule
