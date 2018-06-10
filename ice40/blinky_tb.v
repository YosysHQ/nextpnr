module blinky_tb;
    reg clk;
    always #5 clk = (clk === 1'b0);

    chip uut (
        .io_0_8_1(clk)
    );

    initial begin
        $dumpfile("blinky_tb.vcd");
        $dumpvars(0, blinky_tb);
        repeat (9000000) @(posedge clk);
        $finish;
    end
endmodule
