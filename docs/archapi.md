Each architecture must implement the following types and APIs.

The syntax `const_range<T>` is used to denote anything that has a `begin()` and `end()` method that return const forward iterators. This can be a `std::list<T>`, `std::vector<T>`, a (const) reference to those, or anything else that behaves in a similar way.

archdefs.h
==========

The architecture-specific `archdefs.h` must define the following types.

With the exception of `ArchNetInfo` and `ArchCellInfo`, the following types should be "lightweight" enough so that passing them by value is sensible.

### delay\_t

A scalar type that is used to  represent delays. May be an integer or float type.

### DelayInfo

A struct representing the delay across a timing arc. Must provide a `+` operator for getting the combined delay of two arcs, and the following methods to access concrete timings:

```
delay_t minRaiseDelay() const { return delay; }
delay_t maxRaiseDelay() const { return delay; }

delay_t minFallDelay() const { return delay; }
delay_t maxFallDelay() const { return delay; }

delay_t minDelay() const { return delay; }
delay_t maxDelay() const { return delay; }
```

### BelId

A type representing a bel name. `BelId()` must construct a unique null-value. Must provide `==`, `!=`, and `<` operators and a specialization for `std::hash<BelId>`.

### WireId

A type representing a wire name. `WireId()` must construct a unique null-value. Must provide `==`, `!=`, and `<` operators and a specialization for `std::hash<WireId>`.

### PipId

A type representing a pip name. `PipId()` must construct a unique null-value. Must provide `==`, `!=`, and `<` operators and a specialization for `std::hash<PipId>`.

### GroupId

A type representing a group name. `GroupId()` must construct a unique null-value. Must provide `==` and `!=` operators and a specialization for `std::hash<GroupId>`.

### DecalId

A type representing a reference to a graphical decal. `DecalId()` must construct a unique null-value. Must provide `==` and `!=` operators and a specialization for `std::hash<DecalId>`.

### ArchNetInfo

The global `NetInfo` type derives from this one. Can be used to add arch-specific data (caches of information derived from wire attributes, bound wires and pips, and other net state). Must be declared as empty struct if unused.

### ArchCellInfo

The global `CellInfo` type derives from this one. Can be used to add arch-specific data (caches of information derived from cell attributes and parameters, bound bel, and other cell state). Must be declared as empty struct if unused.

arch.h
======

Each architecture must provide their own implementation of the `Arch` struct in `arch.h`. `Arch` must derive from `BaseCtx` and must provide the following methods:

General Methods
---------------

### Arch(ArchArgs args)

Constructor. ArchArgs is a architecture-specific type (usually a struct also defined in `arch.h`).

### std::string getChipName() const

Return a string representation of the ArchArgs that was used to construct this object.

### int getGridDimX() const

Get grid X dimension. All bels and pips must have Y coordinates in the range `0 .. getGridDimX()-1` (inclusive).

### int getGridDimY() const

Get grid Y dimension. All bels and pips must have Y coordinates in the range `0 .. getGridDimY()-1` (inclusive).

### int getTileBelDimZ(int x, int y) const

Get Z dimension for the specified tile for bels. All bels with at specified X and Y coordinates must have a Z coordinate in the range `0 .. getTileDimZ(X,Y)-1` (inclusive).

### int getTilePipDimZ(int x, int y) const

Get Z dimension for the specified tile for pips. All pips with at specified X and Y coordinates must have a Z coordinate in the range `0 .. getTileDimZ(X,Y)-1` (inclusive).

Bel Methods
-----------

### BelId getBelByName(IdString name) const

Lookup a bel by its name.

### IdString getBelName(BelId bel) const

Get the name for a bel. (Bel names must be unique.)

### Loc getBelLocation(BelId bel) const

Get the X/Y/Z location of a given bel. Each bel must have a unique X/Y/Z location.

### BelId getBelByLocation(Loc loc) const

Lookup a bel by its X/Y/Z location.

### const\_range\<BelId\> getBelsByTile(int x, int y) const

Return a list of all bels at the give X/Y location.

### bool getBelGlobalBuf(BelId bel) const

Returns true if the given bel is a global buffer. A global buffer does not "pull in" other cells it drives to be close to the location of the global buffer.

### uint32\_t getBelChecksum(BelId bel) const

Return a (preferably unique) number that represents this bel. This is used in design state checksum calculations.

### void bindBel(BelId bel, CellInfo \*cell, PlaceStrength strength)

Bind a given bel to a given cell with the given strength.

This method must also update `cell->bel` and `cell->belStrength`.

### void unbindBel(BelId bel)

Unbind a bel.

This method must also update `CellInfo::bel` and `CellInfo::belStrength`.

### bool checkBelAvail(BelId bel) const

