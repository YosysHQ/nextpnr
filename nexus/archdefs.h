/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  gatecat <gatecat@ds0.me>
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

#ifndef NEXUS_ARCHDEFS_H
#define NEXUS_ARCHDEFS_H

#include "base_clusterinfo.h"
#include "hashlib.h"
#include "idstring.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

typedef int delay_t;

// https://bugreports.qt.io/browse/QTBUG-80789

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

struct BelId
{
    int32_t tile = -1;
    // PIP index in tile
    int32_t index = -1;

    BelId() = default;
    inline BelId(int32_t tile, int32_t index) : tile(tile), index(index){};

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
    // Node wires: tile == -1; index = node index in chipdb
    // Tile wires: tile != -1; index = wire index in tile
    int32_t index = -1;

    WireId() = default;
    inline WireId(int32_t tile, int32_t index) : tile(tile), index(index){};

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
    inline PipId(int32_t tile, int32_t index) : tile(tile), index(index){};

    bool operator==(const PipId &other) const { return tile == other.tile && index == other.index; }
    bool operator!=(const PipId &other) const { return tile != other.tile || index != other.index; }
    bool operator<(const PipId &other) const
    {
        return tile < other.tile || (tile == other.tile && index < other.index);
    }
    unsigned int hash() const { return mkhash(tile, index); }
};

typedef IdString BelBucketId;

struct GroupId
{
    enum : int8_t
    {
        TYPE_NONE,
    } type = TYPE_NONE;
    int8_t x = 0, y = 0;

    bool operator==(const GroupId &other) const { return (type == other.type) && (x == other.x) && (y == other.y); }
    bool operator!=(const GroupId &other) const { return (type != other.type) || (x != other.x) || (y == other.y); }
    unsigned int hash() const { return mkhash(mkhash(x, y), int(type)); }
};

struct DecalId
{
    enum : int8_t
    {
        TYPE_NONE,
        TYPE_BEL,
        TYPE_WIRE,
        TYPE_PIP,
        TYPE_GROUP
    } type = TYPE_NONE;
    int32_t index = -1;
    bool active = false;

    bool operator==(const DecalId &other) const
    {
        return (type == other.type) && (index == other.index) && (active == other.active);
    }
    bool operator!=(const DecalId &other) const
    {
        return (type != other.type) || (index != other.index) || (active != other.active);
    }
    unsigned int hash() const { return mkhash(index, int(type)); }
};

struct ArchNetInfo
{
    bool is_global;
    bool is_clock, is_reset;
};

struct NetInfo;

struct FFControlSet
{
    int clkmux, cemux, lsrmux;
    bool async, regddr_en, gsr_en;
    NetInfo *clk, *lsr, *ce;
};

inline bool operator!=(const FFControlSet &a, const FFControlSet &b)
{
    return (a.clkmux != b.clkmux) || (a.cemux != b.cemux) || (a.lsrmux != b.lsrmux) || (a.async != b.async) ||
           (a.regddr_en != b.regddr_en) || (a.gsr_en != b.gsr_en) || (a.clk != b.clk) || (a.lsr != b.lsr) ||
           (a.ce != b.ce);
}

typedef IdString ClusterId;

struct ArchCellInfo : BaseClusterInfo
{
    union
    {
        struct
        {
            bool is_memory, is_carry, mux2_used;
            NetInfo *f, *ofx;
        } lutInfo;
        struct
        {
            FFControlSet ctrlset;
            NetInfo *di, *m;
        } ffInfo;
    };

    int tmg_index = -1;
    // Map from cell/bel ports to logical timing ports
    dict<IdString, IdString> tmg_portmap;
    // For DSP cluster override
    bool is_9x9_18x18 = false;
};

NEXTPNR_NAMESPACE_END

#endif /* NEXUS_ARCHDEFS_H */
