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
 - `ports` is a map from port name `IdString` to `PortInfo` structures for each cell port
 - `bel` and `belStrength` contain the ID of the Bel the cell is placed onto; and placement strength of the cell; if placed. Placement/ripup should always be done by `Arch::bindBel` and `Arch::unbindBel` rather than by manipulating these fields.
 - `params` and `attrs` store parameters and attributes - from the input JSON or assigned in flows to add metadata - by mapping from parameter name `IdString` to `Property`.
 - The `constr_` fields are for relative constraints:
    - `constr_parent` is a reference to the cell this cell is constrained with respect to; or `nullptr` if not relatively constrained. If not `nullptr`, this cell should be in the parent's `constr_children`.
    - `constr_children` is a list of cells relatively constrained to this one. All children should have `constr_parent == this`. 
    - `constr_x` and `constr_y` are absolute (`constr_parent == nullptr`) or relative (`constr_parent != nullptr`) tile coordinate constraints. If set to `UNCONSTR` then the cell is not constrained in this axis (defaults to `UNCONSTR`)
    - `constr_z` is an absolute (`constr_abs_z`) or relative (`!constr_abs_z`) 'Z-axis' (index inside tile, e.g. logic cell) constraint
 - `region` is a reference to a `Region` if the cell is constrained to a placement region (e.g. for partial reconfiguration or out-of-context flows) or `nullptr` otherwise.

## NetInfo

`NetInfo` instances have the following fields:

 - `name` is the IdString name of the net - for nets with multiple names, one name is chosen according to a set of rules by the JSON frontend
 - `driver` refers to the source of the net using `PortRef`; `driver.cell == nullptr` means that the net is undriven. Nets must have zero or one driver only. The corresponding cell port must be an output and its `PortInfo::net` must refer back to this net.
 - `users` contains a list of `PortRef` references to sink ports on the net. Nets can have zero or more sinks. Each corresponding cell port must be an input or inout; and its `PortInfo::net` must refer back to this net.
 - `wires` is a map that stores the routing tree of a net, if the net is routed.
    - Each entry in `wires` maps from *sink* wire in the routing tree to its driving pip, and the binding strength of that pip (e.g. how freely the router may rip up the pip)
    - Manipulation of this structure is done automatically by `Arch::bindWire`, `Arch::unbindWire`, `Arch::bindPip` and `Arch::unbindPip`; which should almost always be used in lieu of manual manipulation
 - `attrs` stores metadata about the wire (which may come from the JSON or be added by passes)
