Each architecture must implement the following types and APIs.

Architectures can either inherit from `ArchAPI<ArchRanges>`, which is a pure virtual description of the architecture API; or `BaseArch<ArchRanges>` which provides some default implementations described below.

`ArchRanges` is a `struct` of `using`s that allows arches to return custom range types. These ranges can be anything that has a `begin()` and `end()` method that return const forward iterators. This can be a `std::list<T>`, `std::vector<T>`, a (const) reference to those, or anything else that behaves in a similar way.

The contents of `ArchRanges` is as follows:

| Type                      | Range of                         |
|---------------------------|----------------------------------|
|`ArchArgsT`                | N/A (struct of device params)    |
|`AllBelsRangeT`            | `BelId`                          |
|`TileBelsRangeT`           | `BelId`                          |
|`BelAttrsRangeT`           | std::pair<IdString, std::string> |
|`BelPinsRangeT`            | `IdString`                       |
|`CellBelPinRangeT`         | `IdString`                       |
|`AllWiresRangeT`           | `WireId`                         |
|`DownhillPipRangeT`        | `PipId`                          |
|`UphillPipRangeT`          | `PipId`                          |
|`WireBelPinRangeT`         | `BelPin`                         |
|`AllPipsRangeT`            | `PipId`                          |
|`PipAttrsRangeT`           | std::pair<IdString, std::string> |
|`AllGroupsRangeT`          | `GroupId`                        |
|`GroupBelsRangeT`          | `BelId`                          |
|`GroupWiresRangeT`         | `WireId`                         |
|`GroupPipsRangeT`          | `PipId`                          |
|`GroupGroupsRangeT`        | `GroupId`                        |
|`DecalGfxRangeT`           | `GraphicElement`                 |
|`CellTypeRangeT`           | `IdString`                       |
|`BelBucketRangeT`          | `BelBucketRange`                 |
|`BucketBelRangeT`          | `BelId`                          |

The functions that return a particular type are described below. Where a default function implementation is provided, `BaseArchRanges` (which `ArchRanges` can inherit from) will set the range type appropriately.

archdefs.h
==========

The architecture-specific `archdefs.h` must define the following types.

With the exception of `ArchNetInfo` and `ArchCellInfo`, the following types should be "lightweight" enough so that passing them by value is sensible.

### delay\_t

A scalar type that is used to  represent delays. May be an integer or float type.

### BelId

A type representing a bel name. `BelId()` must construct a unique null-value. Must provide `==`, `!=`, and `<` operators and a `unsigned int hash() const` member function.

### WireId

A type representing a wire name. `WireId()` must construct a unique null-value. Must provide `==`, `!=`, and `<` operators and a `unsigned int hash() const` member function.

### PipId

A type representing a pip name. `PipId()` must construct a unique null-value. Must provide `==`, `!=`, and `<` operators and a `unsigned int hash() const` member function.

### BelBucketId

A type representing a bel bucket. `BelBucketId()` must construct a unique null-value. Must provide `==`, `!=`, and `<` operators and a `unsigned int hash() const` member function.

### GroupId

A type representing a group name. `GroupId()` must construct a unique null-value. Must provide `==` and `!=` operators and a `unsigned int hash() const` member function.

### DecalId

A type representing a reference to a graphical decal. `DecalId()` must construct a unique null-value. Must provide `==` and `!=` operators and a `unsigned int hash() const` member function.

### ClusterId

A type representing a reference to a constrained cluster of cells. `ClusterId()` must construct a unique null-value. Must provide `==` and `!=` operators and `unsigned int hash() const` member function.

### ArchNetInfo

The global `NetInfo` type derives from this one. Can be used to add arch-specific data (caches of information derived from wire attributes, bound wires and pips, and other net state). Must be declared as empty struct if unused.

### ArchCellInfo

The global `CellInfo` type derives from this one. Can be used to add arch-specific data (caches of information derived from cell attributes and parameters, bound bel, and other cell state). Must be declared as empty struct if unused.

arch.h
======

Each architecture must provide their own implementation of the `Arch` struct in `arch.h`. `Arch` must derive from `ArchAPI<ArchRanges>` or `BaseArch<ArchRanges>` (see above) and must provide the following methods:

General Methods
---------------

