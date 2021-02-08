/* Example UART derived from: https://github.com/cr1901/migen_uart.
   Requires 12MHz clock and runs at 19,200 baud. */

/* Machine-generated using Migen */

module top(
	(* LOC = "14" *)
	output tx,
	(* LOC = "16" *)
	input rx,
	(* LOC = "13" *)
	output rx_led,
	(* LOC = "17" *)
	output tx_led,
	(* LOC = "20" *)
	output load_led,
	(* LOC = "23" *)
	output take_led,
	(* LOC = "25" *)
	output empty_led,
	(* LOC = "21" *)
	input clk
);

wire [7:0] out_data;
wire [7:0] in_data;
reg wr = 1'd0;
reg rd = 1'd0;
wire tx_empty;
wire rx_empty;
wire tx_ov;
wire rx_ov;
wire sout_load;
wire [7:0] sout_out_data;
wire sout_shift;
reg sout_empty = 1'd1;
reg sout_overrun = 1'd0;
reg [3:0] sout_count = 4'd0;
reg [9:0] sout_reg = 10'd0;
reg sout_tx;
wire sin_rx;
wire sin_shift;
wire sin_take;
reg [7:0] sin_in_data = 8'd0;
wire sin_edge;
reg sin_empty = 1'd1;
reg sin_busy = 1'd0;
reg sin_overrun = 1'd0;
reg sin_sync_rx = 1'd0;
reg [8:0] sin_reg = 9'd0;
reg sin_rx_prev = 1'd0;
reg [3:0] sin_count = 4'd0;
wire out_active;
wire in_active;
reg shift_out_strobe = 1'd0;
reg shift_in_strobe = 1'd0;
reg [9:0] in_counter = 10'd0;
reg [9:0] out_counter = 10'd0;
wire sys_clk;
wire sys_rst;
wire por_clk;
reg int_rst = 1'd1;

// synthesis translate_off
reg dummy_s;
initial dummy_s <= 1'd0;
// synthesis translate_on

assign tx_led = (~tx);
assign rx_led = (~rx);
assign load_led = sout_load;
assign take_led = sin_take;
assign empty_led = sin_empty;
assign out_data = in_data;
assign in_data = sin_in_data;
assign sout_out_data = out_data;
assign sin_take = rd;
assign sout_load = wr;
assign tx = sout_tx;
assign sin_rx = rx;
assign tx_empty = sout_empty;
assign rx_empty = sin_empty;
assign tx_ov = sout_overrun;
assign rx_ov = sin_overrun;
assign sout_shift = shift_out_strobe;
assign sin_shift = shift_in_strobe;
assign out_active = (~sout_empty);
assign in_active = sin_busy;

// synthesis translate_off
reg dummy_d;
// synthesis translate_on
always @(*) begin
	sout_tx <= 1'd0;
	if (sout_empty) begin
		sout_tx <= 1'd1;
	end else begin
		sout_tx <= sout_reg[0];
	end
// synthesis translate_off
	dummy_d <= dummy_s;
// synthesis translate_on
end
assign sin_edge = ((sin_rx_prev == 1'd1) & (sin_sync_rx == 1'd0));
assign sys_clk = clk;
assign por_clk = clk;
assign sys_rst = int_rst;

always @(posedge por_clk) begin
	int_rst <= 1'd0;
end

always @(posedge sys_clk) begin
	wr <= 1'd0;
	rd <= 1'd0;
	if ((~sin_empty)) begin
		wr <= 1'd1;
		rd <= 1'd1;
	end
	if (sout_load) begin
		if (sout_empty) begin
			sout_reg[0] <= 1'd0;
			sout_reg[8:1] <= sout_out_data;
			sout_reg[9] <= 1'd1;
			sout_empty <= 1'd0;
			sout_overrun <= 1'd0;
			sout_count <= 1'd0;
		end else begin
			sout_overrun <= 1'd1;
		end
	end
	if (((~sout_empty) & sout_shift)) begin
		sout_reg[8:0] <= sout_reg[9:1];
		sout_reg[9] <= 1'd0;
		if ((sout_count == 4'd9)) begin
			sout_empty <= 1'd1;
			sout_count <= 1'd0;
		end else begin
			sout_count <= (sout_count + 1'd1);
		end
	end
	sin_sync_rx <= sin_rx;
	sin_rx_prev <= sin_sync_rx;
	if (sin_take) begin
		sin_empty <= 1'd1;
		sin_overrun <= 1'd0;
	end
	if (((~sin_busy) & sin_edge)) begin
		sin_busy <= 1'd1;
	end
	if ((sin_shift & sin_busy)) begin
		sin_reg[8] <= sin_sync_rx;
		sin_reg[7:0] <= sin_reg[8:1];
		if ((sin_count == 4'd9)) begin
			sin_in_data <= sin_reg[8:1];
			sin_count <= 1'd0;
			sin_busy <= 1'd0;
			if ((~sin_empty)) begin
				sin_overrun <= 1'd1;
			end else begin
				sin_empty <= 1'd0;
			end
		end else begin
			sin_count <= (sin_count + 1'd1);
		end
	end
	out_counter <= 1'd0;
	in_counter <= 1'd0;
	if (in_active) begin
		shift_in_strobe <= 1'd0;
		in_counter <= (in_counter + 1'd1);
		if ((in_counter == 9'd311)) begin
			shift_in_strobe <= 1'd1;
		end
		if ((in_counter == 10'd623)) begin
			in_counter <= 1'd0;
		end
	end
	if (out_active) begin
		shift_out_strobe <= 1'd0;
		out_counter <= (out_counter + 1'd1);
		if ((out_counter == 10'd623)) begin
			out_counter <= 1'd0;
			shift_out_strobe <= 1'd1;
		end
	end
	if (sys_rst) begin
		wr <= 1'd0;
		rd <= 1'd0;
		sout_empty <= 1'd1;
		sout_overrun <= 1'd0;
		sout_count <= 4'd0;
		sout_reg <= 10'd0;
		sin_in_data <= 8'd0;
		sin_empty <= 1'd1;
		sin_busy <= 1'd0;
		sin_overrun <= 1'd0;
		sin_sync_rx <= 1'd0;
		sin_reg <= 9'd0;
		sin_rx_prev <= 1'd0;
		sin_count <= 4'd0;
		shift_out_strobe <= 1'd0;
		shift_in_strobe <= 1'd0;
		in_counter <= 10'd0;
		out_counter <= 10'd0;
	end
end

endmodule
