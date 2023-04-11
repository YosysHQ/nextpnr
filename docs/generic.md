# nextpnr Generic Architecture

Instead of implementing the full [C++ API](archapi.md), you can programmatically 
build up a description of an FPGA using the generic architecture and the 
Python API, or the [Viaduct C++ API](viaduct.md) (described further in its own
document).

The Viaduct API allows more complex constraints to be implemented and has shorter
startup times than using the Python API.

A basic packer is provided that supports LUTs, flipflops and IO buffer insertion.
Packing could also be implemented using the Python API.

At present there is no support for cell timing in the generic architecture. This
will be worked on in the future.

## Python API

All identifiers (`IdString`, `IdStringList`, `WireId`, `PipId`, and `BelId`) are
automatically converted to and from a Python string, so no manual conversion is
required.

`IdStringList`s will be most efficient if strings can be split according to a
separator (currently fixed to `/`), as only the components need be stored 
in-memory. For example; instead of needing to store an entire pip name
`X33/Y45/V4A_TO_A6` which scales badly for large numbers of pips; the strings
`X33`, `Y45` and `V4A_TO_A6` are stored.

Argument names are included in the Python bindings,
so named arguments may be used.

### void addWire(IdStringList name, IdString type, int x, int y);

Adds a wire with a name, type (for user purposes only, ignored by all nextpnr code other than the UI) to the FPGA description. x and y give a nominal location of the wire for delay estimation purposes. Delay estimates are important for router performance (as the router uses an A* type algorithm), even if timing is not of importance.

### addPip(IdStringList name, IdString type, WireId srcWire, WireId dstWire, float delay, Loc loc);

Adds a pip (programmable connection between two named wires). Pip delays that correspond to delay estimates are important for router performance (as the router uses an A* type algorithm), even if timing is otherwise not of importance.

Loc is constructed using `Loc(x, y, z)`. 'z' for pips is only important if region constraints (e.g. for partial reconfiguration regions) are used.

### void addBel(IdStringList name, IdString type, Loc loc, bool gb, bool hidden);

Adds a bel to the FPGA description. Bel type should match the type of cells in the netlist that are placed at this bel (see below for information on special bel types supported by the packer). Loc is constructed using `Loc(x, y, z)` and must be unique. If `hidden` is true, then the bel will not be included in utilisation reports (e.g. for routing/internal use bels).

### void addBelInput(BelId bel, IdString name, WireId wire);
### void addBelOutput(BelId bel, IdString name, WireId wire);
### void addBelInout(BelId bel, IdString name, WireId wire);

Adds an input, output or inout pin to a bel, with an associated wire. Note that both `bel` and `wire` must have been created before calling this function.

### void addGroupBel(IdString group, BelId bel);
### void addGroupWire(IdString group, WireId wire);
### void addGroupPip(IdString group, PipId pip);
### void addGroupGroup(IdString group, IdString grp);

Add a bel, wire, pip or subgroup to a group, which will be created if it doesn't already exist. Groups are purely for visual presentation purposes in the user interface and are not used by any place-and-route algorithms.

### void addDecalGraphic(IdStringList decal, const GraphicElement &graphic);

Add a graphic element to a _decal_, a reusable drawing that may be used to represent multiple wires, pips, bels or groups in the UI (with different offsets). The decal will be created if it doesn't already exist

### void setWireDecal(WireId wire, float x, float y, IdStringList decal);
### void setPipDecal(PipId pip, float x, float y, IdStringList decal);
### void setBelDecal(BelId bel, float x, float y, IdStringList decal);
### void setGroupDecal(GroupId group, float x, float y, IdStringList decal);

Sets the decal ID and offset for a wire, bel, pip or group in the UI.

### void setWireAttr(WireId wire, IdString key, const std::string &value);
### void setPipAttr(PipId pip, IdString key, const std::string &value);
### void setBelAttr(BelId bel, IdString key, const std::string &value);

Sets an attribute on a wire, pip or bel. Attributes are displayed in the tree view in the UI, but have no bearing on place-and-route itself.

