/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
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

#ifndef ECP5_ARCHDEFS_H
#define ECP5_ARCHDEFS_H

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
    ,
    DB_CONST_ID_COUNT
};

#define X(t) static constexpr auto id_##t = IdString(ID_##t);
#include "constids.inc"
#undef X
#endif

NPNR_PACKED_STRUCT(struct LocationPOD { int16_t x, y; });

struct Location
{
    int16_t x = -1, y = -1;
    Location() : x(-1), y(-1){};
    Location(int16_t x, int16_t y) : x(x), y(y){};
    Location(const LocationPOD &pod) : x(pod.x), y(pod.y){};

    bool operator==(const Location &other) const { return x == other.x && y == other.y; }
    bool operator!=(const Location &other) const { return x != other.x || y != other.y; }
    bool operator<(const Location &other) const { return y == other.y ? x < other.x : y < other.y; }
    unsigned int hash() const { return mkhash(x, y); }
};

inline Location operator+(const Location &a, const Location &b) { return Location(a.x + b.x, a.y + b.y); }

struct BelId
{
    Location location;
    int32_t index = -1;

    bool operator==(const BelId &other) const { return index == other.index && location == other.location; }
    bool operator!=(const BelId &other) const { return index != other.index || location != other.location; }
    bool operator<(const BelId &other) const
    {
        return location == other.location ? index < other.index : location < other.location;
    }
    unsigned int hash() const { return mkhash(location.hash(), index); }
};

struct WireId
{
    Location location;
    int32_t index = -1;

    bool operator==(const WireId &other) const { return index == other.index && location == other.location; }
    bool operator!=(const WireId &other) const { return index != other.index || location != other.location; }
    bool operator<(const WireId &other) const
    {
        return location == other.location ? index < other.index : location < other.location;
    }
    unsigned int hash() const { return mkhash(location.hash(), index); }
};

struct PipId
{
    Location location;
    int32_t index = -1;

    bool operator==(const PipId &other) const { return index == other.index && location == other.location; }
    bool operator!=(const PipId &other) const { return index != other.index || location != other.location; }
    bool operator<(const PipId &other) const
    {
        return location == other.location ? index < other.index : location < other.location;
    }
    unsigned int hash() const { return mkhash(location.hash(), index); }
};

typedef IdString BelBucketId;

struct GroupId
{
    enum : int8_t
    {
        TYPE_NONE,
        TYPE_SWITCHBOX
    } type = TYPE_NONE;
    Location location;

    bool operator==(const GroupId &other) const { return (type == other.type) && (location == other.location); }
    bool operator!=(const GroupId &other) const { return (type != other.type) || (location != other.location); }
    unsigned int hash() const { return mkhash(location.hash(), int(type)); }
};

struct DecalId
{
    enum
    {
        TYPE_NONE,
        TYPE_BEL,
        TYPE_WIRE,
        TYPE_PIP,
        TYPE_GROUP
    } type = TYPE_NONE;
    Location location;
    uint32_t z = 0;
    bool active = false;
    bool operator==(const DecalId &other) const
    {
        return type == other.type && location == other.location && z == other.z && active == other.active;
    }
    bool operator!=(const DecalId &other) const
    {
        return type != other.type || location != other.location || z != other.z || active != other.active;
    }
    unsigned int hash() const { return mkhash(location.hash(), mkhash(z, int(type))); }
};

struct ArchNetInfo
{
    bool is_global = false;
};

typedef IdString ClusterId;

struct NetInfo;

struct ArchCellInfo : BaseClusterInfo
{
    struct
    {
        bool using_dff;
        bool has_l6mux;
        bool is_carry;
        bool is_memory;
        IdString clk_sig, lsr_sig, clkmux, lsrmux, srmode;
        int sd0, sd1;
    } sliceInfo;
    struct
    {
        bool is_pdp;
        // Are the outputs from a DP16KD registered (OUTREG)
        // or non-registered (NOREG)
        bool is_output_a_registered;
        bool is_output_b_registered;
        // Which timing information to use for a DP16KD. Depends on registering
        // configuration.
        IdString regmode_timing_id;
    } ramInfo;
    struct
    {
        bool is_clocked;
        IdString timing_id;
    } multInfo;
};

NEXTPNR_NAMESPACE_END
#endif /* ECP5_ARCHDEFS_H */
