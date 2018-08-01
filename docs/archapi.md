Each architecture must implement the following types and APIs.

The syntax `const_range<T>` is used to denote anything that has a `begin()` and `end()` method that return const forward iterators. This can be a `std::list<T>`, `std::vector<T>`, a (const) reference to those, or anything else that behaves in a similar way.

archdefs.h
==========

The architecture-specific `archdefs.h` must define the following types.

With the exception of `ArchNetInfo` and `ArchCellInfo`, the following types should be "lightweight" enough so that passing them by value is sensible.

### delay_t

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

### BelType

A type representing a bel type name. `BelType()` must construct a unique null-value. Must provide `==` and `!=` operators and a specialization for `std::hash<BelType>`.

### PortPin

A type representing a port or pin name. `PortPin()` must construct a unique null-value. Must provide `==` and `!=` operators and a specialization for `std::hash<PortPin>`.

### BelId

A type representing a bel name. `BelId()` must construct a unique null-value. Must provide `==` and `!=` operators and a specialization for `std::hash<BelId>`.

### WireId

A type representing a wire name. `WireId()` must construct a unique null-value. Must provide `==` and `!=` operators and a specialization for `std::hash<WireId>`.

### PipId

A type representing a pip name. `PipId()` must construct a unique null-value. Must provide `==` and `!=` operators and a specialization for `std::hash<PipId>`.

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

### IdString belTypeToId(BelType type) const

Convert a `BelType` to an `IdString`.

### IdString portPinToId(PortPin type) const

Convert a `PortPin` to an `IdString`.

### BelType belTypeFromId(IdString id) const

Convert `IdString` to `BelType`.

### PortPin portPinFromId(IdString id) const

Convert `IdString` to `PortPin`.

### int getGridDimX() const

Get grid X dimension. All bels must have Y coordinates in the range `0 .. getGridDimX()-1` (inclusive).

### int getGridDimY() const

Get grid Y dimension. All bels must have Y coordinates in the range `0 .. getGridDimY()-1` (inclusive).

### int getTileDimZ(int x, int y) const

Get Z dimension for the specified tile. All bels with the specified X and Y coordinates must have a Z coordinate in the range `0 .. getTileDimZ(X,Y)-1` (inclusive).

Bel Methods
-----------

### BelId getBelByName(IdString name) const

Lookup a bel by its name.

### IdString getBelName(BelId bel) const

Get the name for a bel. (Bel names must be unique.)

### Loc getBelLocation(BelId bel) const

Get the X/Y/Z location of a given bel.

### BelId getBelByLocation(Loc loc) const

Lookup a bel by its X/Y/Z location.

### const_range\<BelId\> getBelsByTile(int x, int y) const

Return a list of all bels at the give X/Y location.

### bool getBelGlobalBuf(BelId bel) const

Returns true if the given bel is a global buffer. A global buffer does not "pull in" other cells it drives to be close to the location of the global buffer.

### uint32_t getBelChecksum(BelId bel) const

Return a checksum for the state of a bel. (Used to calculate the design checksum.)

### void bindBel(BelId bel, IdString cell, PlaceStrength strength)

Bind a given bel to a given cell with the given strength.

### void unbindBel(BelId bel)

Unbind a bel.

### bool checkBelAvail(BelId bel) const

Returns true if the bel is available. A bel can be unavailable because it is bound, or because it is exclusive to some other resource that is bound.

### IdString getBoundBelCell(BelId bel) const

Return the cell the given bel is bound to, or `IdString()` if the bel is not bound.

### IdString getConflictingBelCell(BelId bel) const

If the bel is unavailable, and unbinding a single cell would make it available, then this method must return the name of that cell.

### const_range\<BelId\> getBels() const

Return a list of all bels on the device.

### BelType getBelType(BelId bel) const

Return the type of a given bel.

### WireId getBelPinWire(BelId bel, PortPin pin) const

Return the wire connected to the given bel pin.

### PortType getBelPinType(BelId bel, PortPin pin) const

Return the type (input/output/inout) of the given bel pin.

### const_range\<PortPin\> getBelPins(BelId bel) const

Return a list of all pins on that bel.

Wire Methods
------------

```
WireId getWireByName(IdString name) const
IdString getWireName(WireId wire) const
IdString getWireType(WireId wire) const
uint32_t getWireChecksum(WireId wire) const
void bindWire(WireId wire, IdString net, PlaceStrength strength)
void unbindWire(WireId wire)
bool checkWireAvail(WireId wire) const
IdString getBoundWireNet(WireId wire) const
IdString getConflictingWireNet(WireId wire) const
DelayInfo getWireDelay(WireId wire) const
const_range<WireId> getWires() const
const_range<BelPin> getWireBelPins(WireId wire) const
```

Pip Methods
-----------

```
PipId getPipByName(IdString name) const
IdString getPipName(PipId pip) const
IdString getPipType(PipId pip) const
uint32_t getPipChecksum(PipId pip) const
void bindPip(PipId pip, IdString net, PlaceStrength strength)
void unbindPip(PipId pip)
bool checkPipAvail(PipId pip) const
IdString getBoundPipNet(PipId pip) const
IdString getConflictingPipNet(PipId pip) const
const_range<PipId> getPips() const
WireId getPipSrcWire(PipId pip) const
WireId getPipDstWire(PipId pip) const
DelayInfo getPipDelay(PipId pip) const
const_range<PipId> getPipsDownhill(WireId wire) const
const_range<PipId> getPipsUphill(WireId wire) const
const_range<PipId> getWireAliases(WireId wire) const
```

Group Methods
-------------

```
GroupId getGroupByName(IdString name) const
IdString getGroupName(GroupId group) const
const_range<GroupId> getGroups() const
const_range<BelId> getGroupBels(GroupId group) const
const_range<WireId> getGroupWires(GroupId group) const
const_range<PipId> getGroupPips(GroupId group) const
const_range<GroupId> getGroupGroups(GroupId group) const
```

Delay Methods
-------------

```
delay_t estimateDelay(WireId src, WireId dst) const
delay_t getDelayEpsilon() const
delay_t getRipupDelayPenalty() const
float getDelayNS(delay_t v) const
uint32_t getDelayChecksum(delay_t v) const
```

Flow Methods
------------

```
bool pack()
bool place()
bool route()
```

Graphics Methods
----------------

```
const_range<GraphicElement> getDecalGraphics(DecalId decal) const
DecalXY getFrameDecal() const
DecalXY getBelDecal(BelId bel) const
DecalXY getWireDecal(WireId wire) const
DecalXY getPipDecal(PipId pip) const
DecalXY getGroupDecal(GroupId group) const
```

Cell Delay Methods
------------------

```
bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const
IdString getPortClock(const CellInfo *cell, IdString port) const
bool isClockPort(const CellInfo *cell, IdString port) const
```

Placer Methods
--------------

```
bool isValidBelForCell(CellInfo *cell, BelId bel) const
bool isBelLocationValid(BelId bel) const
```
