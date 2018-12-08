module testbench();
    integer out;
	reg clk;

	always #5 clk = (clk === 1'b0);

	initial begin
        out = $fopen("output.txt","w");
		$dumpfile("testbench.vcd");
		$dumpvars(0, testbench);

		repeat (100) begin
			repeat (256) @(posedge clk);
			$display("+256 cycles");
		end
        $fclose(out);
        #100;
		$finish;
	end

	wire [7:0] led;

	always @(led) begin
		#1 $display("%b", led);
        $fwrite(out, "%b\n", led);
	end

	attosoc uut (
		.clk      (clk      ),
		.led      (led      )
	);
endmodule
