/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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

#ifndef NEXTPNR_H
#error Include "archdefs.h" via "nextpnr.h" only.
#endif

#include <boost/functional/hash.hpp>

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

// -----------------------------------------------------------------------

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

NPNR_PACKED_STRUCT(struct LocationPOD { int16_t x, y; });

struct Location
{
    int16_t x = -1, y = -1;
    Location() : x(-1), y(-1){};
    Location(int16_t x, int16_t y) : x(x), y(y){};
    Location(const LocationPOD &pod) : x(pod.x), y(pod.y){};
    Location(const Location &loc) : x(loc.x), y(loc.y){};

    bool operator==(const Location &other) const { return x == other.x && y == other.y; }
    bool operator!=(const Location &other) const { return x != other.x || y != other.y; }
    bool operator<(const Location &other) const { return y == other.y ? x < other.x : y < other.y; }
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
};

struct GroupId
{
    int32_t index = -1;

    bool operator==(const GroupId &other) const { return index == other.index; }
    bool operator!=(const GroupId &other) const { return index != other.index; }
};

struct DecalId
{
    enum
    {
        TYPE_NONE,
        TYPE_BEL
    } type;
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
};

struct ArchNetInfo
{
    bool is_global = false;
};

struct ArchCellInfo
{
    struct
    {
        bool using_dff;
        bool has_l6mux;
        bool is_carry;
        IdString clk_sig, lsr_sig, clkmux, lsrmux, srmode;
        int sd0, sd1;
    } sliceInfo;
};

NEXTPNR_NAMESPACE_END

namespace std {
template <> struct hash<NEXTPNR_NAMESPACE_PREFIX Location>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX Location &loc) const noexcept
    {
        std::size_t seed = std::hash<int>()(loc.x);
        seed ^= std::hash<int>()(loc.y) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX BelId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX BelId &bel) const noexcept
    {
        std::size_t seed = std::hash<NEXTPNR_NAMESPACE_PREFIX Location>()(bel.location);
        seed ^= std::hash<int>()(bel.index) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX WireId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX WireId &wire) const noexcept
    {
        std::size_t seed = std::hash<NEXTPNR_NAMESPACE_PREFIX Location>()(wire.location);
        seed ^= std::hash<int>()(wire.index) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX PipId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX PipId &pip) const noexcept
    {
        std::size_t seed = std::hash<NEXTPNR_NAMESPACE_PREFIX Location>()(pip.location);
        seed ^= std::hash<int>()(pip.index) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX GroupId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX GroupId &group) const noexcept
    {
        return std::hash<int>()(group.index);
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX DecalId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX DecalId &decal) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, hash<int>()(decal.type));
        boost::hash_combine(seed, hash<NEXTPNR_NAMESPACE_PREFIX Location>()(decal.location));
        boost::hash_combine(seed, hash<int>()(decal.z));
        boost::hash_combine(seed, hash<bool>()(decal.active));
        return seed;
    }
};

} // namespace std
