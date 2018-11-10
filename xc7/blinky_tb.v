module blinky_tb;
    reg clk;
    always #5 clk = (clk === 1'b0);

    wire led0, led1, led2, led3;

    chip uut (
        .\clki$iob.PAD.PAD (clk),
        .\led0$iob.OUTBUF.OUT (led0),
        .\led1$iob.OUTBUF.OUT (led1),
        .\led2$iob.OUTBUF.OUT (led2),
        .\led3$iob.OUTBUF.OUT (led3)
    );

    initial begin
        // $dumpfile("blinky_tb.vcd");
        // $dumpvars(0, blinky_tb);
        $monitor(led0, led1, led2, led3);
        //repeat (10) begin
        //    repeat (900000) @(posedge clk);
        //    $display(led0, led1, led2, led3);
        //end
        //$finish;
    end
endmodule