### Arch(ArchArgs args)

Constructor. ArchArgs is a architecture-specific type (usually a struct also defined in `arch.h`).

### std::string getChipName() const

Return a user-friendly string representation of the ArchArgs that was used to construct this object.

### ArchArgs archArgs() const

Return the `ArchArgs` used to construct this object.

### IdString archArgsToId(ArchArgs args) const

Return an internal IdString representation of the ArchArgs that was used to construct this object.

*BaseArch default: returns empty IdString*

### int getGridDimX() const

Get grid X dimension. All bels and pips must have Y coordinates in the range `0 .. getGridDimX()-1` (inclusive).

### int getGridDimY() const

Get grid Y dimension. All bels and pips must have Y coordinates in the range `0 .. getGridDimY()-1` (inclusive).

### int getTileBelDimZ(int x, int y) const

Get Z dimension for the specified tile for bels. All bels with at specified X and Y coordinates must have a Z coordinate in the range `0 .. getTileDimZ(X,Y)-1` (inclusive).

### int getTilePipDimZ(int x, int y) const

Get Z dimension for the specified tile for pips. All pips with at specified X and Y coordinates must have a Z coordinate in the range `0 .. getTileDimZ(X,Y)-1` (inclusive).

*BaseArch default: returns 1*

### char getNameDelimiter() const

Returns a delimiter that can be used to build up bel, wire and pip names out of hierarchical components (such as tiles and sites) to avoid the high memory usage of storing full names for every object.

*BaseArch default: returns ' '*

Cell Methods
-----------

### CellTypeRangeT getCellTypes() const

Get list of cell types that this architecture accepts.

*BaseArch default: returns list derived from bel types set up by `init_cell_types()`*

Bel Methods
-----------

### BelId getBelByName(IdStringList name) const

Lookup a bel by its name, which is a list of IdStrings joined by `getNameDelimiter()`.

### IdStringList getBelName(BelId bel) const

Get the name for a bel. (Bel names must be unique.)

### Loc getBelLocation(BelId bel) const

Get the X/Y/Z location of a given bel. Each bel must have a unique X/Y/Z location.

### BelId getBelByLocation(Loc loc) const

Lookup a bel by its X/Y/Z location.

### TileBelsRangeT getBelsByTile(int x, int y) const

Return a list of all bels at the give X/Y location.

### bool getBelGlobalBuf(BelId bel) const

Returns true if the given bel is a global buffer. A global buffer does not "pull in" other cells it drives to be close to the location of the global buffer.

*BaseArch default: returns false*

### uint32\_t getBelChecksum(BelId bel) const

Return a (preferably unique) number that represents this bel. This is used in design state checksum calculations.

*BaseArch default: returns `bel.hash()`*

### void bindBel(BelId bel, CellInfo \*cell, PlaceStrength strength)

Bind a given bel to a given cell with the given strength.

This method must also update `cell->bel` and `cell->belStrength`.

*BaseArch default: binds using `base_bel2cell`*

### void unbindBel(BelId bel)

Unbind a bel.

This method must also update `CellInfo::bel` and `CellInfo::belStrength`.

*BaseArch default: unbinds using `base_bel2cell`*

### bool checkBelAvail(BelId bel) const

Returns true if the bel is available. A bel can be unavailable because it is bound, or because it is exclusive to some other resource that is bound.

*BaseArch default: returns `getBoundBelCell(bel) == nullptr`*

### CellInfo \*getBoundBelCell(BelId bel) const

Return the cell the given bel is bound to, or nullptr if the bel is not bound.

*BaseArch default: returns entry in `base_bel2cell`*

### CellInfo \*getConflictingBelCell(BelId bel) const

If the bel is unavailable, and unbinding a single cell would make it available, then this method must return that cell.

*BaseArch default: returns `getBoundBelCell(bel)`*

### AllBelsRangeT getBels() const

Return a list of all bels on the device.

### IdString getBelType(BelId bel) const

Return the type of a given bel.

### bool getBelHidden(BelId bel) const

Should this bel be hidden from utilities?

*BaseArch default: returns false*

### BelAttrsRangeT getBelAttrs(BelId bel) const

Return the attributes for that bel. Bel attributes are only informal. They are displayed by the GUI but are otherwise
unused. An implementation may simply return an empty range.

