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

#ifndef GENERIC_ARCH_H
#define GENERIC_ARCH_H

#include <map>

#include "arch_api.h"
#include "idstring.h"
#include "idstringlist.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

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
    IdStringList name;
    IdString type;
    std::map<IdString, std::string> attrs;
    NetInfo *bound_net;
    WireId srcWire, dstWire;
    delay_t delay;
    DecalXY decalxy;
    Loc loc;
};

struct WireInfo
{
    IdStringList name;
    IdString type;
    std::map<IdString, std::string> attrs;
    NetInfo *bound_net;
    std::vector<PipId> downhill, uphill;
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
    IdStringList name;
    IdString type;
    std::map<IdString, std::string> attrs;
    CellInfo *bound_cell;
    dict<IdString, PinInfo> pins;
    DecalXY decalxy;
    int x, y, z;
    bool gb;
    bool hidden;
};

struct GroupInfo
{
    IdStringList name;
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
    unsigned int hash() const { return mkhash(from.hash(), to.hash()); }
};

struct CellTiming
{
    dict<IdString, TimingPortClass> portClasses;
    dict<CellDelayKey, DelayQuad> combDelays;
    dict<IdString, std::vector<TimingClockingInfo>> clockingInfo;
};

struct ArchRanges
{
    using ArchArgsT = ArchArgs;
    // Bels
    using AllBelsRangeT = const std::vector<BelId> &;
    using TileBelsRangeT = const std::vector<BelId> &;
    using BelAttrsRangeT = const std::map<IdString, std::string> &;
    using BelPinsRangeT = std::vector<IdString>;
    using CellBelPinRangeT = const std::vector<IdString> &;
    // Wires
    using AllWiresRangeT = const std::vector<WireId> &;
    using DownhillPipRangeT = const std::vector<PipId> &;
    using UphillPipRangeT = const std::vector<PipId> &;
    using WireBelPinRangeT = const std::vector<BelPin> &;
    using WireAttrsRangeT = const std::map<IdString, std::string> &;
    // Pips
    using AllPipsRangeT = const std::vector<PipId> &;
    using PipAttrsRangeT = const std::map<IdString, std::string> &;
    // Groups
    using AllGroupsRangeT = std::vector<GroupId>;
    using GroupBelsRangeT = const std::vector<BelId> &;
    using GroupWiresRangeT = const std::vector<WireId> &;
    using GroupPipsRangeT = const std::vector<PipId> &;
    using GroupGroupsRangeT = const std::vector<GroupId> &;
    // Decals
    using DecalGfxRangeT = const std::vector<GraphicElement> &;
    // Placement validity
    using CellTypeRangeT = std::vector<IdString>;
    using BelBucketRangeT = std::vector<BelBucketId>;
    using BucketBelRangeT = std::vector<BelId>;
};

struct Arch : ArchAPI<ArchRanges>
{
    std::string chipName;

    dict<IdStringList, WireInfo> wires;
    dict<IdStringList, PipInfo> pips;
    dict<IdStringList, BelInfo> bels;
    dict<GroupId, GroupInfo> groups;

    // These functions include useful errors if not found
    WireInfo &wire_info(IdStringList wire);
    PipInfo &pip_info(IdStringList wire);
    BelInfo &bel_info(IdStringList wire);

    std::vector<IdStringList> bel_ids, wire_ids, pip_ids;

    dict<Loc, BelId> bel_by_loc;
    std::vector<std::vector<std::vector<BelId>>> bels_by_tile;

    dict<DecalId, std::vector<GraphicElement>> decal_graphics;

    int gridDimX, gridDimY;
    std::vector<std::vector<int>> tileBelDimZ;
    std::vector<std::vector<int>> tilePipDimZ;

    dict<IdString, CellTiming> cellTiming;

    void addWire(IdStringList name, IdString type, int x, int y);
    void addPip(IdStringList name, IdString type, IdStringList srcWire, IdStringList dstWire, delay_t delay, Loc loc);

    void addBel(IdStringList name, IdString type, Loc loc, bool gb, bool hidden);
    void addBelInput(IdStringList bel, IdString name, IdStringList wire);
    void addBelOutput(IdStringList bel, IdString name, IdStringList wire);
    void addBelInout(IdStringList bel, IdString name, IdStringList wire);

    void addGroupBel(IdStringList group, IdStringList bel);
    void addGroupWire(IdStringList group, IdStringList wire);
    void addGroupPip(IdStringList group, IdStringList pip);
    void addGroupGroup(IdStringList group, IdStringList grp);

