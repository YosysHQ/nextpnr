FAQ
===

Terminology
-----------

For nextpnr we are using the following terminology.

### Design Database Terminology

- **Cell**: an instantiation of a physical block inside the netlist. The packer may combine or otherwise modify cells; and the placer places them onto Bels.
- **Port**: an input or output of a Cell, can be connected to a single net.
- **Net**: a connection between cell ports inside the netlist. One net will be routed using one or more wires inside the chip. Nets are always one bit in size, multibit nets are always split.
- **Source**: The cell output port driving a given net
- **Sink**: A cell input port driven by a given net
- **Arc**: A source-sink-pair on a net

### Architecture Database Terminology

- **Bel**: Basic Element, the functional blocks of an FPGA such as logic cells, IO cells, blockrams, etc. Up to one cell may be placed at each Bel.
- **Pin**: an input or output of a Bel, permanently connected to a single wire.
- **Pip**: Programmable Interconnect Point, a configurable connection in one direction between two wires
- **Wire**: a fixed physical connection inside the FPGA between Pips and/or Bel pins.
- **Alias**: a special automatic-on Pip to represent a permanent connection between two wires
- **Group**: a collection of bels, pips, wires, and/or other groups

### Flow Terminology

- **Packing**: The action of grouping cells in synthesis output into larger (logic) cells
- **Placing**: The action of binding packed cells to Bels
- **Routing**: The action of binding nets to wires

### Other Terminology

- **Binding**: Assigning nets to wires and cells to Bels
- **Path**: All the arcs connecting a FF output (or primary input) to a FF input (or primary output)

Adding new architectures to nextpnr
-----------------------------------

### Implementing new architectures

Each nextpnr architecture must implement the *nextpnr architecture API*.
See [archapi.md](archapi.md) for a complete reference of the architecture API.

### Delay Estimates

Each architecture must implement a `estimateDelay()` method that estimates the expected delay for a path from given `src` to `dst` wires.
*It is very important that this method slightly overestimates the expected delay.* Furthermore, it should overestimate the expected delay
by a slightly larger margin for longer paths than for shorter paths. Otherwise there will be performance issues with the router.

The delays estimates returned by that method should also be as fine-grain as possible. It definitely pays off to spend some time improving the `estimateDelay()`
for your architecture once implementing small designs work.

### Ripup Information

The `getConflictingWireWire()`, `getConflictingWireNet()`, `getConflictingPipWire()`, and `getConflictingPipNet()` methods are used by the router
to determine which resources to rip up in order to make a given routing resource (wire or pip) available.

The architecture must guanrantee that the following invariants hold.

**Invariant 1:**

```
    if (!ctx->checkWireAvail(wire)) {
        WireId w = getConflictingWireWire(wire);
        if (w != WireId()) {
            ctx->unbindWire(w);
            assert(ctx->checkWireAvail(wire));
        }
    }
```

**Invariant 2:**

```
    if (!ctx->checkWireAvail(wire)) {
        NetInfo *n = getConflictingWireNet(wire);
        if (n != nullptr) {
            for (auto &it : n->wires)
                ctx->unbindWire(it.first);
            assert(ctx->checkWireAvail(wire));
        }
    }
```

**Invariant 3:**

```
    if (!ctx->checkPipAvail(pip)) {
        WireId w = getConflictingPipWire(pip);
        if (w != WireId()) {
            ctx->unbindWire(w);
            assert(ctx->checkPipAvail(pip));
        }
    }
```

**Invariant 4:**

```
    if (!ctx->checkPipAvail(pip)) {
        NetInfo *n = getConflictingPipNet(pip);
        if (n != nullptr) {
            for (auto &it : n->wires)
                ctx->unbindWire(it.first);
            assert(ctx->checkPipAvail(pip));
        }
    }
```

**Invariant 5:**

```
    if (ctx->checkWireAvail(wire)) {
        // bind is guaranteed to succeed
        ctx->bindWire(wire, net, strength);
    }
```

**Invariant 6:**

```
    if (ctx->checkPipAvail(pip) && ctx->checkWireAvail(ctx->getPipDstWire(pip))) {
        // bind is guaranteed to succeed
        ctx->bindPip(pip, net, strength);
    }
```

Nextpnr and other tools
-----------------------

