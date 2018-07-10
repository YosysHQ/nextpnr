module top(input clk_pin, output [3:0] led_pin, output gpio0_pin);

	wire clk;
	wire [7:0] led;

    wire gpio0;

	(* BEL="X0/Y35/PIOA" *) (* IO_TYPE="LVCMOS33" *)
	TRELLIS_IO #(.DIR("INPUT")) clk_buf (.B(clk_pin), .O(clk));

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

    localparam ctr_width = 28;
	reg [ctr_width-1:0] ctr = 0;

	always@(posedge clk)
		ctr <= ctr + 1'b1;

    reg [9:0] brightness [0:7];

    reg [7:0] led_reg;

    genvar i;
    generate
    for (i = 0; i < 8; i=i+1) begin
       always @ (posedge clk) begin
            if (ctr[ctr_width-1 : ctr_width-3] > i)
                brightness[i] <= 10'h3FF;
            else if (ctr[ctr_width-1 : ctr_width-3] == i)
                brightness[i] <= ctr[ctr_width-4:ctr_width-13];
            else
                brightness[i] <= 0;
            led_reg[i] <= ctr[9:0] < brightness[i];
       end
    end
    endgenerate

    assign led = led_reg;

    // Tie GPIO0, keep board from rebooting
    TRELLIS_SLICE #(.MODE("LOGIC"), .LUT0_INITVAL(16'hFFFF)) vcc (.F0(gpio0));

endmodule
