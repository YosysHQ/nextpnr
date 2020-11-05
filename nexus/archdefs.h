/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  David Shah <dave@ds0.me>
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

#ifndef NEXTPNR_H
#error Include "archdefs.h" via "nextpnr.h" only.
#endif

NEXTPNR_NAMESPACE_BEGIN

typedef int delay_t;

struct DelayInfo
{
    delay_t min_delay = 0, max_delay = 0;

    delay_t minRaiseDelay() const { return min_delay; }
    delay_t maxRaiseDelay() const { return max_delay; }

    delay_t minFallDelay() const { return min_delay; }
    delay_t maxFallDelay() const { return max_delay; }

    delay_t minDelay() const { return min_delay; }
    delay_t maxDelay() const { return max_delay; }

    DelayInfo operator+(const DelayInfo &other) const
    {
        DelayInfo ret;
        ret.min_delay = this->min_delay + other.min_delay;
        ret.max_delay = this->max_delay + other.max_delay;
        return ret;
    }
};
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
};

struct GroupId
{
    enum : int8_t
    {
        TYPE_NONE,
    } type = TYPE_NONE;
    int8_t x = 0, y = 0;

    bool operator==(const GroupId &other) const { return (type == other.type) && (x == other.x) && (y == other.y); }
    bool operator!=(const GroupId &other) const { return (type != other.type) || (x != other.x) || (y == other.y); }
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

struct ArchCellInfo
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
};

NEXTPNR_NAMESPACE_END

namespace std {
template <> struct hash<NEXTPNR_NAMESPACE_PREFIX BelId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX BelId &bel) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, hash<int>()(bel.tile));
        boost::hash_combine(seed, hash<int>()(bel.index));
        return seed;
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX WireId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX WireId &wire) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, hash<int>()(wire.tile));
        boost::hash_combine(seed, hash<int>()(wire.index));
        return seed;
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX PipId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX PipId &pip) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, hash<int>()(pip.tile));
        boost::hash_combine(seed, hash<int>()(pip.index));
        return seed;
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX GroupId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX GroupId &group) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, hash<int>()(group.type));
        boost::hash_combine(seed, hash<int>()(group.x));
        boost::hash_combine(seed, hash<int>()(group.y));
        return seed;
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX DecalId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX DecalId &decal) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, hash<int>()(decal.type));
        boost::hash_combine(seed, hash<int>()(decal.index));
        return seed;
    }
};
} // namespace std
