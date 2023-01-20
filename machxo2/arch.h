/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2021  William D. Jones <wjones@wdj-consulting.com>
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

#ifndef MACHXO2_ARCH_H
#define MACHXO2_ARCH_H

#include <cstdint>
#include <set>

#include "base_arch.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

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

    const T &operator[](std::size_t index) const { return get()[index]; }

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

    mutable dict<IdStringList, PipId> pip_by_name;

    // fast access to  X and Y IdStrings for building object names
    std::vector<IdString> x_ids, y_ids;
    // inverse of the above for name->object mapping
    dict<IdString, int> id_to_x, id_to_y;

    // Helpers
    template <typename Id> const TileTypePOD *tile_info(Id &id) const
    {
        return &(chip_info->tiles[id.location.y * chip_info->width + id.location.x]);
    }

    int get_bel_flat_index(BelId bel) const
    {
        return (bel.location.y * chip_info->width + bel.location.x) * max_loc_bels + bel.index;
    }

    // ---------------------------------------------------------------
    // Common Arch API. Every arch must provide the following methods.

    // General
    ArchArgs args;
    Arch(ArchArgs args);

    static bool is_available(ArchArgs::ArchArgsTypes chip);
    static std::vector<std::string> get_supported_packages(ArchArgs::ArchArgsTypes chip);

    std::string getChipName() const override;
    // Extra helper
    std::string get_full_chip_name() const;

    IdString archId() const override { return id_machxo2; }
    ArchArgs archArgs() const override { return args; }
    IdString archArgsToId(ArchArgs args) const override;

    static const int max_loc_bels = 20;

    int getGridDimX() const override { return chip_info->width; }
    int getGridDimY() const override { return chip_info->height; }
    int getTileBelDimZ(int x, int y) const override { return max_loc_bels; }
    // TODO: Make more precise? The CENTER MUX having config bits across
    // tiles can complicate this?
    int getTilePipDimZ(int x, int y) const override { return 2; }

    char getNameDelimiter() const override { return '/'; }

    // Bels
    BelId getBelByName(IdStringList name) const override;

    IdStringList getBelName(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        std::array<IdString, 3> ids{x_ids.at(bel.location.x), y_ids.at(bel.location.y),
                                    id(tile_info(bel)->bel_data[bel.index].name.get())};
        return IdStringList(ids);
    }

    Loc getBelLocation(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        Loc loc;
        loc.x = bel.location.x;
        loc.y = bel.location.y;
        loc.z = tile_info(bel)->bel_data[bel.index].z;
        return loc;
    }

    BelId getBelByLocation(Loc loc) const override;
    BelRange getBelsByTile(int x, int y) const override;
    bool getBelGlobalBuf(BelId bel) const override;

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
        id.index = tile_info(bel)->bel_data[bel.index].type;
        return id;
    }

    WireId getBelPinWire(BelId bel, IdString pin) const override;
    PortType getBelPinType(BelId bel, IdString pin) const override;
    std::vector<IdString> getBelPins(BelId bel) const override;

    // Package
    BelId getPackagePinBel(const std::string &pin) const;

    // Wires
    WireId getWireByName(IdStringList name) const override;

    IdStringList getWireName(WireId wire) const override
    {
        NPNR_ASSERT(wire != WireId());
        std::array<IdString, 3> ids{x_ids.at(wire.location.x), y_ids.at(wire.location.y),
                                    id(tile_info(wire)->wire_data[wire.index].name.get())};
        return IdStringList(ids);
    }

    DelayQuad getWireDelay(WireId wire) const override { return DelayQuad(0.01); }

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

    BelPinRange getWireBelPins(WireId wire) const override
    {
        BelPinRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.ptr = tile_info(wire)->wire_data[wire.index].bel_pins.get();
        range.b.wire_loc = wire.location;
        range.e.ptr = range.b.ptr + tile_info(wire)->wire_data[wire.index].num_bel_pins;
        range.e.wire_loc = wire.location;
        return range;
    }

    // Pips
    PipId getPipByName(IdStringList name) const override;
    IdStringList getPipName(PipId pip) const override;

    AllPipRange getPips() const override
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

    Loc getPipLocation(PipId pip) const override
    {
        Loc loc;
        loc.x = pip.location.x;
        loc.y = pip.location.y;

        // FIXME: Some Pip's config bits span across tiles. Will Z
        // be affected by this?
        loc.z = 0;
        return loc;
    }

    WireId getPipSrcWire(PipId pip) const override
    {
        WireId wire;
        NPNR_ASSERT(pip != PipId());
        wire.index = tile_info(pip)->pips_data[pip.index].src_idx;
        wire.location = tile_info(pip)->pips_data[pip.index].src;
        return wire;
    }

    WireId getPipDstWire(PipId pip) const override
    {
        WireId wire;
        NPNR_ASSERT(pip != PipId());
        wire.index = tile_info(pip)->pips_data[pip.index].dst_idx;
        wire.location = tile_info(pip)->pips_data[pip.index].dst;
        return wire;
    }

    DelayQuad getPipDelay(PipId pip) const override { return DelayQuad(0.01); }

    PipRange getPipsDownhill(WireId wire) const override
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = tile_info(wire)->wire_data[wire.index].pips_downhill.get();
        range.b.wire_loc = wire.location;
        range.e.cursor = range.b.cursor + tile_info(wire)->wire_data[wire.index].num_downhill;
        range.e.wire_loc = wire.location;
        return range;
    }

    PipRange getPipsUphill(WireId wire) const override
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = tile_info(wire)->wire_data[wire.index].pips_uphill.get();
        range.b.wire_loc = wire.location;
        range.e.cursor = range.b.cursor + tile_info(wire)->wire_data[wire.index].num_uphill;
        range.e.wire_loc = wire.location;
        return range;
    }

    // Extra Pip helpers.
    int8_t get_pip_class(PipId pip) const { return tile_info(pip)->pips_data[pip.index].pip_type; }

    std::string get_pip_tilename(PipId pip) const
    {
        auto &tileloc = chip_info->tile_info[pip.location.y * chip_info->width + pip.location.x];
        for (int i = 0; i < tileloc.num_tiles; i++) {
            if (tileloc.tile_names[i].type_idx == tile_info(pip)->pips_data[pip.index].tile_type)
                return tileloc.tile_names[i].name.get();
        }
        NPNR_ASSERT_FALSE("failed to find Pip tile");
    }

    // Delay
    delay_t estimateDelay(WireId src, WireId dst) const override;
    delay_t predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const override;
    delay_t getDelayEpsilon() const override { return 0.001; }
    delay_t getRipupDelayPenalty() const override { return 0.015; }
    float getDelayNS(delay_t v) const override { return v; }

    delay_t getDelayFromNS(float ns) const override { return ns; }

    uint32_t getDelayChecksum(delay_t v) const override { return v; }

    BoundingBox getRouteBoundingBox(WireId src, WireId dst) const override;

    // Flow
    bool pack() override;
    bool place() override;
    bool route() override;

    // Placer
    bool isBelLocationValid(BelId bel, bool explain_invalid = false) const override;

    static const std::string defaultPlacer;
    static const std::vector<std::string> availablePlacers;
    static const std::string defaultRouter;
    static const std::vector<std::string> availableRouters;

    // ---------------------------------------------------------------
    // Internal usage
    bool cells_compatible(const CellInfo **cells, int count) const;

    std::vector<std::pair<std::string, std::string>> get_tiles_at_location(int row, int col);
    std::string get_tile_by_type_and_loc(int row, int col, std::string type) const
    {
        auto &tileloc = chip_info->tile_info[row * chip_info->width + col];
        for (int i = 0; i < tileloc.num_tiles; i++) {
            if (chip_info->tiletype_names[tileloc.tile_names[i].type_idx].get() == type)
                return tileloc.tile_names[i].name.get();
        }
        NPNR_ASSERT_FALSE_STR("no tile at (" + std::to_string(col) + ", " + std::to_string(row) + ") with type " +
                              type);
    }

    std::string get_tile_by_type_and_loc(int row, int col, const std::set<std::string> &type) const
    {
        auto &tileloc = chip_info->tile_info[row * chip_info->width + col];
        for (int i = 0; i < tileloc.num_tiles; i++) {
            if (type.count(chip_info->tiletype_names[tileloc.tile_names[i].type_idx].get()))
                return tileloc.tile_names[i].name.get();
        }
        NPNR_ASSERT_FALSE_STR("no tile at (" + std::to_string(col) + ", " + std::to_string(row) + ") with type in set");
    }

    std::string get_tile_by_type(std::string type) const
    {
        for (int i = 0; i < chip_info->height * chip_info->width; i++) {
            auto &tileloc = chip_info->tile_info[i];
            for (int j = 0; j < tileloc.num_tiles; j++)
                if (chip_info->tiletype_names[tileloc.tile_names[j].type_idx].get() == type)
                    return tileloc.tile_names[j].name.get();
        }
        NPNR_ASSERT_FALSE_STR("no tile with type " + type);
    }
};

NEXTPNR_NAMESPACE_END

#endif /* MACHXO2_ARCH_H */
