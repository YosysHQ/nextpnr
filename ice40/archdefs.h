/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
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

#ifndef ICE40_ARCHDEFS_H
#define ICE40_ARCHDEFS_H

#include "base_clusterinfo.h"
#include "hashlib.h"
#include "idstring.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

typedef int delay_t;

// -----------------------------------------------------------------------

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
    int32_t index = -1;

    bool operator==(const BelId &other) const { return index == other.index; }
    bool operator!=(const BelId &other) const { return index != other.index; }
    bool operator<(const BelId &other) const { return index < other.index; }
    unsigned int hash() const { return index; }
};

struct WireId
{
    int32_t index = -1;

    bool operator==(const WireId &other) const { return index == other.index; }
    bool operator!=(const WireId &other) const { return index != other.index; }
    bool operator<(const WireId &other) const { return index < other.index; }
    unsigned int hash() const { return index; }
};

struct PipId
{
    int32_t index = -1;

    bool operator==(const PipId &other) const { return index == other.index; }
    bool operator!=(const PipId &other) const { return index != other.index; }
    bool operator<(const PipId &other) const { return index < other.index; }
    unsigned int hash() const { return index; }
};

struct GroupId
{
    enum : int8_t
    {
        TYPE_NONE,
        TYPE_FRAME,
        TYPE_MAIN_SW,
        TYPE_LOCAL_SW,
        TYPE_LC0_SW,
        TYPE_LC1_SW,
        TYPE_LC2_SW,
        TYPE_LC3_SW,
        TYPE_LC4_SW,
        TYPE_LC5_SW,
        TYPE_LC6_SW,
        TYPE_LC7_SW
    } type = TYPE_NONE;
    int8_t x = 0, y = 0;

    bool operator==(const GroupId &other) const { return (type == other.type) && (x == other.x) && (y == other.y); }
    bool operator!=(const GroupId &other) const { return (type != other.type) || (x != other.x) || (y != other.y); }
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

    bool operator==(const DecalId &other) const { return (type == other.type) && (index == other.index); }
    bool operator!=(const DecalId &other) const { return (type != other.type) || (index != other.index); }
    unsigned int hash() const { return mkhash(index, int(type)); }
};

struct ArchNetInfo
{
    bool is_global = false;
    bool is_reset = false, is_enable = false;
};

struct NetInfo;

struct ArchCellInfo : BaseClusterInfo
{
    union
    {
        struct
        {
            bool dffEnable;
            bool carryEnable;
            bool negClk;
            int inputCount;
            unsigned lutInputMask;
            const NetInfo *clk, *cen, *sr;
        } lcInfo;
        struct
        {
            bool lvds;
            bool global;
            bool negtrig;
            int pintype;
            // TODO: clk packing checks...
        } ioInfo;
        struct
        {
            bool forPadIn;
        } gbInfo;
        struct
        {
            bool ledCurConnected;
        } ledInfo;
    };
};

typedef IdString BelBucketId;
typedef IdString ClusterId;

NEXTPNR_NAMESPACE_END

#endif /* ICE40_ARCHDEFS_H */
