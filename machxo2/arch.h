/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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

NEXTPNR_NAMESPACE_BEGIN

struct ArchArgs
{
    // Number of LUT inputs
    int K = 4;
    // y = mx + c relationship between distance and delay for interconnect
    // delay estimates
    double delayScale = 0.1, delayOffset = 0;
};

struct WireInfo;

struct PipInfo
{
    IdString name, type;
    std::map<IdString, std::string> attrs;
    NetInfo *bound_net;
    WireId srcWire, dstWire;
    DelayInfo delay;
    DecalXY decalxy;
    Loc loc;
};

struct WireInfo
{
    IdString name, type;
    std::map<IdString, std::string> attrs;
    NetInfo *bound_net;
    std::vector<PipId> downhill, uphill, aliases;
    BelPin uphill_bel_pin;
    std::vector<BelPin> downhill_bel_pins;
    std::vector<BelPin> bel_pins;
    DecalXY decalxy;
    int x, y;
};

struct PinInfo
{
    IdString name;
    WireId wire;
    PortType type;
};

struct BelInfo
{
    IdString name, type;
    std::map<IdString, std::string> attrs;
    CellInfo *bound_cell;
    std::unordered_map<IdString, PinInfo> pins;
    DecalXY decalxy;
    int x, y, z;
    bool gb;
};

struct GroupInfo
{
    IdString name;
    std::vector<BelId> bels;
    std::vector<WireId> wires;
    std::vector<PipId> pips;
    std::vector<GroupId> groups;
    DecalXY decalxy;
};

struct CellDelayKey
{
    IdString from, to;
    inline bool operator==(const CellDelayKey &other) const { return from == other.from && to == other.to; }
};

NEXTPNR_NAMESPACE_END
namespace std {
template <> struct hash<NEXTPNR_NAMESPACE_PREFIX CellDelayKey>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX CellDelayKey &dk) const noexcept
    {
        std::size_t seed = std::hash<NEXTPNR_NAMESPACE_PREFIX IdString>()(dk.from);
        seed ^= std::hash<NEXTPNR_NAMESPACE_PREFIX IdString>()(dk.to) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};
} // namespace std
NEXTPNR_NAMESPACE_BEGIN

struct CellTiming
{
    std::unordered_map<IdString, TimingPortClass> portClasses;
    std::unordered_map<CellDelayKey, DelayInfo> combDelays;
    std::unordered_map<IdString, std::vector<TimingClockingInfo>> clockingInfo;
};

struct Arch : BaseCtx
{
    std::string chipName;

    std::unordered_map<IdString, WireInfo> wires;
    std::unordered_map<IdString, PipInfo> pips;
    std::unordered_map<IdString, BelInfo> bels;
    std::unordered_map<GroupId, GroupInfo> groups;

    // These functions include useful errors if not found
    WireInfo &wire_info(IdString wire);
    PipInfo &pip_info(IdString wire);
    BelInfo &bel_info(IdString wire);

    std::vector<IdString> bel_ids, wire_ids, pip_ids;

    std::unordered_map<Loc, BelId> bel_by_loc;
    std::vector<std::vector<std::vector<BelId>>> bels_by_tile;

    std::unordered_map<DecalId, std::vector<GraphicElement>> decal_graphics;

    int gridDimX, gridDimY;
    std::vector<std::vector<int>> tileBelDimZ;
    std::vector<std::vector<int>> tilePipDimZ;

    std::unordered_map<IdString, CellTiming> cellTiming;

    void addWire(IdString name, IdString type, int x, int y);
    void addPip(IdString name, IdString type, IdString srcWire, IdString dstWire, DelayInfo delay, Loc loc);
    void addAlias(IdString name, IdString type, IdString srcWire, IdString dstWire, DelayInfo delay);

    void addBel(IdString name, IdString type, Loc loc, bool gb);
    void addBelInput(IdString bel, IdString name, IdString wire);
    void addBelOutput(IdString bel, IdString name, IdString wire);
    void addBelInout(IdString bel, IdString name, IdString wire);

    void addGroupBel(IdString group, IdString bel);
    void addGroupWire(IdString group, IdString wire);
    void addGroupPip(IdString group, IdString pip);
    void addGroupGroup(IdString group, IdString grp);

    void addDecalGraphic(DecalId decal, const GraphicElement &graphic);
    void setWireDecal(WireId wire, DecalXY decalxy);
    void setPipDecal(PipId pip, DecalXY decalxy);
    void setBelDecal(BelId bel, DecalXY decalxy);
    void setGroupDecal(GroupId group, DecalXY decalxy);

    void setWireAttr(IdString wire, IdString key, const std::string &value);
    void setPipAttr(IdString pip, IdString key, const std::string &value);
    void setBelAttr(IdString bel, IdString key, const std::string &value);

    void setLutK(int K);
    void setDelayScaling(double scale, double offset);

    void addCellTimingClock(IdString cell, IdString port);
    void addCellTimingDelay(IdString cell, IdString fromPort, IdString toPort, DelayInfo delay);
    void addCellTimingSetupHold(IdString cell, IdString port, IdString clock, DelayInfo setup, DelayInfo hold);
    void addCellTimingClockToOut(IdString cell, IdString port, IdString clock, DelayInfo clktoq);

    // ---------------------------------------------------------------
    // Common Arch API. Every arch must provide the following methods.

    ArchArgs args;
    Arch(ArchArgs args);

    std::string getChipName() const { return chipName; }