    void addDecalGraphic(DecalId decal, const GraphicElement &graphic);
    void setWireDecal(WireId wire, DecalXY decalxy);
    void setPipDecal(PipId pip, DecalXY decalxy);
    void setBelDecal(BelId bel, DecalXY decalxy);
    void setGroupDecal(GroupId group, DecalXY decalxy);

    void setWireAttr(IdStringList wire, IdString key, const std::string &value);
    void setPipAttr(IdStringList pip, IdString key, const std::string &value);
    void setBelAttr(IdStringList bel, IdString key, const std::string &value);

    void setLutK(int K);
    void setDelayScaling(double scale, double offset);

    void addCellTimingClock(IdString cell, IdString port);
    void addCellTimingDelay(IdString cell, IdString fromPort, IdString toPort, delay_t delay);
    void addCellTimingSetupHold(IdString cell, IdString port, IdString clock, delay_t setup, delay_t hold);
    void addCellTimingClockToOut(IdString cell, IdString port, IdString clock, delay_t clktoq);

    void clearCellBelPinMap(IdString cell, IdString cell_pin);
    void addCellBelPinMapping(IdString cell, IdString cell_pin, IdString bel_pin);

    // ---------------------------------------------------------------
    // Common Arch API. Every arch must provide the following methods.

    ArchArgs args;
    Arch(ArchArgs args);

    std::string getChipName() const override { return chipName; }

    IdString archId() const override { return id("generic"); }
    ArchArgs archArgs() const override { return args; }
    IdString archArgsToId(ArchArgs args) const override { return id("none"); }

    int getGridDimX() const override { return gridDimX; }
    int getGridDimY() const override { return gridDimY; }
    int getTileBelDimZ(int x, int y) const override { return tileBelDimZ[x][y]; }
    int getTilePipDimZ(int x, int y) const override { return tilePipDimZ[x][y]; }
    char getNameDelimiter() const override { return '/'; }

