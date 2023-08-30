# MachXO2 Architecture Example
This directory contains a simple example of running `nextpnr-machxo2`:

* `simple.sh` produces nextpnr output in the files `{pack,place,pnr}*.json`,
  as well as pre-pnr and post-pnr diagrams in `{pack,place,pnr}*.{dot, png}`.
* `simtest.sh` extends `simple.sh` by generating `{pack,place,pnr}*.v` from
  `{pack,place,pnr}*.json`. The script calls the [`iverilog`](http://iverilog.icarus.com)
  compiler and `vvp` runtime to compare the behavior of `{pack,place,pnr}*.v`
  and the original Verilog input (using a testbench `*_tb.v`). This is known as
  post-place-and-route simulation.
* `mitertest.sh` is similar to `simtest.sh`, but more comprehensive. This
  script creates a [miter circuit](https://www21.in.tum.de/~lammich/2015_SS_Seminar_SAT/resources/Equivalence_Checking_11_30_08.pdf)
  to compare the output port values of `{pack,place,pnr}*.v` against the
  original Verilog code _when both modules are fed the same values on their input
  ports._

  All possible inputs and resulting outputs can be tested in reasonable time by
  using `yosys`' built-in SAT solver or [`z3`](https://github.com/Z3Prover/z3),
  an external SMT solver.
* `demo.sh` creates bitstreams for [TinyFPGA Ax](https://tinyfpga.com/a-series-guide.html)
  and writes the resulting bitstream to MachXO2's internal flash using
  [`tinyproga`](https://github.com/tinyfpga/TinyFPGA-A-Programmer).
  `demo-vhdl.sh` does the same, except using the [GHDL Yosys Plugin](https://github.com/ghdl/ghdl-yosys-plugin).

As `nextpnr-machxo2` is developed the contents `simple.sh`, `simtest.sh`,
`mitertest.sh`, and `demo.sh` are subject to change.

## How To Run
Each script requires a prefix that matches one of the self-contained Verilog
examples in this directory. For instance, to create a bitstream from
`tinyfpga.v`, use `demo.sh tinyfpga` (the `*` glob used throughout this file
is filled with the the prefix).

Each of `simple.sh`, `simtest.sh`, and `mitertest.sh` runs yosys and nextpnr
to validate a Verilog design in various ways. They require an additional `mode`
argument- `pack`, `place`, or `pnr`- which stops `nextpnr-machxo2` after the
specified phase and writes out a JSON file of the results in
`{pack,place,pnr}*.json`; `pnr` runs all of the Pack, Place, and Route phases.

`mitertest.sh` requires an third option- `sat` or `smt`- to choose between
verifying the miter with either yosys' built-in SAT solver, or an external
SMT solver.

Each script will exit if it finds an input Verilog example it knows it can't
handle. To keep file count lower, all yosys scripts are written inline inside
the `sh` scripts using the `-p` option.

### Clean
To clean output files from _all_ scripts, run:

```
rm -rf *.dot *.json *.png *.vcd *.smt2 *.log *.txt *.bit {pack,place,pnr}*.v *_simtest*
```

## Known Issues
In principle, `mitertest.sh` should work in `sat` or `smt` mode with all
example Verilog files which don't use the internal oscillator (OSCH) or other
hard IP. However, as of this writing, only `blinky.v` passes correctly for a
few reasons:

  1. The sim models for MachXO2 primitives used by the `gate` module contain
     `initial` values _by design_, as it matches chip behavior. Without any of
     the following in the `gold` module (like `blinky_ext.v` currently):

     * An external reset signal
     * Internal power-on reset signal (e.g. `reg int_rst = 1'd1;`)
     * `initial` values to manually set registers

     the `gold` and `gate` modules will inherently not match.

     Examples using an internal power-on reset (e.g. `uart.v`) also have issues
     that I haven't debugged yet in both `sat` and `smt` mode.
  2. To keep the `gold`/`gate` generation simpler, examples are currently
     assumed to _not_ instantiate MachXO2 simulation primitives directly
    (`FACADE_IO`, `FACADE_FF`, etc).
  3. `synth_lattice` runs `deminout` on `inouts` when generating the `gate`
     module. This is not handled yet when generating the `gold` module.

## Verilog Examples
* `blinky.v`/`blinky_tb.v`- A blinky example meant for simulation.
* `tinyfpga.v`- Blink the LED on TinyFPA Ax.
* `rgbcount.v`- Blink an RGB LED using TinyFPGA Ax, more closely-based on
  [the TinyFPGA Ax guide](https://tinyfpga.com/a-series-guide.html).
* `blinky_ext.v`- Blink the LED on TinyFPA Ax using an external pin (pin 6).
* `uart.v`- UART loopback demo at 19200 baud. Requires the following pins:

  * Pin 1- RX LED
  * Pin 2- TX (will echo RX)
  * Pin 3- RX
  * Pin 4- TX LED
  * Pin 5- Load LED
  * Pin 6- 12 MHz clock input
  * Pin 7- Take LED
  * Pin 8- Empty LED

## Environment Variables For Scripts
* `YOSYS`- Set to the location of the `yosys` binary to test. Defaults to the
  `yosys` on the path. You may want to set this to a `yosys` binary in your
  source tree if doing development.
* `NEXTPNR`- Set to the location of the `nextpnr-machxo2` binary to test.
  Defaults to the `nextpnr-machxo2` binary at the root of the `nextpnr` source
  tree. This should be set, for instance, if doing an out-of-tree build of
  `nextpnr-machxo2`.
* `CELLS_SIM`- Set to the location of `lattice/cells_sim_xo2.v` simulation models.
  Defaults to whatever `yosys-config` associated with the above `YOSYS` binary
  returns. You may want to set this to `/path/to/yosys/src/share/lattice/cells_sim_xo2.v`
  if doing development; `yosys-config` cannot find these "before-installation"
  simulation models.
* `TRELLIS_DB`- Set to the location of the Project Trellis database to use.
  Defaults to nothing, which means `ecppack` will use whatever database is on
  its path.
