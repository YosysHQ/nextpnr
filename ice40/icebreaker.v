module icebreaker (
	input  clki,
	input  btn1,
	input  btn2,
	input  btn3,
	output led1,
	output led2,
	output led3,
	output led4,
	output led5
);
    wire clk;
	SB_GB clk_gb(.USER_SIGNAL_TO_GLOBAL_BUFFER(clki), .GLOBAL_BUFFER_OUTPUT(clk));
	localparam BITS = 5;
	localparam LOG2DELAY = 22;

	reg [BITS+LOG2DELAY-1:0] counter = 0;
	reg [BITS-1:0] outcnt;

	always @(posedge clk) begin
		counter <= counter + 1;
		outcnt <= counter >> LOG2DELAY;
	end
	assign {led1, led2, led3, led4, led5} = outcnt ^ (outcnt >> 1);
	//assign {led1, led2, led3, led4, led5} = {!btn1, btn2, btn3, btn2, btn1};
endmodule
