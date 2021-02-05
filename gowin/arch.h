/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Wolf <claire@symbioticeda.com>
 *  Copyright (C) 2020  Pepijn de Vos <pepijn@symbioticeda.com>
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

template <typename T> struct RelPtr
{
    int32_t offset;

    // void set(const T *ptr) {
    //     offset = reinterpret_cast<const char*>(ptr) -
    //              reinterpret_cast<const char*>(this);
    // }

    const T *get() const { return reinterpret_cast<const T *>(reinterpret_cast<const char *>(this) + offset); }

    T *get_mut() const
    {
        return const_cast<T *>(reinterpret_cast<const T *>(reinterpret_cast<const char *>(this) + offset));
    }

    const T &operator[](size_t index) const { return get()[index]; }

    const T &operator*() const { return *(get()); }

    const T *operator->() const { return get(); }

    RelPtr(const RelPtr &) = delete;
    RelPtr &operator=(const RelPtr &) = delete;
};

NPNR_PACKED_STRUCT(struct PairPOD {
    uint16_t dest_id;
    uint16_t src_id;
});

NPNR_PACKED_STRUCT(struct BelsPOD {
    uint16_t type_id;
    uint16_t num_ports;
    RelPtr<PairPOD> ports;
});

NPNR_PACKED_STRUCT(struct TilePOD /*TidePOD*/ {
    uint32_t num_bels;
    RelPtr<BelsPOD> bels;
    uint32_t num_pips;
    RelPtr<PairPOD> pips;
    uint32_t num_clock_pips;
    RelPtr<PairPOD> clock_pips;
    uint32_t num_aliases;
    RelPtr<PairPOD> aliases;
});

NPNR_PACKED_STRUCT(struct GlobalAliasPOD {
    uint16_t dest_row;
    uint16_t dest_col;
    uint16_t dest_id;
    uint16_t src_row;
    uint16_t src_col;
    uint16_t src_id;
});

NPNR_PACKED_STRUCT(struct TimingPOD {
    uint32_t name_id;
    // input, output
    uint32_t ff;
    uint32_t fr;
    uint32_t rf;
    uint32_t rr;
});

NPNR_PACKED_STRUCT(struct TimingGroupPOD {
    uint32_t name_id;
    uint32_t num_timings;
    RelPtr<TimingPOD> timings;
});

NPNR_PACKED_STRUCT(struct TimingGroupsPOD {
    TimingGroupPOD lut;
    TimingGroupPOD alu;
    TimingGroupPOD sram;
    TimingGroupPOD dff;
    // TimingGroupPOD dl;
    // TimingGroupPOD iddroddr;
    // TimingGroupPOD pll;
    // TimingGroupPOD dll;
    TimingGroupPOD bram;
    // TimingGroupPOD dsp;
    TimingGroupPOD fanout;
    TimingGroupPOD glbsrc;
    TimingGroupPOD hclk;
    TimingGroupPOD iodelay;
    // TimingGroupPOD io;
    // TimingGroupPOD iregoreg;
    TimingGroupPOD wire;
});

NPNR_PACKED_STRUCT(struct TimingClassPOD {
    uint32_t name_id;
    uint32_t num_groups;
    RelPtr<TimingGroupsPOD> groups;
});

NPNR_PACKED_STRUCT(struct PackagePOD {
    uint32_t name_id;
    uint32_t num_pins;
    RelPtr<PairPOD> pins;
});

NPNR_PACKED_STRUCT(struct VariantPOD {
    uint32_t name_id;
    uint32_t num_packages;
    RelPtr<PackagePOD> packages;
});

NPNR_PACKED_STRUCT(struct DatabasePOD {
    RelPtr<char> family;
    uint32_t version;
    uint16_t rows;
    uint16_t cols;
    RelPtr<RelPtr<TilePOD>> grid;
    uint32_t num_aliases;
    RelPtr<GlobalAliasPOD> aliases;
    uint32_t num_speeds;
    RelPtr<TimingClassPOD> speeds;
    uint32_t num_variants;
    RelPtr<VariantPOD> variants;
    uint16_t num_constids;
    uint16_t num_ids;
    RelPtr<RelPtr<char>> id_strs;
});

struct ArchArgs
{
    std::string device;
    std::string family;
    std::string speed;
    std::string package;
    // y = mx + c relationship between distance and delay for interconnect
    // delay estimates
    double delayScale = 0.4, delayOffset = 0.4;
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

struct ArchRanges
{
    using ArchArgsType = ArchArgs;
    // Bels
    using AllBelsRange = const std::vector<BelId> &;
    using TileBelsRange = const std::vector<BelId> &;
    using BelAttrsRange = const std::map<IdString, std::string> &;
    using BelPinsRange = std::vector<IdString>;
    // Wires
    using AllWiresRange = const std::vector<WireId> &;
    using DownhillPipRange = const std::vector<PipId> &;
    using UphillPipRange = const std::vector<PipId> &;
    using WireBelPinRange = const std::vector<BelPin> &;
    using WireAttrsRange = const std::map<IdString, std::string> &;
    // Pips
    using AllPipsRange = const std::vector<PipId> &;
    using PipAttrsRange = const std::map<IdString, std::string> &;
    // Groups
    using AllGroupsRange = std::vector<GroupId>;
    using GroupBelsRange = const std::vector<BelId> &;
    using GroupWiresRange = const std::vector<WireId> &;
    using GroupPipsRange = const std::vector<PipId> &;
    using GroupGroupsRange = const std::vector<GroupId> &;
    // Decals
    using DecalGfxRange = const std::vector<GraphicElement> &;
    // Placement validity
    using CellTypeRange = std::vector<IdString>;
    using BelBucketRange = std::vector<BelBucketId>;
    using BucketBelRange = std::vector<BelId>;
};

struct Arch : BaseArch<ArchRanges>
{
    std::string family;
    std::string device;
    const PackagePOD *package;
    const TimingGroupsPOD *speed;

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

