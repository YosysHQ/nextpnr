# Constraints

There are three types of constraints available for end users of nextpnr.

## Architecture-specific IO Constraints

Architectures may provide support for their native (or any other) IO constraint format.
The iCE40 architecture supports PCF constraints thus:

    set_io led[0] 3

and the ECP5 architecture supports a subset of LPF constraints (for details see Lattice Technical Note "FPGA-TN-02032 1.3"):

    LOCATE COMP "led[0]" SITE "E16";
    IOBUF PORT "led[0]" IO_TYPE=LVCMOS25;
    IOBUF ...  DRIVE=4|8|12|16; // currently LVCMOS33 and LVCMOS33D only
    IOBUF ... OPENDRAIN=ON|OFF;
    IOBUF ... TERMINATION=50|75|150;
    IOBUF ... DIFFRESISTOR=100; //for differential IO only
    IOBUF ... CLAMP=ON|OFF;
    IOBUF ... PULLMODE=UP|DOWN|NONE;
    IOBUF ... SLEWRATE=FAST|SLOW; //outputs only
    IOBUF ... HYSTERESIS=ON|OFF;

## Absolute Placement Constraints

nextpnr provides generic support for placement constraints by setting the Bel attribute on the cell to the name of
the Bel you wish it to be placed at. For example:

    (* BEL="X2/Y5/lc0" *)

## Clock Constraints

There are two ways to apply clock constraints in nextpnr. The `--freq {freq}` command line argument is used to
apply a default frequency (in MHz) to all clocks without a more specific constraint.

The Python API can apply clock constraints to specific named clocks. This is done by passing a Python file
specifying these constraints to the `--pre-pack` command line argument. Inside the file, constraints are applied by
calling the function `ctx.addClock` with the name of the clock and its frequency in MHz, for example:

    ctx.addClock("csi_rx_i.dphy_clk", 96)
    ctx.addClock("video_clk", 24)
    ctx.addClock("uart_i.sys_clk_i", 12)


## Multicycle Paths

The `--sdc` file can relax paths that have more than one clock period, e.g. logic that loads only every other clock
(a clock enable):

    create_clock -period 16 [get_ports clk]
    set_multicycle_path 2 -setup -from [get_nets {u_core.*}] -to [get_nets {u_core.*}]

Paths from the `-from` cells to the `-to` cells may then take 2 periods of their clock. Without `-from`, every path into
the `-to` cells may. Paths into the `-to` cells from other cells keep one period. Cells, cell pins and nets select cells;
a net stands for the cell that drives it, so a register can be named by its output, which keeps its name through
synthesis. `get_cells` and `get_nets` take `*` and `?` wildcards and Tcl lists (`{a b}`).

Hold stays checked on the launch edge, as after `set_multicycle_path N-1 -hold` in other tools; `-hold` itself has
no effect. The reported Fmax counts the N periods. `-through`, `-start` and `-end` are not supported.
