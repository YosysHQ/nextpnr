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

#ifndef DEDICATED_INTERCONNECT_H
#define DEDICATED_INTERCONNECT_H

#include <boost/functional/hash.hpp>
#include <cstdint>
#include <unordered_map>

#include "archdefs.h"
#include "idstring.h"
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

struct TileTypeBelPin
{
    int32_t tile_type;
    int32_t bel_index;
    IdString bel_pin;

    bool operator<(const TileTypeBelPin &other) const
    {
        if (tile_type >= other.tile_type) {
            return false;
        }

        if (bel_index >= other.bel_index) {
            return false;
        }

        return bel_pin < other.bel_pin;
    }

    bool operator==(const TileTypeBelPin &other) const
    {
        return tile_type == other.tile_type && bel_index == other.bel_index && bel_pin == other.bel_pin;
    }
    bool operator!=(const TileTypeBelPin &other) const
    {
        return tile_type != other.tile_type || bel_index != other.bel_index || bel_pin != other.bel_pin;
    }
};

struct DeltaTileTypeBelPin
{
    int32_t delta_x;
    int32_t delta_y;
    TileTypeBelPin type_bel_pin;

    bool operator==(const DeltaTileTypeBelPin &other) const
    {
        return delta_x == other.delta_x && delta_y == other.delta_y && type_bel_pin == other.type_bel_pin;
    }
    bool operator!=(const DeltaTileTypeBelPin &other) const
    {
        return delta_x != other.delta_x || delta_y != other.delta_y || type_bel_pin != other.type_bel_pin;
    }
};

NEXTPNR_NAMESPACE_END

template <> struct std::hash<NEXTPNR_NAMESPACE_PREFIX TileTypeBelPin>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX TileTypeBelPin &type_bel_pin) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, std::hash<int32_t>()(type_bel_pin.tile_type));
        boost::hash_combine(seed, std::hash<int32_t>()(type_bel_pin.bel_index));
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX IdString>()(type_bel_pin.bel_pin));
        return seed;
    }
};

template <> struct std::hash<NEXTPNR_NAMESPACE_PREFIX DeltaTileTypeBelPin>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX DeltaTileTypeBelPin &delta_bel_pin) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, std::hash<int32_t>()(delta_bel_pin.delta_x));
        boost::hash_combine(seed, std::hash<int32_t>()(delta_bel_pin.delta_y));
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX TileTypeBelPin>()(delta_bel_pin.type_bel_pin));
        return seed;
    }
};

NEXTPNR_NAMESPACE_BEGIN

struct Context;

// This class models dedicated interconnect present in the given fabric.
//
// Examples of dedicate interconnect:
//  - IBUF.O -> ISERDES.I
//  - IBUF.O -> IDELAY.I
//  - CARRY4.CO[3] -> CARRY4.CIN
//
//  Note that CARRY4.CYINIT does not **require** dedicated interconnect, so
//  it doesn't qualify.
//
//  This class discovers dedicated interconnect by examing the routing graph.
//  This discovery make be expensive, and require caching to accelerate
//  startup.
struct DedicatedInterconnect
{
    const Context *ctx;

    std::unordered_map<TileTypeBelPin, std::unordered_set<DeltaTileTypeBelPin>> sinks;
    std::unordered_map<TileTypeBelPin, std::unordered_set<DeltaTileTypeBelPin>> sources;

    void init(const Context *ctx);

    // Is this BEL placed in a location that is valid based on dedicated
    // interconnect?
    //
    // Note: Only BEL pin sinks are checked.
    bool isBelLocationValid(BelId bel, const CellInfo *cell) const;
    void explain_bel_status(BelId bel, const CellInfo *cell) const;

    void find_dedicated_interconnect();
    void print_dedicated_interconnect() const;
    bool check_routing(BelId src_bel, IdString src_bel_pin, BelId dst_bel, IdString dst_bel_pin, bool site_only) const;
    void expand_sink_bel(BelId bel, IdString pin, WireId wire);
    void expand_source_bel(BelId bel, IdString pin, WireId wire);

    bool is_driver_on_net_valid(BelId driver_bel, const CellInfo *cell, IdString driver_port, NetInfo *net) const;
    bool is_sink_on_net_valid(BelId bel, const CellInfo *cell, IdString port_name, NetInfo *net) const;
};

NEXTPNR_NAMESPACE_END

#endif /* DEDICATED_INTERCONNECT_H */
