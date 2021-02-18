/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Wolf <claire@symbioticeda.com>
 *  Copyright (C) 2018-19  David Shah <david@symbioticeda.com>
 *  Copyright (C) 2021  Symbiflow Authors
 *
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

#include <boost/iostreams/device/mapped_file.hpp>

#include <iostream>

#include "constraints.h"

NEXTPNR_NAMESPACE_BEGIN

/**** Everything in this section must be kept in sync with chipdb.py ****/

#include "relptr.h"

// Flattened site indexing.
//
// To enable flat BelId.z spaces, every tile and sites within that tile are
// flattened.
//
// This has implications on BelId's, WireId's and PipId's.
// The flattened site space works as follows:
//  - Objects that belong to the tile are first.  BELs are always part of Sites,
//    so no BEL objects are in this category.
//  - All site alternative modes are exposed as a "full" site.
//  - Each site appends it's BEL's, wires (site wires) and PIP's.
//   - Sites add two types of pips.  Sites will add pip data first for site
//     pips, and then for site pin edges.
//     1. The first type is site pips, which connect site wires to other site
//        wires.
//     2. The second type is site pin edges, which connect site wires to tile
//        wires (or vise-versa).

NPNR_PACKED_STRUCT(struct BelInfoPOD {
    int32_t name;       // bel name (in site) constid
    int32_t type;       // Type name constid
    int32_t bel_bucket; // BEL bucket constid.

    int32_t num_bel_wires;
    RelPtr<int32_t> ports; // port name constid
    RelPtr<int32_t> types; // port type (IN/OUT/BIDIR)
    RelPtr<int32_t> wires; // connected wire index in tile, or -1 if NA

    int16_t site;
    int16_t site_variant; // some sites have alternative types
    int16_t category;
    int16_t padding;

    RelPtr<int32_t> pin_map; // Index into CellMapPOD::cell_bel_map
});

enum BELCategory
{
    // BEL is a logic element
    BEL_CATEGORY_LOGIC = 0,
    // BEL is a site routing mux
    BEL_CATEGORY_ROUTING = 1,
    // BEL is a site port, e.g. boundry between site and routing graph.
    BEL_CATEGORY_SITE_PORT = 2
};

NPNR_PACKED_STRUCT(struct BelPortPOD {
    int32_t bel_index;
    int32_t port;
});

NPNR_PACKED_STRUCT(struct TileWireInfoPOD {
    int32_t name; // wire name constid

    // Pip index inside tile
    RelSlice<int32_t> pips_uphill;

    // Pip index inside tile
    RelSlice<int32_t> pips_downhill;

    // Bel index inside tile
    RelSlice<BelPortPOD> bel_pins;

    int16_t site;         // site index in tile
    int16_t site_variant; // site variant index in tile
});

NPNR_PACKED_STRUCT(struct PipInfoPOD {
    int32_t src_index, dst_index;
    int16_t site;         // site index in tile
    int16_t site_variant; // site variant index in tile
    int16_t bel;          // BEL this pip belongs to if site pip.
    int16_t extra_data;
});

NPNR_PACKED_STRUCT(struct ConstraintTagPOD {
    int32_t tag_prefix;       // constid
    int32_t default_state;    // constid
    RelSlice<int32_t> states; // constid
});

NPNR_PACKED_STRUCT(struct TileTypeInfoPOD {
    int32_t name; // Tile type constid

    int32_t number_sites;

    RelSlice<BelInfoPOD> bel_data;

    RelSlice<TileWireInfoPOD> wire_data;

    RelSlice<PipInfoPOD> pip_data;

    RelSlice<ConstraintTagPOD> tags;
});

NPNR_PACKED_STRUCT(struct SiteInstInfoPOD {
    RelPtr<char> name;
    RelPtr<char> site_name;

    // Which site type is this site instance?
    // constid
    int32_t site_type;
});

NPNR_PACKED_STRUCT(struct TileInstInfoPOD {
    // Name of this tile.
    RelPtr<char> name;

    // Index into root.tile_types.
    int32_t type;

    // This array is root.tile_types[type].number_sites long.
    // Index into root.sites
    RelSlice<int32_t> sites;

    // Number of tile wires; excluding any site-internal wires
    // which come after general wires and are not stored here
    // as they will never be nodal
    // -1 if a tile-local wire; node index if nodal wire
    RelSlice<int32_t> tile_wire_to_node;
});

NPNR_PACKED_STRUCT(struct TileWireRefPOD {
    int32_t tile;
    int32_t index;
});

NPNR_PACKED_STRUCT(struct NodeInfoPOD { RelSlice<TileWireRefPOD> tile_wires; });

NPNR_PACKED_STRUCT(struct CellBelPinPOD {
    int32_t cell_pin; // constid
    int32_t bel_pin;  // constid
});

NPNR_PACKED_STRUCT(struct ParameterPinsPOD {
    int32_t key;   // constid
    int32_t value; // constid
    RelSlice<CellBelPinPOD> pins;
});

NPNR_PACKED_STRUCT(struct CellConstraintPOD {
    int32_t tag;              // Tag index
    int32_t constraint_type;  // Constraint::ConstraintType
    RelSlice<int32_t> states; // State indicies
});

NPNR_PACKED_STRUCT(struct CellBelMapPOD {
    RelSlice<CellBelPinPOD> common_pins;
    RelSlice<ParameterPinsPOD> parameter_pins;
    RelSlice<CellConstraintPOD> constraints;
});

NPNR_PACKED_STRUCT(struct CellMapPOD {
    // Cell names supported in this arch.
    RelSlice<int32_t> cell_names;       // constids
    RelSlice<int32_t> cell_bel_buckets; // constids

    RelSlice<CellBelMapPOD> cell_bel_map;
});

