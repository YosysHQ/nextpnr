/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  David Shah <dave@ds0.me>
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

template <typename T> struct RelPtr
{
    int32_t offset;

    // void set(const T *ptr) {
    //     offset = reinterpret_cast<const char*>(ptr) -
    //              reinterpret_cast<const char*>(this);
    // }

    const T *get() const
    {
        return reinterpret_cast<const T *>(reinterpret_cast<const char *>(this) + int64_t(offset) * 4);
    }

    const T &operator[](size_t index) const { return get()[index]; }

    const T &operator*() const { return *(get()); }

    const T *operator->() const { return get(); }
};

/*
    Fully deduplicated database

    There are two key data structures in the database:

    Locations (aka tile but not called this to avoid confusion
    with Lattice terminology), are a (x, y) location.

    Local wires; pips and bels are all stored once per variety of location
    (called a location type) with a separate grid containing the location type
    at a (x, y) coordinate.

    Each location also has _neighbours_, other locations with interconnected
    wires. The set of neighbours for a location are called a _neighbourhood_.

    Each variety of _neighbourhood_ for a location type is also stored once,
    using relative coordinates.

*/

NPNR_PACKED_STRUCT(struct BelWirePOD {
    uint32_t port;
    uint16_t type;
    uint16_t wire_index; // wire index in tile
});

NPNR_PACKED_STRUCT(struct BelInfoPOD {
    int32_t name;             // bel name in tile IdString
    int32_t type;             // bel type IdString
    int16_t rel_x, rel_y;     // bel location relative to parent
    RelPtr<BelWirePOD> ports; // ports, sorted by name IdString
    int32_t num_ports;        // number of ports
});

NPNR_PACKED_STRUCT(struct BelPinPOD {
    uint32_t bel; // bel index in tile
    int32_t pin;  // bel pin name IdString
});

enum TileWireFlags : uint32_t
{
    WIRE_PRIMARY = 0x80000000,
};

NPNR_PACKED_STRUCT(struct LocWireInfoPOD {
    int32_t name; // wire name in tile IdString
    uint32_t flags;
    int32_t num_uphill, num_downhill, num_bpins;
    // Note this pip lists exclude neighbourhood pips
    RelPtr<int32_t> pips_uh, pips_dh; // list of uphill/downhill pip indices in tile
    RelPtr<BelPinPOD> bel_pins;
});

NPNR_PACKED_STRUCT(struct PipInfoPOD {
    uint16_t from_wire, to_wire;
    int32_t tile_type;
});

enum RelLocFlags
{
    REL_GLOBAL = 0x80,
    REL_BRANCH = 0x40,
    REL_SPINE = 0x20,
    REL_HROW = 0x10
};

enum ArcFlags
{
    LOGICAL_TO_PRIMARY = 0x80,
    PHYSICAL_DOWNHILL = 0x08,
};

NPNR_PACKED_STRUCT(struct RelWireInfoPOD {
    int16_t rel_x, rel_y;
    uint16_t wire_index;
    uint8_t loc_flags;
    uint8_t arc_flags;
});

NPNR_PACKED_STRUCT(struct WireNeighboursInfoPOD {
    uint32_t num_nwires;
    RelPtr<RelWireInfoPOD> neigh_wires;
});

NPNR_PACKED_STRUCT(struct LocNeighourhoodPOD { RelPtr<WireNeighboursInfoPOD> wire_neighbours; });

NPNR_PACKED_STRUCT(struct LocTypePOD {
    uint32_t num_bels, num_wires, num_pips, num_nhtypes;
    RelPtr<BelInfoPOD> bels;
    RelPtr<LocWireInfoPOD> wires;
    RelPtr<PipInfoPOD> pips;
    RelPtr<LocNeighourhoodPOD> neighbourhoods;
});