*BaseArch default: returns default-constructed range*

### WireId getBelPinWire(BelId bel, IdString pin) const

Return the wire connected to the given bel pin.

### PortType getBelPinType(BelId bel, IdString pin) const

Return the type (input/output/inout) of the given bel pin.

### BelPinsRangeT getBelPins(BelId bel) const

Return a list of all pins on that bel.

### CellBelPinRangeT getBelPinsForCellPin(const CellInfo *cell_info, IdString pin) const

Return the list of bel pin names that a given cell pin should be routed to. In most cases there will be a single bel pin for each cell pin; and output pins must _always_ have only one bel pin associated with them.

*BaseArch default: returns a one-element array containing `pin`*

Wire Methods
------------

### WireId getWireByName(IdStringList name) const

Lookup a wire by its name, which is a list of IdStrings joined by `getNameDelimiter()`.

### IdStringList getWireName(WireId wire) const

Get the name for a wire. (Wire names must be unique.)

### IdString getWireType(WireId wire) const

Get the type of a wire. The wire type is purely informal and
isn't used by any of the core algorithms. Implementations may
simply return `IdString()`.

*BaseArch default: returns empty IdString*

### WireAttrsRangeT getWireAttrs(WireId wire) const

Return the attributes for that wire. Wire attributes are only informal. They are displayed by the GUI but are otherwise
unused. An implementation may simply return an empty range.

*BaseArch default: returns default-constructed range*

### uint32\_t getWireChecksum(WireId wire) const

Return a (preferably unique) number that represents this wire. This is used in design state checksum calculations.

*BaseArch default: returns `wire.hash()`*

### void bindWire(WireId wire, NetInfo \*net, PlaceStrength strength)

Bind a wire to a net. This method must be used when binding a wire that is driven by a bel pin. Use `bindPip()`
when binding a wire that is driven by a pip.

This method must also update `net->wires`.

*BaseArch default: binds using `base_wire2net`*

### void unbindWire(WireId wire)

Unbind a wire. For wires that are driven by a pip, this will also unbind the driving pip.

This method must also update `NetInfo::wires`.

*BaseArch default: unbinds using `base_wire2net`*

### bool checkWireAvail(WireId wire) const

Return true if the wire is available, i.e. can be bound to a net.

*BaseArch default: returns `getBoundWireNet(wire) == nullptr`*

### NetInfo \*getBoundWireNet(WireId wire) const

Return the net a wire is bound to.

*BaseArch default: returns entry in `base_wire2net`*

### WireId getConflictingWireWire(WireId wire) const

If this returns a non-WireId(), then unbinding that wire
will make the given wire available.

*BaseArch default: returns `wire`*

### NetInfo \*getConflictingWireNet(WireId wire) const

If this returns a non-nullptr, then unbinding that entire net
will make the given wire available.

*BaseArch default: returns `getBoundWireNet(wire)`*

### DelayQuad getWireDelay(WireId wire) const

Get the delay for a wire.

### AllWiresRangeT getWires() const

Get a list of all wires on the device.

### WireBelPinRangeT getWireBelPins(WireId wire) const

Get a list of all bel pins attached to a given wire.

### BoundingBox getRouteBoundingBox(WireId src, WireId dst) const

Get the bounding box required to route an arc, assuming an uncongested
chip. There may be significant performance impacts if routing regularly
exceeds these bounds by more than a small margin; so an over-estimate
of the bounds is almost always better than an under-estimate.

### IdString getWireConstantValue() const

If not an empty string, indicate this wire can be used to source nets
with their `constant_value` equal to its return value.

*BaseArch default: returns `IdString()`*


Pip Methods
-----------

### PipId getPipByName(IdStringList name) const

Lookup a pip by its name, which is a list of IdStrings joined by `getNameDelimiter()`.

### IdStringList getPipName(PipId pip) const

Get the name for a pip. (Pip names must be unique.)

### IdString getPipType(PipId pip) const

Get the type of a pip. Pip types are purely informal and
implementations may simply return `IdString()`.

*BaseArch default: returns empty IdString*

### PipAttrsRangeT getPipAttrs(PipId pip) const

Return the attributes for that pip. Pip attributes are only informal. They are displayed by the GUI but are otherwise
unused. An implementation may simply return an empty range.