Returns true if the bel is available. A bel can be unavailable because it is bound, or because it is exclusive to some other resource that is bound.

### CellInfo \*getBoundBelCell(BelId bel) const

Return the cell the given bel is bound to, or nullptr if the bel is not bound.

### CellInfo \*getConflictingBelCell(BelId bel) const

If the bel is unavailable, and unbinding a single cell would make it available, then this method must return that cell.

### const\_range\<BelId\> getBels() const

Return a list of all bels on the device.

### IdString getBelType(BelId bel) const

Return the type of a given bel.

### const\_range\<std\:\:pair\<IdString, std::string\>\> getBelAttrs(BelId bel) const

Return the attributes for that bel. Bel attributes are only informal. They are displayed by the GUI but are otherwise
unused. An implementation may simply return an empty range.

### WireId getBelPinWire(BelId bel, IdString pin) const

Return the wire connected to the given bel pin.

### PortType getBelPinType(BelId bel, IdString pin) const

Return the type (input/output/inout) of the given bel pin.

### const\_range\<IdString\> getBelPins(BelId bel) const

Return a list of all pins on that bel.

Wire Methods
------------

### WireId getWireByName(IdString name) const

Lookup a wire by its name.

### IdString getWireName(WireId wire) const

Get the name for a wire. (Wire names must be unique.)

### IdString getWireType(WireId wire) const

Get the type of a wire. The wire type is purely informal and
isn't used by any of the core algorithms. Implementations may
simply return `IdString()`.

### const\_range\<std\:\:pair\<IdString, std::string\>\> getWireAttrs(WireId wire) const

Return the attributes for that wire. Wire attributes are only informal. They are displayed by the GUI but are otherwise
unused. An implementation may simply return an empty range.

### uint32\_t getWireChecksum(WireId wire) const

Return a (preferably unique) number that represents this wire. This is used in design state checksum calculations.

### void bindWire(WireId wire, NetInfo \*net, PlaceStrength strength)

Bind a wire to a net. This method must be used when binding a wire that is driven by a bel pin. Use `binPip()`
when binding a wire that is driven by a pip.

This method must also update `net->wires`.

### void unbindWire(WireId wire)

Unbind a wire. For wires that are driven by a pip, this will also unbind the driving pip.

This method must also update `NetInfo::wires`.

### bool checkWireAvail(WireId wire) const

Return true if the wire is available, i.e. can be bound to a net.

### NetInfo \*getBoundWireNet(WireId wire) const

Return the net a wire is bound to.

### WireId getConflictingWireWire(WireId wire) const

If this returns a non-WireId(), then unbinding that wire
will make the given wire available.

### NetInfo \*getConflictingWireNet(WireId wire) const

If this returns a non-nullptr, then unbinding that entire net
will make the given wire available.

### DelayInfo getWireDelay(WireId wire) const

Get the delay for a wire.

### const\_range\<WireId\> getWires() const

Get a list of all wires on the device.

### const\_range\<BelPin\> getWireBelPins(WireId wire) const

Get a list of all bel pins attached to a given wire.

Pip Methods
-----------

### PipId getPipByName(IdString name) const

Lookup a pip by its name.

### IdString getPipName(PipId pip) const

Get the name for a pip. (Pip names must be unique.)

### IdString getPipType(PipId pip) const

Get the type of a pip. Pip types are purely informal and
implementations may simply return `IdString()`.

### const\_range\<std\:\:pair\<IdString, std::string\>\> getPipAttrs(PipId pip) const

Return the attributes for that pip. Pip attributes are only informal. They are displayed by the GUI but are otherwise
unused. An implementation may simply return an empty range.

### Loc getPipLocation(PipId pip) const

Get the X/Y/Z location of a given pip. Pip locations do not need to be unique, and in most cases they aren't. So
for pips a X/Y/Z location refers to a group of pips, not an individual pip.

### uint32\_t getPipChecksum(PipId pip) const

Return a (preferably unique) number that represents this pip. This is used in design state checksum calculations.

### void bindPip(PipId pip, NetInfo \*net, PlaceStrength strength)

Bid a pip to a net. This also bind the destination wire of that pip.

This method must also update `net->wires`.

### void unbindPip(PipId pip)

Unbind a pip and the wire driven by that pip.

This method must also update `NetInfo::wires`.

### bool checkPipAvail(PipId pip) const

Returns true if the given pip is available to be bound to a net.

Users must also check if the pip destination wire is available
with `checkWireAvail(getPipDstWire(pip))` before binding the
pip to a net.

### NetInfo \*getBoundPipNet(PipId pip) const

Return the net this pip is bound to.

### WireId getConflictingPipWire(PipId pip) const

If this returns a non-WireId(), then unbinding that wire
will make the given pip available.

### NetInfo \*getConflictingPipNet(PipId pip) const

