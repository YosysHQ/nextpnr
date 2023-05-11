# Using the Python API

nextpnr provides Python bindings to its internal APIs to enable customisation of the FPGA CAD flow.

If you are interested in using Python to describe the FPGA rather than customise or explore the flow, see the [nextpnr Generic Architecture](generic.md) documentation.

## Running Python Scripts

Python scripts can be run at any point in the flow. The following command line arguments are used to run a script:

 - **--pre-pack script.py**: after reading the JSON, before packing
 - **--pre-place script.py**: after packing, before placement
 - **--pre-route script.py**: after placement, before routing
 - **--post-route script.py**: after routing, before bitstream generation

## Binding Overview

The internal `Context` object providing access to the FPGA architecture and netlist is exposed automatically to the script as a global variable `ctx`.

Internally, nextpnr uses a fast indexed string pool called `IdString`. This is automatically and transparently converted to/from Python strings by the Python binding layer. 

Likewise, the architecture identifiers `BelId`, `WireId` and `PipId` are also translated to/from Python strings containing the object name automatically.

To query the FPGA architecture, use the functions described in the [Architecture API documentation](archapi.md). Ranges can be iterated over in the same way as any other Python range.

## Netlist Access

### Accessing nets

There is a dictionary `ctx.nets` that provides access to all of the nets in a design by name. Each net item has the following fields:

 - `name`: name of the net
 - `wires`: a map of wires used by the net; from wire to the pip driving that wire
 - `driver`: a `PortRef` for the net's driving (source) port
 - `users`: a list of `PortRef`s for the net's sink ports
 - `attrs`: a read-only dictionary of attributes on the net

A `PortRef` has three fields:
    - `cell`: a reference to the cell the port is on
    - `port`: the name of the port

### Accessing cells

There is a dictionary `ctx.cells` that provides access to all of the cells in a design by name. Each cell item has the following fields:

 - `name`: name of the cell
 - `type`: type of the cell
 - `ports`: a read-only dictionary of ports on the cell. Each port has the following fields:
   - `name`: name of the port
   - `net`: reference to the connected net, or `None` if disconnected
   - `type`: direction of the port 
 - `params`: a read-only dictionary of parameters on the cell
 - `attrs`: a read-only dictionary of attributes on the cell
 - `bel`: the Bel the cell is placed on

Cells also have the following member functions:
 - `addInput(name)`: add a new input port with a given name
 - `addOutput(name)`: add a new output port with a given name
 - `addInout(name)`: add a new bidirectional port with a given name
 - `setParam(name, value)`: set a parameter on a cell to a given value
 - `unsetParam(name)`: remove a parameter from the cell
 - `setAttr(name, value)`: set an attribute on a cell to a given value
 - `unsetAttr(name)`: remove an attribute from the cell

The value given to `setParam` and `setAttr` should be a string of `[01xz]*` for four-state bitvectors and numerical values. Other values will be interpreted as a textual string. Textual strings of only `[01xz]* *` should have an extra space added at the end which will be stripped off and avoids any ambiguous cases between strings and four-state bitvectors.

### Creating Objects

`ctx` has two functions for creating new netlist objects. Both return the created object:
 - `createNet(name)`
 - `createCell(name, type)` (note this creates an empty cell, ports must be added separately)

### Other manipulations

`ctx` has some other helper functions for netlist manipulations:

 - `connectPort(netname, cellname, portname)`: connect a cell port to a net
 - `disconnectPort(cellname, portname)`: disconnect a cell port from its net
 - `ripupNet(netname)`: remove all routing from a net (but keep netlist connections intact)
 - `lockNetRouting(netname)`: set the routing of a net as fixed
 - `copyBelPorts(cellname, belname)`: replicate the port definitions of a Bel onto a cell (useful for creating standard cells, as `createCell` doesn't create any ports).

## Constraints

See the [constraints documentation](constraints.md)
