/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Lofty <dan.ravensloft@gmail.com
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

#ifndef MISTRAL_ARCHDEFS_H
#define MISTRAL_ARCHDEFS_H

#include "base_clusterinfo.h"
#include "cyclonev.h"

#include "hashlib.h"
#include "idstring.h"
#include "nextpnr_assertions.h"
#include "nextpnr_namespaces.h"

#include <limits>

NEXTPNR_NAMESPACE_BEGIN

using mistral::CycloneV;

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

struct DelayInfo
{
    delay_t delay = 0;

    delay_t minRaiseDelay() const { return delay; }
    delay_t maxRaiseDelay() const { return delay; }

    delay_t minFallDelay() const { return delay; }
    delay_t maxFallDelay() const { return delay; }

    delay_t minDelay() const { return delay; }
    delay_t maxDelay() const { return delay; }

    DelayInfo operator+(const DelayInfo &other) const
    {
        DelayInfo ret;
        ret.delay = this->delay + other.delay;
        return ret;
    }
};

struct BelId
{
    BelId() = default;
    BelId(CycloneV::pos_t _pos, uint16_t _z) : pos{_pos}, z{_z} {}

    // pos_t is used for X/Y, nextpnr-cyclonev uses its own Z coordinate system.
    CycloneV::pos_t pos = 0;
    uint16_t z = 0;

    bool operator==(const BelId &other) const { return pos == other.pos && z == other.z; }
    bool operator!=(const BelId &other) const { return pos != other.pos || z != other.z; }
    bool operator<(const BelId &other) const { return pos < other.pos || (pos == other.pos && z < other.z); }
    unsigned int hash() const { return mkhash(pos, z); }
};

static constexpr auto invalid_rnode = std::numeric_limits<CycloneV::rnode_t>::max();

struct WireId
{
    WireId() = default;
    explicit WireId(CycloneV::rnode_t node) : node(node){};
    CycloneV::rnode_t node = invalid_rnode;

    // Wires created by nextpnr have rnode type >= 128
    bool is_nextpnr_created() const
    {
        NPNR_ASSERT(node != invalid_rnode);
        return unsigned(CycloneV::rn2t(node)) >= 128;
    }

    bool operator==(const WireId &other) const { return node == other.node; }
    bool operator!=(const WireId &other) const { return node != other.node; }
    bool operator<(const WireId &other) const { return node < other.node; }
    unsigned int hash() const { return unsigned(node); }
};

struct PipId
{
    PipId() = default;
    PipId(CycloneV::rnode_t src, CycloneV::rnode_t dst) : src(src), dst(dst){};
    CycloneV::rnode_t src = invalid_rnode, dst = invalid_rnode;

    bool operator==(const PipId &other) const { return src == other.src && dst == other.dst; }
    bool operator!=(const PipId &other) const { return src != other.src || dst != other.dst; }
    bool operator<(const PipId &other) const { return dst < other.dst || (dst == other.dst && src < other.src); }
    unsigned int hash() const { return mkhash(src, dst); }
};

typedef IdString DecalId;
typedef IdString GroupId;
typedef IdString BelBucketId;
typedef IdString ClusterId;

struct ArchNetInfo
{
    bool is_global = false;
};

enum CellPinState
{
    PIN_SIG = 0,
    PIN_0 = 1,
    PIN_1 = 2,
    PIN_INV = 3,
};

struct ArchPinInfo
{
    // Used to represent signals that are either tied to implicit constants (rather than explicitly routed constants);
    // or are inverted
    CellPinState state = PIN_SIG;
    // The physical bel pins that this logical pin maps to
    std::vector<IdString> bel_pins;
};

struct NetInfo;

// Structures for representing how FF control sets are stored and validity-checked
struct ControlSig
{
    const NetInfo *net;
    bool inverted;

    bool connected() const { return net != nullptr; }
    bool operator==(const ControlSig &other) const { return net == other.net && inverted == other.inverted; }
    bool operator!=(const ControlSig &other) const { return net == other.net && inverted == other.inverted; }
};

struct FFControlSet
{
    ControlSig clk, ena, aclr, sclr, sload;

    bool operator==(const FFControlSet &other) const
    {
        return clk == other.clk && ena == other.ena && aclr == other.aclr && sclr == other.sclr && sload == other.sload;
    }
    bool operator!=(const FFControlSet &other) const { return !(*this == other); }
};

struct ArchCellInfo : BaseClusterInfo
{
    union
    {
        struct
        {
            // Store the nets here for fast validity checking (avoids too many map lookups in a hot path)
            std::array<const NetInfo *, 7> lut_in;
            const NetInfo *comb_out;

            int lut_input_count;
            int used_lut_input_count; // excluding those null/constant
            int lut_bits_count;

            // for the LAB routeability check (see the detailed description in lab.cc); usually the same signal feeding
            // multiple ALMs in a LAB is counted multiple times, due to not knowing which routing resources it will need
            // in each case. But carry chains where we know how things will pack are allowed to share across ALMs as a
            // special case, primarily to support adders/subtractors with a 'B invert' control signal shared across all
            // ALMs.
            int chain_shared_input_count;

            bool is_carry, is_shared, is_extended;
            bool carry_start, carry_end;

            // MLABs with compatible write ports have this set to the same non-negative integer. -1 means this isn't a
            // MLAB
            int mlab_group;
            ControlSig wclk, we;
        } combInfo;
        struct
        {
            FFControlSet ctrlset;
            const NetInfo *sdata, *datain;
        } ffInfo;
    };

    dict<IdString, ArchPinInfo> pin_data;

    CellPinState get_pin_state(IdString pin) const;
};

NEXTPNR_NAMESPACE_END

#endif
