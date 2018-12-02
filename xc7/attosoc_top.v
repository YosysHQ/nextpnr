module top (
	input clk,
	output [3:0] led
);

    (* keep *)
    wire led_unused;

    wire gclk;
    clk_wiz_v3_6 pll(.CLK_IN1(clk), .CLK_OUT1(gclk));
    //assign gclk = clk;
    attosoc soc(.clk(gclk), .led({led_unused, led}));

endmodule