### Which toolchain should I use and why?

 * If you wish to do new **research** into FPGA architectures, place and route
   algorithms or other similar topics, we suggest you look at using
   [Verilog to Routing](https://verilogtorouting.org).

 * If you are developing FPGA code in **Verilog** for a **Lattice iCE40** and
   need an open source toolchain, we suggest you use [Yosys](http://www.clifford.at/yosys/) and nextpnr.

 * If you are developing FPGA code in **Verilog** for a **Lattice iCE40** with
   Yosys and the **existing arachne-pnr toolchain**, we suggest you start thinking about
   migrating to nextpnr.

 * If you are developing Verilog FPGA code targeted at the Lattice ECP5 and
   need an open source toolchain, you may consider the **extremely
   experimental** ECP5 support in Yosys and nextpnr.

 * If you are developing FPGA code in **VHDL** you will need to use either a
   version of [Yosys with Verific support](https://github.com/YosysHQ/yosys/tree/master/frontends/verific) or the vendor provided tools due
   to the lack of useful open source VHDL support in Yosys. You could also look at developing
   one of the experimental open source VHDL frontends, such as [yavhdl](https://github.com/rqou/yavhdl)
   or [ghdlsynth-beta](https://github.com/tgingold/ghdlsynth-beta), further.

### Why didn't you just improve [arachne-pnr](https://github.com/cseed/arachne-pnr)?

[arachne-pnr](https://github.com/cseed/arachne-pnr) was originally developed as
part of [Project IceStorm](http://www.clifford.at/icestorm/) to demonstrate it
was possible to create an open source place and route tool for the iCE40 FPGAs
that actually produced valid bitstreams.

For its original purpose, it has served the community extremely well. However,
it was never designed to support multiple different FPGA families, nor more
complicated timing driven placement and routing used by most commercial place and route
tools.

It felt like extending arachne-pnr was not going to be the best path forward, so
it was decided to build nextpnr as replacement.

### arachne-pnr does X better!

If you have a use case which prevents you from switching to nextpnr from
arachne, we want to hear about it! Please create an issue and we will do our best to solve the problem!

We want nextpnr to be a suitable replacement for anyone who is currently a user
of arachne-pnr.

### Why are you not just contributing to [Verilog to Routing](https://verilogtorouting.org)?

We believe that [Verilog to Routing](https://verilogtorouting.org) is a great
toolchain and many of the nextpnr developers have made (and continue to make)
contributions to the project.

VtR is an extremely flexible toolchain but focuses on research around FPGA
architecture and algorithm development. If your goal is research, then we very
much encourage you to look into VtR further!

nextpnr takes a different approach by focusing on users developing FPGA code
for current FPGAs.

We also believe that support for real architectures will enable interesting new
research. nextpnr (like all place and route tools) depends heavily on
research groups like the VtR developers to investigate and push forward FPGA placement and routing
algorithms in new and exciting ways.

#### What is VPR?

VPR is the "place and route" tool from Verilog To Routing. It has a similar
role in an FPGA development flow as nextpnr.

### What about [SymbiFlow](http://symbiflow.github.io)?

For the moment [SymbiFlow](http://github.com/SymbiFlow) is concentrating on
extending VPR to work with real world architectures.
nextpnr may or may not become a part of SymbiFlow in the future.

### What is [Project Trellis](https://github.com/SymbiFlow/prjtrellis)?

[Project Trellis](https://github.com/SymbiFlow/prjtrellis) is the effort to
document the bitstream format for the Lattice ECP5 series of FPGAs. It also
includes tools for ECP5 bitstream generation.

Project Trellis is used by nextpnr to build the ECP5 chip database and
enable support for creation of bitstreams for these parts.

### What is [Project X-Ray](https://github.com/SymbiFlow/prjxray)?

[Project X-Ray](https://github.com/SymbiFlow/prjxray) is the effort to document
the bitstream format for the Xilinx Series 7 series of FPGAs. It also includes
tooling around bitstream generation for these parts.

While nextpnr currently does **not** support these Xilinx parts, we expect it
will soon be using Project X-Ray in a similar manner to Project Trellis.

### What is [Project IceStorm](http://www.clifford.at/icestorm/)?

[Project IceStorm](http://www.clifford.at/icestorm/) is both a project to
document the bitstream for the Lattice iCE40 series of parts **and** a full
flow including Yosys and arachne-pnr for converting Verilog into a bitstream for
these parts.

As the open source community now has support for multiple different FPGA parts,
in the nextpnr documentation we generally use Project IceStorm to mean the database and
tools that fulfil the same role as Project Trellis or Project X-Ray.
