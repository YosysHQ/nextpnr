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
};

struct WireInfo;

struct PipInfo
{
    IdString name, type, bound_net;
    WireId srcWire, dstWire;
    DelayInfo delay;
    DecalXY decalxy;
};

struct WireInfo
{
    IdString name, type, bound_net;
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
    IdString name, type, bound_cell;
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

struct Arch : BaseCtx
{
    std::string chipName;

    std::unordered_map<IdString, WireInfo> wires;
    std::unordered_map<IdString, PipInfo> pips;
    std::unordered_map<IdString, BelInfo> bels;
    std::unordered_map<GroupId, GroupInfo> groups;

    std::vector<IdString> bel_ids, wire_ids, pip_ids;

    std::unordered_map<Loc, BelId> bel_by_loc;
    std::vector<std::vector<std::vector<BelId>>> bels_by_tile;

    std::unordered_map<DecalId, std::vector<GraphicElement>> decal_graphics;
    DecalXY frame_decalxy;

    int gridDimX, gridDimY;
    std::vector<std::vector<int>> tileDimZ;

    float grid_distance_to_delay;

    void addWire(IdString name, IdString type, int x, int y);
    void addPip(IdString name, IdString type, IdString srcWire, IdString dstWire, DelayInfo delay);
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
    void setFrameDecal(DecalXY decalxy);
    void setWireDecal(WireId wire, DecalXY decalxy);
    void setPipDecal(PipId pip, DecalXY decalxy);
    void setBelDecal(BelId bel, DecalXY decalxy);
    void setGroupDecal(GroupId group, DecalXY decalxy);

    // ---------------------------------------------------------------
    // Common Arch API. Every arch must provide the following methods.

    Arch(ArchArgs args);

    std::string getChipName() const { return chipName; }

    IdString archId() const { return id("generic"); }
    IdString archArgsToId(ArchArgs args) const { return id("none"); }

    IdString belTypeToId(BelType type) const { return type; }
    IdString portPinToId(PortPin type) const { return type; }

    BelType belTypeFromId(IdString id) const { return id; }
    PortPin portPinFromId(IdString id) const { return id; }

    int getGridDimX() const { return gridDimX; }
    int getGridDimY() const { return gridDimY; }
    int getTileDimZ(int x, int y) const { return tileDimZ[x][y]; }

    BelId getBelByName(IdString name) const;
    IdString getBelName(BelId bel) const;
    Loc getBelLocation(BelId bel) const;
    BelId getBelByLocation(Loc loc) const;
    const std::vector<BelId> &getBelsByTile(int x, int y) const;
    bool getBelGlobalBuf(BelId bel) const;
    uint32_t getBelChecksum(BelId bel) const;
    void bindBel(BelId bel, IdString cell, PlaceStrength strength);
    void unbindBel(BelId bel);
    bool checkBelAvail(BelId bel) const;
    IdString getBoundBelCell(BelId bel) const;
    IdString getConflictingBelCell(BelId bel) const;
    const std::vector<BelId> &getBels() const;
    BelType getBelType(BelId bel) const;
    WireId getBelPinWire(BelId bel, PortPin pin) const;
    PortType getBelPinType(BelId bel, PortPin pin) const;
    std::vector<PortPin> getBelPins(BelId bel) const;

    WireId getWireByName(IdString name) const;
    IdString getWireName(WireId wire) const;
    IdString getWireType(WireId wire) const;
    uint32_t getWireChecksum(WireId wire) const;
    void bindWire(WireId wire, IdString net, PlaceStrength strength);
    void unbindWire(WireId wire);
    bool checkWireAvail(WireId wire) const;
    IdString getBoundWireNet(WireId wire) const;
    IdString getConflictingWireNet(WireId wire) const;
    DelayInfo getWireDelay(WireId wire) const { return DelayInfo(); }
    const std::vector<WireId> &getWires() const;
    const std::vector<BelPin> &getWireBelPins(WireId wire) const;

    PipId getPipByName(IdString name) const;
    IdString getPipName(PipId pip) const;
    IdString getPipType(PipId pip) const;
    uint32_t getPipChecksum(PipId pip) const;
    void bindPip(PipId pip, IdString net, PlaceStrength strength);
    void unbindPip(PipId pip);
    bool checkPipAvail(PipId pip) const;
    IdString getBoundPipNet(PipId pip) const;
    IdString getConflictingPipNet(PipId pip) const;
    const std::vector<PipId> &getPips() const;
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
    delay_t getDelayEpsilon() const { return 0.01; }
    delay_t getRipupDelayPenalty() const { return 1.0; }
    float getDelayNS(delay_t v) const { return v; }
    uint32_t getDelayChecksum(delay_t v) const { return 0; }
    delay_t getBudgetOverride(NetInfo *net_info, int user_idx, delay_t budget) const;

    bool pack() { return true; }
    bool place();
    bool route();

    const std::vector<GraphicElement> &getDecalGraphics(DecalId decal) const;
    DecalXY getFrameDecal() const;
    DecalXY getBelDecal(BelId bel) const;
    DecalXY getWireDecal(WireId wire) const;
    DecalXY getPipDecal(PipId pip) const;
    DecalXY getGroupDecal(GroupId group) const;

    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const;
    IdString getPortClock(const CellInfo *cell, IdString port) const;
    bool isClockPort(const CellInfo *cell, IdString port) const;

    bool isValidBelForCell(CellInfo *cell, BelId bel) const;
    bool isBelLocationValid(BelId bel) const;
};

NEXTPNR_NAMESPACE_END
