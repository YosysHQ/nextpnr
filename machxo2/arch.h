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

// FIXME: All "rel locs" are actually absolute, naming typo in facade_import.
// Does not affect runtime functionality.

NPNR_PACKED_STRUCT(struct BelWirePOD {
    LocationPOD rel_wire_loc;
    int32_t wire_index;
    int32_t port;
    int32_t dir; // FIXME: Corresponds to "type" in ECP5.
});

NPNR_PACKED_STRUCT(struct BelInfoPOD {
    RelPtr<char> name;
    int32_t type;
    int32_t z;
    int32_t num_bel_wires;
    RelPtr<BelWirePOD> bel_wires;
});

NPNR_PACKED_STRUCT(struct PipLocatorPOD {
    LocationPOD rel_loc;
    int32_t index;
});

NPNR_PACKED_STRUCT(struct BelPortPOD {
    LocationPOD rel_bel_loc;
    int32_t bel_index;
    int32_t port;
});

NPNR_PACKED_STRUCT(struct PipInfoPOD {
    LocationPOD src;
    LocationPOD dst;
    int32_t src_idx;
    int32_t dst_idx;
    int32_t timing_class;
    int16_t tile_type;
    int8_t pip_type;
    int8_t padding;
});

NPNR_PACKED_STRUCT(struct WireInfoPOD {
    RelPtr<char> name;
    int32_t tile_wire;
    int32_t num_uphill;
    int32_t num_downhill;
    RelPtr<PipLocatorPOD> pips_uphill;
    RelPtr<PipLocatorPOD> pips_downhill;
    int32_t num_bel_pins;
    RelPtr<BelPortPOD> bel_pins;
});

NPNR_PACKED_STRUCT(struct TileTypePOD {
    int32_t num_bels;
    int32_t num_wires;
    int32_t num_pips;
    RelPtr<BelInfoPOD> bel_data;
    RelPtr<WireInfoPOD> wire_data;
    RelPtr<PipInfoPOD> pips_data;
});

NPNR_PACKED_STRUCT(struct PackagePinPOD {
    RelPtr<char> name;
    LocationPOD abs_loc;
    int32_t bel_index;
});

NPNR_PACKED_STRUCT(struct PackageInfoPOD {
    RelPtr<char> name;
    int32_t num_pins;
    RelPtr<PackagePinPOD> pin_data;
});

NPNR_PACKED_STRUCT(struct PIOInfoPOD {
    LocationPOD abs_loc;
    int32_t bel_index;
    RelPtr<char> function_name;
    int16_t bank;
    int16_t dqsgroup;
});

NPNR_PACKED_STRUCT(struct TileNamePOD {
    RelPtr<char> name;
    int16_t type_idx;
    int16_t padding;
});

NPNR_PACKED_STRUCT(struct TileInfoPOD {
    int32_t num_tiles;
    RelPtr<TileNamePOD> tile_names;
});

NPNR_PACKED_STRUCT(struct ChipInfoPOD {
    int32_t width, height;
    int32_t num_tiles;
    int32_t num_packages, num_pios;
    int32_t const_id_count;
    RelPtr<TileTypePOD> tiles;
    RelPtr<RelPtr<char>> tiletype_names;
    RelPtr<PackageInfoPOD> package_info;
    RelPtr<PIOInfoPOD> pio_info;
    RelPtr<TileInfoPOD> tile_info;
});

/************************ End of chipdb section. ************************/

// Iterators
// Iterate over Bels across tiles.
struct BelIterator
{
    const ChipInfoPOD *chip;
    int cursor_index;
    int cursor_tile;

    BelIterator operator++()
    {
        cursor_index++;
        while (cursor_tile < chip->num_tiles && cursor_index >= chip->tiles[cursor_tile].num_bels) {
            cursor_index = 0;
            cursor_tile++;
        }
        return *this;
    }
    BelIterator operator++(int)
    {
        BelIterator prior(*this);
        ++(*this);
        return prior;
    }

    bool operator!=(const BelIterator &other) const
    {
        return cursor_index != other.cursor_index || cursor_tile != other.cursor_tile;
    }

    bool operator==(const BelIterator &other) const
    {
        return cursor_index == other.cursor_index && cursor_tile == other.cursor_tile;
    }

    BelId operator*() const
    {
        BelId ret;
        ret.location.x = cursor_tile % chip->width;
        ret.location.y = cursor_tile / chip->width;
        ret.index = cursor_index;
        return ret;
    }
};

