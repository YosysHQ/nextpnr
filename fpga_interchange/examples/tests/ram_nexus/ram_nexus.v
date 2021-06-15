module ram0(
    // Write port
    input wrclk,
    input [7:0] di,
    input wren,
    input [5:0] wraddr,
    // Read port
    input rdclk,
    input rden,
    input [5:0] rdaddr,
    output reg [7:0] do);

    (* syn_ramstyle = "block_ram" *) reg [7:0] ram[0:63];

    initial begin
        ram[0] = 8'b00000001;
        ram[1] = 8'b10101010;
        ram[2] = 8'b01010101;
        ram[3] = 8'b11111111;
        ram[4] = 8'b11110000;
        ram[5] = 8'b00001111;
        ram[6] = 8'b11001100;
        ram[7] = 8'b00110011;
        ram[8] = 8'b00000010;
        ram[9] = 8'b00000100;
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

    input wire  pb0,
    input wire  pb1,

    input  wire [7:0] sw,
    output wire [13:0] led
);
    wire bufclk;
    DCC gbuf_i(.CLKI(clk), .CLKO(bufclk));


    wire rden;
    reg wren;
    wire [5:0] rdaddr;
    wire [5:0] wraddr;
    wire [7:0] di;
    wire [7:0] do;
    ram0 ram(
        .wrclk(bufclk),
        .di(di),
        .wren(wren),
        .wraddr(wraddr),
        .rdclk(bufclk),
        .rden(rden),
        .rdaddr(rdaddr),
        .do(do)
    );

    reg [5:0] address_reg;
    reg [7:0] data_reg;
    reg [7:0] out_reg;

    assign rdaddr = address_reg;
    assign wraddr = address_reg;

    // input_mode == 00 -> in[3:0] -> address_reg
    // input_mode == 01 -> in[3:0] -> data_reg[3:0]
    // input_mode == 10 -> in[3:0] -> data_reg[7:4]
    // input_mode == 11 -> data_reg -> ram[address_reg]
    wire [1:0] input_mode;

    // WE == 0 -> address_reg and data_reg unchanged.
    // WE == 1 -> address_reg or data_reg is updated because on input_mode.
    wire we;

    assign input_mode[0] = ~sw[6];
    assign input_mode[1] = ~sw[7];

    assign we = ~pb0;
    assign led = ~{address_reg, out_reg};
    assign di = data_reg;
    assign rden = 1;

    initial begin
        wren = 1'b0;
        address_reg = 10'b0;
        data_reg = 16'b0;
        out_reg = 16'b0;
    end

    always @ (posedge bufclk) begin
        out_reg <= do;

        if(we == 1) begin
            if(input_mode == 0) begin
                address_reg <= ~sw[5:0];
                wren <= 0;
            end else if(input_mode == 1) begin
                data_reg[3:0] <= ~sw[3:0];
                wren <= 0;
            end else if(input_mode == 2) begin
                data_reg[7:4] <= ~sw[3:0];
                wren <= 0;
            end else if(input_mode == 3) begin
                wren <= 1;
            end
        end
    end

endmodule
