/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2021  Symbiflow Authors
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

#ifndef FPGA_INTERCHANGE_ARCHDEFS_H
#define FPGA_INTERCHANGE_ARCHDEFS_H

#include <cstdint>

#include "hashlib.h"
#include "luts.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

typedef int delay_t;

// -----------------------------------------------------------------------

struct BelId
{
    // Tile that contains this BEL.
    int32_t tile = -1;
    // Index into tile type BEL array.
    // BEL indicies are the same for all tiles of the same type.
    int32_t index = -1;

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
    // Tile that contains this wire.
    int32_t tile = -1;
    int32_t index = -1;

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
    int32_t index = -1;

    bool operator==(const PipId &other) const { return tile == other.tile && index == other.index; }
    bool operator!=(const PipId &other) const { return tile != other.tile || index != other.index; }
    bool operator<(const PipId &other) const
    {
        return tile < other.tile || (tile == other.tile && index < other.index);
    }
    unsigned int hash() const { return mkhash(tile, index); }
};

struct GroupId
{
    bool operator==(const GroupId &other) const { return true; }
    bool operator!=(const GroupId &other) const { return false; }
    unsigned int hash() const { return 0; }
};

struct DecalId
{
    bool operator==(const DecalId &other) const { return true; }
    bool operator!=(const DecalId &other) const { return false; }
    unsigned int hash() const { return 0; }
};

struct BelBucketId
{
    IdString name;

    bool operator==(const BelBucketId &other) const { return (name == other.name); }
    bool operator!=(const BelBucketId &other) const { return (name != other.name); }
    bool operator<(const BelBucketId &other) const { return name < other.name; }
    unsigned int hash() const { return name.hash(); }
};

typedef IdString ClusterId;

struct SiteExpansionLoop;

struct ArchNetInfo
{
    virtual ~ArchNetInfo();

    SiteExpansionLoop *loop = nullptr;
};

struct ArchCellInfo
{
    int32_t cell_mapping = -1;
    dict<IdString, std::vector<IdString>> cell_bel_pins;
    dict<IdString, std::vector<IdString>> masked_cell_bel_pins;
    pool<IdString> const_ports;
    IdString macro_parent = IdString();
    LutCell lut_cell;
};

NEXTPNR_NAMESPACE_END

#endif /* FPGA_INTERCHANGE_ARCHDEFS_H */
