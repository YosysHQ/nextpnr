/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  gatecat <gatecat@ds0.me>
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

#ifndef NEXUS_ARCH_H
#define NEXUS_ARCH_H

#include <boost/iostreams/device/mapped_file.hpp>
#include <iostream>

#include "base_arch.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"
#include "relptr.h"

NEXTPNR_NAMESPACE_BEGIN

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
    int32_t name;               // bel name in tile IdString
    int32_t type;               // bel type IdString
    int16_t rel_x, rel_y;       // bel location relative to parent
    int32_t z;                  // bel location absolute Z
    RelSlice<BelWirePOD> ports; // ports, sorted by name IdString
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
    // Note this pip lists exclude neighbourhood pips
    RelSlice<int32_t> pips_uh, pips_dh; // list of uphill/downhill pip indices in tile
    RelSlice<BelPinPOD> bel_pins;
});

enum PipFlags
{
    PIP_FIXED_CONN = 0x8000,
    PIP_LUT_PERM = 0x4000,
    PIP_ZERO_RR_COST = 0x2000,
    PIP_DRMUX_C = 0x1000,
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

NPNR_PACKED_STRUCT(struct WireNeighboursInfoPOD { RelSlice<RelWireInfoPOD> neigh_wires; });

NPNR_PACKED_STRUCT(struct LocNeighourhoodPOD { RelSlice<WireNeighboursInfoPOD> wire_neighbours; });

NPNR_PACKED_STRUCT(struct LocTypePOD {
    RelSlice<BelInfoPOD> bels;
    RelSlice<LocWireInfoPOD> wires;
    RelSlice<PipInfoPOD> pips;
    RelSlice<LocNeighourhoodPOD> neighbourhoods;
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
    uint16_t padding;
    RelSlice<PhysicalTileInfoPOD> phys_tiles;
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

    int16_t padding;

    RelSlice<uint32_t> func_strs; // list of special function IdStrings
    RelSlice<RelPtr<char>> pins;  // package index --> package pin name
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
    RelSlice<uint32_t> spine_cols;
});

NPNR_PACKED_STRUCT(struct GlobalInfoPOD {
    RelSlice<GlobalBranchInfoPOD> branches;
    RelSlice<GlobalSpineInfoPOD> spines;
    RelSlice<GlobalHrowInfoPOD> hrows;
});

NPNR_PACKED_STRUCT(struct ChipInfoPOD {
    RelPtr<char> device_name;
    uint16_t width;
    uint16_t height;
    RelSlice<GridLocationPOD> grid;
    RelPtr<GlobalInfoPOD> globals;
    RelSlice<PadInfoPOD> pads;
    RelSlice<PackageInfoPOD> packages;
});

NPNR_PACKED_STRUCT(struct IdStringDBPOD {
    uint32_t num_file_ids;
    RelSlice<RelPtr<char>> bba_id_strs;
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
    RelSlice<CellPropDelayPOD> prop_delays;
    RelSlice<CellSetupHoldPOD> setup_holds;
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
    RelSlice<CellTimingPOD> cell_types;
    RelSlice<PipTimingPOD> pip_classes;
});

NPNR_PACKED_STRUCT(struct DatabasePOD {
    uint32_t version;
    RelPtr<char> family;
    RelSlice<ChipInfoPOD> chips;
    RelSlice<LocTypePOD> loctypes;
    RelSlice<SpeedGradePOD> speed_grades;
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
    for (auto &b : chip->globals->branches) {
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
    for (auto &s : chip->globals->spines) {
        if (y >= s.from_row && y <= s.to_row) {
            spine_y = s.spine_row;
            y_found = true;
            break;
        }
    }
    if (!y_found)
        return false;
    for (auto &hr : chip->globals->hrows) {
        for (int32_t sc : hr.spine_cols) {
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
    for (auto &s : chip->globals->spines) {
        if (std::abs(y - s.spine_row) <= 3) {
            hrow_y = s.spine_row;
            y_found = true;
            break;
        }
    }
    if (!y_found)
        return false;
    for (auto &hr : chip->globals->hrows) {
        for (int32_t sc : hr.spine_cols) {
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
    for (auto &nw : wn.neigh_wires) {
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
    for (auto &nw : wn.neigh_wires) {
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
        while (cursor_tile < chip->grid.ssize() &&
               cursor_index >= db->loctypes[chip->grid[cursor_tile].loc_type].bels.ssize()) {
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
            while (cursor_tile < chip->grid.ssize() &&
                   cursor_index >= db->loctypes[chip->grid[cursor_tile].loc_type].wires.ssize()) {
                cursor_index = 0;
                cursor_tile++;
            }
        } while (cursor_tile < chip->grid.ssize() && !chip_wire_is_primary(db, chip, cursor_tile, cursor_index));

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
        while (cursor < wn.neigh_wires.ssize() &&
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
        while (cursor_tile < chip->grid.ssize() &&
               cursor_index >= db->loctypes[chip->grid[cursor_tile].loc_type].pips.ssize()) {
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
            if (cursor < (uphill ? tile.wires[w.index].pips_uh.ssize() : tile.wires[w.index].pips_dh.ssize()))
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

struct BelPinIterator
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
            if (cursor < chip_wire_data(db, chip, *twi).bel_pins.ssize())
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
        auto &bp = chip_wire_data(db, chip, w).bel_pins[cursor];
        ret.bel.tile = w.tile;
        ret.bel.index = bp.bel;
        ret.pin = IdString(bp.pin);
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

    PINSTYLE_NONE = 0x0000,      // default
    PINSTYLE_CIB = 0x4012,       // 'CIB' signal, floats high but explicitly zeroed if not used
    PINSTYLE_CLK = 0x0107,       // CLK type signal, invertible and defaults to disconnected
    PINSTYLE_CE = 0x0027,        // CE type signal, invertible and defaults to enabled
    PINSTYLE_LSR = 0x0017,       // LSR type signal, invertible and defaults to not reset
    PINSTYLE_DEDI = 0x0000,      // dedicated signals, leave alone
    PINSTYLE_PU = 0x4022,        // signals that float high and default high
    PINSTYLE_PU_NONCIB = 0x0022, // signals that float high and default high
    PINSTYLE_PD_NONCIB = 0x0012, // signals that float high and default low
    PINSTYLE_T = 0x4027,         // PIO 'T' signal

    PINSTYLE_ADLSB = 0x4017,      // special case of the EBR address MSBs
    PINSTYLE_INV_PD = 0x0017,     // invertible, pull down by default
    PINSTYLE_INV_PD_CIB = 0x4017, // invertible, pull down by default
    PINSTYLE_INV_PU = 0x4027,     // invertible, pull up by default

    PINSTYLE_IOL_CELSR = 0x3007, // CE type signal, with explicit 'const-1' config bit
    PINSTYLE_IOL_CLK = 0x3107,   // CE type signal, with explicit 'const-1' config bit
    PINSTYLE_GATE = 0x1011,      // gated signal that defaults to 0
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

struct ArchRanges : BaseArchRanges
{
    using ArchArgsT = ArchArgs;
    // Bels
    using AllBelsRangeT = BelRange;
    using TileBelsRangeT = std::vector<BelId>;
    using BelPinsRangeT = std::vector<IdString>;
    // Wires
    using AllWiresRangeT = WireRange;
    using DownhillPipRangeT = UpDownhillPipRange;
    using UphillPipRangeT = UpDownhillPipRange;
    using WireBelPinRangeT = BelPinRange;
    // Pips
    using AllPipsRangeT = AllPipRange;
};

struct Arch : BaseArch<ArchRanges>
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
        std::vector<NetInfo *> boundwires, boundpips;
        LogicTileStatus *lts = nullptr;
        ~TileStatus() { delete lts; }
    };

    std::vector<TileStatus> tileStatus;

    // fast access to  X and Y IdStrings for building object names
    std::vector<IdString> x_ids, y_ids;
    // inverse of the above for name->object mapping
    dict<IdString, int> id_to_x, id_to_y;

    pool<PipId> disabled_pips;

    // -------------------------------------------------

    std::string getChipName() const override;

    ArchArgs archArgs() const override { return args; }
    IdString archArgsToId(ArchArgs args) const override;

    int getGridDimX() const override { return chip_info->width; }
    int getGridDimY() const override { return chip_info->height; }
    int getTileBelDimZ(int, int) const override { return 256; }
    int getTilePipDimZ(int, int) const override { return 1; }
    char getNameDelimiter() const override { return '/'; }

    // -------------------------------------------------

    BelId getBelByName(IdStringList name) const override;

    IdStringList getBelName(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        std::array<IdString, 3> ids{x_ids.at(bel.tile % chip_info->width), y_ids.at(bel.tile / chip_info->width),
                                    IdString(bel_data(bel).name)};
        return IdStringList(ids);
    }

    void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength) override
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

    void unbindBel(BelId bel) override
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

    bool checkBelAvail(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        return tileStatus[bel.tile].boundcells[bel.index] == nullptr;
    }

    bool is_pseudo_pip_disabled(PipId pip) const
    {
        const auto &data = pip_data(pip);
        if (data.flags & PIP_LUT_PERM) {
            int lut_idx = (data.flags >> 8) & 0xF;
            int from_pin = (data.flags >> 4) & 0xF;
            int to_pin = (data.flags >> 0) & 0xF;
            auto &ts = tileStatus.at(pip.tile);
            if (!ts.lts)
                return false;
            const CellInfo *lut = ts.lts->cells[((lut_idx / 2) << 3) | (BEL_LUT0 + (lut_idx % 2))];
            if (lut) {
                if (lut->lutInfo.is_memory)
                    return true;
                if (lut->lutInfo.is_carry && (from_pin == 3 || to_pin == 3))
                    return true; // Upper pin is special for carries
            }
            if (lut_idx == 4 || lut_idx == 5) {
                const CellInfo *ramw = ts.lts->cells[((lut_idx / 2) << 3) | BEL_RAMW];
                if (ramw)
                    return true; // Don't permute RAM write address
            }
        }
        return false;
    }

    bool checkPipAvail(PipId pip) const override
    {
        if (disabled_pips.count(pip))
            return false;
        if (is_pseudo_pip_disabled(pip))
            return false;
        return getBoundPipNet(pip) == nullptr;
    }

    bool checkPipAvailForNet(PipId pip, const NetInfo *net) const override
    {
        if (disabled_pips.count(pip))
            return false;
        if (is_pseudo_pip_disabled(pip))
            return false;
        NetInfo *bound = getBoundPipNet(pip);
        return (bound == nullptr) || (bound == net);
    }

    CellInfo *getBoundBelCell(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        return tileStatus[bel.tile].boundcells[bel.index];
    }

    BelRange getBels() const override
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

    Loc getBelLocation(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        Loc loc;
        loc.x = bel.tile % chip_info->width + bel_data(bel).rel_x;
        loc.y = bel.tile / chip_info->width + bel_data(bel).rel_y;
        loc.z = bel_data(bel).z;
        return loc;
    }

    BelId getBelByLocation(Loc loc) const override
    {
        auto &t = tileStatus.at(loc.y * chip_info->width + loc.x);
        if (loc.z >= int(t.bels_by_z.size()))
            return BelId();
        return t.bels_by_z.at(loc.z);
    }

    std::vector<BelId> getBelsByTile(int x, int y) const override;

    bool getBelGlobalBuf(BelId bel) const override
    {
        IdString type = getBelType(bel);
        return type.in(id_DCC, id_VCC_DRV);
    }

    IdString getBelType(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        return IdString(bel_data(bel).type);
    }

    std::vector<std::pair<IdString, std::string>> getBelAttrs(BelId bel) const override;

    WireId getBelPinWire(BelId bel, IdString pin) const override;
    PortType getBelPinType(BelId bel, IdString pin) const override;
    std::vector<IdString> getBelPins(BelId bel) const override;

    // -------------------------------------------------

    WireId getWireByName(IdStringList name) const override;

    IdStringList getWireName(WireId wire) const override
    {
        NPNR_ASSERT(wire != WireId());
        std::array<IdString, 3> ids{x_ids.at(wire.tile % chip_info->width), y_ids.at(wire.tile / chip_info->width),
                                    IdString(wire_data(wire).name)};
        return IdStringList(ids);
    }

    std::vector<std::pair<IdString, std::string>> getWireAttrs(WireId wire) const override;
    IdString getWireType(WireId wire) const override;

    DelayQuad getWireDelay(WireId wire) const override { return DelayQuad(0); }

    BelPinRange getWireBelPins(WireId wire) const override
    {
        BelPinRange range;
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

    WireRange getWires() const override
    {
        WireRange range;
        range.b.chip = chip_info;
        range.b.db = db;
        range.b.cursor_tile = 0;
        range.b.cursor_index = -1;
        ++range.b; //-1 and then ++ deals with the case of no wires in the first tile
        range.e.chip = chip_info;
        range.e.db = db;
        range.e.cursor_tile = chip_info->grid.size();
        range.e.cursor_index = 0;
        return range;
    }

    void bindWire(WireId wire, NetInfo *net, PlaceStrength strength) override
    {
        NPNR_ASSERT(wire != WireId());
        auto &w2n_entry = tileStatus.at(wire.tile).boundwires.at(wire.index);
        NPNR_ASSERT(w2n_entry == nullptr);
        net->wires[wire].pip = PipId();
        net->wires[wire].strength = strength;
        w2n_entry = net;
        this->refreshUiWire(wire);
    }
    void unbindWire(WireId wire) override
    {
        NPNR_ASSERT(wire != WireId());
        auto &w2n_entry = tileStatus.at(wire.tile).boundwires.at(wire.index);
        NPNR_ASSERT(w2n_entry != nullptr);

        auto &net_wires = w2n_entry->wires;
        auto it = net_wires.find(wire);
        NPNR_ASSERT(it != net_wires.end());

        auto pip = it->second.pip;
        if (pip != PipId()) {
            tileStatus.at(pip.tile).boundpips.at(pip.index) = nullptr;
        }

        net_wires.erase(it);
        w2n_entry = nullptr;
        this->refreshUiWire(wire);
    }
    virtual bool checkWireAvail(WireId wire) const override { return getBoundWireNet(wire) == nullptr; }
    NetInfo *getBoundWireNet(WireId wire) const override { return tileStatus.at(wire.tile).boundwires.at(wire.index); }

    IdString getWireConstantValue(WireId wire) const override
    {
        if (chip_wire_data(db, chip_info, wire).name == ID_LOCAL_VCC)
            return id_VCC_DRV;
        else
            return {};
    }
    // -------------------------------------------------

    PipId getPipByName(IdStringList name) const override;
    IdStringList getPipName(PipId pip) const override;

    AllPipRange getPips() const override
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

    Loc getPipLocation(PipId pip) const override
    {
        Loc loc;
        loc.x = pip.tile % chip_info->width;
        loc.y = pip.tile / chip_info->width;
        loc.z = 0;
        return loc;
    }

    IdString getPipType(PipId pip) const override;
    std::vector<std::pair<IdString, std::string>> getPipAttrs(PipId pip) const override;

    WireId getPipSrcWire(PipId pip) const override { return canonical_wire(pip.tile, pip_data(pip).from_wire); }

    WireId getPipDstWire(PipId pip) const override { return canonical_wire(pip.tile, pip_data(pip).to_wire); }

    DelayQuad getPipDelay(PipId pip) const override
    {
        auto &cls = speed_grade->pip_classes[pip_data(pip).timing_class];
        return DelayQuad(std::max(0, cls.min_delay), std::max(0, cls.max_delay));
    }

    UpDownhillPipRange getPipsDownhill(WireId wire) const override
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

    UpDownhillPipRange getPipsUphill(WireId wire) const override
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

    void bindPip(PipId pip, NetInfo *net, PlaceStrength strength) override
    {
        NPNR_ASSERT(pip != PipId());

        auto &p2n_entry = tileStatus.at(pip.tile).boundpips.at(pip.index);
        NPNR_ASSERT(p2n_entry == nullptr);
        p2n_entry = net;

        WireId dst = this->getPipDstWire(pip);
        auto &w2n_entry = tileStatus.at(dst.tile).boundwires.at(dst.index);
        NPNR_ASSERT(w2n_entry == nullptr);
        w2n_entry = net;
        net->wires[dst].pip = pip;
        net->wires[dst].strength = strength;
    }

    void unbindPip(PipId pip) override
    {
        NPNR_ASSERT(pip != PipId());

        auto &p2n_entry = tileStatus.at(pip.tile).boundpips.at(pip.index);
        NPNR_ASSERT(p2n_entry != nullptr);
        WireId dst = this->getPipDstWire(pip);

        auto &w2n_entry = tileStatus.at(dst.tile).boundwires.at(dst.index);
        NPNR_ASSERT(w2n_entry != nullptr);
        w2n_entry = nullptr;

        p2n_entry->wires.erase(dst);
        p2n_entry = nullptr;
    }

    NetInfo *getBoundPipNet(PipId pip) const override { return tileStatus.at(pip.tile).boundpips.at(pip.index); }

    // -------------------------------------------------

    int32_t estimate_delay_mult;
    delay_t estimateDelay(WireId src, WireId dst) const override;
    delay_t predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const override;
    delay_t getDelayEpsilon() const override { return 20; }
    delay_t getRipupDelayPenalty() const override;
    delay_t getWireRipupDelayPenalty(WireId wire) const;
    float getDelayNS(delay_t v) const override { return v * 0.001; }
    delay_t getDelayFromNS(float ns) const override { return delay_t(ns * 1000); }
    uint32_t getDelayChecksum(delay_t v) const override { return v; }
    BoundingBox getRouteBoundingBox(WireId src, WireId dst) const override;

    // for better DSP bounding boxes
    void pre_routing();
    pool<WireId> dsp_wires, lram_wires;

    // -------------------------------------------------

    // Get the delay through a cell from one port to another, returning false
    // if no path exists. This only considers combinational delays, as required by the Arch API
    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const override;
    // Get the port class, also setting clockInfoCount to the number of TimingClockingInfos associated with a port
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const override;
    // Get the TimingClockingInfo of a port
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const override;

    // -------------------------------------------------

    // Perform placement validity checks, returning false on failure (all
    // implemented in arch_place.cc)

    // Return true whether all Bels at a given location are valid
    bool isBelLocationValid(BelId bel, bool explain_invalid = false) const override;

    // -------------------------------------------------

    bool pack() override;
    bool place() override;
    bool route() override;

    // arch-specific post-placement optimisations
    void post_place_opt();

    // -------------------------------------------------
    // Assign architecture-specific arguments to nets and cells, which must be
    // called between packing or further
    // netlist modifications, and validity checks
    void assignArchInfo() override;
    void assignCellInfo(CellInfo *cell);

    // -------------------------------------------------
    // Arch-specific global routing
    void route_globals();
    // -------------------------------------------------
    // Override for DSP clusters
    bool getClusterPlacement(ClusterId cluster, BelId root_bel,
                             std::vector<std::pair<CellInfo *, BelId>> &placement) const override;

    // -------------------------------------------------

    std::vector<GraphicElement> getDecalGraphics(DecalId decal) const override;

    DecalXY getBelDecal(BelId bel) const override;
    DecalXY getWireDecal(WireId wire) const override;
    DecalXY getPipDecal(PipId pip) const override;
    DecalXY getGroupDecal(GroupId group) const override;

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
        return IdString(db->loctypes[chip_info->grid[pip.tile].loc_type].wires[wire].name);
    }
    IdString pip_dst_wire_name(PipId pip) const
    {
        int wire = pip_data(pip).to_wire;
        return IdString(db->loctypes[chip_info->grid[pip.tile].loc_type].wires[wire].name);
    }

    // -------------------------------------------------

    typedef dict<IdString, CellPinStyle> CellPinsData;

    dict<IdString, CellPinsData> cell_pins_db;
    CellPinStyle get_cell_pin_style(const CellInfo *cell, IdString port) const;

    void init_cell_pin_data();

    // -------------------------------------------------

    // Parse a possibly-Lattice-style (C literal in Verilog string) style parameter
    Property parse_lattice_param_from_cell(const CellInfo *ci, IdString prop, int width, int64_t defval) const;
    Property parse_lattice_param(const Property &val, IdString prop, int width, const char *ci = "") const;

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
        range.e.cursor = nh_data(wire).wire_neighbours[wire.index].neigh_wires.size();
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
    static const dict<std::string, IOTypeData> io_types;
    int get_io_type_vcc(const std::string &io_type) const;
    bool is_io_type_diff(const std::string &io_type) const;
    bool is_io_type_ref(const std::string &io_type) const;

    // -------------------------------------------------
    // Cell timing lookup helpers

    bool is_dsp_cell(const CellInfo *cell) const;

    // Given cell type and variant, get the index inside the speed grade timing data
    int get_cell_timing_idx(IdString cell_type, IdString cell_variant = IdString()) const;
    // Return true and set delay if a comb path exists in a given cell timing index
    bool lookup_cell_delay(int type_idx, IdString from_port, IdString to_port, DelayQuad &delay) const;
    // Get setup and hold time for a given cell timing index and signal/clock pair
    void lookup_cell_setuphold(int type_idx, IdString from_port, IdString clock, DelayPair &setup,
                               DelayPair &hold) const;
    // Get setup and hold time and associated clock for a given cell timing index and signal
    void lookup_cell_setuphold_clock(int type_idx, IdString from_port, IdString &clock, DelayPair &setup,
                                     DelayPair &hold) const;
    // Similar to lookup_cell_delay but only needs the 'to' signal, intended for clk->out delays
    void lookup_cell_clock_out(int type_idx, IdString to_port, IdString &clock, DelayQuad &delay) const;
    // Attempt to look up port type based on database
    TimingPortClass lookup_port_type(int type_idx, IdString port, PortType dir, IdString clock) const;
    // -------------------------------------------------

    // List of IO constraints, used by PDC parser
    dict<IdString, dict<IdString, Property>> io_attr;

    void read_pdc(std::istream &in);

    // -------------------------------------------------
    void write_fasm(std::ostream &out) const;
    // -------------------------------------------------
    static void list_devices();
};

NEXTPNR_NAMESPACE_END

#endif /* NEXUS_ARCH_H */
