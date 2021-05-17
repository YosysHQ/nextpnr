# nexptnr-xlnxic

The xilinx-interchange architecture is currently experimental and not yet intended for end user purposes. However, it does contain a deduplicated Xilinx routing graph from RapidWright and is in development as a platform for benchmarking nextpnr on larger fabrics than it would otherwise support (mistral can also be used to access large Cyclone V devices).

Currently it only supports LUTs, FFs and global clocking on UltraScale+ and (experimentally) 7-series and Versal; and has not yet been through any tuning for placement quality or routing lookahead. It also lacks timing information.

Databases are built from RapidWright; and RapidWright is currently also required for generating bitstreams via Vivado although an open source route may be provided here in the future.

## Building databases

`xlnxic_bbaexport.jar` is built and used to extract the database from RapidWright.

Run the following to generate the bba (omit the `--add-exports` on older Java versions):

```
java --add-exports=java.base/sun.nio.ch=ALL-UNNAMED -jar xilinx_interchange_bbaexport.jar xczu7ev-ffvc1156-2-e ../xlnxic/constids.inc xczu7ev.bba
```

Replace the device; and the relative path to `constids.inc`; as appropriate.

To create the binary database for nextpnr

```
./bba/bbasm --l xczu7ev.bba xczu7ev.bin
```

## Building designs

The input is in the form of a Yosys JSON netlist and XDC constraints; the outputs are currently FPGA Interchange logical and physical netlists:

```
nextpnr-xlnxic --json blinky.json --xdc zcu104.xdc --chipdb xczu7ev.bin --package ffvc1156 --write-log blinky.netlist --write-phys blinky.phys 
```

To create a DCP that can be opened in Vivado for DRC, timing, bitstream generation, etc:

```
$RAPIDWRIGHT_PATH/scripts/invoke_rapidwright.sh com.xilinx.rapidwright.interchange.PhysicalNetlistToDcp blinky.netlist blinky.phys zcu104.xdc blinky.dcp
```

