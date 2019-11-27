module top(input clk, rst, output reg [7:0] leds);

reg [7:0] ctr;
always @(posedge clk)
	if (rst)
		ctr <= 8'h00;
	else
		ctr <= ctr + 1'b1;

assign leds = ctr;

endmodule
