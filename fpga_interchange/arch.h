/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018-19  David Shah <david@symbioticeda.com>
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

NEXTPNR_NAMESPACE_BEGIN

/**** Everything in this section must be kept in sync with chipdb.py ****/

template <typename T> struct RelPtr
{
    int32_t offset;

    // void set(const T *ptr) {
    //     offset = reinterpret_cast<const char*>(ptr) -
    //              reinterpret_cast<const char*>(this);
    // }

    const T *get() const
    {
        return reinterpret_cast<const T *>(reinterpret_cast<const char *>(this) + offset);
    }

    const T &operator[](size_t index) const { return get()[index]; }

    const T &operator*() const { return *(get()); }

    const T *operator->() const { return get(); }
};


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
    int32_t name;        // bel name (in site) constid
    int32_t type;        // Type name constid

    int32_t num_bel_wires;
    RelPtr<int32_t> ports; // port name constid
    RelPtr<int32_t> types; // port name (IN/OUT/BIDIR)
    RelPtr<int32_t> wires; // connected wire index in tile, or -1 if NA

    int16_t site;
    int16_t site_variant; // some sites have alternative types
    int16_t is_routing;
    int16_t padding;
});

NPNR_PACKED_STRUCT(struct BelPortPOD {
    int32_t bel_index;
    int32_t port;
});

