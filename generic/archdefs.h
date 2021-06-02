/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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

#ifndef GENERIC_ARCHDEFS_H
#define GENERIC_ARCHDEFS_H

#include "hashlib.h"
#include "idstringlist.h"

NEXTPNR_NAMESPACE_BEGIN

typedef float delay_t;

typedef IdStringList BelId;
typedef IdStringList WireId;
typedef IdStringList PipId;
typedef IdStringList GroupId;
typedef IdStringList DecalId;
typedef IdString BelBucketId;
typedef IdString ClusterId;

struct ArchNetInfo
{
};

struct NetInfo;

struct ArchCellInfo
{
    // Custom grouping set via "PACK_GROUP" attribute. All cells with the same group
    // value may share a tile (-1 = don't care, default if not set)
    int user_group;
    // Is a slice type primitive
    bool is_slice;
    // Only packing rule for slice type primitives is a single clock per tile
    const NetInfo *slice_clk;
    // Cell to bel pin mapping
    dict<IdString, std::vector<IdString>> bel_pins;
};

NEXTPNR_NAMESPACE_END

#endif /* GENERIC_ARCHDEFS_H */
