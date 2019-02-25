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
    int32_t num_bel_wires;
    RelPtr<BelWirePOD> bel_wires;
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
    int32_t num_uphill, num_downhill;
    RelPtr<PipLocatorPOD> pips_uphill, pips_downhill;

    int32_t num_bel_pins;
    RelPtr<BelPortPOD> bel_pins;
});

NPNR_PACKED_STRUCT(struct LocationTypePOD {
    int32_t num_bels, num_wires, num_pips;
    RelPtr<BelInfoPOD> bel_data;
    RelPtr<WireInfoPOD> wire_data;
    RelPtr<PipInfoPOD> pip_data;
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
    int32_t num_pins;
    RelPtr<PackagePinPOD> pin_data;
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
    int32_t num_prop_delays;
    int32_t num_setup_holds;
    RelPtr<CellPropDelayPOD> prop_delays;
    RelPtr<CellSetupHoldPOD> setup_holds;
});

NPNR_PACKED_STRUCT(struct PipDelayPOD {
    int32_t min_base_delay;
    int32_t max_base_delay;
    int32_t min_fanout_adder;
    int32_t max_fanout_adder;
});

NPNR_PACKED_STRUCT(struct SpeedGradePOD {
    int32_t num_cell_timings;
    int32_t num_pip_classes;
    RelPtr<CellTimingPOD> cell_timings;
    RelPtr<PipDelayPOD> pip_classes;
});

NPNR_PACKED_STRUCT(struct ChipInfoPOD {
    int32_t width, height;
    int32_t num_tiles;
    int32_t num_location_types;
    int32_t num_packages, num_pios;
    RelPtr<LocationTypePOD> locations;
    RelPtr<int32_t> location_type;
    RelPtr<GlobalInfoPOD> location_glbinfo;
    RelPtr<RelPtr<char>> tiletype_names;
    RelPtr<PackageInfoPOD> package_info;
    RelPtr<PIOInfoPOD> pio_info;
    RelPtr<TileInfoPOD> tile_info;
    RelPtr<SpeedGradePOD> speed_grades;
});

