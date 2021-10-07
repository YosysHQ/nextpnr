/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2020  Pepijn de Vos <pepijn@symbioticeda.com>
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

#ifndef GOWIN_ARCHDEFS_H
#define GOWIN_ARCHDEFS_H

#include "base_clusterinfo.h"
#include "idstring.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

typedef float delay_t;

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

typedef IdString BelId;
typedef IdString WireId;
typedef IdString PipId;
typedef IdString GroupId;
typedef IdString DecalId;
typedef IdString BelBucketId;
typedef IdString ClusterId;

struct ArchNetInfo
{
};

struct NetInfo;

struct ArchCellInfo : BaseClusterInfo
{
    // Is the flip-flop of this slice used
    bool ff_used;
    // The type of this flip-flop
    IdString ff_type;
    // Is a slice type primitive
    bool is_slice;

    // Only packing rule for slice type primitives is a single clock per tile
    const NetInfo *slice_clk;
    const NetInfo *slice_ce;
    const NetInfo *slice_lsr;
};

NEXTPNR_NAMESPACE_END

#endif /* GOWIN_ARCHDEFS_H */