    BelId getBelByName(IdStringList name) const override;
    IdStringList getBelName(BelId bel) const override;
    Loc getBelLocation(BelId bel) const override;
    BelId getBelByLocation(Loc loc) const override;
    const std::vector<BelId> &getBelsByTile(int x, int y) const override;
    bool getBelGlobalBuf(BelId bel) const override;
    uint32_t getBelChecksum(BelId bel) const override;
    void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength) override;
    void unbindBel(BelId bel) override;
    bool checkBelAvail(BelId bel) const override;
    CellInfo *getBoundBelCell(BelId bel) const override;
    CellInfo *getConflictingBelCell(BelId bel) const override;
    const std::vector<BelId> &getBels() const override;
    IdString getBelType(BelId bel) const override;
    bool getBelHidden(BelId bel) const override;
    const std::map<IdString, std::string> &getBelAttrs(BelId bel) const override;
    WireId getBelPinWire(BelId bel, IdString pin) const override;
    PortType getBelPinType(BelId bel, IdString pin) const override;
    std::vector<IdString> getBelPins(BelId bel) const override;
    const std::vector<IdString> &getBelPinsForCellPin(const CellInfo *cell_info, IdString pin) const override;

    WireId getWireByName(IdStringList name) const override;
    IdStringList getWireName(WireId wire) const override;
    IdString getWireType(WireId wire) const override;
    const std::map<IdString, std::string> &getWireAttrs(WireId wire) const override;
    uint32_t getWireChecksum(WireId wire) const override;
    void bindWire(WireId wire, NetInfo *net, PlaceStrength strength) override;
    void unbindWire(WireId wire) override;
    bool checkWireAvail(WireId wire) const override;
    NetInfo *getBoundWireNet(WireId wire) const override;
    WireId getConflictingWireWire(WireId wire) const override { return wire; }
    NetInfo *getConflictingWireNet(WireId wire) const override;
    DelayQuad getWireDelay(WireId wire) const override { return DelayQuad(0); }
    const std::vector<WireId> &getWires() const override;
    const std::vector<BelPin> &getWireBelPins(WireId wire) const override;

    PipId getPipByName(IdStringList name) const override;
    IdStringList getPipName(PipId pip) const override;
    IdString getPipType(PipId pip) const override;
    const std::map<IdString, std::string> &getPipAttrs(PipId pip) const override;
    uint32_t getPipChecksum(PipId pip) const override;
    void bindPip(PipId pip, NetInfo *net, PlaceStrength strength) override;
    void unbindPip(PipId pip) override;
    bool checkPipAvail(PipId pip) const override;
    bool checkPipAvailForNet(PipId pip, NetInfo *net) const override;
    NetInfo *getBoundPipNet(PipId pip) const override;
    WireId getConflictingPipWire(PipId pip) const override;
    NetInfo *getConflictingPipNet(PipId pip) const override;
    const std::vector<PipId> &getPips() const override;
    Loc getPipLocation(PipId pip) const override;
    WireId getPipSrcWire(PipId pip) const override;
    WireId getPipDstWire(PipId pip) const override;
    DelayQuad getPipDelay(PipId pip) const override;
    const std::vector<PipId> &getPipsDownhill(WireId wire) const override;
    const std::vector<PipId> &getPipsUphill(WireId wire) const override;

    GroupId getGroupByName(IdStringList name) const override;
    IdStringList getGroupName(GroupId group) const override;
    std::vector<GroupId> getGroups() const override;
    const std::vector<BelId> &getGroupBels(GroupId group) const override;
    const std::vector<WireId> &getGroupWires(GroupId group) const override;
    const std::vector<PipId> &getGroupPips(GroupId group) const override;
    const std::vector<GroupId> &getGroupGroups(GroupId group) const override;

    delay_t estimateDelay(WireId src, WireId dst) const override;
    delay_t predictDelay(const NetInfo *net_info, const PortRef &sink) const override;
    delay_t getDelayEpsilon() const override { return 0.001; }
    delay_t getRipupDelayPenalty() const override { return 0.015; }
    float getDelayNS(delay_t v) const override { return v; }

    delay_t getDelayFromNS(float ns) const override { return ns; }

    uint32_t getDelayChecksum(delay_t v) const override { return 0; }
    bool getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const override;

    ArcBounds getRouteBoundingBox(WireId src, WireId dst) const override;

    bool pack() override;
    bool place() override;
    bool route() override;

    std::vector<IdString> getCellTypes() const override
    {
        pool<IdString> cell_types;
        for (auto bel : bels) {
            cell_types.insert(bel.second.type);
        }

        return std::vector<IdString>{cell_types.begin(), cell_types.end()};
    }

    std::vector<BelBucketId> getBelBuckets() const override { return getCellTypes(); }

    IdString getBelBucketName(BelBucketId bucket) const override { return bucket; }

    BelBucketId getBelBucketByName(IdString bucket) const override { return bucket; }

    BelBucketId getBelBucketForBel(BelId bel) const override { return getBelType(bel); }

    BelBucketId getBelBucketForCellType(IdString cell_type) const override { return cell_type; }

    std::vector<BelId> getBelsInBucket(BelBucketId bucket) const override
    {
        std::vector<BelId> bels;
        for (BelId bel : getBels()) {
            if (bucket == getBelBucketForBel(bel)) {
                bels.push_back(bel);
            }
        }
        return bels;
    }

    const std::vector<GraphicElement> &getDecalGraphics(DecalId decal) const override;
    DecalXY getBelDecal(BelId bel) const override;
    DecalXY getWireDecal(WireId wire) const override;
    DecalXY getPipDecal(PipId pip) const override;
    DecalXY getGroupDecal(GroupId group) const override;

    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const override;
    // Get the port class, also setting clockInfoCount to the number of TimingClockingInfos associated with a port
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const override;
    // Get the TimingClockingInfo of a port
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const override;

    bool isValidBelForCellType(IdString cell_type, BelId bel) const override { return cell_type == getBelType(bel); }
    bool isBelLocationValid(BelId bel) const override;

    // TODO
    CellInfo *getClusterRootCell(ClusterId cluster) const override { NPNR_ASSERT_FALSE("unimplemented"); }
    ArcBounds getClusterBounds(ClusterId cluster) const override { NPNR_ASSERT_FALSE("unimplemented"); }
    Loc getClusterOffset(const CellInfo *cell) const override { NPNR_ASSERT_FALSE("unimplemented"); }
    bool isClusterStrict(const CellInfo *cell) const override { NPNR_ASSERT_FALSE("unimplemented"); }
    bool getClusterPlacement(ClusterId cluster, BelId root_bel,
                             std::vector<std::pair<CellInfo *, BelId>> &placement) const override
    {
        NPNR_ASSERT_FALSE("unimplemented");
    }

    static const std::string defaultPlacer;
    static const std::vector<std::string> availablePlacers;
    static const std::string defaultRouter;
    static const std::vector<std::string> availableRouters;

    // ---------------------------------------------------------------
    // Internal usage
    void assignArchInfo() override;
    bool cellsCompatible(const CellInfo **cells, int count) const;
};

NEXTPNR_NAMESPACE_END

#endif /* GENERIC_ARCH_H */
