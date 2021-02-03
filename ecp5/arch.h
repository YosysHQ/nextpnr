/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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

#include <set>
#include <sstream>

NEXTPNR_NAMESPACE_BEGIN

/**** Everything in this section must be kept in sync with chipdb.py ****/

#include "relptr.h"

NPNR_PACKED_STRUCT(struct BelWirePOD {
    LocationPOD rel_wire_loc;
    int32_t wire_index;
    int32_t port;
    int32_t type;
});

NPNR_PACKED_STRUCT(struct BelInfoPOD {
    RelPtr<char> name;
    int32_t type;
    int32_t z;
    RelSlice<BelWirePOD> bel_wires;
});

NPNR_PACKED_STRUCT(struct BelPortPOD {
    LocationPOD rel_bel_loc;
    int32_t bel_index;
    int32_t port;
});

NPNR_PACKED_STRUCT(struct PipInfoPOD {
    LocationPOD rel_src_loc, rel_dst_loc;
    int32_t src_idx, dst_idx;
    int32_t timing_class;
    int16_t tile_type;
    int8_t pip_type;
    int8_t padding_0;
});

NPNR_PACKED_STRUCT(struct PipLocatorPOD {
    LocationPOD rel_loc;
    int32_t index;
});

NPNR_PACKED_STRUCT(struct WireInfoPOD {
    RelPtr<char> name;
    int32_t type;
    int32_t tile_wire;
    RelSlice<PipLocatorPOD> pips_uphill, pips_downhill;
    RelSlice<BelPortPOD> bel_pins;
});

NPNR_PACKED_STRUCT(struct LocationTypePOD {
    RelSlice<BelInfoPOD> bel_data;
    RelSlice<WireInfoPOD> wire_data;
    RelSlice<PipInfoPOD> pip_data;
});

NPNR_PACKED_STRUCT(struct PIOInfoPOD {
    LocationPOD abs_loc;
    int32_t bel_index;
    RelPtr<char> function_name;
    int16_t bank;
    int16_t dqsgroup;
});

NPNR_PACKED_STRUCT(struct PackagePinPOD {
    RelPtr<char> name;
    LocationPOD abs_loc;
    int32_t bel_index;
});

NPNR_PACKED_STRUCT(struct PackageInfoPOD {
    RelPtr<char> name;
    RelSlice<PackagePinPOD> pin_data;
});

NPNR_PACKED_STRUCT(struct TileNamePOD {
    RelPtr<char> name;
    int16_t type_idx;
    int16_t padding;
});

NPNR_PACKED_STRUCT(struct TileInfoPOD { RelSlice<TileNamePOD> tile_names; });

enum TapDirection : int8_t
{
    TAP_DIR_LEFT = 0,
    TAP_DIR_RIGHT = 1
};

enum GlobalQuadrant : int8_t
{
    QUAD_UL = 0,
    QUAD_UR = 1,
    QUAD_LL = 2,
    QUAD_LR = 3,
};

NPNR_PACKED_STRUCT(struct GlobalInfoPOD {
    int16_t tap_col;
    TapDirection tap_dir;
    GlobalQuadrant quad;
    int16_t spine_row;
    int16_t spine_col;
});

NPNR_PACKED_STRUCT(struct CellPropDelayPOD {
    int32_t from_port;
    int32_t to_port;
    int32_t min_delay;
    int32_t max_delay;
});

NPNR_PACKED_STRUCT(struct CellSetupHoldPOD {
    int32_t sig_port;
    int32_t clock_port;
    int32_t min_setup;
    int32_t max_setup;
    int32_t min_hold;
    int32_t max_hold;
});

NPNR_PACKED_STRUCT(struct CellTimingPOD {
    int32_t cell_type;
    RelSlice<CellPropDelayPOD> prop_delays;
    RelSlice<CellSetupHoldPOD> setup_holds;
});

NPNR_PACKED_STRUCT(struct PipDelayPOD {
    int32_t min_base_delay;
    int32_t max_base_delay;
    int32_t min_fanout_adder;
    int32_t max_fanout_adder;
});

