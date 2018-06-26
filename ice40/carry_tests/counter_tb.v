module counter_tb;
    reg clk;
    always #5 clk = (clk === 1'b0);

    wire outa, outb, outc, outd;

    chip uut (
        .clk(clk),
        .cen(1'b1),
        .rst(1'b0),
        .outa(outa),
        .outb(outb),
        .outc(outc),
	.outd(outd)
    );

    initial begin
        $dumpfile("counter_tb.vcd");
        $dumpvars(0, counter_tb);
        repeat (100000) @(posedge clk);
        $finish;
    end
endmodule
