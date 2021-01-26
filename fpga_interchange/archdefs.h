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

#ifndef NEXTPNR_H
#error Include "archdefs.h" via "nextpnr.h" only.
#endif

NEXTPNR_NAMESPACE_BEGIN

#include <cstdint>

typedef int delay_t;

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
};

struct GroupId
{
    bool operator==(const GroupId &other) const { return true; }
    bool operator!=(const GroupId &other) const { return false; }
};

struct DecalId
{
    bool operator==(const DecalId &other) const { return true; }
    bool operator!=(const DecalId &other) const { return false; }
};

struct ArchNetInfo
{
};

struct NetInfo;

struct ArchCellInfo
{
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
        return seed;
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX DecalId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX DecalId &decal) const noexcept
    {
        std::size_t seed = 0;
        return seed;
    }
};
} // namespace std
