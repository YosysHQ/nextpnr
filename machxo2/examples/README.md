# MachXO2 Architecture Example

This contains a simple example of running `nextpnr-machxo2`:

* `simple.sh` generates JSON output (`pnrblinky.json`) of a classic blinky
  example from `blinky.v`.
* `simtest.sh` will use `yosys` to generate a Verilog file from
  `pnrblinky.json`, called `pnrblinky.v`. It will then and compare
  `pnrblinky.v`'s simulation behavior to the original verilog file (`blinky.v`)
  using the [`iverilog`](http://iverilog.icarus.com) compiler and `vvp`
  runtime. This is known as post-place-and-route simulation.

As `nextpnr-machxo2` is developed the contents `simple.sh` and `simtest.sh`
are subject to change.

## Environment Variables For `simple.sh` And `simtest.sh`

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