If this returns a non-nullptr, then unbinding that entire net
will make the given pip available.

### const\_range\<PipId\> getPips() const

Return a list of all pips on the device.

### WireId getPipSrcWire(PipId pip) const

Get the source wire for a pip.

### WireId getPipDstWire(PipId pip) const

Get the destination wire for a pip.

Bi-directional switches (transfer gates) are modelled using two
antiparallel pips.

### DelayInfo getPipDelay(PipId pip) const

Get the delay for a pip.

### const\_range\<PipId\> getPipsDownhill(WireId wire) const

Get all pips downhill of a wire, i.e. pips that use this wire as source wire.

### const\_range\<PipId\> getPipsUphill(WireId wire) const

Get all pips uphill of a wire, i.e. pips that use this wire as destination wire.

### const\_range\<PipId\> getWireAliases(WireId wire) const

Get all alias pips downhill of a wire.

There is no api for getting the alias pips uphill of a wire.

Alias pips come in antiparallel pairs if a signal can be injected on either
side of the alias pip.

Group Methods
-------------

### GroupId getGroupByName(IdString name) const

Lookup a group by its name.

### IdString getGroupName(GroupId group) const

Get the name for a group. (Group names must be unique.)

### const\_range\<GroupId\> getGroups() const

Get a list of all groups on the device.

### const\_range\<BelId\> getGroupBels(GroupId group) const

Get a list of all bels within a group.

### const\_range\<WireId\> getGroupWires(GroupId group) const

Get a list of all wires within a group.

### const\_range\<PipId\> getGroupPips(GroupId group) const

Get a list of all pips within a group.

### const\_range\<GroupId\> getGroupGroups(GroupId group) const

Get a list of all groups within a group.

Delay Methods
-------------

### delay\_t estimateDelay(WireId src, WireId dst) const

Return a rough estimate for the total `maxDelay()` delay from the given src wire to
the given dst wire.

This should return a low upper bound for the fastest route from `src` to `dst`.

Or in other words it should assume an otherwise unused chip (thus "fastest route").
But it only produces an estimate for that fastest route, not an exact 
result, and for that estimate it is considered more accaptable to return a
slightly too high result and it is considered less accaptable to return a
too low result (thus "low upper bound").

### delay\_t predictDelay(const NetInfo \*net\_info, const PortRef &sink) const

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

### DelayInfo getDelayFromNS(float v) const

Convert a real-world delay in nanoseconds to a DelayInfo with equal min/max rising/falling values.

### uint32\_t getDelayChecksum(delay\_t v) const

Convert a `delay_t` to an integer for checksum calculations.

### bool getBudgetOverride(const NetInfo \*net\_info, const PortRef &sink, delay\_t &budget) const

Overwrite or modify (in-place) the timing budget for a given arc. Returns a bool to indicate whether this was done.

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

### const\_range\<GraphicElement\> getDecalGraphics(DecalId decal) const

Return the graphic elements that make up a decal.

The same decal must always produce the same list. If the graphics for
a design element changes, that element must return another decal.

### DecalXY getBelDecal(BelId bel) const

Return the decal and X/Y position for the graphics representing a bel.

### DecalXY getWireDecal(WireId wire) const

Return the decal and X/Y position for the graphics representing a wire.

### DecalXY getPipDecal(PipId pip) const

Return the decal and X/Y position for the graphics representing a pip.

### DecalXY getGroupDecal(GroupId group) const

Return the decal and X/Y position for the graphics representing a group.

Cell Delay Methods
------------------

### bool getCellDelay(const CellInfo \*cell, IdString fromPort, IdString toPort, DelayInfo &delay) const

Returns the delay for the specified path through a cell in the `&delay` argument. The method returns
false if there is no timing relationship from `fromPort` to `toPort`.

### TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const

Return the _timing port class_ of a port. This can be a register or combinational input or output; clock input or
output; general startpoint or endpoint; or a port ignored for timing purposes. For register ports, clockInfoCount is set
to the number of associated _clock edges_ that can be queried by getPortClockingInfo.

### TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const

Return the _clocking info_ (including port name of clock, clock polarity and setup/hold/clock-to-out times) of a
port. Where ports have more than one clock edge associated with them (such as DDR outputs), `index` can be used to obtain
information for all edges. `index` must be in [0, clockInfoCount), behaviour is undefined otherwise.

Placer Methods
--------------

### bool isValidBelForCell(CellInfo \*cell, BelId bel) const

Returns true if the given cell can be bound to the given bel, considering
other bound resources. For example, this can be used if there is only
a certain number of different clock signals allowed for a group of bels.

### bool isBelLocationValid(BelId bel) const

Returns true if a bell in the current configuration is valid, i.e. if
`isValidBelForCell()` would return true for the current mapping.
