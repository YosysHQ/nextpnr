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

#ifndef SITE_ROUTING_CACHE_H
#define SITE_ROUTING_CACHE_H

#include "PhysicalNetlist.capnp.h"
#include "nextpnr_namespaces.h"
#include "site_arch.h"
#include "site_routing_storage.h"

NEXTPNR_NAMESPACE_BEGIN

struct SiteRoutingSolution
{
    void store_solution(const SiteArch *ctx, const RouteNodeStorage *node_storage, const SiteWire &driver,
                        std::vector<size_t> solutions);
    void verify(const SiteArch *ctx, const SiteNetInfo &net);

    void clear()
    {
        solution_offsets.clear();
        solution_storage.clear();
        solution_sinks.clear();
    }

    size_t num_solutions() const { return solution_sinks.size(); }

    const SiteWire &solution_sink(size_t solution) const { return solution_sinks.at(solution); }

    std::vector<SitePip>::const_iterator solution_begin(size_t solution) const
    {
        NPNR_ASSERT(solution + 1 < solution_offsets.size());
        return solution_storage.begin() + solution_offsets.at(solution);
    }

    std::vector<SitePip>::const_iterator solution_end(size_t solution) const
    {
        NPNR_ASSERT(solution + 1 < solution_offsets.size());
        return solution_storage.begin() + solution_offsets.at(solution + 1);
    }

    std::vector<size_t> solution_offsets;
    std::vector<SitePip> solution_storage;
    std::vector<SiteWire> solution_sinks;
};

struct SiteRoutingKey
{
    int32_t tile_type;
    int32_t site;
    // The net type matters for site routing. Legal routes for VCC/GND/SIGNAL
    // nets are different.
    PhysicalNetlist::PhysNetlist::NetType net_type;
    SiteWire::Type driver_type;
    int32_t driver_index;
    std::vector<SiteWire::Type> user_types;
    std::vector<int32_t> user_indicies;

    bool operator==(const SiteRoutingKey &other) const
    {
        return tile_type == other.tile_type && site == other.site && net_type == other.net_type &&
               driver_type == other.driver_type && driver_index == other.driver_index &&
               user_types == other.user_types && user_indicies == other.user_indicies;
    }
    bool operator!=(const SiteRoutingKey &other) const
    {
        return tile_type != other.tile_type || site != other.site || net_type != other.net_type ||
               driver_type != other.driver_type || driver_index != other.driver_index ||
               user_types != other.user_types || user_indicies != other.user_indicies;
    }

    static SiteRoutingKey make(const SiteArch *ctx, const SiteNetInfo &site_net);
};

NEXTPNR_NAMESPACE_END

template <> struct std::hash<NEXTPNR_NAMESPACE_PREFIX SiteRoutingKey>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX SiteRoutingKey &key) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, std::hash<int32_t>()(key.tile_type));
        boost::hash_combine(seed, std::hash<int32_t>()(key.site));
        boost::hash_combine(seed, std::hash<PhysicalNetlist::PhysNetlist::NetType>()(key.net_type));
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX SiteWire::Type>()(key.driver_type));
        boost::hash_combine(seed, std::hash<int32_t>()(key.driver_index));
        boost::hash_combine(seed, std::hash<std::size_t>()(key.user_types.size()));
        for (NEXTPNR_NAMESPACE_PREFIX SiteWire::Type user_type : key.user_types) {
            boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX SiteWire::Type>()(user_type));
        }

        boost::hash_combine(seed, std::hash<std::size_t>()(key.user_indicies.size()));
        for (int32_t index : key.user_indicies) {
            boost::hash_combine(seed, std::hash<int32_t>()(index));
        }
        return seed;
    }
};

NEXTPNR_NAMESPACE_BEGIN

// Provides an LRU cache for site routing solutions.
class SiteRoutingCache
{
  public:
    bool get_solution(const SiteArch *ctx, const SiteNetInfo &net, SiteRoutingSolution *solution) const;
    void add_solutions(const SiteArch *ctx, const SiteNetInfo &net, const SiteRoutingSolution &solution);

  private:
    absl::flat_hash_map<SiteRoutingKey, SiteRoutingSolution> cache_;
};

NEXTPNR_NAMESPACE_END

#endif /* SITE_ROUTING_CACHE_H */
