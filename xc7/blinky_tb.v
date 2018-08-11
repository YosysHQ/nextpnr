module blinky_tb;
    reg clk;
    always #5 clk = (clk === 1'b0);

    wire led1, led2, led3, led4, led5;

    chip uut (
        .io_0_8_1(clk),
        .io_13_12_1(led1),
        .io_13_12_0(led2),
        .io_13_11_1(led3),
        .io_13_11_0(led4),
        .io_13_9_1(led5)
    );

    initial begin
        // $dumpfile("blinky_tb.vcd");
        // $dumpvars(0, blinky_tb);
        repeat (10) begin
            repeat (900000) @(posedge clk);
            $display(led1, led2, led3, led4, led5);
        end
        $finish;
    end
endmodule