*BaseArch default: returns default-constructed range*

### Loc getPipLocation(PipId pip) const

Get the X/Y/Z location of a given pip. Pip locations do not need to be unique, and in most cases they aren't. So
for pips a X/Y/Z location refers to a group of pips, not an individual pip.

### uint32\_t getPipChecksum(PipId pip) const

Return a (preferably unique) number that represents this pip. This is used in design state checksum calculations.

*BaseArch default: returns `pip.hash()`*

### void bindPip(PipId pip, NetInfo \*net, PlaceStrength strength)

Bid a pip to a net. This also bind the destination wire of that pip.

This method must also update `net->wires`.

*BaseArch default: binds using `base_pip2net` and `base_wire2net`*

### void unbindPip(PipId pip)

Unbind a pip and the wire driven by that pip.

This method must also update `NetInfo::wires`.

*BaseArch default: unbinds using `base_pip2net` and `base_wire2net`*

### bool checkPipAvail(PipId pip) const

Returns true if the given pip is available to be bound to a net.

Users must also check if the pip destination wire is available
with `checkWireAvail(getPipDstWire(pip))` before binding the
pip to a net.

*BaseArch default: returns `getBoundPipNet(pip) == nullptr`*

### bool checkPipAvailForNet(PipId pip, const NetInfo *net) const

Returns true if the given pip is available to be bound to a net, or if the
pip is already bound to that net.

*BaseArch default: returns `getBoundPipNet(pip) == nullptr || getBoundPipNet(pip) == net`*

### NetInfo \*getBoundPipNet(PipId pip) const

Return the net this pip is bound to.

*BaseArch default: returns entry in `base_pip2net`*

### WireId getConflictingPipWire(PipId pip) const

If this returns a non-WireId(), then unbinding that wire
will make the given pip available.

*BaseArch default: returns empty `WireId()`*

### NetInfo \*getConflictingPipNet(PipId pip) const

If this returns a non-nullptr, then unbinding that entire net
will make the given pip available.

*BaseArch default: returns empty `getBoundPipNet(pip)`*

### AllPipsRangeT getPips() const

Return a list of all pips on the device.

### WireId getPipSrcWire(PipId pip) const

Get the source wire for a pip.

### WireId getPipDstWire(PipId pip) const

Get the destination wire for a pip.

Bi-directional switches (transfer gates) are modeled using two
anti-parallel pips.

### DelayQuad getPipDelay(PipId pip) const

Get the delay for a pip.

### DownhillPipRangeT getPipsDownhill(WireId wire) const

Get all pips downhill of a wire, i.e. pips that use this wire as source wire.

### DownhillPipRangeT getPipsUphill(WireId wire) const

Get all pips uphill of a wire, i.e. pips that use this wire as destination wire.

Group Methods
-------------

### GroupId getGroupByName(IdStringList name) const

Lookup a group by its name, which is a list of IdStrings joined by `getNameDelimiter()`.

*BaseArch default: returns `GroupId()`*

### IdStringList getGroupName(GroupId group) const

Get the name for a group. (Group names must be unique.)

*BaseArch default: returns `IdStringList()`*

### AllGroupsRangeT getGroups() const

Get a list of all groups on the device.

*BaseArch default: returns default-constructed range*

### GroupBelsRangeT getGroupBels(GroupId group) const

Get a list of all bels within a group.

*BaseArch default: asserts false as unreachable due to there being no groups*

### GroupWiresRangeT getGroupWires(GroupId group) const

Get a list of all wires within a group.

*BaseArch default: asserts false as unreachable due to there being no groups*

### GroupPipsRangeT getGroupPips(GroupId group) const

Get a list of all pips within a group.

*BaseArch default: asserts false as unreachable due to there being no groups*

### GroupGroupsRangeT getGroupGroups(GroupId group) const

Get a list of all groups within a group.

*BaseArch default: asserts false as unreachable due to there being no groups*

Delay Methods
-------------

### delay\_t estimateDelay(WireId src, WireId dst) const

Return a rough estimate for the total `maxDelay()` delay from the given src wire to
the given dst wire.

This should return a low upper bound for the fastest route from `src` to `dst`.

