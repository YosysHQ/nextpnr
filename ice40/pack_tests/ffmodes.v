module top(input clk, cen, rst, ina, inb, output reg outa, outb, outc, outd);

reg temp0 = 1'b0, temp1 = 1'b0;
initial outa = 1'b0;
initial outb = 1'b0;
initial outc = 1'b0;
initial outd = 1'b0;

always @(posedge clk)
    if (cen)
        if(rst)
            temp0 <= 1'b0;
        else
            temp0 <= ina;

always @(negedge clk)
    if (ina)
        if(rst)
            temp1 <= 1'b1;
        else
            temp1 <= inb;


always @(posedge clk or posedge rst)
    if(rst)
        outa <= 1'b0;
    else
        outa <= temp0;

always @(posedge clk)
    outb <= temp1;

always @(negedge clk)
    outc <= temp0;

always @(negedge clk or posedge rst)
    if (rst)
        outd <= 1'b1;
    else
        outd <= temp1;


endmodule
