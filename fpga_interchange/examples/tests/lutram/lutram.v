module top (
    input  wire clk,

    input  wire rx,
    output wire tx,

    input  wire [15:0] sw,
    output wire [15:0] led
);
    RAM128X1D #(
        .INIT(128'hFFEEDDCCBBAA99887766554433221100)
    ) ram_i (
        .WCLK(clk),
        .A(sw[6:0]),
        .DPRA(sw[13:7]),
        .WE(sw[14]),
        .D(sw[15]),
        .SPO(led[0]),
        .DPO(led[1]),
    );

    assign led[15:2] = 14'b0;
    assign tx = rx;
endmodule
