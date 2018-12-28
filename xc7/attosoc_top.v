module top (
	input clki,
	output [3:0] led
);

    (* keep *)
    wire led_unused;

    wire clk;
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

    attosoc soc(.clk(clk), .led({led_unused, led}));

endmodule

