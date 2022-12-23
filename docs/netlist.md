# nextpnr netlist structures documentation

The current in-memory design in nextpnr uses several basic structures. See the
[FAQ](faq.md) for more info on terminology.

See also the [Arch API reference](archapi.md) for information on developing new architectures and accessing the architecture database.

 - `CellInfo`: instantiation of a physical block in the netlist (currently, all cells in nextpnr are blackboxes.)
 - `NetInfo`: a connection between cell ports. Has at most one driver; and zero or more users
 - `BaseCtx`: contains all cells and nets, subclassed by `Arch` and then becomes `Context`

Other structures used by these basic structures include:
 - `Property`: stores a numeric or string value - isomorphic to `RTLIL::Const` in Yosys
 - `PortInfo`: stores the name, direction and connected net (if applicable) of cell ports
 - `PortRef`: used to reference the source/sink ports of a net; refers back to a cell and a port name

## CellInfo

`CellInfo` instances have the following fields:

 - `name` and `type` are `IdString`s containing the instance name, and type
 - `hierpath` is name of the hierarchical cell containing the instance, for designs with hierarchy
 - `ports` is a map from port name `IdString` to `PortInfo` structures for each cell port
 - `bel` and `belStrength` contain the ID of the Bel the cell is placed onto; and placement strength of the cell; if placed. Placement/ripup should always be done by `Arch::bindBel` and `Arch::unbindBel` rather than by manipulating these fields.
 - `params` and `attrs` store parameters and attributes - from the input JSON or assigned in flows to add metadata - by mapping from parameter name `IdString` to `Property`.
 - `cluster` is used to specify that the cell is inside a placement cluster, with the details of the placement within the cluster provided by the architecture.
 - `region` is a reference to a `Region` if the cell is constrained to a placement region (e.g. for partial reconfiguration or out-of-context flows) or `nullptr` otherwise.
 - `pseudo_cell` is an optional pointer to an implementation of the pseudo-cell API, used for cells implementing virtual functions such as partition pins without a mapped bel. `bel` will always be `BelId()` for pseudo-cells.

## PseudoCellAPI

Pseudo-cells can be used to implement cells with runtime-defined cell pin to wire mappings. This means they don't have to be a fixed part of the architecture, example use cases could be for implementing partition pins for partial reconfiguration regions; or forcing splits between SLRs. Pseudo-cells implement a series of virtual functions to provide data that for an ordinary cell would be obtained by calling 'bel' ArchAPI functions

The pseudo-cell API is as follows:
 - `Loc getLocation() const` : get an approximate location of the pseudocell
 - `WireId getPortWire(IdString port) const`: gets the wire corresponding to a port (or WireId if it has no wire)

It also implements functions for getting timing data, mirroring that of the Arch API:
 - `bool getDelay(IdString fromPort, IdString toPort, DelayQuad &delay) const`
 - `TimingPortClass getPortTimingClass(IdString port, int &clockInfoCount) const`
 - `TimingClockingInfo getPortClockingInfo(IdString port, int index) const`

## NetInfo

`NetInfo` instances have the following fields:
 - `name` is the IdString name of the net - for nets with multiple names, one name is chosen according to a set of rules by the JSON frontend
 - `hierpath` is name of the hierarchical cell containing the instance, for designs with hierarchy
 - `driver` refers to the source of the net using `PortRef`; `driver.cell == nullptr` means that the net is undriven. Nets must have zero or one driver only. The corresponding cell port must be an output and its `PortInfo::net` must refer back to this net.
 - `users` contains a list of `PortRef` references to sink ports on the net. Nets can have zero or more sinks. Each corresponding cell port must be an input or inout; and its `PortInfo::net` must refer back to this net.
 - `wires` is a map that stores the routing tree of a net, if the net is routed.
    - Each entry in `wires` maps from *sink* wire in the routing tree to its driving pip, and the binding strength of that pip (e.g. how freely the router may rip up the pip)
    - Manipulation of this structure is done automatically by `Arch::bindWire`, `Arch::unbindWire`, `Arch::bindPip` and `Arch::unbindPip`; which should almost always be used in lieu of manual manipulation
 - `attrs` stores metadata about the wire (which may come from the JSON or be added by passes)
 - `clkconstr` contains the period constraint if the wire is a constrained clock; or is empty otherwise
 - `region` is a reference to a `Region` if the net is constrained to a device region or `nullptr` otherwise (_N.B. not supported by the current router_).

