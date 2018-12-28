module blinky (
    input  clki,
    output led0,
    output led1,
    output led2,
    output led3
);
    BUFGCTRL clk_gb (
        .I0(clki),
        .CE0(1'b1),
        .CE1(1'b0),
        .S0(1'b1),
        .S1(1'b0),
        .IGNORE0(1'b0),
        .IGNORE1(1'b0),
        .O(clk)
    );

    localparam BITS = 4;
    parameter LOG2DELAY = 23;

    reg [BITS+LOG2DELAY-1:0] counter = 0;
    reg [BITS-1:0] outcnt;

    always @(posedge clk) begin
        counter <= counter + 1;
        outcnt <= counter >> LOG2DELAY;
    end

    assign {led0, led1, led2, led3} = outcnt ^ (outcnt >> 1);
endmodule