#if defined(_MSC_VER) || defined(EXTERNAL_CHIPDB_ROOT)
extern const char *chipdb_blob_25k;
extern const char *chipdb_blob_45k;
extern const char *chipdb_blob_85k;
#else
extern const char chipdb_blob_25k[];
extern const char chipdb_blob_45k[];
extern const char chipdb_blob_85k[];
#endif

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
               cursor_index >= chip->locations[chip->location_type[cursor_tile]].num_bels) {
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
               cursor_index >= chip->locations[chip->location_type[cursor_tile]].num_wires) {
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
               cursor_index >= chip->locations[chip->location_type[cursor_tile]].num_pips) {
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

struct Arch : BaseCtx
{
    const ChipInfoPOD *chip_info;
    const PackageInfoPOD *package_info;
    const SpeedGradePOD *speed_grade;

    mutable std::unordered_map<IdString, BelId> bel_by_name;
    mutable std::unordered_map<IdString, WireId> wire_by_name;
    mutable std::unordered_map<IdString, PipId> pip_by_name;

    std::vector<CellInfo *> bel_to_cell;
    std::unordered_map<WireId, NetInfo *> wire_to_net;
    std::unordered_map<PipId, NetInfo *> pip_to_net;
    std::unordered_map<WireId, int> wire_fanout;

    ArchArgs args;
    Arch(ArchArgs args);

    std::string getChipName() const;

    IdString archId() const { return id("ecp5"); }
    ArchArgs archArgs() const { return args; }
    IdString archArgsToId(ArchArgs args) const;

    // -------------------------------------------------

    int getGridDimX() const { return chip_info->width; };
    int getGridDimY() const { return chip_info->height; };
    int getTileBelDimZ(int, int) const { return 4; };
    int getTilePipDimZ(int, int) const { return 1; };

    // -------------------------------------------------

    BelId getBelByName(IdString name) const;

    template <typename Id> const LocationTypePOD *locInfo(Id &id) const
    {
        return &(chip_info->locations[chip_info->location_type[id.location.y * chip_info->width + id.location.x]]);
    }

    IdString getBelName(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        std::stringstream name;
        name << "X" << bel.location.x << "/Y" << bel.location.y << "/" << locInfo(bel)->bel_data[bel.index].name.get();
        return id(name.str());
    }

    uint32_t getBelChecksum(BelId bel) const { return bel.index; }

    const int max_loc_bels = 20;
    int getBelFlatIndex(BelId bel) const
    {
        return (bel.location.y * chip_info->width + bel.location.x) * max_loc_bels + bel.index;
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

    Loc getBelLocation(BelId bel) const
    {
        Loc loc;
        loc.x = bel.location.x;
        loc.y = bel.location.y;
        loc.z = locInfo(bel)->bel_data[bel.index].z;
        return loc;
    }

    BelId getBelByLocation(Loc loc) const;
    BelRange getBelsByTile(int x, int y) const;

    bool getBelGlobalBuf(BelId bel) const { return getBelType(bel) == id_DCCA; }

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
        id.index = locInfo(bel)->bel_data[bel.index].type;
        return id;
    }

    std::vector<std::pair<IdString, std::string>> getBelAttrs(BelId) const
    {
        std::vector<std::pair<IdString, std::string>> ret;
        return ret;
    }

    WireId getBelPinWire(BelId bel, IdString pin) const;

    BelPinRange getWireBelPins(WireId wire) const
    {
        BelPinRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.ptr = locInfo(wire)->wire_data[wire.index].bel_pins.get();
        range.b.wire_loc = wire.location;
        range.e.ptr = range.b.ptr + locInfo(wire)->wire_data[wire.index].num_bel_pins;
        range.e.wire_loc = wire.location;
        return range;
    }

    std::vector<IdString> getBelPins(BelId bel) const;

    // -------------------------------------------------

    WireId getWireByName(IdString name) const;

    IdString getWireName(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());

        std::stringstream name;
        name << "X" << wire.location.x << "/Y" << wire.location.y << "/"
             << locInfo(wire)->wire_data[wire.index].name.get();
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
        net->wires[wire].pip = PipId();
        net->wires[wire].strength = strength;
    }

    void unbindWire(WireId wire)
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

    DelayInfo getWireDelay(WireId wire) const
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

    IdString getWireBasename(WireId wire) const { return id(locInfo(wire)->wire_data[wire.index].name.get()); }

    WireId getWireByLocAndBasename(Location loc, std::string basename) const
    {
        WireId wireId;
        wireId.location = loc;
        for (int i = 0; i < locInfo(wireId)->num_wires; i++) {
            if (locInfo(wireId)->wire_data[i].name.get() == basename) {
                wireId.index = i;
                return wireId;
            }
        }
        return WireId();
    }

    // -------------------------------------------------

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
        wire_fanout[getPipSrcWire(pip)]++;

        WireId dst;
        dst.index = locInfo(pip)->pip_data[pip.index].dst_idx;
        dst.location = pip.location + locInfo(pip)->pip_data[pip.index].rel_dst_loc;
        NPNR_ASSERT(wire_to_net[dst] == nullptr);
        wire_to_net[dst] = net;
        net->wires[dst].pip = pip;
        net->wires[dst].strength = strength;
    }

    void unbindPip(PipId pip)
    {
        NPNR_ASSERT(pip != PipId());
        NPNR_ASSERT(pip_to_net[pip] != nullptr);
        wire_fanout[getPipSrcWire(pip)]--;

        WireId dst;
        dst.index = locInfo(pip)->pip_data[pip.index].dst_idx;
        dst.location = pip.location + locInfo(pip)->pip_data[pip.index].rel_dst_loc;
        NPNR_ASSERT(wire_to_net[dst] != nullptr);
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
        ++range.b; //-1 and then ++ deals with the case of no wries in the first tile
        range.e.cursor_tile = chip_info->width * chip_info->height;
        range.e.cursor_index = 0;
        range.e.chip = chip_info;
        return range;
    }

    WireId getPipSrcWire(PipId pip) const
    {
        WireId wire;
        NPNR_ASSERT(pip != PipId());
        wire.index = locInfo(pip)->pip_data[pip.index].src_idx;
        wire.location = pip.location + locInfo(pip)->pip_data[pip.index].rel_src_loc;
        return wire;
    }

    WireId getPipDstWire(PipId pip) const
    {
        WireId wire;
        NPNR_ASSERT(pip != PipId());
        wire.index = locInfo(pip)->pip_data[pip.index].dst_idx;
        wire.location = pip.location + locInfo(pip)->pip_data[pip.index].rel_dst_loc;
        return wire;
    }

    DelayInfo getPipDelay(PipId pip) const
    {
        DelayInfo delay;
        NPNR_ASSERT(pip != PipId());
        int fanout = 0;
        auto fnd_fanout = wire_fanout.find(getPipSrcWire(pip));
        if (fnd_fanout != wire_fanout.end())
            fanout = fnd_fanout->second;
        NPNR_ASSERT(locInfo(pip)->pip_data[pip.index].timing_class < speed_grade->num_pip_classes);
        delay.min_delay =
                speed_grade->pip_classes[locInfo(pip)->pip_data[pip.index].timing_class].min_base_delay +
                fanout * speed_grade->pip_classes[locInfo(pip)->pip_data[pip.index].timing_class].min_fanout_adder;
        delay.max_delay =
                speed_grade->pip_classes[locInfo(pip)->pip_data[pip.index].timing_class].max_base_delay +
                fanout * speed_grade->pip_classes[locInfo(pip)->pip_data[pip.index].timing_class].max_fanout_adder;
        return delay;
    }

    PipRange getPipsDownhill(WireId wire) const
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = locInfo(wire)->wire_data[wire.index].pips_downhill.get();
        range.b.wire_loc = wire.location;
        range.e.cursor = range.b.cursor + locInfo(wire)->wire_data[wire.index].num_downhill;
        range.e.wire_loc = wire.location;
        return range;
    }

    PipRange getPipsUphill(WireId wire) const
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = locInfo(wire)->wire_data[wire.index].pips_uphill.get();
        range.b.wire_loc = wire.location;
        range.e.cursor = range.b.cursor + locInfo(wire)->wire_data[wire.index].num_uphill;
        range.e.wire_loc = wire.location;
        return range;
    }

    PipRange getWireAliases(WireId wire) const
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = nullptr;
        range.e.cursor = nullptr;
        return range;
    }

    std::string getPipTilename(PipId pip) const
    {
        auto &tileloc = chip_info->tile_info[pip.location.y * chip_info->width + pip.location.x];
        for (int i = 0; i < tileloc.num_tiles; i++) {
            if (tileloc.tile_names[i].type_idx == locInfo(pip)->pip_data[pip.index].tile_type)
                return tileloc.tile_names[i].name.get();
        }
        NPNR_ASSERT_FALSE("failed to find Pip tile");
    }

    std::string getPipTiletype(PipId pip) const
    {
        return chip_info->tiletype_names[locInfo(pip)->pip_data[pip.index].tile_type].get();
    }

    Loc getPipLocation(PipId pip) const
    {
        Loc loc;
        loc.x = pip.location.x;
        loc.y = pip.location.y;
        loc.z = 0;
        return loc;
    }

    int8_t getPipClass(PipId pip) const { return locInfo(pip)->pip_data[pip.index].pip_type; }

    BelId getPackagePinBel(const std::string &pin) const;
    std::string getBelPackagePin(BelId bel) const;
    int getPioBelBank(BelId bel) const;
    // For getting GCLK, PLL, Vref, etc, pins
    std::string getPioFunctionName(BelId bel) const;
    BelId getPioByFunctionName(const std::string &name) const;

    PortType getBelPinType(BelId bel, IdString pin) const;

    // -------------------------------------------------

    GroupId getGroupByName(IdString name) const { return GroupId(); }
    IdString getGroupName(GroupId group) const { return IdString(); }
    std::vector<GroupId> getGroups() const { return std::vector<GroupId>(); }
    std::vector<BelId> getGroupBels(GroupId group) const { return std::vector<BelId>(); }
    std::vector<WireId> getGroupWires(GroupId group) const { return std::vector<WireId>(); }
    std::vector<PipId> getGroupPips(GroupId group) const { return std::vector<PipId>(); }
    std::vector<GroupId> getGroupGroups(GroupId group) const { return std::vector<GroupId>(); }

    // -------------------------------------------------

    delay_t estimateDelay(WireId src, WireId dst) const;
    delay_t predictDelay(const NetInfo *net_info, const PortRef &sink) const;
    delay_t getDelayEpsilon() const { return 20; }
    delay_t getRipupDelayPenalty() const { return 400; }
    float getDelayNS(delay_t v) const { return v * 0.001; }
    DelayInfo getDelayFromNS(float ns) const
    {
        DelayInfo del;
        del.min_delay = delay_t(ns * 1000);
        del.max_delay = delay_t(ns * 1000);
        return del;
    }
    uint32_t getDelayChecksum(delay_t v) const { return v; }
    bool getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const;

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
    // if no path exists
    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const;
    // Get the port class, also setting clockInfoCount to the number of TimingClockingInfos associated with a port
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const;
    // Get the TimingClockingInfo of a port
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const;
    // Return true if a port is a net
    bool isGlobalNet(const NetInfo *net) const;

    bool getDelayFromTimingDatabase(IdString tctype, IdString from, IdString to, DelayInfo &delay) const;
    void getSetupHoldFromTimingDatabase(IdString tctype, IdString clock, IdString port, DelayInfo &setup,
                                        DelayInfo &hold) const;

    // -------------------------------------------------
    // Placement validity checks
    bool isValidBelForCell(CellInfo *cell, BelId bel) const;
    bool isBelLocationValid(BelId bel) const;

    // Helper function for above
    bool slicesCompatible(const std::vector<const CellInfo *> &cells) const;

    void assignArchInfo();

    void permute_luts();

    std::vector<std::pair<std::string, std::string>> getTilesAtLocation(int row, int col);
    std::string getTileByTypeAndLocation(int row, int col, std::string type) const
    {
        auto &tileloc = chip_info->tile_info[row * chip_info->width + col];
        for (int i = 0; i < tileloc.num_tiles; i++) {
            if (chip_info->tiletype_names[tileloc.tile_names[i].type_idx].get() == type)
                return tileloc.tile_names[i].name.get();
        }
        NPNR_ASSERT_FALSE_STR("no tile at (" + std::to_string(col) + ", " + std::to_string(row) + ") with type " +
                              type);
    }

    std::string getTileByTypeAndLocation(int row, int col, const std::set<std::string> &type) const
    {
        auto &tileloc = chip_info->tile_info[row * chip_info->width + col];
        for (int i = 0; i < tileloc.num_tiles; i++) {
            if (type.count(chip_info->tiletype_names[tileloc.tile_names[i].type_idx].get()))
                return tileloc.tile_names[i].name.get();
        }
        NPNR_ASSERT_FALSE_STR("no tile at (" + std::to_string(col) + ", " + std::to_string(row) + ") with type in set");
    }

    std::string getTileByType(std::string type) const
    {
        for (int i = 0; i < chip_info->height * chip_info->width; i++) {
            auto &tileloc = chip_info->tile_info[i];
            for (int j = 0; j < tileloc.num_tiles; j++)
                if (chip_info->tiletype_names[tileloc.tile_names[j].type_idx].get() == type)
                    return tileloc.tile_names[j].name.get();
        }
        NPNR_ASSERT_FALSE_STR("no with type " + type);
    }

    GlobalInfoPOD globalInfoAtLoc(Location loc);

    bool getPIODQSGroup(BelId pio, bool &dqsright, int &dqsrow);
    BelId getDQSBUF(bool dqsright, int dqsrow);
    WireId getBankECLK(int bank, int eclk);

    // Apply LPF constraints to the context
    bool applyLPF(std::string filename, std::istream &in);

    IdString id_trellis_slice;
    IdString id_clk, id_lsr;
    IdString id_clkmux, id_lsrmux;
    IdString id_srmode, id_mode;
};

NEXTPNR_NAMESPACE_END
