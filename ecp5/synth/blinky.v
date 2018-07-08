module top(input clk_pin, output [3:0] led_pin);

	wire clk;
	wire [3:0] led;

	TRELLIS_IO #(.DIR("INPUT")) clk_buf (.B(clk_pin), .O(clk));
	TRELLIS_IO #(.DIR("OUTPUT")) led_buf [3:0] (.B(led_pin), .I(led));

	reg [25:0] ctr = 0;

	always@(posedge clk)
		ctr <= ctr + 1'b1;

	assign led = ctr[25:22];

endmodule
