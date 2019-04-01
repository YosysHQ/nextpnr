module top(input clk, output reg [7:0] leds);

reg [25:0] ctr;
always @(posedge clk)
	ctr <= ctr + 1'b1;

assign leds = ctr[25:18];

endmodule