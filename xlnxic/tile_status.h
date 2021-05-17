/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

#ifndef XILINX_INTERCHANGE_TILESTATUS_H
#define XILINX_INTERCHANGE_TILESTATUS_H

#include "chipdb.h"
#include "hashlib.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

#include <array>

NEXTPNR_NAMESPACE_BEGIN

struct LogicGroupStatus
{
    bool valid = true, dirty = false;
};

struct LUTStatus
{
    bool is_fractured = false;
    dict<IdString, int> net2input;
};

struct LogicSiteStatus
{
    ArchFamily family;
    LogicSiteStatus(ArchFamily family);

    mutable bool tile_valid = true, tile_dirty = false;    // tile-wide config
    std::vector<CellInfo *> bound;                         // bound cells by place_idx
    mutable std::array<LogicGroupStatus, 8> eighth_status; // eighth (xcup/versal) or quarter (xc7) of a site - LUTs
    mutable std::array<LogicGroupStatus, 2> half_status; // half (xcup/versal) or whole (xc7) of a site - FF control set

    std::array<LUTStatus, 8> lut_status; // per-LUT data

    CellInfo *get_cell(uint32_t eighth, LogicBelIdx::LogicBel bel) const
    {
        return bound.at(LogicBelIdx(eighth, bel).idx);
    }
    void update_bel(uint32_t place_idx, CellInfo *cell);
    void bind_bel(uint32_t place_idx, CellInfo *cell);
    void unbind_bel(uint32_t place_idx);
    void update_lut_inputs();
};

struct LutKey
{
    int32_t tile;
    int16_t site;
    int16_t eighth;

    LutKey(int32_t tile, int16_t site, int16_t eighth) : tile(tile), site(site), eighth(eighth) {}
    bool operator==(const LutKey &other) const
    {
        return tile == other.tile && site == other.site && eighth == other.eighth;
    }
    bool operator!=(const LutKey &other) const
    {
        return tile != other.tile || site != other.site || eighth != other.eighth;
    }
    unsigned int hash() const { return mkhash_add(tile, mkhash_add(site, eighth)); }
};

struct SiteStatus
{
    // Number of currently bound cells
    int bound_count = 0;
    // Index of site variant currently in use
    int variant = -1;
    // Site-type-specific status info
    std::unique_ptr<LogicSiteStatus> logic;
};

struct Context;

struct TileStatus
{
    TileStatus(Context *ctx, int tile_idx);
    // Back-references
    Context *ctx;
    int tile_idx;
    // Fast lookup of bound cells by bel index
    std::vector<CellInfo *> bound_cells;
    // Per-site information
    std::vector<SiteStatus> sites;

    void bind_bel(BelId bel, CellInfo *cell, PlaceStrength strength);
    void unbind_bel(BelId bel);
    bool is_bel_avail(BelId bel) const;
    CellInfo *get_bound_bel_cell(BelId bel) const;
    const LUTStatus &get_lut_status(BelId bel) const;
};

NEXTPNR_NAMESPACE_END

#endif
