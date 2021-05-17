
/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021 gatecat <gatecat@ds0.me>
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

#ifndef XILINX_INTERCHANGE_ARCHDEFS_H
#define XILINX_INTERCHANGE_ARCHDEFS_H

#include <array>
#include <cstdint>

#include "hashlib.h"
#include "idstring.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

#ifndef Q_MOC_RUN

enum ConstIds
{
    ID_NONE
#define X(t) , ID_##t
#include "constids.inc"
#undef X
};

#define X(t) static constexpr auto id_##t = IdString(ID_##t);
#include "constids.inc"
#undef X

#endif

typedef int32_t delay_t;

struct BelId
{
    int32_t tile = -1;
    // PIP index in tile
    int32_t index = -1;

    BelId() = default;
    BelId(int32_t tile, int32_t index) : tile(tile), index(index){};

    bool operator==(const BelId &other) const { return tile == other.tile && index == other.index; }
    bool operator!=(const BelId &other) const { return tile != other.tile || index != other.index; }
    bool operator<(const BelId &other) const
    {
        return tile < other.tile || (tile == other.tile && index < other.index);
    }
    unsigned int hash() const { return mkhash(tile, index); }
};

struct WireId
{
    int32_t tile = -1;
    int32_t index = -1;

    WireId() = default;
    WireId(int32_t tile, int32_t index) : tile(tile), index(index){};

    bool operator==(const WireId &other) const { return tile == other.tile && index == other.index; }
    bool operator!=(const WireId &other) const { return tile != other.tile || index != other.index; }
    bool operator<(const WireId &other) const
    {
        return tile < other.tile || (tile == other.tile && index < other.index);
    }
    unsigned int hash() const { return mkhash(tile, index); }
};

struct PipId
{
    int32_t tile = -1;
    // PIP index in tile
    int32_t index = -1;

    PipId() = default;
    PipId(int32_t tile, int32_t index) : tile(tile), index(index){};

    bool operator==(const PipId &other) const { return tile == other.tile && index == other.index; }
    bool operator!=(const PipId &other) const { return tile != other.tile || index != other.index; }
    bool operator<(const PipId &other) const
    {
        return tile < other.tile || (tile == other.tile && index < other.index);
    }
    unsigned int hash() const { return mkhash(tile, index); }
};

typedef IdString DecalId;
typedef IdString GroupId;
typedef IdString BelBucketId;
typedef IdString ClusterId;

struct NetInfo;

struct ArchNetInfo
{
    IdString macro_parent;
};

struct CellInfo;

struct ClusterInfo
{
    std::vector<CellInfo *> cluster_cells;
    int tile_dx, tile_dy;
    int site_dx, site_dy;
    int place_idx;
    enum RelationType
    {
        UNCONSTRAINED = 0,
        ABS_PLACE_IDX = 1,
        REL_PLACE_IDX = 2,
        // ... (special relations for muxes/LUTFF pairs/etc?)
    } type;
};

struct ArchCellInfo
{
    union
    {
        struct
        {
            bool is_memory, is_srl;
            int input_count;
            std::array<NetInfo *, 6> input_sigs;
            std::array<NetInfo *, 3> address_msb; // WA[8..6]
            NetInfo *out, *out_casc;
            NetInfo *wclk;
            NetInfo *di;
            NetInfo *we, *we2;
            bool wclk_inv;
        } lutInfo;
        struct
        {
            bool is_latch, is_clkinv, is_srinv, is_async;
            NetInfo *clk, *sr, *ce, *d;
        } ffInfo;
        struct
        {
            std::array<NetInfo *, 8> out, cout, x, di;
            std::array<int, 8> di_port;
            bool ci_using_ax;
            std::array<bool, 8> di_using_x;
        } carryInfo;
        struct
        {
            NetInfo *sel, *out;
        } muxInfo;
    };

    IdString macro_parent, macro_inst;
    dict<IdString, std::vector<IdString>> cell_bel_pins;
    ClusterInfo cluster_info;
};

enum class ArchFamily
{
    XC7,
    XCU,
    XCUP,
    VERSAL,
};

NEXTPNR_NAMESPACE_END

#endif
