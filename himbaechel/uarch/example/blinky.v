module top(input rst, output reg [7:0] leds);

wire clk;

(* BEL="X1Y0/IO0" *) INBUF ib_i (.O(clk));

reg [7:0] ctr;
always @(posedge clk)
	if (rst)
		ctr <= 8'h00;
	else
		ctr <= ctr + 1'b1;

assign leds = {4'b1010, ctr[7:4]};

endmodule
