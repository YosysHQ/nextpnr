module top(input clk, cen, rst, ina, inb, output outa, outb, outc, outd);

    reg [3:0] ctr = 0;

    always @(posedge clk)
        ctr <= ctr + 1'b1;

    assign {outa, outb, outc, outd} = ctr;
endmodule
