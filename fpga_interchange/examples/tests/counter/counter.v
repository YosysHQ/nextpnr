module top(input clk, input rst, output [7:4] io_led);

localparam SIZE = 32;

reg [SIZE-1:0] counter = SIZE'b0;

assign io_led = {counter[SIZE-1], counter[25:23]};

always @(posedge clk)
begin
    if(rst)
        counter <= SIZE'b0;
    else
        counter <= counter + 1;
end

endmodule
