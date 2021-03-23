module ram0(
    // Write port
    input wrclk,
    input [15:0] di,
    input wren,
    input [9:0] wraddr,
    // Read port
    input rdclk,
    input rden,
    input [9:0] rdaddr,
    output reg [15:0] do);

    (* ram_style = "block" *) reg [15:0] ram[0:1023];

    initial begin
        ram[0] = 16'b00000000_00000001;
        ram[1] = 16'b10101010_10101010;
        ram[2] = 16'b01010101_01010101;
        ram[3] = 16'b11111111_11111111;
        ram[4] = 16'b11110000_11110000;
        ram[5] = 16'b00001111_00001111;
        ram[6] = 16'b11001100_11001100;
        ram[7] = 16'b00110011_00110011;
        ram[8] = 16'b00000000_00000010;
        ram[9] = 16'b00000000_00000100;
    end

    always @ (posedge wrclk) begin
        if(wren == 1) begin
            ram[wraddr] <= di;
        end
    end

    always @ (posedge rdclk) begin
        if(rden == 1) begin
            do <= ram[rdaddr];
        end
    end

endmodule

module top (
    input  wire clk,

    input  wire rx,
    output wire tx,

    input  wire [15:0] sw,
    output wire [15:0] led
);
    wire rden;
    reg wren;
    wire [9:0] rdaddr;
    wire [9:0] wraddr;
    wire [15:0] di;
    wire [15:0] do;
    ram0 ram(
        .wrclk(clk),
        .di(di),
        .wren(wren),
        .wraddr(wraddr),
        .rdclk(clk),
        .rden(rden),
        .rdaddr(rdaddr),
        .do(do)
    );

    reg [9:0] address_reg;
    reg [15:0] data_reg;
    reg [15:0] out_reg;

    assign rdaddr = address_reg;
    assign wraddr = address_reg;

    // display_mode == 00 -> ram[address_reg]
    // display_mode == 01 -> address_reg
    // display_mode == 10 -> data_reg
    wire [1:0] display_mode;

    // input_mode == 00 -> in[9:0] -> address_reg
    // input_mode == 01 -> in[7:0] -> data_reg[7:0]
    // input_mode == 10 -> in[7:0] -> data_reg[15:8]
    // input_mode == 11 -> data_reg -> ram[address_reg]
    wire [1:0] input_mode;

    // WE == 0 -> address_reg and data_reg unchanged.
    // WE == 1 -> address_reg or data_reg is updated because on input_mode.
    wire we;

    assign display_mode[0] = sw[14];
    assign display_mode[1] = sw[15];

    assign input_mode[0] = sw[12];
    assign input_mode[1] = sw[13];

    assign we = sw[11];
    assign led = out_reg;
    assign di = data_reg;
    assign rden = 1;

    initial begin
        address_reg = 10'b0;
        data_reg = 16'b0;
        out_reg = 16'b0;
    end

    always @ (posedge clk) begin
        if(display_mode == 0) begin
            out_reg <= do;
        end else if(display_mode == 1) begin
            out_reg <= address_reg;
        end else if(display_mode == 2) begin
            out_reg <= data_reg;
        end

        if(we == 1) begin
            if(input_mode == 0) begin
                address_reg <= sw[9:0];
                wren <= 0;
            end else if(input_mode == 1) begin
                data_reg[7:0] <= sw[7:0];
                wren <= 0;
            end else if(input_mode == 2) begin
                data_reg[15:8] <= sw[7:0];
                wren <= 0;
            end else if(input_mode == 3) begin
                wren <= 1;
            end
        end
    end

    // Uart loopback
    assign tx = rx;
endmodule