## BaseCtx/Context

Relevant fields from a netlist point of view are:
 - `cells` is a map from cell name to a `unique_ptr<CellInfo>` containing cell data
 - `nets` is a map from net name to a `unique_ptr<NetInfo>` containing net data
 - `net_aliases` maps every alias for a net to its canonical name (i.e. index into `nets`) - net aliases often occur when a net has a name both inside a submodule and higher level module
 - `ports` is a list of top level ports, primarily used during JSON export (e.g. to produce a useful post-PnR simulation model). Unlike other ports, top level ports are _not_ added to the driver or users of any connected net. In this sense, nets connected to top-level ports are _dangling_. However, top level ports _can_ still see their connected net as part of their `PortInfo`.
 - `port_cells` is a map of top level port cells.  This is a subset of the `cells` maps containing only ports.

Context also has a method `check()` that ensures all of the contracts met above are satisfied. It is strongly suggested to run this after any pass that may modify the netlist.

## Performance Improvements

Two features are provided to enable performance improvements in some algorithms, generally by reducing the number of `unordered_map` accesses.

The first is `udata`. This is a field of both nets and cells that can be used to give an index into algorithm-specific structures, such as a flat `vector` of cells. Placers and routers may use this for any purpose, but it should not be used to exchange data between passes.

The second is `ArchCellInfo` and `ArchNetInfo`. These are provided by architectures and used as base classes for `CellInfo` and `NetInfo` respectively. They allow architectures to tag information that is needed frequently - for example the clock polarity and clock net for a flipflop are needed for placement validity checking. They should only be used inside arch-specific code, and are lost when netlists are saved/loaded thus must not be used as primary storage - usually these should mirror attributes/parameters. `assignArchInfo` should set these up accordingly.

## Helper Functions - Context

`Context` and its subclass `BaseCtx` provides several helper functions that are often needed inside CAD algorithms.

 - `nameOfBel`, `nameOfWire`, and `nameOfPip` gets the name of an identified object as a C string, often used in conjunction with the logging functions
 - `nameOf` is similar to above but for netlist objects that have a `name` field (e.g. cells, nets, etc)
 - `getNetinfoSourceWire` gets the physical wire `WireId` associated with the source of a net
 - `getNetinfoSinkWire` gets the physical wire `WireId` associated with a given sink (specified by `PortRef`)
 - `getNetinfoRouteDelay` gets the routing delay - actual if the net is fully routed, estimated otherwise - between the source and a given sink of a net
 - `getNetByAlias` returns the pointer to a net given any of its aliases - this should be used in preference to a direct lookup in `nets` whenever a net name is provided by the user

## Hierarchy

As most place and route algorithms require a flattened netlist to work with (consider - each leaf cell instance must have its own bel), the primary netlist structures are flattened. However, some tasks such as floorplanning require an understanding of hierarchy. 

`HierarchicalCell` is the main data structure for storing hierarchy. This represents an instance of a hierarchical, rather than leaf cell (leaf cells are represented by a `CellInfo`).

 - `name` and `type` are the instance name and cell type
 - `parent` is the hierarchical path of the parent cell, and `fullpath` is the hierarchical path of this cell
 - `leaf_cells`, `nets` map from a name inside the hierarchical cell to a 'global' name in the flattened netlist (i.e. one that indexes into `ctx->{cells,nets}`)
 - `leaf_cells_by_gname`, `nets_by_gname` are the inverse of the above maps; going from `{CellInfo,NetInfo}::name` to an instance name inside the cell
 - `hier_cells` maps instance names of sub-hierarchical (non-leaf) cells to global names (indexing into `ctx->hierarchy`)

To preserve hierarchy during passes such as packing, ensure that `hierpath` is set on new cells derived from existing ones, and call `fixupHierarchy()` at the end to rebuild `HierarchicalCell` structures.
