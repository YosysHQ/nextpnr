module top(input a_pin, output [3:0] led_pin);

	wire a;
	wire [3:0] led;

	TRELLIS_IO #(.DIR("INPUT")) a_buf (.B(a_pin), .O(a));
	TRELLIS_IO #(.DIR("OUTPUT")) led_buf [3:0] (.B(led_pin), .I(led));

    //assign led[0] = !a;
    always @(posedge a) led[0] <= !led[0];
endmodule
