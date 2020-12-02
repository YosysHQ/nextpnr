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

    const T *get() const { return reinterpret_cast<const T *>(reinterpret_cast<const char *>(this) + offset); }

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
    int32_t z;                // bel location absolute Z
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

enum PipFlags
{
    PIP_FIXED_CONN = 0x8000,
};

NPNR_PACKED_STRUCT(struct PipInfoPOD {
    uint16_t from_wire, to_wire;
    uint16_t flags;
    uint16_t timing_class;
    int32_t tile_type;
});

enum RelLocType : uint8_t
{
    REL_LOC_XY = 0,
    REL_LOC_GLOBAL = 1,
    REL_LOC_BRANCH = 2,
    REL_LOC_BRANCH_L = 3,
    REL_LOC_BRANCH_R = 4,
    REL_LOC_SPINE = 5,
    REL_LOC_HROW = 6,
    REL_LOC_VCC = 7,
};

enum ArcFlags
{
    LOGICAL_TO_PRIMARY = 0x80,
    PHYSICAL_DOWNHILL = 0x08,
};

NPNR_PACKED_STRUCT(struct RelWireInfoPOD {
    int16_t rel_x, rel_y;
    uint16_t wire_index;
    uint8_t loc_type;
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

enum LocFlags : uint32_t
{
    LOC_LOGIC = 0x000001,
    LOC_IO18 = 0x000002,
    LOC_IO33 = 0x000004,
    LOC_BRAM = 0x000008,
    LOC_DSP = 0x000010,
    LOC_IP = 0x000020,
    LOC_CIB = 0x000040,
    LOC_TAP = 0x001000,
    LOC_SPINE = 0x002000,
    LOC_TRUNK = 0x004000,
    LOC_MIDMUX = 0x008000,
    LOC_CMUX = 0x010000,
};

NPNR_PACKED_STRUCT(struct GridLocationPOD {
    uint32_t loc_type;
    uint32_t loc_flags;
    uint16_t neighbourhood_type;
    uint16_t num_phys_tiles;
    RelPtr<PhysicalTileInfoPOD> phys_tiles;
});

enum PioSide : uint8_t
{
    PIO_LEFT = 0,
    PIO_RIGHT = 1,
    PIO_TOP = 2,
    PIO_BOTTOM = 3
};

enum PioDqsFunction : uint8_t
{
    PIO_DQS_DQ = 0,
    PIO_DQS_DQS = 1,
    PIO_DQS_DQSN = 2
};

NPNR_PACKED_STRUCT(struct PackageInfoPOD {
    RelPtr<char> full_name;  // full package name, e.g. CABGA400
    RelPtr<char> short_name; // name used in part number, e.g. BG400
});

NPNR_PACKED_STRUCT(struct PadInfoPOD {
    int16_t offset;   // position offset of tile along side (-1 if not a regular PIO)
    int8_t side;      // PIO side (see PioSide enum)
    int8_t pio_index; // index within IO tile

    int16_t bank; // IO bank

    int16_t dqs_group; // DQS group offset
    int8_t dqs_func;   // DQS function

    int8_t vref_index; // VREF index in bank, or -1 if N/A

    uint16_t num_funcs; // length of special function list
    uint16_t padding;   // padding for alignment

    RelPtr<uint32_t> func_strs; // list of special function IdStrings

    RelPtr<RelPtr<char>> pins; // package index --> package pin name
});

NPNR_PACKED_STRUCT(struct GlobalBranchInfoPOD {
    uint16_t branch_col;
    uint16_t from_col;
    uint16_t tap_driver_col;
    uint16_t tap_side;
    uint16_t to_col;
    uint16_t padding;
});

NPNR_PACKED_STRUCT(struct GlobalSpineInfoPOD {
    uint16_t from_row;
    uint16_t to_row;
    uint16_t spine_row;
    uint16_t padding;
});

NPNR_PACKED_STRUCT(struct GlobalHrowInfoPOD {
    uint16_t hrow_col;
    uint16_t padding;
    uint32_t num_spine_cols;
    RelPtr<uint32_t> spine_cols;
});

NPNR_PACKED_STRUCT(struct GlobalInfoPOD {
    uint32_t num_branches, num_spines, num_hrows;
    RelPtr<GlobalBranchInfoPOD> branches;
    RelPtr<GlobalSpineInfoPOD> spines;
    RelPtr<GlobalHrowInfoPOD> hrows;
});

NPNR_PACKED_STRUCT(struct ChipInfoPOD {
    RelPtr<char> device_name;
    uint16_t width;
    uint16_t height;
    uint32_t num_tiles;
    uint32_t num_pads;
    uint32_t num_packages;
    RelPtr<GridLocationPOD> grid;
    RelPtr<GlobalInfoPOD> globals;
    RelPtr<PadInfoPOD> pads;
    RelPtr<PackageInfoPOD> packages;
});

NPNR_PACKED_STRUCT(struct IdStringDBPOD {
    uint32_t num_file_ids; // number of IDs loaded from constids.inc
    uint32_t num_bba_ids;  // number of IDs in BBA file
    RelPtr<RelPtr<char>> bba_id_strs;
});

// Timing structures are generally sorted using IdString indices as keys for fast binary searches
// All delays are integer picoseconds

// Sort key: (to_port, from_port) for binary search by IdString
NPNR_PACKED_STRUCT(struct CellPropDelayPOD {
    int32_t from_port;
    int32_t to_port;
    int32_t min_delay;
    int32_t max_delay;
});

// Sort key: (sig_port, clock_port) for binary search by IdString
NPNR_PACKED_STRUCT(struct CellSetupHoldPOD {
    int32_t sig_port;
    int32_t clock_port;
    int32_t min_setup;
    int32_t max_setup;
    int32_t min_hold;
    int32_t max_hold;
});

// Sort key: (cell_type, cell_variant) for binary search by IdString
NPNR_PACKED_STRUCT(struct CellTimingPOD {
    int32_t cell_type;
    int32_t cell_variant;
    int32_t num_prop_delays;
    int32_t num_setup_holds;
    RelPtr<CellPropDelayPOD> prop_delays;
    RelPtr<CellSetupHoldPOD> setup_holds;
});

NPNR_PACKED_STRUCT(struct PipTimingPOD {
    int32_t min_delay;
    int32_t max_delay;
    // fanout adder seemingly unused by nexus, reserved for future ECP5 etc support
    int32_t min_fanout_adder;
    int32_t max_fanout_adder;
});

NPNR_PACKED_STRUCT(struct SpeedGradePOD {
    RelPtr<char> name;
    int32_t num_cell_types;
    int32_t num_pip_classes;
    RelPtr<CellTimingPOD> cell_types;
    RelPtr<PipTimingPOD> pip_classes;
});

NPNR_PACKED_STRUCT(struct DatabasePOD {
    uint32_t version;
    uint32_t num_chips;
    uint32_t num_loctypes;
    uint32_t num_speed_grades;
    RelPtr<char> family;
    RelPtr<ChipInfoPOD> chips;
    RelPtr<LocTypePOD> loctypes;
    RelPtr<SpeedGradePOD> speed_grades;
    RelPtr<IdStringDBPOD> ids;
});

// -----------------------------------------------------------------------

// Helper functions for database access
namespace {
template <typename Id> const LocTypePOD &chip_loc_data(const DatabasePOD *db, const ChipInfoPOD *chip, const Id &id)
{
    return db->loctypes[chip->grid[id.tile].loc_type];
}

template <typename Id>
const LocNeighourhoodPOD &chip_nh_data(const DatabasePOD *db, const ChipInfoPOD *chip, const Id &id)
{
    auto &t = chip->grid[id.tile];
    return db->loctypes[t.loc_type].neighbourhoods[t.neighbourhood_type];
}

inline const BelInfoPOD &chip_bel_data(const DatabasePOD *db, const ChipInfoPOD *chip, BelId id)
{
    return chip_loc_data(db, chip, id).bels[id.index];
}
inline const LocWireInfoPOD &chip_wire_data(const DatabasePOD *db, const ChipInfoPOD *chip, WireId id)
{
    return chip_loc_data(db, chip, id).wires[id.index];
}
inline const PipInfoPOD &chip_pip_data(const DatabasePOD *db, const ChipInfoPOD *chip, PipId id)
{
    return chip_loc_data(db, chip, id).pips[id.index];
}
inline bool chip_rel_tile(const ChipInfoPOD *chip, int32_t base, int16_t rel_x, int16_t rel_y, int32_t &next)
{
    int32_t curr_x = base % chip->width;
    int32_t curr_y = base / chip->width;
    int32_t new_x = curr_x + rel_x;
    int32_t new_y = curr_y + rel_y;
    if (new_x < 0 || new_x >= chip->width)
        return false;
    if (new_y < 0 || new_y >= chip->height)
        return false;
    next = new_y * chip->width + new_x;
    return true;
}
inline int32_t chip_tile_from_xy(const ChipInfoPOD *chip, int32_t x, int32_t y) { return y * chip->width + x; }
inline bool chip_get_branch_loc(const ChipInfoPOD *chip, int32_t x, int32_t &branch_x)
{
    for (int i = 0; i < int(chip->globals->num_branches); i++) {
        auto &b = chip->globals->branches[i];
        if (x >= b.from_col && x <= b.to_col) {
            branch_x = b.branch_col;
            return true;
        }
    }
    return false;
}
inline bool chip_get_spine_loc(const ChipInfoPOD *chip, int32_t x, int32_t y, int32_t &spine_x, int32_t &spine_y)
{
    bool y_found = false;
    for (int i = 0; i < int(chip->globals->num_spines); i++) {
        auto &s = chip->globals->spines[i];
        if (y >= s.from_row && y <= s.to_row) {
            spine_y = s.spine_row;
            y_found = true;
            break;
        }
    }
    if (!y_found)
        return false;
    for (int i = 0; i < int(chip->globals->num_hrows); i++) {
        auto &hr = chip->globals->hrows[i];
        for (int j = 0; j < int(hr.num_spine_cols); j++) {
            int32_t sc = hr.spine_cols[j];
            if (std::abs(sc - x) < 3) {
                spine_x = sc;
                return true;
            }
        }
    }
    return false;
}
inline bool chip_get_hrow_loc(const ChipInfoPOD *chip, int32_t x, int32_t y, int32_t &hrow_x, int32_t &hrow_y)
{
    bool y_found = false;
    for (int i = 0; i < int(chip->globals->num_spines); i++) {
        auto &s = chip->globals->spines[i];
        if (std::abs(y - s.spine_row) < 3) {
            hrow_y = s.spine_row;
            y_found = true;
            break;
        }
    }
    if (!y_found)
        return false;
    for (int i = 0; i < int(chip->globals->num_hrows); i++) {
        auto &hr = chip->globals->hrows[i];
        for (int j = 0; j < int(hr.num_spine_cols); j++) {
            int32_t sc = hr.spine_cols[j];
            if (std::abs(sc - x) < 3) {
                hrow_x = hr.hrow_col;
                return true;
            }
        }
    }
    return false;
}
inline bool chip_branch_tile(const ChipInfoPOD *chip, int32_t x, int32_t y, int32_t &next)
{
    int32_t branch_x;
    if (!chip_get_branch_loc(chip, x, branch_x))
        return false;
    next = chip_tile_from_xy(chip, branch_x, y);
    return true;
}
inline bool chip_rel_loc_tile(const ChipInfoPOD *chip, int32_t base, const RelWireInfoPOD &rel, int32_t &next)
{
    int32_t curr_x = base % chip->width;
    int32_t curr_y = base / chip->width;
    switch (rel.loc_type) {
    case REL_LOC_XY:
        return chip_rel_tile(chip, base, rel.rel_x, rel.rel_y, next);
    case REL_LOC_BRANCH:
        return chip_branch_tile(chip, curr_x, curr_y, next);
    case REL_LOC_BRANCH_L:
        return chip_branch_tile(chip, curr_x - 2, curr_y, next);
    case REL_LOC_BRANCH_R:
        return chip_branch_tile(chip, curr_x + 2, curr_y, next);
    case REL_LOC_SPINE: {
        int32_t spine_x, spine_y;
        if (!chip_get_spine_loc(chip, curr_x, curr_y, spine_x, spine_y))
            return false;
        next = chip_tile_from_xy(chip, spine_x, spine_y);
        return true;
    }
    case REL_LOC_HROW: {
        int32_t hrow_x, hrow_y;
        if (!chip_get_hrow_loc(chip, curr_x, curr_y, hrow_x, hrow_y))
            return false;
        next = chip_tile_from_xy(chip, hrow_x, hrow_y);
        return true;
    }
    case REL_LOC_GLOBAL:
    case REL_LOC_VCC:
        next = 0;
        return true;
    default:
        return false;
    }
}
inline WireId chip_canonical_wire(const DatabasePOD *db, const ChipInfoPOD *chip, int32_t tile, uint16_t index)
{
    WireId wire{tile, index};
    // `tile` is the primary location for the wire, so ID is already canonical
    if (chip_wire_data(db, chip, wire).flags & WIRE_PRIMARY)
        return wire;
    // Not primary; find the primary location which forms the canonical ID
    auto &nd = chip_nh_data(db, chip, wire);
    auto &wn = nd.wire_neighbours[index];
    for (size_t i = 0; i < wn.num_nwires; i++) {
        auto &nw = wn.neigh_wires[i];
        if (nw.arc_flags & LOGICAL_TO_PRIMARY) {
            if (chip_rel_loc_tile(chip, tile, nw, wire.tile)) {
                wire.index = nw.wire_index;
                break;
            }
        }
    }
    return wire;
}
inline bool chip_wire_is_primary(const DatabasePOD *db, const ChipInfoPOD *chip, int32_t tile, uint16_t index)
{
    WireId wire{tile, index};
    // `tile` is the primary location for the wire, so ID is already canonical
    if (chip_wire_data(db, chip, wire).flags & WIRE_PRIMARY)
        return true;
    // Not primary; find the primary location which forms the canonical ID
    auto &nd = chip_nh_data(db, chip, wire);
    auto &wn = nd.wire_neighbours[index];
    for (size_t i = 0; i < wn.num_nwires; i++) {
        auto &nw = wn.neigh_wires[i];
        if (nw.arc_flags & LOGICAL_TO_PRIMARY) {
            if (chip_rel_loc_tile(chip, tile, nw, wire.tile)) {
                return false;
            }
        }
    }
    return true;
}
} // namespace

// -----------------------------------------------------------------------

struct BelIterator
{
    const DatabasePOD *db;
    const ChipInfoPOD *chip;
    int cursor_index;
    int cursor_tile;

    BelIterator operator++()
    {
        cursor_index++;
        while (cursor_tile < int(chip->num_tiles) &&
               cursor_index >= int(db->loctypes[chip->grid[cursor_tile].loc_type].num_bels)) {
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

struct WireIterator
{
    const DatabasePOD *db;
    const ChipInfoPOD *chip;
    int cursor_index;
    int cursor_tile = 0;

    WireIterator operator++()
    {
        // Iterate over nodes first, then tile wires that aren't nodes
        do {
            cursor_index++;
            while (cursor_tile < int(chip->num_tiles) &&
                   cursor_index >= int(db->loctypes[chip->grid[cursor_tile].loc_type].num_wires)) {
                cursor_index = 0;
                cursor_tile++;
            }
        } while (cursor_tile < int(chip->num_tiles) && !chip_wire_is_primary(db, chip, cursor_tile, cursor_index));

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

// Iterate over all neighour wires for a wire
struct NeighWireIterator
{
    const DatabasePOD *db;
    const ChipInfoPOD *chip;
    WireId baseWire;
    int cursor = -1;

    void operator++()
    {
        auto &wn = chip_nh_data(db, chip, baseWire).wire_neighbours[baseWire.index];
        int32_t tile;
        do
            cursor++;
        while (cursor < int(wn.num_nwires) &&
               ((wn.neigh_wires[cursor].arc_flags & LOGICAL_TO_PRIMARY) ||
                !chip_rel_tile(chip, baseWire.tile, wn.neigh_wires[cursor].rel_x, wn.neigh_wires[cursor].rel_y, tile)));
    }
    bool operator!=(const NeighWireIterator &other) const { return cursor != other.cursor; }

    // Returns a *denormalised* identifier that may be a non-primary wire (and thus should _not_ be used
    // as a WireId in general as it will break invariants)
    WireId operator*() const
    {
        if (cursor == -1) {
            return baseWire;
        } else {
            auto &nw = chip_nh_data(db, chip, baseWire).wire_neighbours[baseWire.index].neigh_wires[cursor];
            WireId result;
            result.index = nw.wire_index;
            if (!chip_rel_tile(chip, baseWire.tile, nw.rel_x, nw.rel_y, result.tile))
                return WireId();
            return result;
        }
    }
};

struct NeighWireRange
{
    NeighWireIterator b, e;
    NeighWireIterator begin() const { return b; }
    NeighWireIterator end() const { return e; }
};

// -----------------------------------------------------------------------

struct AllPipIterator
{
    const DatabasePOD *db;
    const ChipInfoPOD *chip;
    int cursor_index;
    int cursor_tile;

    AllPipIterator operator++()
    {
        cursor_index++;
        while (cursor_tile < int(chip->num_tiles) &&
               cursor_index >= int(db->loctypes[chip->grid[cursor_tile].loc_type].num_pips)) {
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

struct UpDownhillPipIterator
{
    const DatabasePOD *db;
    const ChipInfoPOD *chip;
    NeighWireIterator twi, twi_end;
    int cursor = -1;
    bool uphill = false;

    void operator++()
    {
        cursor++;
        while (true) {
            if (!(twi != twi_end))
                break;
            WireId w = *twi;
            auto &tile = db->loctypes[chip->grid[w.tile].loc_type];
            if (cursor < int(uphill ? tile.wires[w.index].num_uphill : tile.wires[w.index].num_downhill))
                break;
            ++twi;
            cursor = 0;
        }
    }
    bool operator!=(const UpDownhillPipIterator &other) const { return twi != other.twi || cursor != other.cursor; }

    PipId operator*() const
    {
        PipId ret;
        WireId w = *twi;
        ret.tile = w.tile;
        auto &tile = db->loctypes[chip->grid[w.tile].loc_type];
        ret.index = uphill ? tile.wires[w.index].pips_uh[cursor] : tile.wires[w.index].pips_dh[cursor];
        return ret;
    }
};

struct UpDownhillPipRange
{
    UpDownhillPipIterator b, e;
    UpDownhillPipIterator begin() const { return b; }
    UpDownhillPipIterator end() const { return e; }
};

struct WireBelPinIterator
{
    const DatabasePOD *db;
    const ChipInfoPOD *chip;
    NeighWireIterator twi, twi_end;
    int cursor = -1;

    void operator++()
    {
        cursor++;
        while (true) {
            if (!(twi != twi_end))
                break;
            if (cursor < chip_wire_data(db, chip, *twi).num_bpins)
                break;
            ++twi;
            cursor = 0;
        }
    }
    bool operator!=(const WireBelPinIterator &other) const { return twi != other.twi || cursor != other.cursor; }

    BelPin operator*() const
    {
        BelPin ret;
        WireId w = *twi;
        auto &bp = chip_wire_data(db, chip, w).bel_pins[cursor];
        ret.bel.tile = w.tile;
        ret.bel.index = bp.bel;
        ret.pin = IdString(bp.pin);
        return ret;
    }
};

struct WireBelPinRange
{
    WireBelPinIterator b, e;
    WireBelPinIterator begin() const { return b; }
    WireBelPinIterator end() const { return e; }
};

// -----------------------------------------------------------------------

// This enum captures different 'styles' of cell pins
// This is a combination of the modes available for a pin (tied high, low or inverted)
// and the default value to set it to not connected
enum CellPinStyle
{
    PINOPT_NONE = 0x0, // no options, just signal as-is
    PINOPT_LO = 0x1,   // can be tied low
    PINOPT_HI = 0x2,   // can be tied high
    PINOPT_INV = 0x4,  // can be inverted

    PINOPT_LOHI = 0x3,    // can be tied low or high
    PINOPT_LOHIINV = 0x7, // can be tied low or high; or inverted

    PINOPT_MASK = 0x7,

    PINDEF_NONE = 0x00, // leave disconnected
    PINDEF_0 = 0x10,    // connect to 0 if not used
    PINDEF_1 = 0x20,    // connect to 1 if not used

    PINDEF_MASK = 0x30,

    PINGLB_CLK = 0x100, // pin is a 'clock' for global purposes

    PINGLB_MASK = 0x100,

    PINBIT_GATED = 0x1000,  // pin must be enabled in bitstream if used
    PINBIT_1 = 0x2000,      // pin has an explicit bit that must be set if tied to 1
    PINBIT_CIBMUX = 0x4000, // pin's CIBMUX must be floating for pin to be 1

    PINSTYLE_NONE = 0x0000, // default
    PINSTYLE_CIB = 0x4012,  // 'CIB' signal, floats high but explicitly zeroed if not used
    PINSTYLE_CLK = 0x0107,  // CLK type signal, invertible and defaults to disconnected
    PINSTYLE_CE = 0x0027,   // CE type signal, invertible and defaults to enabled
    PINSTYLE_LSR = 0x0017,  // LSR type signal, invertible and defaults to not reset
    PINSTYLE_DEDI = 0x0000, // dedicated signals, leave alone
    PINSTYLE_PU = 0x4022,   // signals that float high and default high
    PINSTYLE_T = 0x4027,    // PIO 'T' signal

    PINSTYLE_ADLSB = 0x4017,  // special case of the EBR address MSBs
    PINSTYLE_INV_PD = 0x0017, // invertible, pull down by default
    PINSTYLE_INV_PU = 0x4027, // invertible, pull up by default

    PINSTYLE_IOL_CE = 0x2027, // CE type signal, with explicit 'const-1' config bit
    PINSTYLE_GATE = 0x1011,   // gated signal that defaults to 0
};

// This represents the mux options for a pin
enum CellPinMux
{
    PINMUX_SIG = 0,
    PINMUX_0 = 1,
    PINMUX_1 = 2,
    PINMUX_INV = 3,
};

// This represents the various kinds of IO pins
enum IOStyle
{
    IOBANK_WR = 0x1, // needs wide range IO bank
    IOBANK_HP = 0x2, // needs high perf IO bank

    IOMODE_REF = 0x10,         // IO is referenced
    IOMODE_DIFF = 0x20,        // IO is true differential
    IOMODE_PSEUDO_DIFF = 0x40, // IO is pseduo differential

    IOSTYLE_SE_WR = 0x01, // single ended, wide range
    IOSTYLE_SE_HP = 0x02, // single ended, high perf
    IOSTYLE_PD_WR = 0x41, // pseudo diff, wide range

    IOSTYLE_REF_HP = 0x12,  // referenced high perf
    IOSTYLE_DIFF_HP = 0x22, // differential high perf
};

struct IOTypeData
{
    IOStyle style;
    int vcco; // required Vcco in 10mV
};

// -----------------------------------------------------------------------

const int bba_version =
#include "bba_version.inc"
        ;

struct ArchArgs
{
    std::string device;
};

struct Arch : BaseCtx
{
    ArchArgs args;
    std::string family, device, package, speed, rating, variant;
    Arch(ArchArgs args);

    // -------------------------------------------------

    // Database references
    boost::iostreams::mapped_file_source blob_file;
    const DatabasePOD *db;
    const ChipInfoPOD *chip_info;
    const SpeedGradePOD *speed_grade;

    int package_idx;

    // Binding states
    struct LogicTileStatus
    {
        struct SliceStatus
        {
            bool valid = true, dirty = true;
        } slices[4];
        struct HalfTileStatus
        {
            bool valid = true, dirty = true;
        } halfs[2];
        CellInfo *cells[32];
    };

    struct TileStatus
    {
        std::vector<CellInfo *> boundcells;
        std::vector<BelId> bels_by_z;
        LogicTileStatus *lts = nullptr;
        ~TileStatus() { delete lts; }
    };

    std::vector<TileStatus> tileStatus;
    std::unordered_map<WireId, NetInfo *> wire_to_net;
    std::unordered_map<PipId, NetInfo *> pip_to_net;

    // -------------------------------------------------

    std::string getChipName() const;

    IdString archId() const { return id("nexus"); }
    ArchArgs archArgs() const { return args; }
    IdString archArgsToId(ArchArgs args) const;

    int getGridDimX() const { return chip_info->width; }
    int getGridDimY() const { return chip_info->height; }
    int getTileBelDimZ(int, int) const { return 256; }
    int getTilePipDimZ(int, int) const { return 1; }

    // -------------------------------------------------

    BelId getBelByName(IdString name) const;

    IdString getBelName(BelId bel) const
    {
        std::string name = "X";
        name += std::to_string(bel.tile % chip_info->width);
        name += "/Y";
        name += std::to_string(bel.tile / chip_info->width);
        name += "/";
        name += nameOf(IdString(bel_data(bel).name));
        return id(name);
    }

    uint32_t getBelChecksum(BelId bel) const { return (bel.tile << 16) ^ bel.index; }

    void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength)
    {
        NPNR_ASSERT(bel != BelId());
        NPNR_ASSERT(tileStatus[bel.tile].boundcells[bel.index] == nullptr);
        tileStatus[bel.tile].boundcells[bel.index] = cell;
        cell->bel = bel;
        cell->belStrength = strength;
        refreshUiBel(bel);

        if (bel_tile_is(bel, LOC_LOGIC))
            update_logic_bel(bel, cell);
    }

    void unbindBel(BelId bel)
    {
        NPNR_ASSERT(bel != BelId());
        NPNR_ASSERT(tileStatus[bel.tile].boundcells[bel.index] != nullptr);

        if (bel_tile_is(bel, LOC_LOGIC))
            update_logic_bel(bel, nullptr);

        tileStatus[bel.tile].boundcells[bel.index]->bel = BelId();
        tileStatus[bel.tile].boundcells[bel.index]->belStrength = STRENGTH_NONE;
        tileStatus[bel.tile].boundcells[bel.index] = nullptr;
        refreshUiBel(bel);
    }

    bool checkBelAvail(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
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
        range.b.db = db;
        ++range.b; //-1 and then ++ deals with the case of no bels in the first tile
        range.e.cursor_tile = chip_info->width * chip_info->height;
        range.e.cursor_index = 0;
        range.e.chip = chip_info;
        range.e.db = db;
        return range;
    }

    Loc getBelLocation(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        Loc loc;
        loc.x = bel.tile % chip_info->width + bel_data(bel).rel_x;
        loc.y = bel.tile / chip_info->width + bel_data(bel).rel_y;
        loc.z = bel_data(bel).z;
        return loc;
    }

    BelId getBelByLocation(Loc loc) const
    {
        BelId ret;
        auto &t = tileStatus.at(loc.y * chip_info->width + loc.x);
        if (loc.z >= int(t.bels_by_z.size()))
            return BelId();
        return t.bels_by_z.at(loc.z);
    }

    std::vector<BelId> getBelsByTile(int x, int y) const;

    bool getBelGlobalBuf(BelId bel) const { return false; }

    IdString getBelType(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        return IdString(bel_data(bel).type);
    }

    std::vector<std::pair<IdString, std::string>> getBelAttrs(BelId bel) const;

    WireId getBelPinWire(BelId bel, IdString pin) const;
    PortType getBelPinType(BelId bel, IdString pin) const;
    std::vector<IdString> getBelPins(BelId bel) const;

    // -------------------------------------------------

    WireId getWireByName(IdString name) const;
    IdString getWireName(WireId wire) const
    {
        std::string name = "X";
        name += std::to_string(wire.tile % chip_info->width);
        name += "/Y";
        name += std::to_string(wire.tile / chip_info->width);
        name += "/";
        name += nameOf(IdString(wire_data(wire).name));
        return id(name);
    }

    IdString getWireType(WireId wire) const;
    std::vector<std::pair<IdString, std::string>> getWireAttrs(WireId wire) const;

    uint32_t getWireChecksum(WireId wire) const { return (wire.tile << 16) ^ wire.index; }

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

    NetInfo *getBoundWireNet(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        auto w2n = wire_to_net.find(wire);
        return w2n == wire_to_net.end() ? nullptr : w2n->second;
    }

    NetInfo *getConflictingWireNet(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        auto w2n = wire_to_net.find(wire);
        return w2n == wire_to_net.end() ? nullptr : w2n->second;
    }

    WireId getConflictingWireWire(WireId wire) const { return wire; }

    DelayInfo getWireDelay(WireId wire) const
    {
        DelayInfo delay;
        delay.min_delay = 0;
        delay.max_delay = 0;
        return delay;
    }

    WireBelPinRange getWireBelPins(WireId wire) const
    {
        WireBelPinRange range;
        NPNR_ASSERT(wire != WireId());
        NeighWireRange nwr = neigh_wire_range(wire);
        range.b.chip = chip_info;
        range.b.db = db;
        range.b.twi = nwr.b;
        range.b.twi_end = nwr.e;
        range.b.cursor = -1;
        ++range.b;
        range.e.chip = chip_info;
        range.e.db = db;
        range.e.twi = nwr.e;
        range.e.twi_end = nwr.e;
        range.e.cursor = 0;
        return range;
    }

    WireRange getWires() const
    {
        WireRange range;
        range.b.chip = chip_info;
        range.b.db = db;
        range.b.cursor_tile = 0;
        range.b.cursor_index = -1;
        ++range.b; //-1 and then ++ deals with the case of no wires in the first tile
        range.e.chip = chip_info;
        range.e.db = db;
        range.e.cursor_tile = chip_info->num_tiles;
        range.e.cursor_index = 0;
        return range;
    }

    // -------------------------------------------------

    PipId getPipByName(IdString name) const;
    IdString getPipName(PipId pip) const;

    void bindPip(PipId pip, NetInfo *net, PlaceStrength strength)
    {
        NPNR_ASSERT(pip != PipId());
        NPNR_ASSERT(pip_to_net[pip] == nullptr);

        WireId dst = canonical_wire(pip.tile, pip_data(pip).to_wire);
        NPNR_ASSERT(wire_to_net[dst] == nullptr || wire_to_net[dst] == net);

        pip_to_net[pip] = net;

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

        WireId dst = canonical_wire(pip.tile, pip_data(pip).to_wire);
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

    WireId getConflictingPipWire(PipId pip) const { return getPipDstWire(pip); }

    NetInfo *getConflictingPipNet(PipId pip) const
    {
        NPNR_ASSERT(pip != PipId());
        auto p2n = pip_to_net.find(pip);
        return p2n == pip_to_net.end() ? nullptr : p2n->second;
    }

    AllPipRange getPips() const
    {
        AllPipRange range;
        range.b.cursor_tile = 0;
        range.b.cursor_index = -1;
        range.b.chip = chip_info;
        range.b.db = db;
        ++range.b; //-1 and then ++ deals with the case of no pips in the first tile
        range.e.cursor_tile = chip_info->width * chip_info->height;
        range.e.cursor_index = 0;
        range.e.chip = chip_info;
        range.e.db = db;
        return range;
    }

    Loc getPipLocation(PipId pip) const
    {
        Loc loc;
        loc.x = pip.tile % chip_info->width;
        loc.y = pip.tile / chip_info->width;
        loc.z = 0;
        return loc;
    }

    IdString getPipType(PipId pip) const;
    std::vector<std::pair<IdString, std::string>> getPipAttrs(PipId pip) const;

    uint32_t getPipChecksum(PipId pip) const { return pip.tile << 16 | pip.index; }

    WireId getPipSrcWire(PipId pip) const { return canonical_wire(pip.tile, pip_data(pip).from_wire); }

    WireId getPipDstWire(PipId pip) const { return canonical_wire(pip.tile, pip_data(pip).to_wire); }

    DelayInfo getPipDelay(PipId pip) const
    {
        DelayInfo delay;
        auto &cls = speed_grade->pip_classes[pip_data(pip).timing_class];
        delay.min_delay = std::max(0, cls.min_delay);
        delay.max_delay = std::max(0, cls.max_delay);
        return delay;
    }

    UpDownhillPipRange getPipsDownhill(WireId wire) const
    {
        UpDownhillPipRange range;
        NPNR_ASSERT(wire != WireId());
        NeighWireRange nwr = neigh_wire_range(wire);
        range.b.chip = chip_info;
        range.b.db = db;
        range.b.twi = nwr.b;
        range.b.twi_end = nwr.e;
        range.b.cursor = -1;
        range.b.uphill = false;
        ++range.b;
        range.e.chip = chip_info;
        range.e.db = db;
        range.e.twi = nwr.e;
        range.e.twi_end = nwr.e;
        range.e.cursor = 0;
        range.e.uphill = false;
        return range;
    }

    UpDownhillPipRange getPipsUphill(WireId wire) const
    {
        UpDownhillPipRange range;
        NPNR_ASSERT(wire != WireId());
        NeighWireRange nwr = neigh_wire_range(wire);
        range.b.chip = chip_info;
        range.b.db = db;
        range.b.twi = nwr.b;
        range.b.twi_end = nwr.e;
        range.b.cursor = -1;
        range.b.uphill = true;
        ++range.b;
        range.e.chip = chip_info;
        range.e.db = db;
        range.e.twi = nwr.e;
        range.e.twi_end = nwr.e;
        range.e.cursor = 0;
        range.e.uphill = true;
        return range;
    }

    UpDownhillPipRange getWireAliases(WireId wire) const
    {
        UpDownhillPipRange range;
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

    delay_t estimateDelay(WireId src, WireId dst) const;
    delay_t predictDelay(const NetInfo *net_info, const PortRef &sink) const;
    delay_t getDelayEpsilon() const { return 20; }
    delay_t getRipupDelayPenalty() const { return 120; }
    delay_t getWireRipupDelayPenalty(WireId wire) const;
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
    ArcBounds getRouteBoundingBox(WireId src, WireId dst) const;

    // for better DSP bounding boxes
    void pre_routing();
    std::unordered_set<WireId> dsp_wires;

    // -------------------------------------------------

    // Get the delay through a cell from one port to another, returning false
    // if no path exists. This only considers combinational delays, as required by the Arch API
    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const;
    // Get the port class, also setting clockInfoCount to the number of TimingClockingInfos associated with a port
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const;
    // Get the TimingClockingInfo of a port
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const;

    // -------------------------------------------------

    // Perform placement validity checks, returning false on failure (all
    // implemented in arch_place.cc)

    // Whether or not a given cell can be placed at a given Bel
    // This is not intended for Bel type checks, but finer-grained constraints
    // such as conflicting set/reset signals, etc
    bool isValidBelForCell(CellInfo *cell, BelId bel) const;

    // Return true whether all Bels at a given location are valid
    bool isBelLocationValid(BelId bel) const;

    // -------------------------------------------------

    bool pack();
    bool place();
    bool route();

    // arch-specific post-placement optimisations
    void post_place_opt();

    // -------------------------------------------------
    // Assign architecure-specific arguments to nets and cells, which must be
    // called between packing or further
    // netlist modifications, and validity checks
    void assignArchInfo();
    void assignCellInfo(CellInfo *cell);

    // -------------------------------------------------
    // Arch-specific global routing
    void route_globals();

    // -------------------------------------------------

    std::vector<GraphicElement> getDecalGraphics(DecalId decal) const;

    DecalXY getBelDecal(BelId bel) const;
    DecalXY getWireDecal(WireId wire) const;
    DecalXY getPipDecal(PipId pip) const;
    DecalXY getGroupDecal(GroupId group) const;

    // -------------------------------------------------

    static const std::string defaultPlacer;
    static const std::vector<std::string> availablePlacers;
    static const std::string defaultRouter;
    static const std::vector<std::string> availableRouters;

    // -------------------------------------------------

    template <typename Id> const LocTypePOD &loc_data(const Id &id) const { return chip_loc_data(db, chip_info, id); }

    template <typename Id> const LocNeighourhoodPOD &nh_data(const Id &id) const
    {
        return chip_nh_data(db, chip_info, id);
    }

    inline const BelInfoPOD &bel_data(BelId id) const { return chip_bel_data(db, chip_info, id); }
    inline const LocWireInfoPOD &wire_data(WireId id) const { return chip_wire_data(db, chip_info, id); }
    inline const PipInfoPOD &pip_data(PipId id) const { return chip_pip_data(db, chip_info, id); }
    inline bool rel_tile(int32_t base, int16_t rel_x, int16_t rel_y, int32_t &next) const
    {
        return chip_rel_tile(chip_info, base, rel_x, rel_y, next);
    }
    inline WireId canonical_wire(int32_t tile, uint16_t index) const
    {
        WireId c = chip_canonical_wire(db, chip_info, tile, index);
        return c;
    }
    IdString pip_src_wire_name(PipId pip) const
    {
        int wire = pip_data(pip).from_wire;
        return db->loctypes[chip_info->grid[pip.tile].loc_type].wires[wire].name;
    }
    IdString pip_dst_wire_name(PipId pip) const
    {
        int wire = pip_data(pip).to_wire;
        return db->loctypes[chip_info->grid[pip.tile].loc_type].wires[wire].name;
    }

    // -------------------------------------------------

    typedef std::unordered_map<IdString, CellPinStyle> CellPinsData;

    std::unordered_map<IdString, CellPinsData> cell_pins_db;
    CellPinStyle get_cell_pin_style(const CellInfo *cell, IdString port) const;

    void init_cell_pin_data();

    // -------------------------------------------------

    // Parse a possibly-Lattice-style (C literal in Verilog string) style parameter
    Property parse_lattice_param(const CellInfo *ci, IdString prop, int width, int64_t defval) const;

    // -------------------------------------------------

    NeighWireRange neigh_wire_range(WireId wire) const
    {
        NeighWireRange range;
        range.b.chip = chip_info;
        range.b.db = db;
        range.b.baseWire = wire;
        range.b.cursor = -1;

        range.e.chip = chip_info;
        range.e.db = db;
        range.e.baseWire = wire;
        range.e.cursor = nh_data(wire).wire_neighbours[wire.index].num_nwires;
        return range;
    }

    // -------------------------------------------------

    template <typename TId> uint32_t tile_loc_flags(TId id) const { return chip_info->grid[id.tile].loc_flags; }

    template <typename TId> bool tile_is(TId id, LocFlags lf) const { return tile_loc_flags(id) & lf; }

    bool bel_tile_is(BelId bel, LocFlags lf) const
    {
        int32_t tile;
        NPNR_ASSERT(rel_tile(bel.tile, bel_data(bel).rel_x, bel_data(bel).rel_y, tile));
        return chip_info->grid[tile].loc_flags & lf;
    }

    // -------------------------------------------------

    enum LogicBelZ
    {
        BEL_LUT0 = 0,
        BEL_LUT1 = 1,
        BEL_FF0 = 2,
        BEL_FF1 = 3,
        BEL_RAMW = 4,
    };

    void update_logic_bel(BelId bel, CellInfo *cell)
    {
        int z = bel_data(bel).z;
        NPNR_ASSERT(z < 32);
        auto &tts = tileStatus[bel.tile];
        if (tts.lts == nullptr)
            tts.lts = new LogicTileStatus();
        auto &ts = *(tts.lts);
        ts.cells[z] = cell;
        switch (z & 0x7) {
        case BEL_FF0:
        case BEL_FF1:
        case BEL_RAMW:
            ts.halfs[(z >> 3) / 2].dirty = true;
        /* fall-through */
        case BEL_LUT0:
        case BEL_LUT1:
            ts.slices[(z >> 3)].dirty = true;
            break;
        }
    }

    bool nexus_logic_tile_valid(LogicTileStatus &lts) const;

    CellPinMux get_cell_pinmux(const CellInfo *cell, IdString pin) const;
    void set_cell_pinmux(CellInfo *cell, IdString pin, CellPinMux state);

    // -------------------------------------------------

    const PadInfoPOD *get_pkg_pin_data(const std::string &pin) const;
    Loc get_pad_loc(const PadInfoPOD *pad) const;
    BelId get_pad_pio_bel(const PadInfoPOD *pad) const;
    const PadInfoPOD *get_bel_pad(BelId bel) const;
    std::string get_pad_functions(const PadInfoPOD *pad) const;

    // -------------------------------------------------
    // Data about different IO standard, mostly used by bitgen
    static const std::unordered_map<std::string, IOTypeData> io_types;
    int get_io_type_vcc(const std::string &io_type) const;
    bool is_io_type_diff(const std::string &io_type) const;
    bool is_io_type_ref(const std::string &io_type) const;

    // -------------------------------------------------
    // Cell timing lookup helpers

    bool is_dsp_cell(const CellInfo *cell) const;

    // Given cell type and variant, get the index inside the speed grade timing data
    int get_cell_timing_idx(IdString cell_type, IdString cell_variant = IdString()) const;
    // Return true and set delay if a comb path exists in a given cell timing index
    bool lookup_cell_delay(int type_idx, IdString from_port, IdString to_port, DelayInfo &delay) const;
    // Get setup and hold time for a given cell timing index and signal/clock pair
    void lookup_cell_setuphold(int type_idx, IdString from_port, IdString clock, DelayInfo &setup,
                               DelayInfo &hold) const;
    // Get setup and hold time and associated clock for a given cell timing index and signal
    void lookup_cell_setuphold_clock(int type_idx, IdString from_port, IdString &clock, DelayInfo &setup,
                                     DelayInfo &hold) const;
    // Similar to lookup_cell_delay but only needs the 'to' signal, intended for clk->out delays
    void lookup_cell_clock_out(int type_idx, IdString to_port, IdString &clock, DelayInfo &delay) const;
    // Attempt to look up port type based on database
    TimingPortClass lookup_port_type(int type_idx, IdString port, PortType dir, IdString clock) const;
    // -------------------------------------------------

    // List of IO constraints, used by PDC parser
    std::unordered_map<IdString, std::unordered_map<IdString, Property>> io_attr;

    void read_pdc(std::istream &in);

    // -------------------------------------------------
    void write_fasm(std::ostream &out) const;
};

NEXTPNR_NAMESPACE_END