    IdString archId() const { return id("generic"); }
    ArchArgs archArgs() const { return args; }
    IdString archArgsToId(ArchArgs args) const { return id("none"); }

    int getGridDimX() const { return gridDimX; }
    int getGridDimY() const { return gridDimY; }
    int getTileBelDimZ(int x, int y) const { return tileBelDimZ[x][y]; }
    int getTilePipDimZ(int x, int y) const { return tilePipDimZ[x][y]; }

    BelId getBelByName(IdString name) const;
    IdString getBelName(BelId bel) const;
    Loc getBelLocation(BelId bel) const;
    BelId getBelByLocation(Loc loc) const;
    const std::vector<BelId> &getBelsByTile(int x, int y) const;
    bool getBelGlobalBuf(BelId bel) const;
    uint32_t getBelChecksum(BelId bel) const;
    void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength);
    void unbindBel(BelId bel);
    bool checkBelAvail(BelId bel) const;
    CellInfo *getBoundBelCell(BelId bel) const;
    CellInfo *getConflictingBelCell(BelId bel) const;
    const std::vector<BelId> &getBels() const;
    IdString getBelType(BelId bel) const;
    const std::map<IdString, std::string> &getBelAttrs(BelId bel) const;
    WireId getBelPinWire(BelId bel, IdString pin) const;
    PortType getBelPinType(BelId bel, IdString pin) const;
    std::vector<IdString> getBelPins(BelId bel) const;

    WireId getWireByName(IdString name) const;
    IdString getWireName(WireId wire) const;
    IdString getWireType(WireId wire) const;
    const std::map<IdString, std::string> &getWireAttrs(WireId wire) const;
    uint32_t getWireChecksum(WireId wire) const;
    void bindWire(WireId wire, NetInfo *net, PlaceStrength strength);
    void unbindWire(WireId wire);
    bool checkWireAvail(WireId wire) const;
    NetInfo *getBoundWireNet(WireId wire) const;
    WireId getConflictingWireWire(WireId wire) const { return wire; }
    NetInfo *getConflictingWireNet(WireId wire) const;
    DelayInfo getWireDelay(WireId wire) const { return DelayInfo(); }
    const std::vector<WireId> &getWires() const;
    const std::vector<BelPin> &getWireBelPins(WireId wire) const;

    PipId getPipByName(IdString name) const;
    IdString getPipName(PipId pip) const;
    IdString getPipType(PipId pip) const;
    const std::map<IdString, std::string> &getPipAttrs(PipId pip) const;
    uint32_t getPipChecksum(PipId pip) const;
    void bindPip(PipId pip, NetInfo *net, PlaceStrength strength);
    void unbindPip(PipId pip);
    bool checkPipAvail(PipId pip) const;
    NetInfo *getBoundPipNet(PipId pip) const;
    WireId getConflictingPipWire(PipId pip) const;
    NetInfo *getConflictingPipNet(PipId pip) const;
    const std::vector<PipId> &getPips() const;
    Loc getPipLocation(PipId pip) const;
    WireId getPipSrcWire(PipId pip) const;
    WireId getPipDstWire(PipId pip) const;
    DelayInfo getPipDelay(PipId pip) const;
    const std::vector<PipId> &getPipsDownhill(WireId wire) const;
    const std::vector<PipId> &getPipsUphill(WireId wire) const;
    const std::vector<PipId> &getWireAliases(WireId wire) const;

    GroupId getGroupByName(IdString name) const;
    IdString getGroupName(GroupId group) const;
    std::vector<GroupId> getGroups() const;
    const std::vector<BelId> &getGroupBels(GroupId group) const;
    const std::vector<WireId> &getGroupWires(GroupId group) const;
    const std::vector<PipId> &getGroupPips(GroupId group) const;
    const std::vector<GroupId> &getGroupGroups(GroupId group) const;

    delay_t estimateDelay(WireId src, WireId dst) const;
    delay_t predictDelay(const NetInfo *net_info, const PortRef &sink) const;
    delay_t getDelayEpsilon() const { return 0.001; }
    delay_t getRipupDelayPenalty() const { return 0.015; }
    float getDelayNS(delay_t v) const { return v; }

    DelayInfo getDelayFromNS(float ns) const
    {
        DelayInfo del;
        del.delay = ns;
        return del;
    }

    uint32_t getDelayChecksum(delay_t v) const { return 0; }
    bool getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const;

    ArcBounds getRouteBoundingBox(WireId src, WireId dst) const;

    bool pack();
    bool place();
    bool route();

    const std::vector<GraphicElement> &getDecalGraphics(DecalId decal) const;
    DecalXY getBelDecal(BelId bel) const;
    DecalXY getWireDecal(WireId wire) const;
    DecalXY getPipDecal(PipId pip) const;
    DecalXY getGroupDecal(GroupId group) const;

    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const;
    // Get the port class, also setting clockInfoCount to the number of TimingClockingInfos associated with a port
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const;
    // Get the TimingClockingInfo of a port
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const;

    bool isValidBelForCell(CellInfo *cell, BelId bel) const;
    bool isBelLocationValid(BelId bel) const;

    static const std::string defaultPlacer;
    static const std::vector<std::string> availablePlacers;
    static const std::string defaultRouter;
    static const std::vector<std::string> availableRouters;

    // ---------------------------------------------------------------
    // Internal usage
    void assignArchInfo();
    bool cellsCompatible(const CellInfo **cells, int count) const;
};

NEXTPNR_NAMESPACE_END
