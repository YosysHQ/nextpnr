/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Lofty <dan.ravensloft@gmail.com>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#ifndef NEXTPNR_H
#error Include "arch.h" via "nextpnr.h" only.
#endif

#include "mistral/lib/cyclonev.h"

NEXTPNR_NAMESPACE_BEGIN

struct ArchArgs
{
    std::string device;
};

struct Arch : BaseCtx
{
    ArchArgs args;
    mistral::CycloneV* cyclonev;

    Arch(ArchArgs args);

    std::string getChipName() const { return std::string{"TODO: getChipName"}; }

    IdString archId() const { return id("cyclonev"); }
    ArchArgs archArgs() const { return args; }
    IdString archArgsToId(ArchArgs args) const { return id("TODO: archArgsToId"); }

    // -------------------------------------------------

    int getGridDimX() const { return cyclonev->get_tile_sx(); }
    int getGridDimY() const { return cyclonev->get_tile_sy(); }
    int getTileBelDimZ(int x, int y) const; // arch.cc
    int getTilePipDimZ(int x, int y) const { return 1; }

    // -------------------------------------------------

    BelId getBelByName(IdString name) const; // arch.cc
    IdString getBelName(BelId bel) const; // arch.cc
    uint32_t getBelChecksum(BelId bel) const { return (bel.pos << 16) | bel.z; }
    void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength);
    void unbindBel(BelId bel);
    bool checkBelAvail(BelId bel) const;
    CellInfo *getBoundBelCell(BelId bel) const;
    CellInfo *getConflictingBelCell(BelId bel) const;
    const std::vector<BelId> &getBels() const;
    Loc getBelLocation(BelId bel) const;
    BelId getBelByLocation(Loc loc) const;
    const std::vector<BelId> &getBelsByTile(int x, int y) const;
    bool getBelGlobalBuf(BelId bel) const;
    IdString getBelType(BelId bel) const;
    std::vector<std::pair<IdString, std::string>> getBelAttrs(BelId bel) const;
    WireId getBelPinWire(BelId bel, IdString pin) const;
    PortType getBelPinType(BelId bel, IdString pin) const;
    std::vector<IdString> getBelPins(BelId bel) const;
    bool isBelLocked(BelId bel) const;

    // -------------------------------------------------

    WireId getWireByName(IdString name) const;
    IdString getWireName(WireId wire) const;
    IdString getWireType(WireId wire) const;
    std::vector<std::pair<IdString, std::string>> getWireAttrs(WireId wire) const;
    uint32_t getWireChecksum(WireId wire) const;
    void bindWire(WireId wire, NetInfo *net, PlaceStrength strength);
    void unbindWire(WireId wire);
    bool checkWireAvail(WireId wire) const;
    NetInfo *getBoundWireNet(WireId wire) const;
    WireId getConflictingWireWire(WireId wire) const;
    NetInfo *getConflictingWireNet(WireId wire) const;
    DelayInfo getWireDelay(WireId wire) const;
    const std::vector<BelPin> &getWireBelPins(WireId wire) const;
    const std::vector<WireId> &getWires() const;

    // -------------------------------------------------

    PipId getPipByName(IdString name) const;
    void bindPip(PipId pip, NetInfo *net, PlaceStrength strength);
    void unbindPip(PipId pip);
    bool checkPipAvail(PipId pip) const;
    NetInfo *getBoundPipNet(PipId pip) const;
    WireId getConflictingPipWire(PipId pip) const;
    NetInfo *getConflictingPipNet(PipId pip) const;
    const std::vector<PipId> &getPips() const;
    Loc getPipLocation(PipId pip) const;
    IdString getPipName(PipId pip) const;
    IdString getPipType(PipId pip) const;
    std::vector<std::pair<IdString, std::string>> getPipAttrs(PipId pip) const;
    uint32_t getPipChecksum(PipId pip) const;
    WireId getPipSrcWire(PipId pip) const;
    WireId getPipDstWire(PipId pip) const;
    DelayInfo getPipDelay(PipId pip) const;
    const std::vector<PipId> &getPipsDownhill(WireId wire) const;
    const std::vector<PipId> &getPipsUphill(WireId wire) const;
    const std::vector<PipId> &getWireAliases(WireId wire) const;
    BelId getPackagePinBel(const std::string &pin) const;
    std::string getBelPackagePin(BelId bel) const;

    // -------------------------------------------------

    GroupId getGroupByName(IdString name) const;
    IdString getGroupName(GroupId group) const;
    std::vector<GroupId> getGroups() const;
    std::vector<BelId> getGroupBels(GroupId group) const;
    std::vector<WireId> getGroupWires(GroupId group) const;
    std::vector<PipId> getGroupPips(GroupId group) const;
    std::vector<GroupId> getGroupGroups(GroupId group) const;

    // -------------------------------------------------

    delay_t estimateDelay(WireId src, WireId dst) const;
    delay_t predictDelay(const NetInfo *net_info, const PortRef &sink) const;
    delay_t getDelayEpsilon() const;
    delay_t getRipupDelayPenalty() const;
    float getDelayNS(delay_t v) const;
    DelayInfo getDelayFromNS(float ns) const;
    uint32_t getDelayChecksum(delay_t v) const;
    bool getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const;

    ArcBounds getRouteBoundingBox(WireId src, WireId dst) const;

    // -------------------------------------------------

    bool pack();
    bool place();
    bool route();

    // -------------------------------------------------

    std::vector<GraphicElement> getDecalGraphics(DecalId decal) const;

    DecalXY getBelDecal(BelId bel) const;
    DecalXY getWireDecal(WireId wire) const;
    DecalXY getPipDecal(PipId pip) const;
    DecalXY getGroupDecal(GroupId group) const;

    // -------------------------------------------------

    // Get the delay through a cell from one port to another, returning false
    // if no path exists. This only considers combinational delays, as required by the Arch API
    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const;
    // getCellDelayInternal is similar to the above, but without false path checks and including clock to out delays
    // for internal arch use only
    bool getCellDelayInternal(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const;
    // Get the port class, also setting clockInfoCount to the number of TimingClockingInfos associated with a port
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const;
    // Get the TimingClockingInfo of a port
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const;
    // Return true if a port is a net
    bool isGlobalNet(const NetInfo *net) const;

    // -------------------------------------------------

    // Perform placement validity checks, returning false on failure (all
    // implemented in arch_place.cc)

    // Whether or not a given cell can be placed at a given Bel
    // This is not intended for Bel type checks, but finer-grained constraints
    // such as conflicting set/reset signals, etc
    bool isValidBelForCell(CellInfo *cell, BelId bel) const;

    // Return true whether all Bels at a given location are valid
    bool isBelLocationValid(BelId bel) const;

    // Helper function for above
    bool logicCellsCompatible(const CellInfo **it, const size_t size) const;

    // -------------------------------------------------
    // Assign architecure-specific arguments to nets and cells, which must be
    // called between packing or further
    // netlist modifications, and validity checks
    void assignArchInfo();
    void assignCellInfo(CellInfo *cell);

    // -------------------------------------------------
    BelPin getIOBSharingPLLPin(BelId pll, IdString pll_pin) const;

    int getDrivenGlobalNetwork(BelId bel) const;

    static const std::string defaultPlacer;
    static const std::vector<std::string> availablePlacers;
    static const std::string defaultRouter;
    static const std::vector<std::string> availableRouters;
};

NEXTPNR_NAMESPACE_END
