module io_wrapper(input clk_pin, cen_pin, rst_pin, ina_pin, inb_pin,
                  output outa_pin, outb_pin, outc_pin, outd_pin);

    wire clk, cen, rst, ina, inb, outa, outb, outc, outd;

    (* BEL="0_14_io1" *)
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
        .D_IN_0(clk),
        .D_IN_1()
    );

    (* BEL="0_14_io0" *)
    SB_IO #(
        .PIN_TYPE(6'b 0000_01),
        .PULLUP(1'b0),
        .NEG_TRIGGER(1'b0)
    ) cen_iob (
        .PACKAGE_PIN(cen_pin),
        .LATCH_INPUT_VALUE(),
        .CLOCK_ENABLE(),
        .INPUT_CLK(),
        .OUTPUT_CLK(),
        .OUTPUT_ENABLE(),
        .D_OUT_0(),
        .D_OUT_1(),
        .D_IN_0(cen),
        .D_IN_1()
    );

    (* BEL="0_13_io1" *)
    SB_IO #(
        .PIN_TYPE(6'b 0000_01),
        .PULLUP(1'b0),
        .NEG_TRIGGER(1'b0)
    ) rst_iob (
        .PACKAGE_PIN(rst_pin),
        .LATCH_INPUT_VALUE(),
        .CLOCK_ENABLE(),
        .INPUT_CLK(),
        .OUTPUT_CLK(),
        .OUTPUT_ENABLE(),
        .D_OUT_0(),
        .D_OUT_1(),
        .D_IN_0(rst),
        .D_IN_1()
    );

    (* BEL="0_13_io0" *)
    SB_IO #(
        .PIN_TYPE(6'b 0000_01),
        .PULLUP(1'b0),
        .NEG_TRIGGER(1'b0)
    ) ina_iob (
        .PACKAGE_PIN(ina_pin),
        .LATCH_INPUT_VALUE(),
        .CLOCK_ENABLE(),
        .INPUT_CLK(),
        .OUTPUT_CLK(),
        .OUTPUT_ENABLE(),
        .D_OUT_0(),
        .D_OUT_1(),
        .D_IN_0(ina),
        .D_IN_1()
    );

    (* BEL="0_12_io1" *)
    SB_IO #(
        .PIN_TYPE(6'b 0000_01),
        .PULLUP(1'b0),
        .NEG_TRIGGER(1'b0)
    ) inb_iob (
        .PACKAGE_PIN(inb_pin),
        .LATCH_INPUT_VALUE(),
        .CLOCK_ENABLE(),
        .INPUT_CLK(),
        .OUTPUT_CLK(),
        .OUTPUT_ENABLE(),
        .D_OUT_0(),
        .D_OUT_1(),
        .D_IN_0(inb),
        .D_IN_1()
    );

    (* BEL="0_12_io0" *)
    SB_IO #(
        .PIN_TYPE(6'b 0110_01),
        .PULLUP(1'b0),
        .NEG_TRIGGER(1'b0)
    ) outa_iob (
        .PACKAGE_PIN(outa_pin),
        .LATCH_INPUT_VALUE(),
        .CLOCK_ENABLE(),
        .INPUT_CLK(),
        .OUTPUT_CLK(),
        .OUTPUT_ENABLE(),
        .D_OUT_0(outa),
        .D_OUT_1(),
        .D_IN_0(),
        .D_IN_1()
    );

    (* BEL="0_11_io1" *)
    SB_IO #(
        .PIN_TYPE(6'b 0110_01),
        .PULLUP(1'b0),
        .NEG_TRIGGER(1'b0)
    ) outb_iob (
        .PACKAGE_PIN(outb_pin),
        .LATCH_INPUT_VALUE(),
        .CLOCK_ENABLE(),
        .INPUT_CLK(),
        .OUTPUT_CLK(),
        .OUTPUT_ENABLE(),
        .D_OUT_0(outb),
        .D_OUT_1(),
        .D_IN_0(),
        .D_IN_1()
    );

    (* BEL="0_11_io0" *)
    SB_IO #(
        .PIN_TYPE(6'b 0110_01),
        .PULLUP(1'b0),
        .NEG_TRIGGER(1'b0)
    ) outc_iob (
        .PACKAGE_PIN(outc_pin),
        .LATCH_INPUT_VALUE(),
        .CLOCK_ENABLE(),
        .INPUT_CLK(),
        .OUTPUT_CLK(),
        .OUTPUT_ENABLE(),
        .D_OUT_0(outc),
        .D_OUT_1(),
        .D_IN_0(),
        .D_IN_1()
    );

    (* BEL="0_10_io1" *)
    SB_IO #(
        .PIN_TYPE(6'b 0110_01),
        .PULLUP(1'b0),
        .NEG_TRIGGER(1'b0)
    ) outd_iob (
        .PACKAGE_PIN(outa_pin),
        .LATCH_INPUT_VALUE(),
        .CLOCK_ENABLE(),
        .INPUT_CLK(),
        .OUTPUT_CLK(),
        .OUTPUT_ENABLE(),
        .D_OUT_0(outd),
        .D_OUT_1(),
        .D_IN_0(),
        .D_IN_1()
    );

    top top_i(.clk(clk), .rst(rst), .cen(cen), .ina(ina), .inb(inb), .outa(outa), .outb(outb), .outc(outc), .outd(outd));
endmodule