Or in other words it should assume an otherwise unused chip (thus "fastest route").
But it only produces an estimate for that fastest route, not an exact
result, and for that estimate it is considered more acceptable to return a
slightly too high result and it is considered less acceptable to return a
too low result (thus "low upper bound").

### delay\_t predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const

Return a reasonably good estimate for the total `maxDelay()` delay for the
given arc. This should return a low upper bound for the fastest route for that arc.

### delay\_t getDelayEpsilon() const

Return a small delay value that can be used as small epsilon during routing.
The router will for example not re-calculate cached routing data if faster routes
are found when the difference is smaller than this value.

### delay\_t getRipupDelayPenalty() const

The base penality when calculating delay penalty for ripping up routed nets. The
actual penalty used is a multiple of this value (i.e. a weighted version of this value).

### float getDelayNS(delay\_t v) const

Convert an `delay_t` to an actual real-world delay in nanoseconds.

### delay_t getDelayFromNS(float v) const

Convert a real-world delay in nanoseconds to a `delay_t`.

### uint32\_t getDelayChecksum(delay\_t v) const

Convert a `delay_t` to an integer for checksum calculations.

### bool getArcDelayOverride(const NetInfo \*net_info, const PortRef &sink, DelayQuad &delay) const

This allows an arch to provide a more precise method for calculating the delay for a routed arc than
summing wire and pip delays; for example a SPICE simulation. If available, `delay` should be set and
`true` returned.

*BaseArch default: returns false*

Flow Methods
------------

### bool pack()

Run the packer.

### bool place()

Run the placer.

### bool route()

run the router.

Graphics Methods
----------------

### DecalGfxRangeT getDecalGraphics(DecalId decal) const

Return the graphic elements that make up a decal.

The same decal must always produce the same list. If the graphics for
a design element changes, that element must return another decal.

*BaseArch default: returns default-constructed range*

### DecalXY getBelDecal(BelId bel) const

Return the decal and X/Y position for the graphics representing a bel.

*BaseArch default: returns `DecalXY()`*

### DecalXY getWireDecal(WireId wire) const

Return the decal and X/Y position for the graphics representing a wire.

*BaseArch default: returns `DecalXY()`*

### DecalXY getPipDecal(PipId pip) const

Return the decal and X/Y position for the graphics representing a pip.

*BaseArch default: returns `DecalXY()`*

### DecalXY getGroupDecal(GroupId group) const

Return the decal and X/Y position for the graphics representing a group.

*BaseArch default: returns `DecalXY()`*

Cell Delay Methods
------------------

### bool getCellDelay(const CellInfo \*cell, IdString fromPort, IdString toPort, DelayQuad &delay) const

Returns the delay for the specified path through a cell in the `&delay` argument. The method returns
false if there is no timing relationship from `fromPort` to `toPort`.

*BaseArch default: returns false*

### TimingPortClass getPortTimingClass(const CellInfo \*cell, IdString port, int &clockInfoCount) const

Return the _timing port class_ of a port. This can be a register or combinational input or output; clock input or
output; general startpoint or endpoint; or a port ignored for timing purposes. For register ports, clockInfoCount is set
to the number of associated _clock edges_ that can be queried by getPortClockingInfo.

*BaseArch default: returns `TMG_IGNORE`*

### TimingClockingInfo getPortClockingInfo(const CellInfo \*cell, IdString port, int index) const

Return the _clocking info_ (including port name of clock, clock polarity and setup/hold/clock-to-out times) of a
port. Where ports have more than one clock edge associated with them (such as DDR outputs), `index` can be used to obtain
information for all edges. `index` must be in [0, clockInfoCount), behaviour is undefined otherwise.

*BaseArch default: asserts false as unreachable*

Bel Buckets Methods
-------------------

Bel buckets are subsets of BelIds and cell types used by analytic placer to
seperate types of bels during placement. The buckets should form an exact
cover over all BelIds and cell types.

Each bel bucket should be BelIds and cell types that are roughly
interchangable during placement.  Typical buckets are:
 - All LUT bels
 - All FF bels
 - All multipliers bels
 - All block RAM bels
 - etc.

The bel buckets will be used during analytic placement for spreading prior to
strict legality enforcement.  It is not required that all bels within a bucket
are strictly equivelant.

