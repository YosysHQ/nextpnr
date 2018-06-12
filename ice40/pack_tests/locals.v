module top(input clk, cen, rst, ina, inb, output outa, outb, outc, outd);

reg [31:0] temp = 0;

integer i;

always @(posedge clk)
begin
    if (cen) begin
        if (rst) begin
            temp <= 0;
        end else begin
            temp[0] <= ina;
            temp[1] <= inb;
            for (i = 2; i < 32; i++) begin
                temp[i] <= temp[(i + 3) % 32] ^ temp[(i + 30) % 32] ^ temp[(i + 4) % 16] ^ temp[(i + 2) % 32];
            end
        end
    end
end

assign outa = temp[3];
assign outb = temp[5];
assign outc = temp[9];
assign outd = temp[15];

endmodule
