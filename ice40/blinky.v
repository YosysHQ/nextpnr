module blinky (
    input  clk_pin,
    output led1_pin,
    output led2_pin,
    output led3_pin,
    output led4_pin,
    output led5_pin
);
    wire clk, clki;

    SB_GB clk_gb (
        .USER_SIGNAL_TO_GLOBAL_BUFFER(clki),
        .GLOBAL_BUFFER_OUTPUT(clk)
    );

    wire led1, led2, led3, led4, led5;

    (* BEL="13_12_io1" *)
    SB_IO #(
        .PIN_TYPE(6'b 0110_01),
        .PULLUP(1'b0),
        .NEG_TRIGGER(1'b0)
    ) led1_iob (
        .PACKAGE_PIN(led1_pin),
        .LATCH_INPUT_VALUE(),
        .CLOCK_ENABLE(),
        .INPUT_CLK(),
        .OUTPUT_CLK(),
        .OUTPUT_ENABLE(),
        .D_OUT_0(led1),
        .D_OUT_1(),
        .D_IN_0(),
        .D_IN_1()
    );

    (* BEL="13_12_io0" *)
    SB_IO #(
        .PIN_TYPE(6'b 0110_01),
        .PULLUP(1'b0),
        .NEG_TRIGGER(1'b0)
    ) led2_iob (
        .PACKAGE_PIN(led2_pin),
        .LATCH_INPUT_VALUE(),
        .CLOCK_ENABLE(),
        .INPUT_CLK(),
        .OUTPUT_CLK(),
        .OUTPUT_ENABLE(),
        .D_OUT_0(led2),
        .D_OUT_1(),
        .D_IN_0(),
        .D_IN_1()
    );

    (* BEL="13_11_io1" *)
    SB_IO #(
        .PIN_TYPE(6'b 0110_01),
        .PULLUP(1'b0),
        .NEG_TRIGGER(1'b0)
    ) led3_iob (
        .PACKAGE_PIN(led3_pin),
        .LATCH_INPUT_VALUE(),
        .CLOCK_ENABLE(),
        .INPUT_CLK(),
        .OUTPUT_CLK(),
        .OUTPUT_ENABLE(),
        .D_OUT_0(led3),
        .D_OUT_1(),
        .D_IN_0(),
        .D_IN_1()
    );

    (* BEL="13_11_io0" *)
    SB_IO #(
        .PIN_TYPE(6'b 0110_01),
        .PULLUP(1'b0),
        .NEG_TRIGGER(1'b0)
    ) led4_iob (
        .PACKAGE_PIN(led4_pin),
        .LATCH_INPUT_VALUE(),
        .CLOCK_ENABLE(),
        .INPUT_CLK(),
        .OUTPUT_CLK(),
        .OUTPUT_ENABLE(),
        .D_OUT_0(led4),
        .D_OUT_1(),
        .D_IN_0(),
        .D_IN_1()
    );

    (* BEL="13_9_io1" *)
    SB_IO #(
        .PIN_TYPE(6'b 0110_01),
        .PULLUP(1'b0),
        .NEG_TRIGGER(1'b0)
    ) led5_iob (
        .PACKAGE_PIN(led5_pin),
        .LATCH_INPUT_VALUE(),
        .CLOCK_ENABLE(),
        .INPUT_CLK(),
        .OUTPUT_CLK(),
        .OUTPUT_ENABLE(),
        .D_OUT_0(led5),
        .D_OUT_1(),
        .D_IN_0(),
        .D_IN_1()
    );

    (* BEL="0_8_io1" *)
    SB_IO #(
        .PIN_TYPE(6'b 0000_01),
        .PULLUP(1'b0),
        .NEG_TRIGGER(1'b0)
    ) clk_iob (
        .PACKAGE_PIN(clk_pin),
        .LATCH_INPUT_VALUE(),
        .CLOCK_ENABLE(),
        .INPUT_CLK(),
        .OUTPUT_CLK(),
        .OUTPUT_ENABLE(),
        .D_OUT_0(),
        .D_OUT_1(),
        .D_IN_0(clki),
        .D_IN_1()
    );

    localparam BITS = 5;
    localparam LOG2DELAY = 22;

    reg [BITS+LOG2DELAY-1:0] counter = 0;
    reg [BITS-1:0] outcnt;

    always @(posedge clk) begin
        counter <= counter + 1;
        outcnt <= counter >> LOG2DELAY;
    end

    assign {led1, led2, led3, led4, led5} = outcnt ^ (outcnt >> 1);
endmodule