NPNR_PACKED_STRUCT(struct PackagePinPOD {
    int32_t package_pin; // constid
    int32_t site;        // constid
    int32_t bel;         // constid
});

NPNR_PACKED_STRUCT(struct PackagePOD {
    int32_t package; // constid
    RelSlice<PackagePinPOD> pins;
});

NPNR_PACKED_STRUCT(struct ChipInfoPOD {
    RelPtr<char> name;
    RelPtr<char> generator;

    int32_t version;
    int32_t width, height;

    RelSlice<TileTypeInfoPOD> tile_types;
    RelSlice<SiteInstInfoPOD> sites;
    RelSlice<TileInstInfoPOD> tiles;
    RelSlice<NodeInfoPOD> nodes;
    RelSlice<PackagePOD> packages;

    // BEL bucket constids.
    RelSlice<int32_t> bel_buckets;

    RelPtr<CellMapPOD> cell_map;

    // Constid string data.
    RelPtr<RelSlice<RelPtr<char>>> constids;
});

/************************ End of chipdb section. ************************/

inline const TileTypeInfoPOD &tile_info(const ChipInfoPOD *chip_info, int32_t tile)
{
    return chip_info->tile_types[chip_info->tiles[tile].type];
}

template <typename Id> const TileTypeInfoPOD &loc_info(const ChipInfoPOD *chip_info, Id &id)
{
    return chip_info->tile_types[chip_info->tiles[id.tile].type];
}

inline const BelInfoPOD &bel_info(const ChipInfoPOD *chip_info, BelId bel)
{
    NPNR_ASSERT(bel != BelId());
    return loc_info(chip_info, bel).bel_data[bel.index];
}

inline const PipInfoPOD &pip_info(const ChipInfoPOD *chip_info, PipId pip)
{
    NPNR_ASSERT(pip != PipId());
    return loc_info(chip_info, pip).pip_data[pip.index];
}

inline const SiteInstInfoPOD &site_inst_info(const ChipInfoPOD *chip_info, int32_t tile, int32_t site)
{
    return chip_info->sites[chip_info->tiles[tile].sites[site]];
}

struct BelIterator
{
    const ChipInfoPOD *chip;
    int cursor_index;
    int cursor_tile;

