module top(input a_pin, output led_pin, output gpio0_pin);

	wire a;
	wire led;
    wire gpio0;
	(* BEL="X90/Y65/PIOB" *) (* IO_TYPE="LVCMOS33" *)
	TRELLIS_IO #(.DIR("INPUT")) a_buf (.B(a_pin), .O(a));
	(* BEL="X0/Y23/PIOC" *) (* IO_TYPE="LVCMOS33" *)
	TRELLIS_IO #(.DIR("OUTPUT")) led_buf (.B(led_pin), .I(led), .T(t));
	(* BEL="X0/Y62/PIOD" *) (* IO_TYPE="LVCMOS33" *)
	TRELLIS_IO #(.DIR("OUTPUT")) gpio0_buf (.B(gpio0_pin), .I(gpio0), .T(t));
    assign led = !a;
    wire t;
    TRELLIS_SLICE #(.MODE("LOGIC"), .LUT0_INITVAL(16'h0000)) gnd (.F0(t));
    TRELLIS_SLICE #(.MODE("LOGIC"), .LUT0_INITVAL(16'hFFFF)) vcc (.F0(gpio0));
endmodule
