module top(input a_pin, output led_pin, output led2_pin, output gpio0_pin);

	wire a;
	wire led, led2;
    wire gpio0;
	(* BEL="X4/Y71/PIOA" *) (* IO_TYPE="LVCMOS33" *)
	TRELLIS_IO #(.DIR("INPUT")) a_buf (.B(a_pin), .O(a));
	(* BEL="X0/Y23/PIOC" *) (* IO_TYPE="LVCMOS33" *)
	TRELLIS_IO #(.DIR("OUTPUT")) led_buf (.B(led_pin), .I(led));
    (* BEL="X0/Y26/PIOA" *) (* IO_TYPE="LVCMOS33" *)
    TRELLIS_IO #(.DIR("OUTPUT")) led2_buf (.B(led2_pin), .I(led2));
	(* BEL="X0/Y62/PIOD" *) (* IO_TYPE="LVCMOS33" *)
	TRELLIS_IO #(.DIR("OUTPUT")) gpio0_buf (.B(gpio0_pin), .I(gpio0));
    assign led = a;
    assign led2 = !a;

    TRELLIS_SLICE #(.MODE("LOGIC"), .LUT0_INITVAL(16'hFFFF)) vcc (.F0(gpio0));
endmodule
