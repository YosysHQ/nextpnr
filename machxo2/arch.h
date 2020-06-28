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

/**** Everything in this section must be kept in sync with chipdb.py ****/

template <typename T> struct RelPtr
{
    int32_t offset;

    // void set(const T *ptr) {
    //     offset = reinterpret_cast<const char*>(ptr) -
    //              reinterpret_cast<const char*>(this);
    // }

    const T *get() const { return reinterpret_cast<const T *>(reinterpret_cast<const char *>(this) + offset); }

    const T &operator[](size_t index) const { return get()[index]; }

    const T &operator*() const { return *(get()); }

    const T *operator->() const { return get(); }
};


NPNR_PACKED_STRUCT(struct ChipInfoPOD {
    int32_t stub;
});

/************************ End of chipdb section. ************************/

struct ArchArgs
{
    enum ArchArgsTypes
    {
        NONE,
        LCMXO2_256HC,
        LCMXO2_640HC,
        LCMXO2_1200HC,
        LCMXO2_2000HC,
        LCMXO2_4000HC,
        LCMXO2_7000HC,
    } type = NONE;
    std::string package;
    enum SpeedGrade
    {
        SPEED_1 = 0,
        SPEED_2,
        SPEED_3,
        SPEED_4,
        SPEED_5,
        SPEED_6,
    } speed = SPEED_4;
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

    // Placeholders to be removed.
    std::unordered_map<Loc, BelId> bel_by_loc;
    std::vector<BelId> bel_id_dummy;
    std::vector<BelPin> bel_pin_dummy;
    std::vector<WireId> wire_id_dummy;
    std::vector<PipId> pip_id_dummy;
    std::vector<GroupId> group_id_dummy;
    std::vector<GraphicElement> graphic_element_dummy;
    std::map<IdString, std::string> attrs_dummy;

    // ---------------------------------------------------------------
    // Common Arch API. Every arch must provide the following methods.

    ArchArgs args;
    Arch(ArchArgs args);

    static bool isAvailable(ArchArgs::ArchArgsTypes chip);

    std::string getChipName() const { return chipName; }

    IdString archId() const { return id("machxo2"); }
    ArchArgs archArgs() const { return args; }
    IdString archArgsToId(ArchArgs args) const { return id("none"); }

    int getGridDimX() const { return 0; }
    int getGridDimY() const { return 0; }
    int getTileBelDimZ(int x, int y) const { return 0; }
    int getTilePipDimZ(int x, int y) const { return 0; }

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