### void setLutK(int K);

Sets the number of input pins a LUT in the architecture has. Only affects the generic packer, if a custom packer or no packer is used this value has no effect - there is no need for the architecture to have LUTs at all in this case.

### void setDelayScaling(double scale, double offset);

Set the linear scaling vs distance and fixed offset (both values in nanoseconds) for routing delay estimates. Delay estimates that correlate to pip delays, even if they have no bearing to reality, are important for reasonable routing runtime.

### void addCellTimingClock(IdString cell, IdString port);

Set the timing class of a port on a particular cell to a clock input.

_NOTE: All cell timing functions apply to an individual named cell and not a cell type. This is because
cell-specific configuration might affect timing, e.g. whether or not the register is used for a slice._

### void addCellTimingDelay(IdString cell, IdString fromPort, IdString toPort, float delay);

Specify the combinational delay between two ports of a cell, and set the timing class of
 those ports as combinational input/output.

### void addCellTimingSetupHold(IdString cell, IdString port, IdString clock, float setup, float hold);

Specify setup and hold timings for a port of a cell, and set the timing class of that port as register input.

### void addCellTimingClockToOut(IdString cell, IdString port, IdString clock, float clktoq);

Specify clock-to-out time for a port of a cell, and set the timing class of that port as register output.

### void clearCellBelPinMap(IdString cell, IdString cell_pin);

Remove all bel pin mappings from a given cell pin.

### addCellBelPinMapping(IdString cell, IdString cell_pin, IdString bel_pin);

Add a bel pin to the list of bel pins a cell pin maps to. Note that if no mappings are set up (the usual case), cell pins are assumed to map to an identically named bel pin.

## Generic Packer

The generic packer combines K-input LUTs (`LUT` cells) and simple D-type flip flops (`DFF` cells) (posedge clock only, no set/reset or enable) into a `GENERIC_SLICE` cell. It also inserts `GENERIC_IOB`s onto any top level IO pins without an IO buffer. Constrained IOBs can be implemented by instantiating `GENERIC_IOB` and setting the `BEL` attribute to an IO location.

Thus, the architecture should provide bels with the following ports in order to use the generic packer:

 - `GENERIC_SLICE` bels with `CLK` input, `I[0]` .. `I[K-1]` LUT inputs, `F` LUT output and `Q` FF output (N.B. both LUT and FF outputs are not available at the same time, to represent the constraints of some FPGAs).
 - `GENERIC_IOB` bels with `I` output buffer input, `EN` output enable input, and `O` input buffer output.

See [prims.v](../generic/synth/prims.v) for Verilog simulation models for all these cells.

[synth_generic.tcl](../generic/synth/synth_generic.tcl) can be used with Yosys to perform synthesis to the generic `LUT` and `DFF` cells which the generic packer supports. Invoke it using `tcl synth_generic.tcl K out.json` where _K_ is the number of LUT inputs and _out.json_ the name of the JSON file to write.

The generic packer in its current state is intended for experimentation and proof-of-concept tests. It is _not_ intended to make use of all FPGA features or support complex designs. In these cases a proper [Arch API](archapi.md) implementation is strongly recommended.

## Validity Checks

The following constraints are enforced by the generic architecture during placement.

 - `GENERIC_SLICE` bels may only have one clock signal per tile (xy location)
 - If the `PACK_GROUP` attribute is set to a non-zero value on cells, then only cells with the same `PACK_GROUP` attribute (or `PACK_GROUP` negative or unset) may share a tile. This could be set by the Python API or during synthesis.

## Implementation Example

An artificial, procedural architecture is included in the [generic/examples](../generic/examples) folder. [simple.py](../generic/examples/simple.py) sets up the architecture, and [report.py](../generic/examples/report.py) saves post-place-and-route design to a text file (in place of bitstream generation). [simple.sh](../generic/examples/simple.sh) can be used to synthesise and place-and-route a simple blinky for this architecture.