    BelIterator operator++()
    {
        cursor_index++;
        while (cursor_tile < chip->tiles.ssize() && cursor_index >= tile_info(chip, cursor_tile).bel_data.ssize()) {
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
        ret.tile = cursor_tile;
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

struct FilteredBelIterator
{
    std::function<bool(BelId)> filter;
    BelIterator b, e;

    FilteredBelIterator operator++()
    {
        ++b;
        while (b != e) {
            if (filter(*b)) {
                break;
            }

            ++b;
        }
        return *this;
    }

    bool operator!=(const FilteredBelIterator &other) const
    {
        NPNR_ASSERT(e == other.e);
        return b != other.b;
    }

    bool operator==(const FilteredBelIterator &other) const
    {
        NPNR_ASSERT(e == other.e);
        return b == other.b;
    }

    BelId operator*() const
    {
        BelId bel = *b;
        NPNR_ASSERT(filter(bel));
        return bel;
    }
};

struct FilteredBelRange
{
    FilteredBelRange(BelIterator bel_b, BelIterator bel_e, std::function<bool(BelId)> filter)
    {
        b.filter = filter;
        b.b = bel_b;
        b.e = bel_e;

        if (b.b != b.e && !filter(*b.b)) {
            ++b;
        }

        e.b = bel_e;
        e.e = bel_e;

        if (b != e) {
            NPNR_ASSERT(filter(*b.b));
        }
    }

    FilteredBelIterator b, e;
    FilteredBelIterator begin() const { return b; }
    FilteredBelIterator end() const { return e; }
};

// -----------------------------------------------------------------------

// Iterate over TileWires for a wire (will be more than one if nodal)
struct TileWireIterator
{
    const ChipInfoPOD *chip;
    WireId baseWire;
    int cursor = -1;

    void operator++() { cursor++; }

    bool operator==(const TileWireIterator &other) const { return cursor == other.cursor; }
    bool operator!=(const TileWireIterator &other) const { return cursor != other.cursor; }

    // Returns a *denormalised* identifier always pointing to a tile wire rather than a node
    WireId operator*() const
    {
        if (baseWire.tile == -1) {
            WireId tw;
            const auto &node_wire = chip->nodes[baseWire.index].tile_wires[cursor];
            tw.tile = node_wire.tile;
            tw.index = node_wire.index;
            return tw;
        } else {
            return baseWire;
        }
    }
};

struct TileWireRange
{
    TileWireIterator b, e;
    TileWireIterator begin() const { return b; }
    TileWireIterator end() const { return e; }
};

inline WireId canonical_wire(const ChipInfoPOD *chip_info, int32_t tile, int32_t wire)
{
    WireId id;

    if (wire >= chip_info->tiles[tile].tile_wire_to_node.ssize()) {
        // Cannot be a nodal wire
        id.tile = tile;
        id.index = wire;
    } else {
        int32_t node = chip_info->tiles[tile].tile_wire_to_node[wire];
        if (node == -1) {
            // Not a nodal wire
            id.tile = tile;
            id.index = wire;
        } else {
            // Is a nodal wire, set tile to -1
            id.tile = -1;
            id.index = node;
        }
    }

    return id;
}

// -----------------------------------------------------------------------

struct WireIterator
{
    const ChipInfoPOD *chip;
    int cursor_index = 0;
    int cursor_tile = -1;

    WireIterator operator++()
    {
        // Iterate over nodes first, then tile wires that aren't nodes
        do {
            cursor_index++;
            if (cursor_tile == -1 && cursor_index >= chip->nodes.ssize()) {
                cursor_tile = 0;
                cursor_index = 0;
            }
            while (cursor_tile != -1 && cursor_tile < chip->tiles.ssize() &&
                   cursor_index >= chip->tile_types[chip->tiles[cursor_tile].type].wire_data.ssize()) {
                cursor_index = 0;
                cursor_tile++;
            }

        } while ((cursor_tile != -1 && cursor_tile < chip->tiles.ssize() &&
                  cursor_index < chip->tiles[cursor_tile].tile_wire_to_node.ssize() &&
                  chip->tiles[cursor_tile].tile_wire_to_node[cursor_index] != -1));

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
        ret.tile = cursor_tile;
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
        while (cursor_tile < chip->tiles.ssize() &&
               cursor_index >= chip->tile_types[chip->tiles[cursor_tile].type].pip_data.ssize()) {
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
        ret.tile = cursor_tile;
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

struct UphillPipIterator
{
    const ChipInfoPOD *chip;
    TileWireIterator twi, twi_end;
    int cursor = -1;

    void operator++()
    {
        cursor++;
        while (true) {
            if (!(twi != twi_end))
                break;
            WireId w = *twi;
            auto &tile = chip->tile_types[chip->tiles[w.tile].type];
            if (cursor < tile.wire_data[w.index].pips_uphill.ssize())
                break;
            ++twi;
            cursor = 0;
        }
    }
    bool operator!=(const UphillPipIterator &other) const { return twi != other.twi || cursor != other.cursor; }

    PipId operator*() const
    {
        PipId ret;
        WireId w = *twi;
        ret.tile = w.tile;
        ret.index = chip->tile_types[chip->tiles[w.tile].type].wire_data[w.index].pips_uphill[cursor];
        return ret;
    }
};

struct UphillPipRange
{
    UphillPipIterator b, e;
    UphillPipIterator begin() const { return b; }
    UphillPipIterator end() const { return e; }
};

struct DownhillPipIterator
{
    const ChipInfoPOD *chip;
    TileWireIterator twi, twi_end;
    int cursor = -1;

    void operator++()
    {
        cursor++;
        while (true) {
            if (!(twi != twi_end))
                break;
            WireId w = *twi;
            auto &tile = chip->tile_types[chip->tiles[w.tile].type];
            if (cursor < tile.wire_data[w.index].pips_downhill.ssize())
                break;
            ++twi;
            cursor = 0;
        }
    }
    bool operator!=(const DownhillPipIterator &other) const { return twi != other.twi || cursor != other.cursor; }

    PipId operator*() const
    {
        PipId ret;
        WireId w = *twi;
        ret.tile = w.tile;
        ret.index = chip->tile_types[chip->tiles[w.tile].type].wire_data[w.index].pips_downhill[cursor];
        return ret;
    }
};

struct DownhillPipRange
{
    DownhillPipIterator b, e;
    DownhillPipIterator begin() const { return b; }
    DownhillPipIterator end() const { return e; }
};

struct BelPinIterator
{
    const ChipInfoPOD *chip;
    TileWireIterator twi, twi_end;
    int cursor = -1;

    void operator++()
    {
        cursor++;

        while (twi != twi_end) {
            WireId w = *twi;
            auto &tile = tile_info(chip, w.tile);
            if (cursor < tile.wire_data[w.index].bel_pins.ssize())
                break;

            ++twi;
            cursor = 0;
        }
    }
    bool operator!=(const BelPinIterator &other) const { return twi != other.twi || cursor != other.cursor; }

    BelPin operator*() const
    {
        BelPin ret;
        WireId w = *twi;
        ret.bel.tile = w.tile;
        ret.bel.index = tile_info(chip, w.tile).wire_data[w.index].bel_pins[cursor].bel_index;
        ret.pin.index = tile_info(chip, w.tile).wire_data[w.index].bel_pins[cursor].port;
        return ret;
    }
};

struct BelPinRange
{
    BelPinIterator b, e;
    BelPinIterator begin() const { return b; }
    BelPinIterator end() const { return e; }
};

struct IdStringIterator : std::iterator<std::forward_iterator_tag,
                                        /*T=*/IdString,
                                        /*Distance=*/ptrdiff_t,
                                        /*pointer=*/IdString *,
                                        /*reference=*/IdString>
{
    const int32_t *cursor;

    void operator++() { cursor += 1; }

    bool operator!=(const IdStringIterator &other) const { return cursor != other.cursor; }

    bool operator==(const IdStringIterator &other) const { return cursor == other.cursor; }

    IdString operator*() const { return IdString(*cursor); }
};

struct IdStringRange
{
    IdStringIterator b, e;
    IdStringIterator begin() const { return b; }
    IdStringIterator end() const { return e; }
};

struct BelBucketIterator
{
    IdStringIterator cursor;

    void operator++() { ++cursor; }

    bool operator!=(const BelBucketIterator &other) const { return cursor != other.cursor; }

    bool operator==(const BelBucketIterator &other) const { return cursor == other.cursor; }

    BelBucketId operator*() const
    {
        BelBucketId bucket;
        bucket.name = IdString(*cursor);
        return bucket;
    }
};

struct BelBucketRange
{
    BelBucketIterator b, e;
    BelBucketIterator begin() const { return b; }
    BelBucketIterator end() const { return e; }
};

struct ArchArgs
{
    std::string chipdb;
    std::string package;
};

struct ArchRanges
{
    using ArchArgsT = ArchArgs;
    // Bels
    using AllBelsRangeT = BelRange;
    using TileBelsRangeT = BelRange;
    using BelAttrsRangeT = std::vector<std::pair<IdString, std::string>>;
    using BelPinsRangeT = IdStringRange;
    using CellBelPinRangeT = const std::vector<IdString> &;
    // Wires
    using AllWiresRangeT = WireRange;
    using DownhillPipRangeT = DownhillPipRange;
    using UphillPipRangeT = UphillPipRange;
    using WireBelPinRangeT = BelPinRange;
    using WireAttrsRangeT = std::vector<std::pair<IdString, std::string>>;
    // Pips
    using AllPipsRangeT = AllPipRange;
    using PipAttrsRangeT = std::vector<std::pair<IdString, std::string>>;
    // Groups
    using AllGroupsRangeT = std::vector<GroupId>;
    using GroupBelsRangeT = std::vector<BelId>;
    using GroupWiresRangeT = std::vector<WireId>;
    using GroupPipsRangeT = std::vector<PipId>;
    using GroupGroupsRangeT = std::vector<GroupId>;
    // Decals
    using DecalGfxRangeT = std::vector<GraphicElement>;
    // Placement validity
    using CellTypeRangeT = const IdStringRange;
    using BelBucketRangeT = const BelBucketRange;
    using BucketBelRangeT = FilteredBelRange;
};

struct Arch : ArchAPI<ArchRanges>
{
    boost::iostreams::mapped_file_source blob_file;
    const ChipInfoPOD *chip_info;
    int32_t package_index;

    mutable std::unordered_map<IdString, int> tile_by_name;
    mutable std::unordered_map<IdString, std::pair<int, int>> site_by_name;

    std::unordered_map<WireId, NetInfo *> wire_to_net;
    std::unordered_map<PipId, NetInfo *> pip_to_net;
    std::unordered_map<WireId, std::pair<int, int>> driving_pip_loc;
    std::unordered_map<WireId, NetInfo *> reserved_wires;

    static constexpr size_t kMaxState = 8;

    struct TileStatus;
    struct SiteRouter
    {
        SiteRouter(int16_t site) : site(site), dirty(false), site_ok(true) {}

        std::unordered_set<CellInfo *> cells_in_site;
        const int16_t site;

        mutable bool dirty;
        mutable bool site_ok;

        void bindBel(CellInfo *cell);
        void unbindBel(CellInfo *cell);
        bool checkSiteRouting(const Context *ctx, const TileStatus &tile_status) const;
    };

    struct TileStatus
    {
        std::vector<ExclusiveStateGroup<kMaxState>> tags;
        std::vector<CellInfo *> boundcells;
        std::vector<SiteRouter> sites;
    };

    std::unordered_map<int32_t, TileStatus> tileStatus;

    ArchArgs args;
    Arch(ArchArgs args);

    std::string getChipName() const override;

    IdString archId() const override { return id(chip_info->name.get()); }
    ArchArgs archArgs() const override { return args; }
    IdString archArgsToId(ArchArgs args) const override;

    // -------------------------------------------------

    uint32_t get_tile_index(int x, int y) const { return (y * chip_info->width + x); }
    uint32_t get_tile_index(Loc loc) const { return get_tile_index(loc.x, loc.y); }
    template <typename TileIndex, typename CoordIndex>
    void get_tile_x_y(TileIndex tile_index, CoordIndex *x, CoordIndex *y) const
    {
        *x = tile_index % chip_info->width;
        *y = tile_index / chip_info->width;
    }

    template <typename TileIndex> void get_tile_loc(TileIndex tile_index, Loc *loc) const
    {
        get_tile_x_y(tile_index, &loc->x, &loc->y);
    }

    int getGridDimX() const override { return chip_info->width; }
    int getGridDimY() const override { return chip_info->height; }
    int getTileBelDimZ(int x, int y) const override
    {
        return chip_info->tile_types[chip_info->tiles[get_tile_index(x, y)].type].bel_data.size();
    }
    int getTilePipDimZ(int x, int y) const override
    {
        return chip_info->tile_types[chip_info->tiles[get_tile_index(x, y)].type].number_sites;
    }
    char getNameDelimiter() const override { return '/'; }

    std::string get_part() const;

    // -------------------------------------------------

    void setup_byname() const;

    BelId getBelByName(IdStringList name) const override;

    IdStringList getBelName(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        const SiteInstInfoPOD &site = get_site_inst(bel);
        std::array<IdString, 2> ids{id(site.name.get()), IdString(bel_info(chip_info, bel).name)};
        return IdStringList(ids);
    }

    uint32_t getBelChecksum(BelId bel) const override { return bel.index; }

    void map_cell_pins(CellInfo *cell, int32_t mapping) const;
    void map_port_pins(BelId bel, CellInfo *cell) const;

    TileStatus &get_tile_status(int32_t tile)
    {
        auto result = tileStatus.emplace(tile, TileStatus());
        if (result.second) {
            auto &tile_type = chip_info->tile_types[chip_info->tiles[tile].type];
            result.first->second.boundcells.resize(tile_type.bel_data.size());
            result.first->second.tags.resize(default_tags.size());

            result.first->second.sites.reserve(tile_type.number_sites);
            for (size_t i = 0; i < tile_type.number_sites; ++i) {
                result.first->second.sites.push_back(SiteRouter(i));
            }
        }

        return result.first->second;
    }

    const SiteRouter &get_site_status(const TileStatus &tile_status, const BelInfoPOD &bel_data) const
    {
        return tile_status.sites.at(bel_data.site);
    }

    SiteRouter &get_site_status(TileStatus &tile_status, const BelInfoPOD &bel_data)
    {
        return tile_status.sites.at(bel_data.site);
    }

    void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength) override
    {
        NPNR_ASSERT(bel != BelId());

        TileStatus &tile_status = get_tile_status(bel.tile);
        NPNR_ASSERT(tile_status.boundcells[bel.index] == nullptr);

        const auto &bel_data = bel_info(chip_info, bel);
        NPNR_ASSERT(bel_data.category == BEL_CATEGORY_LOGIC);

        if (io_port_types.count(cell->type) == 0) {
            int32_t mapping = bel_info(chip_info, bel).pin_map[get_cell_type_index(cell->type)];
            NPNR_ASSERT(mapping >= 0);

            if (cell->cell_mapping != mapping) {
                map_cell_pins(cell, mapping);
            }
            constraints.bindBel(tile_status.tags.data(), get_cell_constraints(bel, cell->type));
        } else {
            map_port_pins(bel, cell);
            // FIXME: Probably need to actually constraint io port cell/bel,
            // but the current BBA emission doesn't support that.  This only
            // really matters if the placer can choose IO port locations.
        }

        get_site_status(tile_status, bel_data).bindBel(cell);

        tile_status.boundcells[bel.index] = cell;

        cell->bel = bel;
        cell->belStrength = strength;

        refreshUiBel(bel);
    }

    void unbindBel(BelId bel) override
    {
        NPNR_ASSERT(bel != BelId());

        TileStatus &tile_status = get_tile_status(bel.tile);
        NPNR_ASSERT(tile_status.boundcells[bel.index] != nullptr);

        CellInfo *cell = tile_status.boundcells[bel.index];
        tile_status.boundcells[bel.index] = nullptr;

        cell->bel = BelId();
        cell->belStrength = STRENGTH_NONE;

        // FIXME: Probably need to actually constraint io port cell/bel,
        // but the current BBA emission doesn't support that.  This only
        // really matters if the placer can choose IO port locations.
        if (io_port_types.count(cell->type) == 0) {
            constraints.unbindBel(tile_status.tags.data(), get_cell_constraints(bel, cell->type));
        }

        const auto &bel_data = bel_info(chip_info, bel);
        get_site_status(tile_status, bel_data).unbindBel(cell);

        refreshUiBel(bel);
    }

    bool checkBelAvail(BelId bel) const override
    {
        // FIXME: This could consult the constraint system to see if this BEL
        // is blocked (e.g. site type is wrong).
        return getBoundBelCell(bel) == nullptr;
    }

    CellInfo *getBoundBelCell(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        auto iter = tileStatus.find(bel.tile);
        if (iter == tileStatus.end()) {
            return nullptr;
        } else {
            return iter->second.boundcells[bel.index];
        }
    }

    CellInfo *getConflictingBelCell(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        // FIXME: This could consult the constraint system to see why this BEL
        // is blocked.
        return getBoundBelCell(bel);
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

    Loc getBelLocation(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        Loc loc;
        get_tile_x_y(bel.tile, &loc.x, &loc.y);
        loc.z = bel.index;
        return loc;
    }

    BelId getBelByLocation(Loc loc) const override;
    BelRange getBelsByTile(int x, int y) const override;

    bool getBelGlobalBuf(BelId bel) const override
    {
        // FIXME: This probably needs to be fixed!
        return false;
    }

    bool getBelHidden(BelId bel) const override { return bel_info(chip_info, bel).category != BEL_CATEGORY_LOGIC; }

    IdString getBelType(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        return IdString(bel_info(chip_info, bel).type);
    }

    std::vector<std::pair<IdString, std::string>> getBelAttrs(BelId bel) const override;

    int get_bel_pin_index(BelId bel, IdString pin) const
    {
        NPNR_ASSERT(bel != BelId());
        int num_bel_wires = bel_info(chip_info, bel).num_bel_wires;
        const int32_t *ports = bel_info(chip_info, bel).ports.get();
        for (int i = 0; i < num_bel_wires; i++) {
            if (ports[i] == pin.index) {
                return i;
            }
        }

        return -1;
    }

    WireId getBelPinWire(BelId bel, IdString pin) const override;
    PortType getBelPinType(BelId bel, IdString pin) const override;

    IdStringRange getBelPins(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());

        int num_bel_wires = bel_info(chip_info, bel).num_bel_wires;
        const int32_t *ports = bel_info(chip_info, bel).ports.get();

        IdStringRange str_range;
        str_range.b.cursor = &ports[0];
        str_range.e.cursor = &ports[num_bel_wires];

        return str_range;
    }

    const std::vector<IdString> &getBelPinsForCellPin(CellInfo *cell_info, IdString pin) const override
    {
        return cell_info->cell_bel_pins.at(pin);
    }

    // -------------------------------------------------

    WireId getWireByName(IdStringList name) const override;

    const TileWireInfoPOD &wire_info(WireId wire) const
    {
        if (wire.tile == -1) {
            const TileWireRefPOD &wr = chip_info->nodes[wire.index].tile_wires[0];
            return chip_info->tile_types[chip_info->tiles[wr.tile].type].wire_data[wr.index];
        } else {
            return loc_info(chip_info, wire).wire_data[wire.index];
        }
    }

    IdStringList getWireName(WireId wire) const override
    {
        NPNR_ASSERT(wire != WireId());
        if (wire.tile != -1) {
            const auto &tile_type = loc_info(chip_info, wire);
            if (tile_type.wire_data[wire.index].site != -1) {
                const SiteInstInfoPOD &site = get_site_inst(wire);
                std::array<IdString, 2> ids{id(site.name.get()), IdString(tile_type.wire_data[wire.index].name)};
                return IdStringList(ids);
            }
        }

        int32_t tile = wire.tile == -1 ? chip_info->nodes[wire.index].tile_wires[0].tile : wire.tile;
        IdString tile_name = id(chip_info->tiles[tile].name.get());
        std::array<IdString, 2> ids{tile_name, IdString(wire_info(wire).name)};
        return IdStringList(ids);
    }

    IdString getWireType(WireId wire) const override;
    std::vector<std::pair<IdString, std::string>> getWireAttrs(WireId wire) const override;

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
            pip_to_net[pip] = nullptr;
        }

        net_wires.erase(it);
        wire_to_net[wire] = nullptr;
        refreshUiWire(wire);
    }

    bool checkWireAvail(WireId wire) const override
    {
        NPNR_ASSERT(wire != WireId());
        auto w2n = wire_to_net.find(wire);
        return w2n == wire_to_net.end() || w2n->second == nullptr;
    }

    NetInfo *getBoundWireNet(WireId wire) const override
    {
        NPNR_ASSERT(wire != WireId());
        auto w2n = wire_to_net.find(wire);
        return w2n == wire_to_net.end() ? nullptr : w2n->second;
    }

    WireId getConflictingWireWire(WireId wire) const override { return wire; }

    NetInfo *getConflictingWireNet(WireId wire) const override
    {
        NPNR_ASSERT(wire != WireId());
        auto w2n = wire_to_net.find(wire);
        return w2n == wire_to_net.end() ? nullptr : w2n->second;
    }

    DelayInfo getWireDelay(WireId wire) const override
    {
        DelayInfo delay;
        delay.delay = 0;
        return delay;
    }

    TileWireRange get_tile_wire_range(WireId wire) const
    {
        TileWireRange range;
        range.b.chip = chip_info;
        range.b.baseWire = wire;
        range.b.cursor = -1;
        ++range.b;

        range.e.chip = chip_info;
        range.e.baseWire = wire;
        if (wire.tile == -1) {
            range.e.cursor = chip_info->nodes[wire.index].tile_wires.size();
        } else {
            range.e.cursor = 1;
        }
        return range;
    }

    BelPinRange getWireBelPins(WireId wire) const override
    {
        BelPinRange range;
        NPNR_ASSERT(wire != WireId());

        TileWireRange twr = get_tile_wire_range(wire);
        range.b.chip = chip_info;
        range.b.twi = twr.b;
        range.b.twi_end = twr.e;
        range.b.cursor = -1;
        ++range.b;

        range.e.chip = chip_info;
        range.e.twi = twr.e;
        range.e.twi_end = twr.e;
        range.e.cursor = 0;
        return range;
    }

    WireRange getWires() const override
    {
        WireRange range;
        range.b.chip = chip_info;
        range.b.cursor_tile = -1;
        range.b.cursor_index = 0;
        range.e.chip = chip_info;
        range.e.cursor_tile = chip_info->tiles.size();
        range.e.cursor_index = 0;
        return range;
    }

    // -------------------------------------------------

    PipId getPipByName(IdStringList name) const override;
    IdStringList getPipName(PipId pip) const override;
    IdString getPipType(PipId pip) const override;
    std::vector<std::pair<IdString, std::string>> getPipAttrs(PipId pip) const override;

    void bindPip(PipId pip, NetInfo *net, PlaceStrength strength) override
    {
        NPNR_ASSERT(pip != PipId());
        NPNR_ASSERT(pip_to_net[pip] == nullptr);

        WireId dst = getPipDstWire(pip);
        NPNR_ASSERT(wire_to_net[dst] == nullptr || wire_to_net[dst] == net);

        pip_to_net[pip] = net;
        std::pair<int, int> loc;
        get_tile_x_y(pip.tile, &loc.first, &loc.second);
        driving_pip_loc[dst] = loc;

        wire_to_net[dst] = net;
        net->wires[dst].pip = pip;
        net->wires[dst].strength = strength;
        refreshUiPip(pip);
        refreshUiWire(dst);
    }

    void unbindPip(PipId pip) override
    {
        NPNR_ASSERT(pip != PipId());
        NPNR_ASSERT(pip_to_net[pip] != nullptr);

        WireId dst = getPipDstWire(pip);
        NPNR_ASSERT(wire_to_net[dst] != nullptr);
        wire_to_net[dst] = nullptr;
        pip_to_net[pip]->wires.erase(dst);

        pip_to_net[pip] = nullptr;
        refreshUiPip(pip);
        refreshUiWire(dst);
    }

    bool checkPipAvail(PipId pip) const override
    {
        NPNR_ASSERT(pip != PipId());
        return pip_to_net.find(pip) == pip_to_net.end() || pip_to_net.at(pip) == nullptr;
    }

    NetInfo *getBoundPipNet(PipId pip) const override
    {
        NPNR_ASSERT(pip != PipId());
        auto p2n = pip_to_net.find(pip);
        return p2n == pip_to_net.end() ? nullptr : p2n->second;
    }

    WireId getConflictingPipWire(PipId pip) const override { return getPipDstWire(pip); }

    NetInfo *getConflictingPipNet(PipId pip) const override
    {
        auto p2n = pip_to_net.find(pip);
        return p2n == pip_to_net.end() ? nullptr : p2n->second;
    }

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

    Loc getPipLocation(PipId pip) const override
    {
        Loc loc;
        get_tile_loc(pip.tile, &loc);
        loc.z = 0;
        return loc;
    }

    uint32_t getPipChecksum(PipId pip) const override { return pip.index; }

    WireId getPipSrcWire(PipId pip) const override
    {
        return canonical_wire(chip_info, pip.tile, loc_info(chip_info, pip).pip_data[pip.index].src_index);
    }

    WireId getPipDstWire(PipId pip) const override
    {
        return canonical_wire(chip_info, pip.tile, loc_info(chip_info, pip).pip_data[pip.index].dst_index);
    }

    DelayInfo getPipDelay(PipId pip) const override { return DelayInfo(); }

    DownhillPipRange getPipsDownhill(WireId wire) const override
    {
        DownhillPipRange range;
        NPNR_ASSERT(wire != WireId());
        TileWireRange twr = get_tile_wire_range(wire);
        range.b.chip = chip_info;
        range.b.twi = twr.b;
        range.b.twi_end = twr.e;
        range.b.cursor = -1;
        ++range.b;
        range.e.chip = chip_info;
        range.e.twi = twr.e;
        range.e.twi_end = twr.e;
        range.e.cursor = 0;
        return range;
    }

    UphillPipRange getPipsUphill(WireId wire) const override
    {
        UphillPipRange range;
        NPNR_ASSERT(wire != WireId());
        TileWireRange twr = get_tile_wire_range(wire);
        range.b.chip = chip_info;
        range.b.twi = twr.b;
        range.b.twi_end = twr.e;
        range.b.cursor = -1;
        ++range.b;
        range.e.chip = chip_info;
        range.e.twi = twr.e;
        range.e.twi_end = twr.e;
        range.e.cursor = 0;
        return range;
    }

    // -------------------------------------------------

    // FIXME: Use groups to get access to sites.
    GroupId getGroupByName(IdStringList name) const override { return GroupId(); }
    IdStringList getGroupName(GroupId group) const override { return IdStringList(); }
    std::vector<GroupId> getGroups() const override { return {}; }
    std::vector<BelId> getGroupBels(GroupId group) const override { return {}; }
    std::vector<WireId> getGroupWires(GroupId group) const override { return {}; }
    std::vector<PipId> getGroupPips(GroupId group) const override { return {}; }
    std::vector<GroupId> getGroupGroups(GroupId group) const override { return {}; }

    // -------------------------------------------------
    delay_t estimateDelay(WireId src, WireId dst) const override;
    delay_t predictDelay(const NetInfo *net_info, const PortRef &sink) const override;
    ArcBounds getRouteBoundingBox(WireId src, WireId dst) const override;
    delay_t getDelayEpsilon() const override { return 20; }
    delay_t getRipupDelayPenalty() const override { return 120; }
    float getDelayNS(delay_t v) const override { return v * 0.001; }
    DelayInfo getDelayFromNS(float ns) const override
    {
        DelayInfo del;
        del.delay = delay_t(ns * 1000);
        return del;
    }
    uint32_t getDelayChecksum(delay_t v) const override { return v; }
    bool getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const override;

    // -------------------------------------------------

    void place_iobufs(WireId pad_wire, NetInfo *net, const std::unordered_set<CellInfo *> &tightly_attached_bels,
                      std::unordered_set<CellInfo *> *placed_cells);
    void pack_ports();
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
    // if no path exists. This only considers combinational delays, as required by the Arch API
    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const override;
    // Get the port class, also setting clockInfoCount to the number of TimingClockingInfos associated with a port
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const override;
    // Get the TimingClockingInfo of a port
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const override;

    // -------------------------------------------------

    const BelBucketRange getBelBuckets() const override
    {
        BelBucketRange bel_bucket_range;
        bel_bucket_range.b.cursor.cursor = chip_info->bel_buckets.begin();
        bel_bucket_range.e.cursor.cursor = chip_info->bel_buckets.end();
        return bel_bucket_range;
    }

    BelBucketId getBelBucketForBel(BelId bel) const override
    {
        BelBucketId bel_bucket;
        bel_bucket.name = IdString(bel_info(chip_info, bel).bel_bucket);
        return bel_bucket;
    }

    const IdStringRange getCellTypes() const override
    {
        const CellMapPOD &cell_map = *chip_info->cell_map;

        IdStringRange id_range;
        id_range.b.cursor = cell_map.cell_names.begin();
        id_range.e.cursor = cell_map.cell_names.end();

        return id_range;
    }

    IdString getBelBucketName(BelBucketId bucket) const override { return bucket.name; }

    BelBucketId getBelBucketByName(IdString name) const override
    {
        for (BelBucketId bel_bucket : getBelBuckets()) {
            if (bel_bucket.name == name) {
                return bel_bucket;
            }
        }

        NPNR_ASSERT_FALSE("Failed to find BEL bucket for name.");
        return BelBucketId();
    }

    size_t get_cell_type_index(IdString cell_type) const;

    BelBucketId getBelBucketForCellType(IdString cell_type) const override
    {
        if (io_port_types.count(cell_type)) {
            BelBucketId bucket;
            bucket.name = id("IOPORTS");
            return bucket;
        }

        BelBucketId bucket;
        const CellMapPOD &cell_map = *chip_info->cell_map;
        bucket.name = IdString(cell_map.cell_bel_buckets[get_cell_type_index(cell_type)]);
        return bucket;
    }

    FilteredBelRange getBelsInBucket(BelBucketId bucket) const override
    {
        BelRange range = getBels();
        FilteredBelRange filtered_range(range.begin(), range.end(),
                                        [this, bucket](BelId bel) { return getBelBucketForBel(bel) == bucket; });

        return filtered_range;
    }

    bool isValidBelForCellType(IdString cell_type, BelId bel) const override
    {
        if (io_port_types.count(cell_type)) {
            return pads.count(bel) > 0;
        }

        const auto &bel_data = bel_info(chip_info, bel);
        if (bel_data.category != BEL_CATEGORY_LOGIC) {
            return false;
        }

        auto cell_type_index = get_cell_type_index(cell_type);
        return bel_data.pin_map[cell_type_index] != -1;
    }

    bool is_cell_valid_constraints(const CellInfo *cell, const TileStatus &tile_status, bool explain) const
    {
        if (io_port_types.count(cell->type)) {
            return true;
        }

        BelId bel = cell->bel;
        NPNR_ASSERT(bel != BelId());

        return constraints.isValidBelForCellType(getCtx(), get_constraint_prototype(bel), tile_status.tags.data(),
                                                 get_cell_constraints(bel, cell->type),
                                                 id(chip_info->tiles[bel.tile].name.get()), cell->name, bel, explain);
    }

    // Return true whether all Bels at a given location are valid
    bool isBelLocationValid(BelId bel) const override
    {
        auto iter = tileStatus.find(bel.tile);
        if (iter == tileStatus.end()) {
            return true;
        }
        const TileStatus &tile_status = iter->second;
        const CellInfo *cell = tile_status.boundcells[bel.index];
        if (cell == nullptr) {
            return true;
        } else {
            if (io_port_types.count(cell->type)) {
                // FIXME: Probably need to actually constraint io port cell/bel,
                // but the current BBA emission doesn't support that.  This only
                // really matters if the placer can choose IO port locations.
                return true;
            }

            if (!is_cell_valid_constraints(cell, tile_status, explain_constraints)) {
                return false;
            }

            auto &bel_data = bel_info(chip_info, bel);
            return get_site_status(tile_status, bel_data).checkSiteRouting(getCtx(), tile_status);
        }
    }

    IdString get_bel_tiletype(BelId bel) const { return IdString(loc_info(chip_info, bel).name); }

    std::unordered_map<WireId, Loc> sink_locs, source_locs;
    // -------------------------------------------------
    void assignArchInfo() override {}

    // -------------------------------------------------

    static const std::string defaultPlacer;
    static const std::vector<std::string> availablePlacers;

    static const std::string defaultRouter;
    static const std::vector<std::string> availableRouters;

    // -------------------------------------------------
    void read_logical_netlist(const std::string &filename);
    void write_physical_netlist(const std::string &filename) const;
    void parse_xdc(const std::string &filename);

    std::unordered_set<IdString> io_port_types;
    std::unordered_set<BelId> pads;

    bool is_site_port(PipId pip) const
    {
        const PipInfoPOD &pip_data = pip_info(chip_info, pip);
        if (pip_data.site == -1) {
            return false;
        }

        BelId bel;
        bel.tile = pip.tile;
        bel.index = pip_data.bel;

        const BelInfoPOD &bel_data = bel_info(chip_info, bel);

        return bel_data.category == BEL_CATEGORY_SITE_PORT;
    }

    // Is the driver and all users of this net located within the same site?
    //
    // Returns false if any element of the net is not placed.
    bool is_net_within_site(const NetInfo &net) const;

    using ArchConstraints = Constraints<kMaxState>;
    ArchConstraints constraints;
    std::vector<ArchConstraints::TagState> default_tags;
    bool explain_constraints;

    struct StateRange
    {
        const int32_t *b;
        const int32_t *e;

        const int32_t *begin() const { return b; }
        const int32_t *end() const { return e; }
    };

    struct Constraint : ArchConstraints::Constraint<StateRange>
    {
        const CellConstraintPOD *constraint;
        Constraint(const CellConstraintPOD *constraint) : constraint(constraint) {}

        size_t tag() const override { return constraint->tag; }

        ArchConstraints::ConstraintType constraint_type() const override
        {
            return Constraints<kMaxState>::ConstraintType(constraint->constraint_type);
        }

        ArchConstraints::ConstraintStateType state() const override
        {
            NPNR_ASSERT(constraint_type() == Constraints<kMaxState>::CONSTRAINT_TAG_IMPLIES);
            NPNR_ASSERT(constraint->states.size() == 1);
            return constraint->states[0];
        }

        StateRange states() const override
        {
            StateRange range;
            range.b = constraint->states.get();
            range.e = range.b + constraint->states.size();

            return range;
        }
    };

    struct ConstraintIterator
    {
        const CellConstraintPOD *constraint;
        ConstraintIterator() {}

        ConstraintIterator operator++()
        {
            ++constraint;
            return *this;
        }

        bool operator!=(const ConstraintIterator &other) const { return constraint != other.constraint; }

        bool operator==(const ConstraintIterator &other) const { return constraint == other.constraint; }

        Constraint operator*() const { return Constraint(constraint); }
    };

    struct ConstraintRange
    {
        ConstraintIterator b, e;

        ConstraintIterator begin() const { return b; }
        ConstraintIterator end() const { return e; }
    };

    uint32_t get_constraint_prototype(BelId bel) const { return chip_info->tiles[bel.tile].type; }

    ConstraintRange get_cell_constraints(BelId bel, IdString cell_type) const
    {
        const auto &bel_data = bel_info(chip_info, bel);
        NPNR_ASSERT(bel_data.category == BEL_CATEGORY_LOGIC);

        int32_t mapping = bel_data.pin_map[get_cell_type_index(cell_type)];
        NPNR_ASSERT(mapping >= 0);

        auto &cell_bel_map = chip_info->cell_map->cell_bel_map[mapping];
        ConstraintRange range;
        range.b.constraint = cell_bel_map.constraints.get();
        range.e.constraint = range.b.constraint + cell_bel_map.constraints.size();

        return range;
    }

    const char *get_site_name(int32_t tile, size_t site) const
    {
        return site_inst_info(chip_info, tile, site).name.get();
    }

    const char *get_site_name(BelId bel) const
    {
        auto &bel_data = bel_info(chip_info, bel);
        return get_site_name(bel.tile, bel_data.site);
    }

    const SiteInstInfoPOD &get_site_inst(BelId bel) const
    {
        auto &bel_data = bel_info(chip_info, bel);
        return site_inst_info(chip_info, bel.tile, bel_data.site);
    }

    const SiteInstInfoPOD &get_site_inst(WireId wire) const
    {
        auto &wire_data = wire_info(wire);
        NPNR_ASSERT(wire_data.site != -1);
        return site_inst_info(chip_info, wire.tile, wire_data.site);
    }

    const SiteInstInfoPOD &get_site_inst(PipId pip) const
    {
        auto &pip_data = pip_info(chip_info, pip);
        return site_inst_info(chip_info, pip.tile, pip_data.site);
    }
};

NEXTPNR_NAMESPACE_END