NPNR_PACKED_STRUCT(struct TileWireInfoPOD {
    int32_t name; // wire name constid

    // Pip index inside tile
    int32_t num_uphill;
    RelPtr<int32_t> pips_uphill;

    // Pip index inside tile
    int32_t num_downhill;
    RelPtr<int32_t> pips_downhill;

    // Bel index inside tile
    int32_t num_bel_pins;
    RelPtr<BelPortPOD> bel_pins;

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

NPNR_PACKED_STRUCT(struct TileTypeInfoPOD {
    int32_t name; // Tile type constid

    int32_t number_sites;

    int32_t num_bels;
    RelPtr<BelInfoPOD> bel_data;

    int32_t num_wires;
    RelPtr<TileWireInfoPOD> wire_data;

    int32_t num_pips;
    RelPtr<PipInfoPOD> pip_data;
});

NPNR_PACKED_STRUCT(struct SiteInstInfoPOD {
    RelPtr<char> name;

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
    RelPtr<int32_t> sites;

    // Number of tile wires; excluding any site-internal wires
    // which come after general wires and are not stored here
    // as they will never be nodal
    int32_t num_tile_wires;
    // -1 if a tile-local wire; node index if nodal wire
    RelPtr<int32_t> tile_wire_to_node;
});

NPNR_PACKED_STRUCT(struct TileWireRefPOD {
    int32_t tile;
    int32_t index;
});

NPNR_PACKED_STRUCT(struct NodeInfoPOD {
    int32_t num_tile_wires;
    RelPtr<TileWireRefPOD> tile_wires;
});

NPNR_PACKED_STRUCT(struct ChipInfoPOD {
    RelPtr<char> name;
    RelPtr<char> generator;

    int32_t version;
    int32_t width, height;

    int32_t num_tile_types;
    RelPtr<TileTypeInfoPOD> tile_types;

    int32_t num_sites;
    RelPtr<SiteInstInfoPOD> sites;

    int32_t num_tiles;
    RelPtr<TileInstInfoPOD> tiles;

    int32_t num_nodes;
    RelPtr<NodeInfoPOD> nodes;
});

/************************ End of chipdb section. ************************/

inline const TileTypeInfoPOD &tileInfo(const ChipInfoPOD *chip_info, int32_t tile)
{
    return chip_info->tile_types[chip_info->tiles[tile].type];
}

struct BelIterator
{
    const ChipInfoPOD *chip;
    int cursor_index;
    int cursor_tile;

    BelIterator operator++()
    {
        cursor_index++;
        while (cursor_tile < chip->num_tiles &&
               cursor_index >= tileInfo(chip, cursor_tile).num_bels) {
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

// -----------------------------------------------------------------------

// Iterate over TileWires for a wire (will be more than one if nodal)
struct TileWireIterator
{
    const ChipInfoPOD *chip;
    WireId baseWire;
    int cursor = -1;

    void operator++() { cursor++; }
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

inline WireId canonicalWireId(const ChipInfoPOD *chip_info, int32_t tile, int32_t wire)
{
    WireId id;

    if (wire >= chip_info->tiles[tile].num_tile_wires) {
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
            if (cursor_tile == -1 && cursor_index >= chip->num_nodes) {
                cursor_tile = 0;
                cursor_index = 0;
            }
            while (cursor_tile != -1 && cursor_tile < chip->num_tiles &&
                   cursor_index >= chip->tile_types[chip->tiles[cursor_tile].type].num_wires) {
                cursor_index = 0;
                cursor_tile++;
            }

        } while ((cursor_tile != -1 && cursor_tile < chip->num_tiles &&
                  cursor_index < chip->tiles[cursor_tile].num_tile_wires &&
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
        while (cursor_tile < chip->num_tiles &&
               cursor_index >= chip->tile_types[chip->tiles[cursor_tile].type].num_pips) {
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
            if (cursor < tile.wire_data[w.index].num_uphill)
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
            if (cursor < tile.wire_data[w.index].num_downhill)
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
        while (true) {
            if (!(twi != twi_end))
                break;
            WireId w = *twi;
            auto &tile = tileInfo(chip, w.tile);
            if (cursor < tile.wire_data[w.index].num_bel_pins)
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
        ret.bel.index = tileInfo(chip, w.tile).wire_data[w.index].bel_pins[cursor].bel_index;
        ret.pin.index = tileInfo(chip, w.tile).wire_data[w.index].bel_pins[cursor].port;
        return ret;
    }
};

struct BelPinRange
{
    BelPinIterator b, e;
    BelPinIterator begin() const { return b; }
    BelPinIterator end() const { return e; }
};

struct ArchArgs
{
    std::string chipdb;
};

struct Arch : BaseCtx
{
    boost::iostreams::mapped_file_source blob_file;
    const ChipInfoPOD *chip_info;

    mutable std::unordered_map<std::string, int> tile_by_name;
    mutable std::unordered_map<std::string, std::pair<int, int>> site_by_name;

    std::unordered_map<WireId, NetInfo *> wire_to_net;
    std::unordered_map<PipId, NetInfo *> pip_to_net;
    std::unordered_map<WireId, std::pair<int, int>> driving_pip_loc;
    std::unordered_map<WireId, NetInfo *> reserved_wires;

    struct TileStatus
    {
        std::vector<CellInfo *> boundcells;
    };

    std::vector<TileStatus> tileStatus;

    ArchArgs args;
    Arch(ArchArgs args);

    std::string getChipName() const;

    IdString archId() const { return id(chip_info->name.get()); }
    ArchArgs archArgs() const { return args; }
    IdString archArgsToId(ArchArgs args) const;

    // -------------------------------------------------

    uint32_t getTileIndex(int x, int y) const {
        return (y * chip_info->width + x);
    }
    uint32_t getTileIndex(Loc loc) const {
        return getTileIndex(loc.x, loc.y);
    }
    template<typename TileIndex, typename CoordIndex> void getTileXY(TileIndex tile_index, CoordIndex *x, CoordIndex *y) const {
        *x = tile_index % chip_info->width;
        *y = tile_index / chip_info->width;
    }

    template<typename TileIndex> void getTileLoc(TileIndex tile_index, Loc * loc) const {
        getTileXY(tile_index, &loc->x, &loc->y);
    }

    int getGridDimX() const { return chip_info->width; }
    int getGridDimY() const { return chip_info->height; }
    int getTileBelDimZ(int x, int y) const {
        return chip_info->tile_types[chip_info->tiles[getTileIndex(x, y)].type].num_bels;
    }
    int getTilePipDimZ(int x, int y) const {
        return chip_info->tile_types[chip_info->tiles[getTileIndex(x, y)].type].number_sites;
    }

    // -------------------------------------------------

    void setup_byname() const;

    BelId getBelByName(IdString name) const;

    IdString getBelName(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        int site_index = locInfo(bel).bel_data[bel.index].site;
        NPNR_ASSERT(site_index != -1);
        const SiteInstInfoPOD &site = chip_info->sites[chip_info->tiles[bel.tile].sites[site_index]];
        return id(std::string(site.name.get()) +
                "/" + IdString(locInfo(bel).bel_data[bel.index].name).str(this));
    }

    uint32_t getBelChecksum(BelId bel) const { return bel.index; }

    void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength)
    {
        NPNR_ASSERT(bel != BelId());
        NPNR_ASSERT(tileStatus[bel.tile].boundcells[bel.index] == nullptr);

        tileStatus[bel.tile].boundcells[bel.index] = cell;
        cell->bel = bel;
        cell->belStrength = strength;
        refreshUiBel(bel);
    }

    void unbindBel(BelId bel)
    {
        NPNR_ASSERT(bel != BelId());
        NPNR_ASSERT(tileStatus[bel.tile].boundcells[bel.index] != nullptr);
        tileStatus[bel.tile].boundcells[bel.index]->bel = BelId();
        tileStatus[bel.tile].boundcells[bel.index]->belStrength = STRENGTH_NONE;
        tileStatus[bel.tile].boundcells[bel.index] = nullptr;
        refreshUiBel(bel);
    }

    bool checkBelAvail(BelId bel) const
    {
        return tileStatus[bel.tile].boundcells[bel.index] == nullptr;
    }

    CellInfo *getBoundBelCell(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        return tileStatus[bel.tile].boundcells[bel.index];
    }

    CellInfo *getConflictingBelCell(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        return tileStatus[bel.tile].boundcells[bel.index];
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

    Loc getBelLocation(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        Loc loc;
        getTileXY(bel.tile, &loc.x, &loc.y);
        loc.z = bel.index;
        return loc;
    }

    BelId getBelByLocation(Loc loc) const;
    BelRange getBelsByTile(int x, int y) const;

    bool getBelGlobalBuf(BelId bel) const
    {
        // TODO: This probably needs to be fixed!
        return false;
    }

    bool getBelHidden(BelId bel) const { return locInfo(bel).bel_data[bel.index].is_routing; }

    IdString getBelType(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        return IdString(locInfo(bel).bel_data[bel.index].type);
    }

    std::vector<std::pair<IdString, std::string>> getBelAttrs(BelId bel) const;

    WireId getBelPinWire(BelId bel, IdString pin) const;
    PortType getBelPinType(BelId bel, IdString pin) const;
    std::vector<IdString> getBelPins(BelId bel) const;

    bool isBelLocked(BelId bel) const;

    // -------------------------------------------------

    mutable std::unordered_map<IdString, WireId> wire_by_name_cache;

    WireId getWireByName(IdString name) const;

    const TileWireInfoPOD &wireInfo(WireId wire) const
    {
        if (wire.tile == -1) {
            const TileWireRefPOD &wr = chip_info->nodes[wire.index].tile_wires[0];
            return chip_info->tile_types[chip_info->tiles[wr.tile].type].wire_data[wr.index];
        } else {
            return locInfo(wire).wire_data[wire.index];
        }
    }

    IdString getWireName(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        if (wire.tile != -1 && locInfo(wire).wire_data[wire.index].site != -1) {
            int site_index = locInfo(wire).wire_data[wire.index].site;
            const SiteInstInfoPOD &site = chip_info->sites[chip_info->tiles[wire.tile].sites[site_index]];
            return id(site.name.get() +
                      std::string("/") + IdString(locInfo(wire).wire_data[wire.index].name).str(this));
        } else {
            return id(std::string(chip_info
                                          ->tiles[wire.tile == -1 ? chip_info->nodes[wire.index].tile_wires[0].tile
                                                                       : wire.tile]
                                          .name.get()) +
                      "/" + IdString(wireInfo(wire).name).c_str(this));
        }
    }

    IdString getWireType(WireId wire) const;
    std::vector<std::pair<IdString, std::string>> getWireAttrs(WireId wire) const;

    uint32_t getWireChecksum(WireId wire) const { return wire.index; }

    void bindWire(WireId wire, NetInfo *net, PlaceStrength strength)
    {
        NPNR_ASSERT(wire != WireId());
        NPNR_ASSERT(wire_to_net[wire] == nullptr);
        wire_to_net[wire] = net;
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

        auto pip = it->second.pip;
        if (pip != PipId()) {
            pip_to_net[pip] = nullptr;
        }

        net_wires.erase(it);
        wire_to_net[wire] = nullptr;
        refreshUiWire(wire);
    }

    bool checkWireAvail(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        auto w2n = wire_to_net.find(wire);
        return w2n == wire_to_net.end() || w2n->second == nullptr;
    }

    NetInfo *getReservedWireNet(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        auto w2n = reserved_wires.find(wire);
        return w2n == reserved_wires.end() ? nullptr : w2n->second;
    }

    NetInfo *getBoundWireNet(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        auto w2n = wire_to_net.find(wire);
        return w2n == wire_to_net.end() ? nullptr : w2n->second;
    }

    WireId getConflictingWireWire(WireId wire) const { return wire; }

    NetInfo *getConflictingWireNet(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        auto w2n = wire_to_net.find(wire);
        return w2n == wire_to_net.end() ? nullptr : w2n->second;
    }

    DelayInfo getWireDelay(WireId wire) const
    {
        DelayInfo delay;
        delay.delay = 0;
        return delay;
    }

    TileWireRange getTileWireRange(WireId wire) const
    {
        TileWireRange range;
        range.b.chip = chip_info;
        range.b.baseWire = wire;
        range.b.cursor = -1;
        ++range.b;

        range.e.chip = chip_info;
        range.e.baseWire = wire;
        if (wire.tile == -1)
            range.e.cursor = chip_info->nodes[wire.index].num_tile_wires;
        else
            range.e.cursor = 1;
        return range;
    }

    BelPinRange getWireBelPins(WireId wire) const
    {
        BelPinRange range;
        NPNR_ASSERT(wire != WireId());
        TileWireRange twr = getTileWireRange(wire);
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

    WireRange getWires() const
    {
        WireRange range;
        range.b.chip = chip_info;
        range.b.cursor_tile = -1;
        range.b.cursor_index = 0;
        range.e.chip = chip_info;
        range.e.cursor_tile = chip_info->num_tiles;
        range.e.cursor_index = 0;
        return range;
    }

    // -------------------------------------------------

    mutable std::unordered_map<IdString, PipId> pip_by_name_cache;

    PipId getPipByName(IdString name) const;

    void bindPip(PipId pip, NetInfo *net, PlaceStrength strength)
    {
        NPNR_ASSERT(pip != PipId());
        NPNR_ASSERT(pip_to_net[pip] == nullptr);

        WireId dst = canonicalWireId(chip_info, pip.tile, locInfo(pip).pip_data[pip.index].dst_index);
        NPNR_ASSERT(wire_to_net[dst] == nullptr || wire_to_net[dst] == net);

        pip_to_net[pip] = net;
        std::pair<int, int> loc;
        getTileXY(pip.tile, &loc.first, &loc.second);
        driving_pip_loc[dst] = loc;

        wire_to_net[dst] = net;
        net->wires[dst].pip = pip;
        net->wires[dst].strength = strength;
        refreshUiPip(pip);
        refreshUiWire(dst);
    }

    void unbindPip(PipId pip)
    {
        NPNR_ASSERT(pip != PipId());
        NPNR_ASSERT(pip_to_net[pip] != nullptr);

        WireId dst = canonicalWireId(chip_info, pip.tile, locInfo(pip).pip_data[pip.index].dst_index);
        NPNR_ASSERT(wire_to_net[dst] != nullptr);
        wire_to_net[dst] = nullptr;
        pip_to_net[pip]->wires.erase(dst);

        pip_to_net[pip] = nullptr;
        refreshUiPip(pip);
        refreshUiWire(dst);
    }

    bool checkPipAvail(PipId pip) const
    {
        NPNR_ASSERT(pip != PipId());
        return pip_to_net.find(pip) == pip_to_net.end() || pip_to_net.at(pip) == nullptr;
    }

    NetInfo *getBoundPipNet(PipId pip) const
    {
        NPNR_ASSERT(pip != PipId());
        auto p2n = pip_to_net.find(pip);
        return p2n == pip_to_net.end() ? nullptr : p2n->second;
    }

    WireId getConflictingPipWire(PipId pip) const
    {
        return getPipDstWire(pip);
    }

    NetInfo *getConflictingPipNet(PipId pip) const
    {
        auto p2n = pip_to_net.find(pip);
        return p2n == pip_to_net.end() ? nullptr : p2n->second;
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

    Loc getPipLocation(PipId pip) const
    {
        Loc loc;
        getTileLoc(pip.tile, &loc);
        loc.z = 0;
        return loc;
    }

    IdString getPipName(PipId pip) const;

    IdString getPipType(PipId pip) const;
    std::vector<std::pair<IdString, std::string>> getPipAttrs(PipId pip) const;

    uint32_t getPipChecksum(PipId pip) const { return pip.index; }

    WireId getPipSrcWire(PipId pip) const
    {
        return canonicalWireId(chip_info, pip.tile, locInfo(pip).pip_data[pip.index].src_index);
    }

    WireId getPipDstWire(PipId pip) const
    {
        return canonicalWireId(chip_info, pip.tile, locInfo(pip).pip_data[pip.index].dst_index);
    }

    DelayInfo getPipDelay(PipId pip) const
    {
        return DelayInfo();
    }

    DownhillPipRange getPipsDownhill(WireId wire) const
    {
        DownhillPipRange range;
        NPNR_ASSERT(wire != WireId());
        TileWireRange twr = getTileWireRange(wire);
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

    UphillPipRange getPipsUphill(WireId wire) const
    {
        UphillPipRange range;
        NPNR_ASSERT(wire != WireId());
        TileWireRange twr = getTileWireRange(wire);
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

    UphillPipRange getWireAliases(WireId wire) const
    {
        UphillPipRange range;
        range.b.cursor = 0;
        range.b.twi.cursor = 0;
        range.e.cursor = 0;
        range.e.twi.cursor = 0;
        return range;
    }

    // -------------------------------------------------

    GroupId getGroupByName(IdString name) const { return GroupId(); }
    IdString getGroupName(GroupId group) const { return IdString(); }
    std::vector<GroupId> getGroups() const { return {}; }
    std::vector<BelId> getGroupBels(GroupId group) const { return {}; }
    std::vector<WireId> getGroupWires(GroupId group) const { return {}; }
    std::vector<PipId> getGroupPips(GroupId group) const { return {}; }
    std::vector<GroupId> getGroupGroups(GroupId group) const { return {}; }

    // -------------------------------------------------
    delay_t estimateDelay(WireId src, WireId dst, bool debug = false) const;
    delay_t predictDelay(const NetInfo *net_info, const PortRef &sink) const;
    ArcBounds getRouteBoundingBox(WireId src, WireId dst) const;
    delay_t getBoundingBoxCost(WireId src, WireId dst, int distance) const;
    delay_t getDelayEpsilon() const { return 20; }
    delay_t getRipupDelayPenalty() const { return 120; }
    delay_t getWireRipupDelayPenalty(WireId wire) const;
    float getDelayNS(delay_t v) const { return v * 0.001; }
    DelayInfo getDelayFromNS(float ns) const
    {
        DelayInfo del;
        del.delay = delay_t(ns * 1000);
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
    // if no path exists. This only considers combinational delays, as required by the Arch API
    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const;
    // Get the port class, also setting clockInfoCount to the number of TimingClockingInfos associated with a port
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const;
    // Get the TimingClockingInfo of a port
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const;

    // -------------------------------------------------

    // Whether or not a given cell can be placed at a given Bel
    // This is not intended for Bel type checks, but finer-grained constraints
    // such as conflicting set/reset signals, etc
    bool isValidBelForCell(CellInfo *cell, BelId bel) const {
        // FIXME: Implement this
        return true;
    }

    // Return true whether all Bels at a given location are valid
    bool isBelLocationValid(BelId bel) const {
        // FIXME: Implement this
        return true;
    }

    IdString getBelTileType(BelId bel) const { return IdString(locInfo(bel).name); }

    std::unordered_map<WireId, Loc> sink_locs, source_locs;
    // -------------------------------------------------
    void assignArchInfo() {}

    // -------------------------------------------------

    static const std::string defaultPlacer;
    static const std::vector<std::string> availablePlacers;

    static const std::string defaultRouter;
    static const std::vector<std::string> availableRouters;

    // -------------------------------------------------
    template <typename Id> const TileTypeInfoPOD &locInfo(Id &id) const
    {
        return chip_info->tile_types[chip_info->tiles[id.tile].type];
    }

    void writePhysicalNetlist(const std::string &filename) const {
    }
};

NEXTPNR_NAMESPACE_END
