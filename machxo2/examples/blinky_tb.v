`timescale 1ns / 1ps
module blinky_tb;

reg clk = 1'b0, rst = 1'b0;
reg [7:0] ctr_gold = 8'h00;
wire [7:0] ctr_gate;
top dut_i(.clk(clk), .rst(rst), .leds(ctr_gate));

task oneclk;
	begin
		clk = 1'b1;
		#10;
		clk = 1'b0;
		#10;
	end
endtask

initial begin
	$dumpfile("blinky_simtest.vcd");
	$dumpvars(0, blinky_tb);
	#100;
	rst = 1'b1;
	repeat (5) oneclk;
	#5
	rst = 1'b0;
	#5
	repeat (500) begin
		if (ctr_gold !== ctr_gate) begin
			$display("mismatch gold=%b gate=%b", ctr_gold, ctr_gate);
			$stop;
		end
		oneclk;
		ctr_gold = ctr_gold + 1'b1;
	end
	$finish;
end

endmodule
