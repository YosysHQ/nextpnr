module icebreaker (
	input  clk_pin,
	input  btn1_pin,
	input  btn2_pin,
	input  btn3_pin,
	output led1_pin,
	output led2_pin,
	output led3_pin,
	output led4_pin,
	output led5_pin
);
	wire clk, clk_pre, led1, led2, led3, led4, led5, btn1, btn2, btn3;

	(* BEL="18_31_io1" *) //27
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

	(* BEL="19_31_io1" *) //25
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

	(* BEL="18_0_io1" *) //21
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

	(* BEL="19_31_io0" *) //23
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

	(* BEL="18_31_io0" *) //26
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

	(* BEL="12_31_io1" *) //35
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
		.D_IN_0(clk_pre),
		.D_IN_1()
	);

	(* BEL="19_0_io1" *) //20
	SB_IO #(
		.PIN_TYPE(6'b 0000_01),
		.PULLUP(1'b0),
		.NEG_TRIGGER(1'b0)
	) btn1_iob (
		.PACKAGE_PIN(btn1_pin),
		.LATCH_INPUT_VALUE(),
		.CLOCK_ENABLE(),
		.INPUT_CLK(),
		.OUTPUT_CLK(),
		.OUTPUT_ENABLE(),
		.D_OUT_0(),
		.D_OUT_1(),
		.D_IN_0(btn1),
		.D_IN_1()
	);

    (* BEL="21_0_io1" *) //19
    SB_IO #(
        .PIN_TYPE(6'b 0000_01),
        .PULLUP(1'b0),
        .NEG_TRIGGER(1'b0)
    ) btn2_iob (
        .PACKAGE_PIN(btn2_pin),
        .LATCH_INPUT_VALUE(),
        .CLOCK_ENABLE(),
        .INPUT_CLK(),
        .OUTPUT_CLK(),
        .OUTPUT_ENABLE(),
        .D_OUT_0(),
        .D_OUT_1(),
        .D_IN_0(btn2),
        .D_IN_1()
    );

    (* BEL="22_0_io1" *) //18
    SB_IO #(
        .PIN_TYPE(6'b 0000_01),
        .PULLUP(1'b0),
        .NEG_TRIGGER(1'b0)
    ) btn3_iob (
        .PACKAGE_PIN(btn3_pin),
        .LATCH_INPUT_VALUE(),
        .CLOCK_ENABLE(),
        .INPUT_CLK(),
        .OUTPUT_CLK(),
        .OUTPUT_ENABLE(),
        .D_OUT_0(),
        .D_OUT_1(),
        .D_IN_0(btn3),
        .D_IN_1()
    );

	SB_GB clk_gb(.USER_SIGNAL_TO_GLOBAL_BUFFER(clk_pre), .GLOBAL_BUFFER_OUTPUT(clk));
	localparam BITS = 5;
	localparam LOG2DELAY = 22;

	reg [BITS+LOG2DELAY-1:0] counter = 0;
	reg [BITS-1:0] outcnt;

	always @(posedge clk) begin
		counter <= counter + 1;
		outcnt <= counter >> LOG2DELAY;
	end
	assign {led1, led2, led3, led4, led5} = outcnt ^ (outcnt >> 1);
	//assign {led1, led2, led3, led4, led5} = {!btn1, btn2, btn3, btn2, btn1};
endmodule