NPNR_PACKED_STRUCT(struct SpeedGradePOD {
    RelSlice<CellTimingPOD> cell_timings;
    RelSlice<PipDelayPOD> pip_classes;
});

NPNR_PACKED_STRUCT(struct ChipInfoPOD {
    int32_t width, height;
    int32_t num_tiles;
    int32_t const_id_count;
    RelSlice<LocationTypePOD> locations;
    RelSlice<int32_t> location_type;
    RelSlice<GlobalInfoPOD> location_glbinfo;
    RelSlice<RelPtr<char>> tiletype_names;
    RelSlice<PackageInfoPOD> package_info;
    RelSlice<PIOInfoPOD> pio_info;
    RelSlice<TileInfoPOD> tile_info;
    RelSlice<SpeedGradePOD> speed_grades;
});

/************************ End of chipdb section. ************************/

struct BelIterator
{
    const ChipInfoPOD *chip;
    int cursor_index;
    int cursor_tile;

    BelIterator operator++()
    {
        cursor_index++;
        while (cursor_tile < chip->num_tiles &&
               cursor_index >= int(chip->locations[chip->location_type[cursor_tile]].bel_data.size())) {
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

// -----------------------------------------------------------------------

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
        ret.bel.location = wire_loc + ptr->rel_bel_loc;
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

// -----------------------------------------------------------------------

struct WireIterator
{
    const ChipInfoPOD *chip;
    int cursor_index;
    int cursor_tile;

    WireIterator operator++()
    {
        cursor_index++;
        while (cursor_tile < chip->num_tiles &&
               cursor_index >= int(chip->locations[chip->location_type[cursor_tile]].wire_data.size())) {
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

// -----------------------------------------------------------------------

struct AllPipIterator
{
    const ChipInfoPOD *chip;
    int cursor_index;
    int cursor_tile;

    AllPipIterator operator++()
    {
        cursor_index++;
        while (cursor_tile < chip->num_tiles &&
               cursor_index >= int(chip->locations[chip->location_type[cursor_tile]].pip_data.size())) {
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

// -----------------------------------------------------------------------

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
        ret.location = wire_loc + cursor->rel_loc;
        return ret;
    }
};

struct PipRange
{
    PipIterator b, e;
    PipIterator begin() const { return b; }
    PipIterator end() const { return e; }
};

struct ArchArgs
{
    enum ArchArgsTypes
    {
        NONE,
        LFE5U_12F,
        LFE5U_25F,
        LFE5U_45F,
        LFE5U_85F,
        LFE5UM_25F,
        LFE5UM_45F,
        LFE5UM_85F,
        LFE5UM5G_25F,
        LFE5UM5G_45F,
        LFE5UM5G_85F,
    } type = NONE;
    std::string package;
    enum SpeedGrade
    {
        SPEED_6 = 0,
        SPEED_7,
        SPEED_8,
        SPEED_8_5G,
    } speed = SPEED_6;
};

struct DelayKey
{
    IdString celltype, from, to;
    inline bool operator==(const DelayKey &other) const
    {
        return celltype == other.celltype && from == other.from && to == other.to;
    }
};

NEXTPNR_NAMESPACE_END
namespace std {
template <> struct hash<NEXTPNR_NAMESPACE_PREFIX DelayKey>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX DelayKey &dk) const noexcept
    {
        std::size_t seed = std::hash<NEXTPNR_NAMESPACE_PREFIX IdString>()(dk.celltype);
        seed ^= std::hash<NEXTPNR_NAMESPACE_PREFIX IdString>()(dk.from) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= std::hash<NEXTPNR_NAMESPACE_PREFIX IdString>()(dk.to) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};
} // namespace std
NEXTPNR_NAMESPACE_BEGIN

struct Arch : BaseCtx
{
    const ChipInfoPOD *chip_info;
    const PackageInfoPOD *package_info;
    const SpeedGradePOD *speed_grade;

    mutable std::unordered_map<IdStringList, PipId> pip_by_name;

    std::vector<CellInfo *> bel_to_cell;
    std::unordered_map<WireId, NetInfo *> wire_to_net;
    std::unordered_map<PipId, NetInfo *> pip_to_net;
    std::unordered_map<WireId, int> wire_fanout;

    // fast access to  X and Y IdStrings for building object names
    std::vector<IdString> x_ids, y_ids;
    // inverse of the above for name->object mapping
    std::unordered_map<IdString, int> id_to_x, id_to_y;

    ArchArgs args;
    Arch(ArchArgs args);

    static bool is_available(ArchArgs::ArchArgsTypes chip);
    static std::vector<std::string> get_supported_packages(ArchArgs::ArchArgsTypes chip);

    std::string getChipName() const;
    std::string get_full_chip_name() const;

    IdString archId() const { return id("ecp5"); }
    ArchArgs archArgs() const { return args; }
    IdString archArgsToId(ArchArgs args) const;

    // -------------------------------------------------

    static const int max_loc_bels = 20;

    int getGridDimX() const override { return chip_info->width; };
    int getGridDimY() const override { return chip_info->height; };
    int getTileBelDimZ(int, int) const override { return max_loc_bels; };
    int getTilePipDimZ(int, int) const override { return 1; };
    char getNameDelimiter() const override { return '/'; }

    // -------------------------------------------------

    BelId getBelByName(IdStringList name) const override;

    template <typename Id> const LocationTypePOD *loc_info(Id &id) const
    {
        return &(chip_info->locations[chip_info->location_type[id.location.y * chip_info->width + id.location.x]]);
    }

    IdStringList getBelName(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        std::array<IdString, 3> ids{x_ids.at(bel.location.x), y_ids.at(bel.location.y),
                                    id(loc_info(bel)->bel_data[bel.index].name.get())};
        return IdStringList(ids);
    }

    uint32_t getBelChecksum(BelId bel) const override { return bel.index; }

    int get_bel_flat_index(BelId bel) const
    {
        return (bel.location.y * chip_info->width + bel.location.x) * max_loc_bels + bel.index;
    }

    void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength) override
    {
        NPNR_ASSERT(bel != BelId());
        int idx = get_bel_flat_index(bel);
        NPNR_ASSERT(bel_to_cell.at(idx) == nullptr);
        bel_to_cell[idx] = cell;
        cell->bel = bel;
        cell->belStrength = strength;
        refreshUiBel(bel);
    }

    void unbindBel(BelId bel) override
    {
        NPNR_ASSERT(bel != BelId());
        int idx = get_bel_flat_index(bel);
        NPNR_ASSERT(bel_to_cell.at(idx) != nullptr);
        bel_to_cell[idx]->bel = BelId();
        bel_to_cell[idx]->belStrength = STRENGTH_NONE;
        bel_to_cell[idx] = nullptr;
        refreshUiBel(bel);
    }

    Loc getBelLocation(BelId bel) const override
    {
        Loc loc;
        loc.x = bel.location.x;
        loc.y = bel.location.y;
        loc.z = loc_info(bel)->bel_data[bel.index].z;
        return loc;
    }

    BelId getBelByLocation(Loc loc) const override;
    BelRange getBelsByTile(int x, int y) const;

    bool getBelGlobalBuf(BelId bel) const override { return getBelType(bel) == id_DCCA; }

    bool checkBelAvail(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        return bel_to_cell[get_bel_flat_index(bel)] == nullptr;
    }

    CellInfo *getBoundBelCell(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        return bel_to_cell[get_bel_flat_index(bel)];
    }

    CellInfo *getConflictingBelCell(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        return bel_to_cell[get_bel_flat_index(bel)];
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

    IdString getBelType(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        IdString id;
        id.index = loc_info(bel)->bel_data[bel.index].type;
        return id;
    }

    std::vector<std::pair<IdString, std::string>> getBelAttrs(BelId) const
    {
        std::vector<std::pair<IdString, std::string>> ret;
        return ret;
    }

    WireId getBelPinWire(BelId bel, IdString pin) const override;

    BelPinRange getWireBelPins(WireId wire) const
    {
        BelPinRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.ptr = loc_info(wire)->wire_data[wire.index].bel_pins.begin();
        range.b.wire_loc = wire.location;
        range.e.ptr = loc_info(wire)->wire_data[wire.index].bel_pins.end();
        range.e.wire_loc = wire.location;
        return range;
    }

    std::vector<IdString> getBelPins(BelId bel) const;

    // -------------------------------------------------

    WireId getWireByName(IdStringList name) const override;

    IdStringList getWireName(WireId wire) const override
    {
        NPNR_ASSERT(wire != WireId());
        std::array<IdString, 3> ids{x_ids.at(wire.location.x), y_ids.at(wire.location.y),
                                    id(loc_info(wire)->wire_data[wire.index].name.get())};
        return IdStringList(ids);
    }

    IdString getWireType(WireId wire) const override
    {
        NPNR_ASSERT(wire != WireId());
        IdString id;
        id.index = loc_info(wire)->wire_data[wire.index].type;
        return id;
    }

    std::vector<std::pair<IdString, std::string>> getWireAttrs(WireId) const;

    uint32_t getWireChecksum(WireId wire) const override { return wire.index; }

    void bindWire(WireId wire, NetInfo *net, PlaceStrength strength) override
    {
        NPNR_ASSERT(wire != WireId());
        NPNR_ASSERT(wire_to_net[wire] == nullptr);
        wire_to_net[wire] = net;
        net->wires[wire].pip = PipId();
        net->wires[wire].strength = strength;
        refreshUiWire(wire);
    }

    void unbindWire(WireId wire) override
    {
        NPNR_ASSERT(wire != WireId());
        NPNR_ASSERT(wire_to_net[wire] != nullptr);

        auto &net_wires = wire_to_net[wire]->wires;
        auto it = net_wires.find(wire);
        NPNR_ASSERT(it != net_wires.end());

        auto pip = it->second.pip;
        if (pip != PipId()) {
            wire_fanout[getPipSrcWire(pip)]--;
            pip_to_net[pip] = nullptr;
        }

        net_wires.erase(it);
        wire_to_net[wire] = nullptr;
        refreshUiWire(wire);
    }

    bool checkWireAvail(WireId wire) const override
    {
        NPNR_ASSERT(wire != WireId());
        return wire_to_net.find(wire) == wire_to_net.end() || wire_to_net.at(wire) == nullptr;
    }

    NetInfo *getBoundWireNet(WireId wire) const override
    {
        NPNR_ASSERT(wire != WireId());
        if (wire_to_net.find(wire) == wire_to_net.end())
            return nullptr;
        else
            return wire_to_net.at(wire);
    }

    DelayInfo getWireDelay(WireId wire) const override
    {
        DelayInfo delay;
        delay.min_delay = 0;
        delay.max_delay = 0;
        return delay;
    }

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

    IdString get_wire_basename(WireId wire) const { return id(loc_info(wire)->wire_data[wire.index].name.get()); }

    WireId get_wire_by_loc_basename(Location loc, std::string basename) const
    {
        WireId wireId;
        wireId.location = loc;
        for (int i = 0; i < int(loc_info(wireId)->wire_data.size()); i++) {
            if (loc_info(wireId)->wire_data[i].name.get() == basename) {
                wireId.index = i;
                return wireId;
            }
        }
        return WireId();
    }

    // -------------------------------------------------

    PipId getPipByName(IdStringList name) const override;
    IdStringList getPipName(PipId pip) const override;

    std::vector<std::pair<IdString, std::string>> getPipAttrs(PipId) const
    {
        std::vector<std::pair<IdString, std::string>> ret;
        return ret;
    }

    uint32_t getPipChecksum(PipId pip) const override { return pip.index; }

    void bindPip(PipId pip, NetInfo *net, PlaceStrength strength) override
    {
        NPNR_ASSERT(pip != PipId());
        NPNR_ASSERT(pip_to_net[pip] == nullptr);

        pip_to_net[pip] = net;
        wire_fanout[getPipSrcWire(pip)]++;

        WireId dst;
        dst.index = loc_info(pip)->pip_data[pip.index].dst_idx;
        dst.location = pip.location + loc_info(pip)->pip_data[pip.index].rel_dst_loc;
        NPNR_ASSERT(wire_to_net[dst] == nullptr);
        wire_to_net[dst] = net;
        net->wires[dst].pip = pip;
        net->wires[dst].strength = strength;
    }

    void unbindPip(PipId pip) override
    {
        NPNR_ASSERT(pip != PipId());
        NPNR_ASSERT(pip_to_net[pip] != nullptr);
        wire_fanout[getPipSrcWire(pip)]--;

        WireId dst;
        dst.index = loc_info(pip)->pip_data[pip.index].dst_idx;
        dst.location = pip.location + loc_info(pip)->pip_data[pip.index].rel_dst_loc;
        NPNR_ASSERT(wire_to_net[dst] != nullptr);
        wire_to_net[dst] = nullptr;
        pip_to_net[pip]->wires.erase(dst);

        pip_to_net[pip] = nullptr;
    }

    bool checkPipAvail(PipId pip) const override
    {
        NPNR_ASSERT(pip != PipId());
        return pip_to_net.find(pip) == pip_to_net.end() || pip_to_net.at(pip) == nullptr;
    }

    NetInfo *getBoundPipNet(PipId pip) const override
    {
        NPNR_ASSERT(pip != PipId());
        if (pip_to_net.find(pip) == pip_to_net.end())
            return nullptr;
        else
            return pip_to_net.at(pip);
    }

    NetInfo *getConflictingPipNet(PipId pip) const override
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
        ++range.b; //-1 and then ++ deals with the case of no wries in the first tile
        range.e.cursor_tile = chip_info->width * chip_info->height;
        range.e.cursor_index = 0;
        range.e.chip = chip_info;
        return range;
    }

    WireId getPipSrcWire(PipId pip) const override
    {
        WireId wire;
        NPNR_ASSERT(pip != PipId());
        wire.index = loc_info(pip)->pip_data[pip.index].src_idx;
        wire.location = pip.location + loc_info(pip)->pip_data[pip.index].rel_src_loc;
        return wire;
    }

    WireId getPipDstWire(PipId pip) const override
    {
        WireId wire;
        NPNR_ASSERT(pip != PipId());
        wire.index = loc_info(pip)->pip_data[pip.index].dst_idx;
        wire.location = pip.location + loc_info(pip)->pip_data[pip.index].rel_dst_loc;
        return wire;
    }

    DelayInfo getPipDelay(PipId pip) const override
    {
        DelayInfo delay;
        NPNR_ASSERT(pip != PipId());
        int fanout = 0;
        auto fnd_fanout = wire_fanout.find(getPipSrcWire(pip));
        if (fnd_fanout != wire_fanout.end())
            fanout = fnd_fanout->second;
        delay.min_delay =
                speed_grade->pip_classes[loc_info(pip)->pip_data[pip.index].timing_class].min_base_delay +
                fanout * speed_grade->pip_classes[loc_info(pip)->pip_data[pip.index].timing_class].min_fanout_adder;
        delay.max_delay =
                speed_grade->pip_classes[loc_info(pip)->pip_data[pip.index].timing_class].max_base_delay +
                fanout * speed_grade->pip_classes[loc_info(pip)->pip_data[pip.index].timing_class].max_fanout_adder;
        return delay;
    }

    PipRange getPipsDownhill(WireId wire) const
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = loc_info(wire)->wire_data[wire.index].pips_downhill.get();
        range.b.wire_loc = wire.location;
        range.e.cursor = range.b.cursor + loc_info(wire)->wire_data[wire.index].pips_downhill.size();
        range.e.wire_loc = wire.location;
        return range;
    }

    PipRange getPipsUphill(WireId wire) const
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = loc_info(wire)->wire_data[wire.index].pips_uphill.get();
        range.b.wire_loc = wire.location;
        range.e.cursor = range.b.cursor + loc_info(wire)->wire_data[wire.index].pips_uphill.size();
        range.e.wire_loc = wire.location;
        return range;
    }

    std::string get_pip_tilename(PipId pip) const
    {
        auto &tileloc = chip_info->tile_info[pip.location.y * chip_info->width + pip.location.x];
        for (auto &tn : tileloc.tile_names) {
            if (tn.type_idx == loc_info(pip)->pip_data[pip.index].tile_type)
                return tn.name.get();
        }
        NPNR_ASSERT_FALSE("failed to find Pip tile");
    }

    std::string get_pip_tiletype(PipId pip) const
    {
        return chip_info->tiletype_names[loc_info(pip)->pip_data[pip.index].tile_type].get();
    }

    Loc getPipLocation(PipId pip) const override
    {
        Loc loc;
        loc.x = pip.location.x;
        loc.y = pip.location.y;
        loc.z = 0;
        return loc;
    }

    int8_t get_pip_class(PipId pip) const { return loc_info(pip)->pip_data[pip.index].pip_type; }

    BelId get_package_pin_bel(const std::string &pin) const;
    std::string get_bel_package_pin(BelId bel) const;
    int get_pio_bel_bank(BelId bel) const;
    // For getting GCLK, PLL, Vref, etc, pins
    std::string get_pio_function_name(BelId bel) const;
    BelId get_pio_by_function_name(const std::string &name) const;

    PortType getBelPinType(BelId bel, IdString pin) const override;

    // -------------------------------------------------

    GroupId getGroupByName(IdStringList name) const override;
    IdStringList getGroupName(GroupId group) const override;
    std::vector<GroupId> getGroups() const;
    std::vector<BelId> getGroupBels(GroupId group) const;
    std::vector<WireId> getGroupWires(GroupId group) const;
    std::vector<PipId> getGroupPips(GroupId group) const;
    std::vector<GroupId> getGroupGroups(GroupId group) const;

    // -------------------------------------------------

    delay_t estimateDelay(WireId src, WireId dst) const override;
    ArcBounds getRouteBoundingBox(WireId src, WireId dst) const override;
    delay_t predictDelay(const NetInfo *net_info, const PortRef &sink) const override;
    delay_t getDelayEpsilon() const override { return 20; }
    delay_t getRipupDelayPenalty() const override;
    float getDelayNS(delay_t v) const override { return v * 0.001; }
    DelayInfo getDelayFromNS(float ns) const override
    {
        DelayInfo del;
        del.min_delay = delay_t(ns * 1000);
        del.max_delay = delay_t(ns * 1000);
        return del;
    }
    uint32_t getDelayChecksum(delay_t v) const override { return v; }
    bool getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const override;

    // -------------------------------------------------

    bool pack() override;
    bool place() override;
    bool route() override;

    // -------------------------------------------------

    std::vector<GraphicElement> getDecalGraphics(DecalId decal) const;

    DecalXY getBelDecal(BelId bel) const override;
    DecalXY getWireDecal(WireId wire) const override;
    DecalXY getPipDecal(PipId pip) const override;
    DecalXY getGroupDecal(GroupId group) const override;

    // -------------------------------------------------

    // Get the delay through a cell from one port to another, returning false
    // if no path exists
    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const override;
    // Get the port class, also setting clockInfoCount to the number of TimingClockingInfos associated with a port
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const override;
    // Get the TimingClockingInfo of a port
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const override;
    // Return true if a port is a net
    bool is_global_net(const NetInfo *net) const;

    bool get_delay_from_tmg_db(IdString tctype, IdString from, IdString to, DelayInfo &delay) const;
    void get_setuphold_from_tmg_db(IdString tctype, IdString clock, IdString port, DelayInfo &setup,
                                   DelayInfo &hold) const;

    // -------------------------------------------------
    // Placement validity checks

    const std::vector<IdString> &getCellTypes() const { return cell_types; }

    std::vector<BelBucketId> getBelBuckets() const { return buckets; }

    IdString getBelBucketName(BelBucketId bucket) const override { return bucket.name; }

    BelBucketId getBelBucketByName(IdString name) const override
    {
        BelBucketId bucket;
        bucket.name = name;
        return bucket;
    }

    BelBucketId getBelBucketForBel(BelId bel) const override
    {
        BelBucketId bucket;
        bucket.name = getBelType(bel);
        return bucket;
    }

    BelBucketId getBelBucketForCellType(IdString cell_type) const override
    {
        BelBucketId bucket;
        bucket.name = cell_type;
        return bucket;
    }

    std::vector<BelId> getBelsInBucket(BelBucketId bucket) const
    {
        std::vector<BelId> bels;
        for (BelId bel : getBels()) {
            if (getBelType(bel) == bucket.name) {
                bels.push_back(bel);
            }
        }
        return bels;
    }

    bool isValidBelForCell(CellInfo *cell, BelId bel) const override;
    bool isBelLocationValid(BelId bel) const override;

    // Helper function for above
    bool slices_compatible(const std::vector<const CellInfo *> &cells) const;

    void assignArchInfo() override;

    void permute_luts();

    std::vector<std::pair<std::string, std::string>> get_tiles_at_loc(int row, int col);
    std::string get_tile_by_type_loc(int row, int col, std::string type) const
    {
        auto &tileloc = chip_info->tile_info[row * chip_info->width + col];
        for (auto &tn : tileloc.tile_names) {
            if (chip_info->tiletype_names[tn.type_idx].get() == type)
                return tn.name.get();
        }
        NPNR_ASSERT_FALSE_STR("no tile at (" + std::to_string(col) + ", " + std::to_string(row) + ") with type " +
                              type);
    }

    std::string get_tile_by_type_loc(int row, int col, const std::set<std::string> &type) const
    {
        auto &tileloc = chip_info->tile_info[row * chip_info->width + col];
        for (auto &tn : tileloc.tile_names) {
            if (type.count(chip_info->tiletype_names[tn.type_idx].get()))
                return tn.name.get();
        }
        NPNR_ASSERT_FALSE_STR("no tile at (" + std::to_string(col) + ", " + std::to_string(row) + ") with type in set");
    }

    std::string get_tile_by_type(std::string type) const
    {
        for (int i = 0; i < chip_info->height * chip_info->width; i++) {
            auto &tileloc = chip_info->tile_info[i];
            for (auto &tn : tileloc.tile_names)
                if (chip_info->tiletype_names[tn.type_idx].get() == type)
                    return tn.name.get();
        }
        NPNR_ASSERT_FALSE_STR("no tile with type " + type);
    }

    GlobalInfoPOD global_info_at_loc(Location loc);

    bool get_pio_dqs_group(BelId pio, bool &dqsright, int &dqsrow);
    BelId get_dqsbuf(bool dqsright, int dqsrow);
    WireId get_bank_eclk(int bank, int eclk);

    // Apply LPF constraints to the context
    bool apply_lpf(std::string filename, std::istream &in);

    IdString id_trellis_slice;
    IdString id_clk, id_lsr;
    IdString id_clkmux, id_lsrmux;
    IdString id_srmode, id_mode;

    // Special case for delay estimates due to its physical location
    // being far from the logical location of its primitive
    WireId gsrclk_wire;
    // Improves directivity of routing to DSP inputs, avoids issues
    // with different routes to the same physical reset wire causing
    // conflicts and slow routing
    std::unordered_map<WireId, std::pair<int, int>> wire_loc_overrides;
    void setup_wire_locations();

    mutable std::unordered_map<DelayKey, std::pair<bool, DelayInfo>> celldelay_cache;

    static const std::string defaultPlacer;
    static const std::vector<std::string> availablePlacers;
    static const std::string defaultRouter;
    static const std::vector<std::string> availableRouters;

    std::vector<IdString> cell_types;
    std::vector<BelBucketId> buckets;
};

NEXTPNR_NAMESPACE_END
