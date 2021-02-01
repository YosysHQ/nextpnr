# MachXO2 Architecture Example

This contains a simple example of running `nextpnr-machxo2`:

* `simple.sh` generates JSON output (`{pack,place,pnr}blinky.json`) of a
  classic blinky example from `blinky.v`.
* `simtest.sh` will use `yosys` to generate a Verilog file from
  `{pack,place,pnr}blinky.json`, called `{pack,place,pnr}blinky.v`. It will
  then and compare `{pack,place,pnr}blinky.v`'s simulation behavior to the
  original verilog file (`blinky.v`) using the [`iverilog`](http://iverilog.icarus.com)
  compiler and `vvp` runtime. This is known as post-place-and-route simulation.
* `mitertest.sh` is similar to `simtest.sh`, but more comprehensive. This
  script creates a [miter circuit](https://www21.in.tum.de/~lammich/2015_SS_Seminar_SAT/resources/Equivalence_Checking_11_30_08.pdf)
  to compare the output port values of `{pack,place,pnr}blinky.v` against the
  original `blinky.v` _when both modules are fed the same values on their input
  ports._

  All possible inputs and resulting outputs can be tested in reasonable time by
  using `yosys`' built-in SAT solver or [`z3`](https://github.com/Z3Prover/z3),
  an external SMT solver.
* `demo.sh` creates a blinky bitstream for [TinyFPGA Ax](https://tinyfpga.com/a-series-guide.html)
  and writes the resulting bitstream to MachXO2's internal flash using
  [`tinyproga`](https://github.com/tinyfpga/TinyFPGA-A-Programmer).

As `nextpnr-machxo2` is developed the contents `simple.sh`, `simtest.sh`, and
`mitertest.sh` are subject to change.

## How To Run
The following applies to all `sh` scripts except `demo.sh`, which requires no
arguments.

Each `sh` script runs yosys and nextpnr to validate a blinky design in various
ways. The `mode` argument to each script- `pack`, `place`, or `pnr`- stop
`nextpnr-machxo2` after the specified phase and writes out a JSON file of the
results in `{pack,place,pnr}blinky.json`; `pnr` runs all of the Pack, Place,
and Route phases.

`mitertest.sh` requires an additional option- `sat` or `smt`- to choose between
verifying the miter with either yosys' built-in SAT solver, or an external
SMT solver.

To keep file count lower, all yosys scripts are written inline inside the
`sh` scripts using the `-p` option.

### Clean
To clean output files from _all_ scripts, run: `rm -rf *.dot *.json *.png *.vcd *.smt2 *.log tinyfpga.txt tinyfpga.bit {pack,place,pnr}*.v blinky_simtest*`

## Environment Variables For Scripts

* `YOSYS`- Set to the location of the `yosys` binary to test. Defaults to the
  `yosys` on the path. You may want to set this to a `yosys` binary in your
  source tree if doing development.
* `NEXTPNR`- Set to the location of the `nextpnr-machxo2` binary to test.
  Defaults to the `nextpnr-machxo2` binary at the root of the `nextpnr` source
  tree. This should be set, for instance, if doing an out-of-tree build of
  `nextpnr-machxo2`.
* `CELLS_SIM`- Set to the location of `machxo2/cells_sim.v` simulation models.
  Defaults to whatever `yosys-config` associated with the above `YOSYS` binary
  returns. You may want to set this to `/path/to/yosys/src/share/machxo2/cells_sim.v`
  if doing development; `yosys-config` cannot find these "before-installation"
  simulation models.
* `TRELLIS_DB`- Set to the location of the Project Trellis database to use.
  Defaults to nothing, which means `ecppack` will use whatever database is on
  its path.
