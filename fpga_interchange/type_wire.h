/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Symbiflow Authors
 *
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

#ifndef TYPE_WIRE_H
#define TYPE_WIRE_H

#include <vector>
#include <algorithm>

#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

NEXTPNR_NAMESPACE_BEGIN

struct Context;

struct TypeWireId {
    TypeWireId() : type(-1), index(-1) {}
    TypeWireId(const Context *ctx, WireId wire_inst);

    bool operator==(const TypeWireId &other) const { return type == other.type && index == other.index; }
    bool operator!=(const TypeWireId &other) const { return type != other.type || index != other.index; }
    bool operator<(const TypeWireId &other) const
    {
        return type < other.type || (type == other.type && index < other.index);
    }

    int32_t type;
    int32_t index;
};

struct TypeWirePair {
    TypeWireId src;
    TypeWireId dst;

    bool operator==(const TypeWirePair &other) const {
        return src == other.src && dst == other.dst;
    }
    bool operator!=(const TypeWirePair &other) const {
        return src != other.src || dst != other.dst;
    }
};

struct TypeWireSet {
public:
    TypeWireSet(const Context *ctx, WireId wire);
    std::size_t hash() const {
        return hash_;
    }

    bool operator==(const TypeWireSet &other) const {
        return wire_types_ == other.wire_types_;
    }
    bool operator!=(const TypeWireSet &other) const {
        return wire_types_ != other.wire_types_;
    }
private:
    std::size_t hash_;
    std::vector<TypeWireId> wire_types_;
};

NEXTPNR_NAMESPACE_END

template <> struct std::hash<NEXTPNR_NAMESPACE_PREFIX TypeWireId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX TypeWireId &wire) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, std::hash<int>()(wire.type));
        boost::hash_combine(seed, std::hash<int>()(wire.index));
        return seed;
    }
};

template <> struct std::hash<NEXTPNR_NAMESPACE_PREFIX TypeWirePair>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX TypeWirePair &pair) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX TypeWireId>()(pair.src));
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX TypeWireId>()(pair.dst));
        return seed;
    }
};

template <> struct std::hash<NEXTPNR_NAMESPACE_PREFIX TypeWireSet>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX TypeWireSet &set) const noexcept
    {
        return set.hash();
    }
};

#endif /* TYPE_WIRE_H */
