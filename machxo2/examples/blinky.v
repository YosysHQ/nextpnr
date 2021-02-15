module top(input clk, rst, output [7:0] leds);

// TODO: Test miter circuit without reset value. SAT and SMT diverge without
// reset value (SAT succeeds, SMT fails). I haven't figured out the correct
// init set of options to make SAT fail.
// "sat -verify -prove-asserts -set-init-def -seq 1 miter" causes assertion
// failure in yosys.
reg [7:0] ctr = 8'h00;
always @(posedge clk)
	if (rst)
		ctr <= 8'h00;
	else
		ctr <= ctr + 1'b1;

assign leds = ctr;

endmodule
