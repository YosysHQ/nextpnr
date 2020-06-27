# MachXO2 Architecture Example

This contains a simple example of running `nextpnr-machxo2`:

* `simple.sh` generates JSON output (`pnrblinky.json`) of a classic blinky
  example from `blinky.v`.
* `simtest.sh` will use `yosys` to generate a Verilog file from
  `pnrblinky.json`, called `pnrblinky.v`. It will then and compare
  `pnrblinky.v`'s simulation behavior to the original verilog file (`blinky.v`)
  using the [`iverilog`](http://iverilog.icarus.com) compiler and `vvp`
  runtime. This is known as post-place-and-route simulation.

As `nextpnr-machxo2` is developed the `nextpnr` invocation in `simple.sh` and
`simtest.sh` is subject to change. Other command invocations, such as `yosys`,
_should_ remain unchanged, even as files under the [synth](../synth) directory
change.
