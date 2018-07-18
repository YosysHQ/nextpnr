module top(input clk_pin, input btn_pin, output [7:0] led_pin, output gpio0_pin);

	wire clk;
	wire [7:0] led;
    wire btn;
    wire gpio0;

	(* BEL="X0/Y35/PIOA" *) (* IO_TYPE="LVCMOS33" *)
	TRELLIS_IO #(.DIR("INPUT")) clk_buf (.B(clk_pin), .O(clk));

	(* BEL="X4/Y71/PIOA" *) (* IO_TYPE="LVCMOS33" *) (* keep *)
	TRELLIS_IO #(.DIR("INPUT")) btn_buf (.B(btn_pin), .O(btn));

	(* BEL="X0/Y23/PIOC" *) (* IO_TYPE="LVCMOS33" *)
	TRELLIS_IO #(.DIR("OUTPUT")) led_buf_0 (.B(led_pin[0]), .I(led[0]));
	(* BEL="X0/Y23/PIOD" *) (* IO_TYPE="LVCMOS33" *)
	TRELLIS_IO #(.DIR("OUTPUT")) led_buf_1 (.B(led_pin[1]), .I(led[1]));
	(* BEL="X0/Y26/PIOA" *) (* IO_TYPE="LVCMOS33" *)
	TRELLIS_IO #(.DIR("OUTPUT")) led_buf_2 (.B(led_pin[2]), .I(led[2]));
    (* BEL="X0/Y26/PIOC" *) (* IO_TYPE="LVCMOS33" *)
	TRELLIS_IO #(.DIR("OUTPUT")) led_buf_3 (.B(led_pin[3]), .I(led[3]));

	(* BEL="X0/Y26/PIOB" *) (* IO_TYPE="LVCMOS33" *)
	TRELLIS_IO #(.DIR("OUTPUT")) led_buf_4 (.B(led_pin[4]), .I(led[4]));
	(* BEL="X0/Y32/PIOD" *) (* IO_TYPE="LVCMOS33" *)
	TRELLIS_IO #(.DIR("OUTPUT")) led_buf_5 (.B(led_pin[5]), .I(led[5]));
	(* BEL="X0/Y26/PIOD" *) (* IO_TYPE="LVCMOS33" *)
	TRELLIS_IO #(.DIR("OUTPUT")) led_buf_6 (.B(led_pin[6]), .I(led[6]));
    (* BEL="X0/Y29/PIOD" *) (* IO_TYPE="LVCMOS33" *)
	TRELLIS_IO #(.DIR("OUTPUT")) led_buf_7 (.B(led_pin[7]), .I(led[7]));


	(* BEL="X0/Y62/PIOD" *) (* IO_TYPE="LVCMOS33" *)
	TRELLIS_IO #(.DIR("OUTPUT")) gpio0_buf (.B(gpio0_pin), .I(gpio0));

    localparam ctr_width = 30;
    localparam ctr_max = 2**ctr_width - 1;
	reg [ctr_width-1:0] ctr = 0;
	reg [9:0] pwm_ctr = 0;
    reg dir = 0;

	always@(posedge clk) begin
		ctr <= ctr + 1'b1;
    end


    assign led = ctr[ctr_width-1:ctr_width-8];

    // Tie GPIO0, keep board from rebooting
    assign gpio0 = 1'b1;

endmodule
