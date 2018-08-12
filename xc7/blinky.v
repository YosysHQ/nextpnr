module blinky (
    input  clki,
    output led1,
    output led2,
    output led3,
    output led4,
    output led5
);

    BUFGCTRL clk_gb (
        .I0(clki),
        .O(clk)
    );

    localparam BITS = 5;
    localparam LOG2DELAY = 21;

    reg [BITS+LOG2DELAY-1:0] counter = 0;
    reg [BITS-1:0] outcnt;

    always @(posedge clk) begin
        counter <= counter + 1;
        outcnt <= counter >> LOG2DELAY;
    end

    assign {led1, led2, led3, led4, led5} = outcnt ^ (outcnt >> 1);
endmodule
