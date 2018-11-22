# Constraints

There are three types of constraints available for end users of nextpnr.

## Architecture-specific IO Constraints

Architectures may provide support for their native (or any other) IO constraint format.
The iCE40 architecture supports PCF constraints thus:

    set_io led[0] 3

and the ECP5 architecture supports a subset of LPF constraints:

    LOCATE COMP "led[0]" SITE "E16";
    IOBUF PORT "led[0]" IO_TYPE=LVCMOS25;


## Absolute Placement Constraints

nextpnr provides generic support for placement constraints by setting the Bel attribute on the cell to the name of
the Bel you wish it to be placed at. For example:

    (* BEL="X2/Y5/lc0" *)

## Clock Constraints

There are two ways to apply clock constraints in nextpnr. The `--clock {freq}` command line argument is used to
apply a default frequency (in MHz) to all clocks without a more specific constraint.

The Python API can apply clock constraints to specific named clocks. This is done by passing a Python file
specifying these constraints to the `--pre-pack` command line argument. Inside the file, constraints are applied by
calling the function `ctx.addClock` with the name of the clock and its frequency in MHz, for example:

    ctx.addClock("csi_rx_i.dphy_clk", 96)
    ctx.addClock("video_clk", 24)
    ctx.addClock("uart_i.sys_clk_i", 12)