// A physical (bitstream) tile; of which there may be more than
// one in a logical tile (XY grid location).
// Tile name is reconstructed {prefix}R{row}C{col}:{tiletype}
NPNR_PACKED_STRUCT(struct PhysicalTileInfoPOD {
    int32_t prefix;   // tile name prefix IdString
    int32_t tiletype; // tile type IdString
});

NPNR_PACKED_STRUCT(struct GridLocationPOD {
    uint32_t loc_type;
    uint16_t neighbourhood_type;
    uint16_t num_phys_tiles;
    RelPtr<PhysicalTileInfoPOD> phys_tiles;
});

NPNR_PACKED_STRUCT(struct ChipInfoPOD {
    RelPtr<char> device_name;
    uint16_t width;
    uint16_t height;
    RelPtr<GridLocationPOD> grid;
});

NPNR_PACKED_STRUCT(struct DatabasePOD {
    uint32_t version;
    uint32_t num_chips;
    RelPtr<char> family;
    RelPtr<ChipInfoPOD> chips;
    uint32_t num_loctypes;
    RelPtr<LocTypePOD> loctypes;
});

const int bba_version =
#include "bba_version.inc"
        ;

struct ArchArgs
{
    std::string chipdb;
    std::string device;
    std::string package;
};

struct Arch : BaseCtx
{
    ArchArgs args;
    Arch(ArchArgs args);

    boost::iostreams::mapped_file_source blob_file;
    const DatabasePOD *db;
    const ChipInfoPOD *chip_info;

    std::string getChipName() const;

    IdString archId() const { return id("nexus"); }
    ArchArgs archArgs() const { return args; }
    IdString archArgsToId(ArchArgs args) const;

    int getGridDimX() const { return chip_info->width; }
    int getGridDimY() const { return chip_info->height; }
    int getTileBelDimZ(int, int) const { return 256; }
    int getTilePipDimZ(int, int) const { return 1; }

    template <typename Id> const LocTypePOD &loc_data(Id &id) const
    {
        return db->loctypes[chip_info->grid[id.tile].loc_type];
    }

    template <typename Id> const LocNeighourhoodPOD &nh_data(Id &id) const
    {
        auto &t = chip_info->grid[id.tile];
        return db->loctypes[t.loc_type].neighbourhoods[t.neighbourhood_type];
    }

    inline const BelInfoPOD &bel_data(BelId id) const { return loc_data(id).bels[id.index]; }
    inline const LocWireInfoPOD &wire_data(WireId &id) const { return loc_data(id).wires[id.index]; }
    inline const PipInfoPOD &pip_data(PipId &id) const { return loc_data(id).pips[id.index]; }
    inline bool rel_tile(int32_t base, int16_t rel_x, int16_t rel_y, int32_t &next)
    {
        int32_t curr_x = base % chip_info->width;
        int32_t curr_y = base / chip_info->width;
        int32_t new_x = curr_x + rel_x;
        int32_t new_y = curr_y + rel_y;
        if (new_x < 0 || new_x >= chip_info->width)
            return false;
        if (new_y < 0 || new_y >= chip_info->height)
            return false;
        next = new_y * chip_info->width + new_x;
        return true;
    }
    inline const WireId canonical_wire(int32_t tile, uint16_t index)
    {
        WireId wire{tile, index};
        // `tile` is the primary location for the wire, so ID is already canonical
        if (wire_data(wire).flags & WIRE_PRIMARY)
            return wire;
        // Not primary; find the primary location which forms the canonical ID
        auto &nd = nh_data(wire);
        auto &wn = nd.wire_neighbours[index];
        for (size_t i = 0; i < wn.num_nwires; i++) {
            auto &nw = wn.neigh_wires[i];
            if (nw.arc_flags & LOGICAL_TO_PRIMARY) {
                if (rel_tile(tile, nw.rel_x, nw.rel_y, wire.tile)) {
                    wire.index = nw.wire_index;
                    break;
                }
            }
        }
        return wire;
    }
};

NEXTPNR_NAMESPACE_END