struct BelRange
{
    BelIterator b, e;
    BelIterator begin() const { return b; }
    BelIterator end() const { return e; }
};

// Iterate over Downstream/Upstream Bels for a Wire.
struct BelPinIterator
{
    const BelPortPOD *ptr = nullptr;
    Location wire_loc;
    void operator++() { ptr++; }
    bool operator!=(const BelPinIterator &other) const { return ptr != other.ptr; }

    BelPin operator*() const
    {
        BelPin ret;
        ret.bel.index = ptr->bel_index;
        ret.bel.location = ptr->rel_bel_loc;
        ret.pin.index = ptr->port;
        return ret;
    }
};

struct BelPinRange
{
    BelPinIterator b, e;
    BelPinIterator begin() const { return b; }
    BelPinIterator end() const { return e; }
};

// Iterator over Wires across tiles.
struct WireIterator
{
    const ChipInfoPOD *chip;
    int cursor_index;
    int cursor_tile;

    WireIterator operator++()
    {
        cursor_index++;
        while (cursor_tile < chip->num_tiles && cursor_index >= chip->tiles[cursor_tile].num_wires) {
            cursor_index = 0;
            cursor_tile++;
        }
        return *this;
    }
    WireIterator operator++(int)
    {
        WireIterator prior(*this);
        ++(*this);
        return prior;
    }

    bool operator!=(const WireIterator &other) const
    {
        return cursor_index != other.cursor_index || cursor_tile != other.cursor_tile;
    }

    bool operator==(const WireIterator &other) const
    {
        return cursor_index == other.cursor_index && cursor_tile == other.cursor_tile;
    }

    WireId operator*() const
    {
        WireId ret;
        ret.location.x = cursor_tile % chip->width;
        ret.location.y = cursor_tile / chip->width;
        ret.index = cursor_index;
        return ret;
    }
};

struct WireRange
{
    WireIterator b, e;
    WireIterator begin() const { return b; }
    WireIterator end() const { return e; }
};

// Iterator over Pips across tiles.
struct AllPipIterator
{
    const ChipInfoPOD *chip;
    int cursor_index;
    int cursor_tile;

    AllPipIterator operator++()
    {
        cursor_index++;
        while (cursor_tile < chip->num_tiles && cursor_index >= chip->tiles[cursor_tile].num_pips) {
            cursor_index = 0;
            cursor_tile++;
        }
        return *this;
    }
    AllPipIterator operator++(int)
    {
        AllPipIterator prior(*this);
        ++(*this);
        return prior;
    }

    bool operator!=(const AllPipIterator &other) const
    {
        return cursor_index != other.cursor_index || cursor_tile != other.cursor_tile;
    }

    bool operator==(const AllPipIterator &other) const
    {
        return cursor_index == other.cursor_index && cursor_tile == other.cursor_tile;
    }

    PipId operator*() const
    {
        PipId ret;
        ret.location.x = cursor_tile % chip->width;
        ret.location.y = cursor_tile / chip->width;
        ret.index = cursor_index;
        return ret;
    }
};

struct AllPipRange
{
    AllPipIterator b, e;
    AllPipIterator begin() const { return b; }
    AllPipIterator end() const { return e; }
};

// Iterate over Downstream/Upstream Pips for a Wire.
struct PipIterator
{

    const PipLocatorPOD *cursor = nullptr;
    Location wire_loc;

    void operator++() { cursor++; }
    bool operator!=(const PipIterator &other) const { return cursor != other.cursor; }

    PipId operator*() const
    {
        PipId ret;
        ret.index = cursor->index;
        ret.location = cursor->rel_loc;
        return ret;
    }
};

struct PipRange
{
    PipIterator b, e;
    PipIterator begin() const { return b; }
    PipIterator end() const { return e; }
};

