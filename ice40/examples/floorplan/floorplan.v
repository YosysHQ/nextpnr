module top(output LED1, LED2, LED3, LED4, LED5);
    localparam N = 31;
    wire [N:0] x;
    assign x[0] = x[N];

    genvar ii;
    generate

    for (ii = 0; ii < N; ii = ii + 1) begin
        (* ringosc *)
        SB_LUT4 #(.LUT_INIT(1)) lut_i(.I0(x[ii]), .I1(), .I2(), .I3(), .O(x[ii+1]));
    end
    endgenerate

    assign clk = x[N];


    reg [19:0] ctr;
    always @(posedge clk)
        ctr <= ctr + 1'b1;
    assign {LED5, LED4, LED3, LED2, LED1} = ctr[19:15];
endmodule
