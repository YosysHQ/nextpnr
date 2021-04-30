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

#include <boost/functional/hash.hpp>

#include "cyclonev.h"

#include "idstring.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

using mistral::CycloneV;

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
};

struct WireId
{
    int32_t index = -1;

    bool operator==(const WireId &other) const { return index == other.index; }
    bool operator!=(const WireId &other) const { return index != other.index; }
    bool operator<(const WireId &other) const { return index < other.index; }
};

struct PipId
{
    int32_t index = -1;

    bool operator==(const PipId &other) const { return index == other.index; }
    bool operator!=(const PipId &other) const { return index != other.index; }
    bool operator<(const PipId &other) const { return index < other.index; }
};

typedef IdString DecalId;
typedef IdString GroupId;
typedef IdString BelBucketId;

struct ArchNetInfo
{
};

struct ArchCellInfo
{
};

NEXTPNR_NAMESPACE_END

namespace std {
template <> struct hash<NEXTPNR_NAMESPACE_PREFIX BelId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX BelId &bel) const noexcept
    {
        return hash<uint32_t>()((static_cast<uint32_t>(bel.pos) << 16) | bel.z);
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX WireId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX WireId &wire) const noexcept
    {
        return hash<int>()(wire.index);
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX PipId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX PipId &pip) const noexcept { return hash<int>()(pip.index); }
};

} // namespace std

#endif