// -----------------------------------------------------------------------

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
    const ChipInfoPOD *chip_info;
    const PackageInfoPOD *package_info;

    std::vector<CellInfo *> bel_to_cell;
    std::unordered_map<WireId, NetInfo *> wire_to_net;
    std::unordered_map<PipId, NetInfo *> pip_to_net;

    mutable std::unordered_map<IdString, BelId> bel_by_name;
    mutable std::unordered_map<IdString, WireId> wire_by_name;
    mutable std::unordered_map<IdString, PipId> pip_by_name;

    // Placeholders to be removed.
    std::unordered_map<Loc, BelId> bel_by_loc;
    std::vector<BelId> bel_id_dummy;
    std::vector<BelPin> bel_pin_dummy;
    std::vector<WireId> wire_id_dummy;
    std::vector<PipId> pip_id_dummy;
    std::vector<GroupId> group_id_dummy;
    std::vector<GraphicElement> graphic_element_dummy;

    // Helpers
    template <typename Id> const TileTypePOD *tileInfo(Id &id) const
    {
        return &(chip_info->tiles[id.location.y * chip_info->width + id.location.x]);
    }

    int getBelFlatIndex(BelId bel) const
    {
        return (bel.location.y * chip_info->width + bel.location.x) * max_loc_bels + bel.index;
    }

    // ---------------------------------------------------------------
    // Common Arch API. Every arch must provide the following methods.

    // General
    ArchArgs args;
    Arch(ArchArgs args);

    static bool isAvailable(ArchArgs::ArchArgsTypes chip);

    std::string getChipName() const;

    IdString archId() const { return id("machxo2"); }
    ArchArgs archArgs() const { return args; }
    IdString archArgsToId(ArchArgs args) const;

    static const int max_loc_bels = 20;

    int getGridDimX() const { return chip_info->width; }
    int getGridDimY() const { return chip_info->height; }
    int getTileBelDimZ(int x, int y) const { return max_loc_bels; }
    // TODO: Make more precise? The CENTER MUX having config bits across
    // tiles can complicate this?
    int getTilePipDimZ(int x, int y) const { return 2; }

    // Bels
    BelId getBelByName(IdString name) const;

    IdString getBelName(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        std::stringstream name;
        name << "X" << bel.location.x << "/Y" << bel.location.y << "/" << tileInfo(bel)->bel_data[bel.index].name.get();
        return id(name.str());
    }

    Loc getBelLocation(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        Loc loc;
        loc.x = bel.location.x;
        loc.y = bel.location.y;
        loc.z = tileInfo(bel)->bel_data[bel.index].z;
        return loc;
    }

    BelId getBelByLocation(Loc loc) const;
    BelRange getBelsByTile(int x, int y) const;
    bool getBelGlobalBuf(BelId bel) const;

    uint32_t getBelChecksum(BelId bel) const
    {
        // FIXME- Copied from ECP5. Should be return val from getBelFlatIndex?
        return bel.index;
    }

    void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength)
    {
        NPNR_ASSERT(bel != BelId());
        int idx = getBelFlatIndex(bel);
        NPNR_ASSERT(bel_to_cell.at(idx) == nullptr);
        bel_to_cell[idx] = cell;
        cell->bel = bel;
        cell->belStrength = strength;
        refreshUiBel(bel);
    }

    void unbindBel(BelId bel)
    {
        NPNR_ASSERT(bel != BelId());
        int idx = getBelFlatIndex(bel);
        NPNR_ASSERT(bel_to_cell.at(idx) != nullptr);
        bel_to_cell[idx]->bel = BelId();
        bel_to_cell[idx]->belStrength = STRENGTH_NONE;
        bel_to_cell[idx] = nullptr;
        refreshUiBel(bel);
    }

    bool checkBelAvail(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        return bel_to_cell[getBelFlatIndex(bel)] == nullptr;
    }

    CellInfo *getBoundBelCell(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        return bel_to_cell[getBelFlatIndex(bel)];
    }

    CellInfo *getConflictingBelCell(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        return bel_to_cell[getBelFlatIndex(bel)];
    }

    BelRange getBels() const
    {
        BelRange range;
        range.b.cursor_tile = 0;
        range.b.cursor_index = -1;
        range.b.chip = chip_info;
        ++range.b; //-1 and then ++ deals with the case of no Bels in the first tile
        range.e.cursor_tile = chip_info->width * chip_info->height;
        range.e.cursor_index = 0;
        range.e.chip = chip_info;
        return range;
    }

    IdString getBelType(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        IdString id;
        id.index = tileInfo(bel)->bel_data[bel.index].type;
        return id;
    }

    std::vector<std::pair<IdString, std::string>> getBelAttrs(BelId) const
    {
        std::vector<std::pair<IdString, std::string>> ret;
        return ret;
    }

    WireId getBelPinWire(BelId bel, IdString pin) const;
    PortType getBelPinType(BelId bel, IdString pin) const;
    std::vector<IdString> getBelPins(BelId bel) const;

    // Package
    BelId getPackagePinBel(const std::string &pin) const;

    // Wires
    WireId getWireByName(IdString name) const;

    IdString getWireName(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        std::stringstream name;
        name << "X" << wire.location.x << "/Y" << wire.location.y << "/" << tileInfo(wire)->wire_data[wire.index].name.get();
        return id(name.str());
    }

    IdString getWireType(WireId wire) const { return IdString(); }

    std::vector<std::pair<IdString, std::string>> getWireAttrs(WireId) const
    {
        std::vector<std::pair<IdString, std::string>> ret;
        return ret;
    }

    uint32_t getWireChecksum(WireId wire) const { return wire.index; }

    void bindWire(WireId wire, NetInfo *net, PlaceStrength strength)
    {
        NPNR_ASSERT(wire != WireId());
        NPNR_ASSERT(wire_to_net[wire] == nullptr);
        wire_to_net[wire] = net;

        // Needs to be set; bindWires is meant for source wires attached
        // to a Bel.
        net->wires[wire].pip = PipId();
        net->wires[wire].strength = strength;
        refreshUiWire(wire);
    }

    void unbindWire(WireId wire)
    {
        NPNR_ASSERT(wire != WireId());
        NPNR_ASSERT(wire_to_net[wire] != nullptr);

        auto &net_wires = wire_to_net[wire]->wires;
        auto it = net_wires.find(wire);
        NPNR_ASSERT(it != net_wires.end());

        // If we have unbound a wire, then the upstream pip is no longer
        // used either.
        auto pip = it->second.pip;
        if (pip != PipId()) {
            // TODO: fanout
            // wire_fanout[getPipSrcWire(pip)]--;
            pip_to_net[pip] = nullptr;
        }

        net_wires.erase(it);
        wire_to_net[wire] = nullptr;
        refreshUiWire(wire);
    }

    bool checkWireAvail(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        return wire_to_net.find(wire) == wire_to_net.end() || wire_to_net.at(wire) == nullptr;
    }

    NetInfo *getBoundWireNet(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        if (wire_to_net.find(wire) == wire_to_net.end())
            return nullptr;
        else
            return wire_to_net.at(wire);
    }

    WireId getConflictingWireWire(WireId wire) const { return wire; }

    NetInfo *getConflictingWireNet(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        if (wire_to_net.find(wire) == wire_to_net.end())
            return nullptr;
        else
            return wire_to_net.at(wire);
    }

    DelayInfo getWireDelay(WireId wire) const { return DelayInfo(); }

    WireRange getWires() const
    {
        WireRange range;
        range.b.cursor_tile = 0;
        range.b.cursor_index = -1;
        range.b.chip = chip_info;
        ++range.b; //-1 and then ++ deals with the case of no wries in the first tile
        range.e.cursor_tile = chip_info->width * chip_info->height;
        range.e.cursor_index = 0;
        range.e.chip = chip_info;
        return range;
    }

    BelPinRange getWireBelPins(WireId wire) const
    {
        BelPinRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.ptr = tileInfo(wire)->wire_data[wire.index].bel_pins.get();
        range.b.wire_loc = wire.location;
        range.e.ptr = range.b.ptr + tileInfo(wire)->wire_data[wire.index].num_bel_pins;
        range.e.wire_loc = wire.location;
        return range;
    }

    // Pips
    PipId getPipByName(IdString name) const;
    IdString getPipName(PipId pip) const;

    IdString getPipType(PipId pip) const { return IdString(); }

    std::vector<std::pair<IdString, std::string>> getPipAttrs(PipId) const
    {
        std::vector<std::pair<IdString, std::string>> ret;
        return ret;
    }

    uint32_t getPipChecksum(PipId pip) const { return pip.index; }

    void bindPip(PipId pip, NetInfo *net, PlaceStrength strength)
    {
        NPNR_ASSERT(pip != PipId());
        NPNR_ASSERT(pip_to_net[pip] == nullptr);

        pip_to_net[pip] = net;
        // wire_fanout[getPipSrcWire(pip)]++;

        WireId dst;
        dst.index = tileInfo(pip)->pips_data[pip.index].dst_idx;
        dst.location = tileInfo(pip)->pips_data[pip.index].dst;
        NPNR_ASSERT(wire_to_net[dst] == nullptr);

        // Since NetInfo::wires holds info about uphill pips, bind info about
        // this pip to the downhill wire.
        wire_to_net[dst] = net;
        net->wires[dst].pip = pip;
        net->wires[dst].strength = strength;
    }

    void unbindPip(PipId pip)
    {
        NPNR_ASSERT(pip != PipId());
        NPNR_ASSERT(pip_to_net[pip] == nullptr);

        // wire_fanout[getPipSrcWire(pip)]--;

        WireId dst;
        dst.index = tileInfo(pip)->pips_data[pip.index].dst_idx;
        dst.location = tileInfo(pip)->pips_data[pip.index].dst;
        NPNR_ASSERT(wire_to_net[dst] != nullptr);

        // If we unbind a pip, then the downstream wire is no longer in use
        // either.
        wire_to_net[dst] = nullptr;
        pip_to_net[pip]->wires.erase(dst);
        pip_to_net[pip] = nullptr;
    }

    bool checkPipAvail(PipId pip) const
    {
        NPNR_ASSERT(pip != PipId());
        return pip_to_net.find(pip) == pip_to_net.end() || pip_to_net.at(pip) == nullptr;
    }

    NetInfo *getBoundPipNet(PipId pip) const
    {
        NPNR_ASSERT(pip != PipId());
        if (pip_to_net.find(pip) == pip_to_net.end())
            return nullptr;
        else
            return pip_to_net.at(pip);
    }

    WireId getConflictingPipWire(PipId pip) const { return WireId(); }

    NetInfo *getConflictingPipNet(PipId pip) const
    {
        NPNR_ASSERT(pip != PipId());
        if (pip_to_net.find(pip) == pip_to_net.end())
            return nullptr;
        else
            return pip_to_net.at(pip);
    }

    AllPipRange getPips() const
    {
        AllPipRange range;
        range.b.cursor_tile = 0;
        range.b.cursor_index = -1;
        range.b.chip = chip_info;
        ++range.b; //-1 and then ++ deals with the case of no Bels in the first tile
        range.e.cursor_tile = chip_info->width * chip_info->height;
        range.e.cursor_index = 0;
        range.e.chip = chip_info;
        return range;
    }

    Loc getPipLocation(PipId pip) const
    {
        Loc loc;
        loc.x = pip.location.x;
        loc.y = pip.location.y;

        // FIXME: Some Pip's config bits span across tiles. Will Z
        // be affected by this?
        loc.z = 0;
        return loc;
    }

    WireId getPipSrcWire(PipId pip) const
    {
        WireId wire;
        NPNR_ASSERT(pip != PipId());
        wire.index = tileInfo(pip)->pips_data[pip.index].src_idx;
        wire.location = tileInfo(pip)->pips_data[pip.index].src;
        return wire;
    }

    WireId getPipDstWire(PipId pip) const
    {
        WireId wire;
        NPNR_ASSERT(pip != PipId());
        wire.index = tileInfo(pip)->pips_data[pip.index].dst_idx;
        wire.location = tileInfo(pip)->pips_data[pip.index].dst;
        return wire;
    }

    DelayInfo getPipDelay(PipId pip) const
    {
        DelayInfo delay;

        delay.delay = 0.01;

        return delay;
    }

    PipRange getPipsDownhill(WireId wire) const
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = tileInfo(wire)->wire_data[wire.index].pips_downhill.get();
        range.b.wire_loc = wire.location;
        range.e.cursor = range.b.cursor + tileInfo(wire)->wire_data[wire.index].num_downhill;
        range.e.wire_loc = wire.location;
        return range;
    }

    PipRange getPipsUphill(WireId wire) const
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = tileInfo(wire)->wire_data[wire.index].pips_uphill.get();
        range.b.wire_loc = wire.location;
        range.e.cursor = range.b.cursor + tileInfo(wire)->wire_data[wire.index].num_uphill;
        range.e.wire_loc = wire.location;
        return range;
    }

    // Group
    GroupId getGroupByName(IdString name) const;
    IdString getGroupName(GroupId group) const;
    std::vector<GroupId> getGroups() const;
    const std::vector<BelId> &getGroupBels(GroupId group) const;
    const std::vector<WireId> &getGroupWires(GroupId group) const;
    const std::vector<PipId> &getGroupPips(GroupId group) const;
    const std::vector<GroupId> &getGroupGroups(GroupId group) const;

    // Delay
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

    // Flow
    bool pack();
    bool place();
    bool route();

    // Graphics
    const std::vector<GraphicElement> &getDecalGraphics(DecalId decal) const;
    DecalXY getBelDecal(BelId bel) const;
    DecalXY getWireDecal(WireId wire) const;
    DecalXY getPipDecal(PipId pip) const;
    DecalXY getGroupDecal(GroupId group) const;

    // Cell Delay
    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const;
    // Get the port class, also setting clockInfoCount to the number of TimingClockingInfos associated with a port
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const;
    // Get the TimingClockingInfo of a port
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const;

    // Placer
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
