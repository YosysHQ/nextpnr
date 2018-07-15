(* blackbox *)
module TRELLIS_SLICE(
	input A0, B0, C0, D0,
	input A1, B1, C1, D1,
	input M0, M1,
	input FCI, FXA, FXB,

	input CLK, LSR, CE,
	input DI0, DI1,

	input WD0, WD1,
	input WAD0, WAD1, WAD2, WAD3,
	input WRE, WCK,

	output F0, Q0,
	output F1, Q1,
	output FCO, OFX0, OFX1,

    output WDO0, WDO1, WDO2, WDO3,
    output WADO0, WADO1, WADO2, WADO3
);

parameter MODE = "LOGIC";
parameter GSR = "ENABLED";
parameter SRMODE = "LSR_OVER_CE";
parameter CEMUX = "1";
parameter CLKMUX = "CLK";
parameter LSRMUX = "LSR";
parameter LUT0_INITVAL = 16'h0000;
parameter LUT1_INITVAL = 16'h0000;
parameter REG0_SD = "0";
parameter REG1_SD = "0";
parameter REG0_REGSET = "RESET";
parameter REG1_REGSET = "RESET";
parameter CCU2_INJECT1_0 = "NO";
parameter CCU2_INJECT1_1 = "NO";

endmodule

(* blackbox *) (* keep *)
module TRELLIS_IO(
	inout B,
	input I,
	input T,
	output O,
);
parameter DIR = "INPUT";

endmodule