    void setDelayScaling(double scale, double offset);

    void addCellTimingClock(IdString cell, IdString port);
    void addCellTimingDelay(IdString cell, IdString fromPort, IdString toPort, DelayInfo delay);
    void addCellTimingSetupHold(IdString cell, IdString port, IdString clock, DelayInfo setup, DelayInfo hold);
    void addCellTimingClockToOut(IdString cell, IdString port, IdString clock, DelayInfo clktoq);

    IdString wireToGlobal(int &row, int &col, const DatabasePOD *db, IdString &wire);
    DelayInfo getWireTypeDelay(IdString wire);
    void read_cst(std::istream &in);

    // ---------------------------------------------------------------
    // Common Arch API. Every arch must provide the following methods.

    ArchArgs args;
    Arch(ArchArgs args);

    std::string getChipName() const override { return device; }

    ArchArgs archArgs() const override { return args; }
    IdString archArgsToId(ArchArgs args) const override { return id("none"); }

    int getGridDimX() const override { return gridDimX; }
    int getGridDimY() const override { return gridDimY; }
    int getTileBelDimZ(int x, int y) const { return tileBelDimZ[x][y]; }
    int getTilePipDimZ(int x, int y) const { return tilePipDimZ[x][y]; }
    char getNameDelimiter() const
    {
        return ' '; /* use a non-existent delimiter as we aren't using IdStringLists yet */
    }

    BelId getBelByName(IdStringList name) const override;
    IdStringList getBelName(BelId bel) const override;
    Loc getBelLocation(BelId bel) const override;
    BelId getBelByLocation(Loc loc) const override;
    const std::vector<BelId> &getBelsByTile(int x, int y) const override;
    bool getBelGlobalBuf(BelId bel) const override;
    void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength) override;
    void unbindBel(BelId bel) override;
    bool checkBelAvail(BelId bel) const override;
    CellInfo *getBoundBelCell(BelId bel) const override;
    CellInfo *getConflictingBelCell(BelId bel) const override;
    const std::vector<BelId> &getBels() const override;
    IdString getBelType(BelId bel) const override;
    const std::map<IdString, std::string> &getBelAttrs(BelId bel) const override;
    WireId getBelPinWire(BelId bel, IdString pin) const override;
    PortType getBelPinType(BelId bel, IdString pin) const override;
    std::vector<IdString> getBelPins(BelId bel) const override;

    WireId getWireByName(IdStringList name) const override;
    IdStringList getWireName(WireId wire) const override;
    IdString getWireType(WireId wire) const override;
    const std::map<IdString, std::string> &getWireAttrs(WireId wire) const override;
    void bindWire(WireId wire, NetInfo *net, PlaceStrength strength) override;
    void unbindWire(WireId wire) override;
    bool checkWireAvail(WireId wire) const override;
    NetInfo *getBoundWireNet(WireId wire) const override;
    WireId getConflictingWireWire(WireId wire) const override { return wire; }
    NetInfo *getConflictingWireNet(WireId wire) const override;
    DelayInfo getWireDelay(WireId wire) const override { return DelayInfo(); }
    const std::vector<WireId> &getWires() const override;
    const std::vector<BelPin> &getWireBelPins(WireId wire) const override;

    PipId getPipByName(IdStringList name) const override;
    IdStringList getPipName(PipId pip) const override;
    IdString getPipType(PipId pip) const override;
    const std::map<IdString, std::string> &getPipAttrs(PipId pip) const override;
    void bindPip(PipId pip, NetInfo *net, PlaceStrength strength) override;
    void unbindPip(PipId pip) override;
    bool checkPipAvail(PipId pip) const override;
    NetInfo *getBoundPipNet(PipId pip) const override;
    WireId getConflictingPipWire(PipId pip) const override;
    NetInfo *getConflictingPipNet(PipId pip) const override;
    const std::vector<PipId> &getPips() const override;
    Loc getPipLocation(PipId pip) const override;
    WireId getPipSrcWire(PipId pip) const override;
    WireId getPipDstWire(PipId pip) const override;
    DelayInfo getPipDelay(PipId pip) const override;
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
    delay_t getDelayEpsilon() const override { return 0.01; }
    delay_t getRipupDelayPenalty() const override { return 0.4; }
    float getDelayNS(delay_t v) const override { return v; }

    DelayInfo getDelayFromNS(float ns) const override
    {
        DelayInfo del;
        del.maxRaise = ns;
        del.maxFall = ns;
        del.minRaise = ns;
        del.minFall = ns;
        return del;
    }

    uint32_t getDelayChecksum(delay_t v) const override { return 0; }
    bool getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const override;

    ArcBounds getRouteBoundingBox(WireId src, WireId dst) const override;

    bool pack() override;
    bool place() override;
    bool route() override;

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

    std::vector<IdString> cell_types;
};

NEXTPNR_NAMESPACE_END
