/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2023 gatecat <gatecat@ds0.me>
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

#ifndef HIMBAECHEL_ARCHDEFS_H
#define HIMBAECHEL_ARCHDEFS_H

#include <array>
#include <cstdint>

#include "base_clusterinfo.h"
#include "hashlib.h"
#include "idstring.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

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

struct DecalId
{
    int32_t tile = -1;
    int32_t index = -1;
    enum DecalType
    {
        TYPE_NONE,
        TYPE_BEL,
        TYPE_WIRE,
        TYPE_PIP,
        TYPE_GROUP
    } type = TYPE_NONE;
    bool active = false;

    DecalId() = default;
    DecalId(int32_t tile, int32_t index, DecalType type) : tile(tile), index(index), type(type) {};

    bool operator==(const DecalId &other) const { return tile == other.tile && index == other.index && type == other.type; }
    bool operator!=(const DecalId &other) const { return tile != other.tile || index != other.index || type != other.type; }
    unsigned int hash() const { return mkhash(tile, mkhash(index, type)); }
};

typedef IdString GroupId;
typedef IdString BelBucketId;
typedef IdString ClusterId;

struct ArchNetInfo
{
    int flat_index;
};

struct ArchCellInfo : BaseClusterInfo
{
    int flat_index;
    int timing_index = -1;
    dict<IdString, std::vector<IdString>> cell_bel_pins;
};

NEXTPNR_NAMESPACE_END

#endif