Strict legality step will enforce those differences, along with additional
local constraints.  `isValidBelForCellType`, and `isBelLocationValid` are used
to enforce strict legality checks.

### BelBucketRangeT getBelBuckets() const

Return a list of all bel buckets on the device.

*BaseArch default: the list of buckets created by calling `init_bel_buckets()`, based on calls to `getBelBucketForBel` for all bels*

### IdString getBelBucketName(BelBucketId bucket) const

Return the name of this bel bucket.

*BaseArch default: `bucket`, if `BelBucketId` is a typedef of `IdString`*

### BelBucketId getBelBucketByName(IdString bucket\_name) const

Return the BelBucketId for the specified bucket name.

*BaseArch default: `bucket_name`, if `BelBucketId` is a typedef of `IdString`*

### BelBucketId getBelBucketForBel(BelId bel) const

Returns the bucket for a particular bel.

*BaseArch default: `getBelBucketForCellType(getBelType(bel))`*

### BelBucketId getBelBucketForCellType(IdString cell\_type) const

Returns the bel bucket for a particular cell type.

*BaseArch default: `getBelBucketByName(cell_type)`*

### BucketBelRangeT getBelsInBucket(BelBucketId bucket) const

Return the list of bels within a bucket.

*BaseArch default: the list of bels in the bucket created by calling `init_bel_buckets()`*

Placer Methods
--------------

### bool isValidBelForCellType(IdString cell\_type, BelId bel) const

Returns true if the given cell can be bound to the given bel.  This check
should be fast, compared with isBelLocationValid.  This check should always
return the same value regardless if other cells are placed within the fabric.

*BaseArch default: returns `cell_type == getBelType(bel)`*

### bool isBelLocationValid(BelId bel, bool explain_invalid = false) const

Returns true if a bel in the current configuration is legal (for example,
a flipflop's clock signal is correctly shared with all bels in a slice.)

If and only if `explain_invalid` is set to true, then a message using
`log_nonfatal_error` should be printed explaining why the placement is invalid
to the end user.

*BaseArch default: returns true*

### static const std::string defaultPlacer

Name of the default placement algorithm for the architecture, if
`--placer` isn't specified on the command line.

### static const std::vector\<std::string\> availablePlacers

Name of available placer algorithms for the architecture, used
to provide help for and validate `--placer`.

### static const std::string defaultRouter

Name of the default router algorithm for the architecture, if
`--router` isn't specified on the command line.

### static const std::vector\<std::string\> availableRouters

Name of available router algorithms for the architecture, used
to provide help for and validate `--router`.

Cluster Methods
---------------

### CellInfo *getClusterRootCell(ClusterId cluster) const

Gets the root cell of a cluster, which is used as a datum point when placing the cluster.

### BoundingBox getClusterBounds(ClusterId cluster) const

Gets an approximate bounding box of the cluster. This is intended for area allocation in the placer and is permitted to occasionally give incorrect estimates, for example due to irregularities in the fabric depending on cluster placement. `getClusterPlacement` should always be used to get exact locations.

### Loc getClusterOffset(const CellInfo \*cell) const

Gets the approximate offset of a cell within its cluster, relative to the root cell. This is intended for global placement usage and is permitted to occasionally give incorrect estimates, for example due to irregularities in the fabric depending on cluster placement. `getClusterPlacement` should always be used to get exact locations.

The returned x and y coordinates, when added to the root location of the cluster, should give an approximate location where `cell` will end up placed at.

### bool isClusterStrict(const CellInfo *cell) const

Returns `true` if the cell **must** be placed according to the cluster; for example typical carry chains, and dedicated IO routing. Returns `false` if the cell can be split from the cluster if placement desires, at the expense of a less optimal result (for example dedicated LUT-FF paths where general routing can also be used).

### bool getClusterPlacement(ClusterId cluster, BelId root\_bel, std::vector\<std::pair\<CellInfo \*, BelId\>\> &placement) const

Gets an exact placement of the cluster, with the root cell placed on or near `root_bel` (and always within the same tile). Returns false if no placement is viable, otherwise returns `true` and populates `placement` with a list of cells inside the cluster and bels they should be placed at.

This approach of allowing architectures to define cluster placements enables easier handling of irregular fabrics than requiring strict and constant x, y and z offsets.
