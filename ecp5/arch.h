/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
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

#ifndef ECP5_ARCH_H
#define ECP5_ARCH_H

#include <set>
#include <sstream>

#include "base_arch.h"
#include "nextpnr_types.h"
#include "relptr.h"

NEXTPNR_NAMESPACE_BEGIN

/**** Everything in this section must be kept in sync with chipdb.py ****/

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
    int16_t src_idx, dst_idx;
    int16_t timing_class;
    int8_t tile_type;
    int8_t pip_type;
    int16_t lutperm_flags;
    int16_t padding;
});

inline bool is_lutperm_pip(int16_t flags) { return flags & 0x4000; }
inline uint8_t lutperm_lut(int16_t flags) { return (flags >> 4) & 0x7; }
inline uint8_t lutperm_out(int16_t flags) { return (flags >> 2) & 0x3; }
inline uint8_t lutperm_in(int16_t flags) { return flags & 0x3; }

NPNR_PACKED_STRUCT(struct PipLocatorPOD {
    LocationPOD rel_loc;
    int32_t index;
});

NPNR_PACKED_STRUCT(struct WireInfoPOD {
    RelPtr<char> name;
    int16_t type;
    int16_t tile_wire;
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
               cursor_index >= chip->locations[chip->location_type[cursor_tile]].bel_data.ssize()) {
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
               cursor_index >= chip->locations[chip->location_type[cursor_tile]].wire_data.ssize()) {
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
               cursor_index >= chip->locations[chip->location_type[cursor_tile]].pip_data.ssize()) {
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
    unsigned int hash() const { return mkhash(celltype.hash(), mkhash(from.hash(), to.hash())); }
};

struct ArchRanges : BaseArchRanges
{
    using ArchArgsT = ArchArgs;
    // Bels
    using AllBelsRangeT = BelRange;
    using TileBelsRangeT = BelRange;
    using BelPinsRangeT = std::vector<IdString>;
    // Wires
    using AllWiresRangeT = WireRange;
    using DownhillPipRangeT = PipRange;
    using UphillPipRangeT = PipRange;
    using WireBelPinRangeT = BelPinRange;
    // Pips
    using AllPipsRangeT = AllPipRange;
};

struct Arch : BaseArch<ArchRanges>
{
    const ChipInfoPOD *chip_info;
    const PackageInfoPOD *package_info;
    const SpeedGradePOD *speed_grade;

    mutable dict<IdStringList, PipId> pip_by_name;

    enum class LutPermRule
    {
        NONE,
        CARRY,
        ALL,
    };
    std::vector<LutPermRule> lutperm_allowed;
    bool disable_router_lutperm = false;

    // For fast, incremental validity checking of split SLICE

    // BEL z-position lookup, x-ored with (index in tile) << 2
    enum LogicBELType
    {
        BEL_COMB = 0,
        BEL_FF = 1,
        BEL_RAMW = 2
    };
    static const int lc_idx_shift = 2;

    struct LogicTileStatus
    {
        // Per-SLICE valid and dirty bits
        struct SliceStatus
        {
            bool valid = true, dirty = true;
        } slices[4];
        // Per-tile legality check for control set legality
        bool tile_valid = true;
        bool tile_dirty = true;
        // Fast index from z-pos to cell
        std::array<CellInfo *, 8 * (1 << lc_idx_shift)> cells;
    };

    struct TileStatus
    {
        std::vector<CellInfo *> boundcells;
        LogicTileStatus *lts = nullptr;
        // TODO: use similar mechanism for DSP legality checking
        ~TileStatus() { delete lts; }
    };

    // faster replacements for base_pip2net, base_wire2net
    // indexed by get_pip_vecidx()
    std::vector<NetInfo *> pip2net;
    // indexed by get_wire_vecidx()
    std::vector<NetInfo *> wire2net;
    std::vector<int> wire_fanout;
    // We record the index=0 offset into pip2net for each tile, allowing us to
    // calculate any PipId's offset from pip.index and pip.location
    std::vector<int32_t> pip_tile_vecidx;
    std::vector<int32_t> wire_tile_vecidx;

    // fast access to  X and Y IdStrings for building object names
    std::vector<IdString> x_ids, y_ids;
    // inverse of the above for name->object mapping
    dict<IdString, int> id_to_x, id_to_y;

    ArchArgs args;
    Arch(ArchArgs args);

    static bool is_available(ArchArgs::ArchArgsTypes chip);
    static std::vector<std::string> get_supported_packages(ArchArgs::ArchArgsTypes chip);

    std::string getChipName() const override;
    std::string get_full_chip_name() const;

    ArchArgs archArgs() const override { return args; }
    IdString archArgsToId(ArchArgs args) const override;

    // -------------------------------------------------

    static const int max_loc_bels = 32;

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

    template <typename Id> inline int tile_index(Id id) const
    {
        return id.location.y * chip_info->width + id.location.x;
    }

    IdStringList getBelName(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        std::array<IdString, 3> ids{x_ids.at(bel.location.x), y_ids.at(bel.location.y),
                                    id(loc_info(bel)->bel_data[bel.index].name.get())};
        return IdStringList(ids);
    }

    uint32_t getBelChecksum(BelId bel) const override { return bel.index; }

    int get_slice_index(int x, int y, int slice) const
    {
        NPNR_ASSERT(slice >= 0 && slice < 4);
        return (y * chip_info->width + x) * 4 + slice;
    }

    void update_bel(BelId bel, CellInfo *old_cell, CellInfo *new_cell)
    {
        CellInfo *act_cell = (old_cell == nullptr) ? new_cell : old_cell;
        if (act_cell->type.in(id_TRELLIS_FF, id_TRELLIS_COMB, id_TRELLIS_RAMW)) {
            LogicTileStatus *lts = tile_status.at(tile_index(bel)).lts;
            NPNR_ASSERT(lts != nullptr);
            int z = loc_info(bel)->bel_data[bel.index].z;
            lts->slices[(z >> lc_idx_shift) / 2].dirty = true;
            if (act_cell->type == id_TRELLIS_FF)
                lts->tile_dirty = true; // because FF CLK/LSR signals are tile-wide
            if (act_cell->type == id_TRELLIS_COMB && (act_cell->combInfo.flags & ArchCellInfo::COMB_LUTRAM))
                lts->tile_dirty = true; // because RAM shares CLK/LSR signals with FFs
            lts->cells[z] = new_cell;
        }
    }

    void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength) override
    {
        NPNR_ASSERT(bel != BelId());
        auto &slot = tile_status.at(tile_index(bel)).boundcells.at(bel.index);
        NPNR_ASSERT(slot == nullptr);
        slot = cell;
        cell->bel = bel;
        cell->belStrength = strength;
        if (getBelType(bel) == id_TRELLIS_COMB) {
            int flags = cell->combInfo.flags;
            lutperm_allowed.at(
                    get_slice_index(bel.location.x, bel.location.y, (getBelLocation(bel).z >> lc_idx_shift) / 2)) =
                    (((flags & ArchCellInfo::COMB_LUTRAM) || (flags & ArchCellInfo::COMB_RAMW_BLOCK))
                             ? LutPermRule::NONE
                             : ((flags & ArchCellInfo::COMB_CARRY) ? LutPermRule::CARRY : LutPermRule::ALL));
        }
        update_bel(bel, nullptr, cell);
        refreshUiBel(bel);
    }

    void unbindBel(BelId bel) override
    {
        NPNR_ASSERT(bel != BelId());
        auto &slot = tile_status.at(tile_index(bel)).boundcells.at(bel.index);
        NPNR_ASSERT(slot != nullptr);
        update_bel(bel, slot, nullptr);
        slot->bel = BelId();
        slot->belStrength = STRENGTH_NONE;
        slot = nullptr;
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
    BelRange getBelsByTile(int x, int y) const override;

    bool getBelGlobalBuf(BelId bel) const override { return getBelType(bel) == id_DCCA; }

    bool checkBelAvail(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        const CellInfo *slot = tile_status.at(tile_index(bel)).boundcells.at(bel.index);
        return slot == nullptr;
    }

    CellInfo *getBoundBelCell(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        CellInfo *slot = tile_status.at(tile_index(bel)).boundcells.at(bel.index);
        return slot;
    }

    CellInfo *getConflictingBelCell(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        CellInfo *slot = tile_status.at(tile_index(bel)).boundcells.at(bel.index);
        return slot;
    }

    BelRange getBels() const override
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

    WireId getBelPinWire(BelId bel, IdString pin) const override;

    BelPinRange getWireBelPins(WireId wire) const override
    {
        BelPinRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.ptr = loc_info(wire)->wire_data[wire.index].bel_pins.begin();
        range.b.wire_loc = wire.location;
        range.e.ptr = loc_info(wire)->wire_data[wire.index].bel_pins.end();
        range.e.wire_loc = wire.location;
        return range;
    }

    std::vector<IdString> getBelPins(BelId bel) const override;

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

    std::vector<std::pair<IdString, std::string>> getWireAttrs(WireId) const override;

    uint32_t getWireChecksum(WireId wire) const override { return wire.index; }

    uint32_t get_wire_vecidx(const WireId &e) const
    {
        uint32_t tile = e.location.y * chip_info->width + e.location.x;
        int32_t base = wire_tile_vecidx.at(tile);
        NPNR_ASSERT(base != -1);
        int32_t i = base + e.index;
        return i;
    }

    void bindWire(WireId wire, NetInfo *net, PlaceStrength strength) override
    {
        NPNR_ASSERT(wire != WireId());
        auto &w2n_entry = wire2net.at(get_wire_vecidx(wire));
        NPNR_ASSERT(w2n_entry == nullptr);
        net->wires[wire].pip = PipId();
        net->wires[wire].strength = strength;
        w2n_entry = net;
        this->refreshUiWire(wire);
    }
    void unbindWire(WireId wire) override
    {
        NPNR_ASSERT(wire != WireId());
        auto &w2n_entry = wire2net.at(get_wire_vecidx(wire));
        NPNR_ASSERT(w2n_entry != nullptr);

        auto &net_wires = w2n_entry->wires;
        auto it = net_wires.find(wire);
        NPNR_ASSERT(it != net_wires.end());

        auto pip = it->second.pip;
        if (pip != PipId()) {
            pip2net.at(get_pip_vecidx(pip)) = nullptr;
            wire_fanout[get_wire_vecidx(getPipSrcWire(pip))]--;
        }

        net_wires.erase(it);
        w2n_entry = nullptr;
        this->refreshUiWire(wire);
    }
    virtual bool checkWireAvail(WireId wire) const override { return getBoundWireNet(wire) == nullptr; }
    NetInfo *getBoundWireNet(WireId wire) const override { return wire2net.at(get_wire_vecidx(wire)); }

    DelayQuad getWireDelay(WireId wire) const override { return DelayQuad(0); }

    WireRange getWires() const override
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
        for (int i = 0; i < loc_info(wireId)->wire_data.ssize(); i++) {
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

    uint32_t getPipChecksum(PipId pip) const override { return pip.index; }

    uint32_t get_pip_vecidx(const PipId &e) const
    {
        uint32_t tile = e.location.y * chip_info->width + e.location.x;
        int32_t base = pip_tile_vecidx.at(tile);
        NPNR_ASSERT(base != -1);
        int32_t i = base + e.index;
        return i;
    }

    void bindPip(PipId pip, NetInfo *net, PlaceStrength strength) override
    {
        NPNR_ASSERT(pip != PipId());
        wire_fanout[get_wire_vecidx(getPipSrcWire(pip))]++;

        auto &p2n_entry = pip2net.at(get_pip_vecidx(pip));
        NPNR_ASSERT(p2n_entry == nullptr);
        p2n_entry = net;

        WireId dst = this->getPipDstWire(pip);
        auto &w2n_entry = wire2net.at(get_wire_vecidx(dst));
        NPNR_ASSERT(w2n_entry == nullptr);
        w2n_entry = net;
        net->wires[dst].pip = pip;
        net->wires[dst].strength = strength;
    }

    void unbindPip(PipId pip) override
    {
        NPNR_ASSERT(pip != PipId());
        wire_fanout[get_wire_vecidx(getPipSrcWire(pip))]--;

        auto &p2n_entry = pip2net.at(get_pip_vecidx(pip));
        NPNR_ASSERT(p2n_entry != nullptr);
        WireId dst = this->getPipDstWire(pip);

        auto &w2n_entry = wire2net.at(get_wire_vecidx(dst));
        NPNR_ASSERT(w2n_entry != nullptr);
        w2n_entry = nullptr;

        p2n_entry->wires.erase(dst);
        p2n_entry = nullptr;
    }
    bool is_pip_blocked(PipId pip) const
    {
        auto &pip_data = loc_info(pip)->pip_data[pip.index];
        int lp = pip_data.lutperm_flags;
        if (is_lutperm_pip(lp)) {
            if (disable_router_lutperm)
                return true;
            auto rule = lutperm_allowed.at(get_slice_index(pip.location.x, pip.location.y, lutperm_lut(lp) / 2));
            if (rule == LutPermRule::NONE) {
                // Permutation not allowed
                return true;
            } else if (rule == LutPermRule::CARRY) {
                // Can swap A/B and C/D only
                int i = lutperm_out(lp), j = lutperm_in(lp);
                if ((i / 2) != (j / 2))
                    return true;
            }
        }
        return false;
    }
    bool checkPipAvail(PipId pip) const override { return (getBoundPipNet(pip) == nullptr) && !is_pip_blocked(pip); }
    bool checkPipAvailForNet(PipId pip, const NetInfo *net) const override
    {
        NetInfo *bound_net = getBoundPipNet(pip);
        return (bound_net == nullptr || bound_net == net) && !is_pip_blocked(pip);
    }
    NetInfo *getBoundPipNet(PipId pip) const override { return pip2net.at(get_pip_vecidx(pip)); }

    AllPipRange getPips() const override
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

    DelayQuad getPipDelay(PipId pip) const override
    {
        NPNR_ASSERT(pip != PipId());
        int fanout = wire_fanout[get_wire_vecidx(getPipSrcWire(pip))];
        delay_t min_dly =
                speed_grade->pip_classes[loc_info(pip)->pip_data[pip.index].timing_class].min_base_delay +
                fanout * speed_grade->pip_classes[loc_info(pip)->pip_data[pip.index].timing_class].min_fanout_adder;
        delay_t max_dly =
                speed_grade->pip_classes[loc_info(pip)->pip_data[pip.index].timing_class].max_base_delay +
                fanout * speed_grade->pip_classes[loc_info(pip)->pip_data[pip.index].timing_class].max_fanout_adder;
        return DelayQuad(min_dly, max_dly);
    }

    PipRange getPipsDownhill(WireId wire) const override
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = loc_info(wire)->wire_data[wire.index].pips_downhill.get();
        range.b.wire_loc = wire.location;
        range.e.cursor = range.b.cursor + loc_info(wire)->wire_data[wire.index].pips_downhill.size();
        range.e.wire_loc = wire.location;
        return range;
    }

    PipRange getPipsUphill(WireId wire) const override
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
    std::vector<GroupId> getGroups() const override;
    std::vector<BelId> getGroupBels(GroupId group) const override;
    std::vector<WireId> getGroupWires(GroupId group) const override;
    std::vector<PipId> getGroupPips(GroupId group) const override;
    std::vector<GroupId> getGroupGroups(GroupId group) const override;

    // -------------------------------------------------

    delay_t estimateDelay(WireId src, WireId dst) const override;
    BoundingBox getRouteBoundingBox(WireId src, WireId dst) const override;
    delay_t predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const override;
    delay_t getDelayEpsilon() const override { return 20; }
    delay_t getRipupDelayPenalty() const override;
    float getDelayNS(delay_t v) const override { return v * 0.001; }
    delay_t getDelayFromNS(float ns) const override { return delay_t(ns * 1000); }
    uint32_t getDelayChecksum(delay_t v) const override { return v; }

    // -------------------------------------------------

    bool pack() override;
    bool place() override;
    bool route() override;

    // -------------------------------------------------

    std::vector<GraphicElement> getDecalGraphics(DecalId decal) const override;

    DecalXY getBelDecal(BelId bel) const override;
    DecalXY getWireDecal(WireId wire) const override;
    DecalXY getPipDecal(PipId pip) const override;
    DecalXY getGroupDecal(GroupId group) const override;

    // -------------------------------------------------

    // Get the delay through a cell from one port to another, returning false
    // if no path exists
    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const override;
    // Get the port class, also setting clockInfoCount to the number of TimingClockingInfos associated with a port
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const override;
    // Get the TimingClockingInfo of a port
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const override;
    // Return true if a port is a net
    bool is_global_net(const NetInfo *net) const;

    bool get_delay_from_tmg_db(IdString tctype, IdString from, IdString to, DelayQuad &delay) const;
    void get_setuphold_from_tmg_db(IdString tctype, IdString clock, IdString port, DelayPair &setup,
                                   DelayPair &hold) const;

    // -------------------------------------------------
    // Placement validity checks
    bool isBelLocationValid(BelId bel, bool explain_invalid = false) const override;

    // Helper function for above
    bool slices_compatible(LogicTileStatus *lts) const;

    void assign_arch_info_for_cell(CellInfo *ci);
    void assignArchInfo() override;

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
    dict<WireId, std::pair<int, int>> wire_loc_overrides;
    void setup_wire_locations();

    mutable dict<DelayKey, std::pair<bool, DelayQuad>> celldelay_cache;

    static const std::string defaultPlacer;
    static const std::vector<std::string> availablePlacers;
    static const std::string defaultRouter;
    static const std::vector<std::string> availableRouters;

    std::vector<IdString> cell_types;
    std::vector<BelBucketId> buckets;

    mutable std::vector<TileStatus> tile_status;

    // -------------------------------------------------
    bool is_dsp_location_valid(CellInfo *cell) const;
    void remap_dsp_blocks();
    void remap_dsp_cell(CellInfo *ci, const std::array<IdString, 4> &ports, std::array<NetInfo *, 4> &assigned_nets);
};

NEXTPNR_NAMESPACE_END

#endif /* ECP5_ARCH_H */
