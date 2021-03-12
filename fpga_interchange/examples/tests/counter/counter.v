module top(input clk, input rst, output [7:4] io_led);

reg [31:0] counter = 32'b0;

assign io_led = counter >> 22;

always @(posedge clk)
begin
    if(rst)
        counter <= 32'b0;
    else
        counter <= counter + 1;
end

endmodule
