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
    bool verify(const SiteArch *ctx, const SiteNetInfo &net);

    void clear()
    {
        solution_offsets.clear();
        solution_storage.clear();
        solution_sinks.clear();
        inverted.clear();
        can_invert.clear();
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

    bool solution_inverted(size_t solution) const { return inverted.at(solution) != 0; }

    bool solution_can_invert(size_t solution) const { return can_invert.at(solution) != 0; }

    std::vector<size_t> solution_offsets;
    std::vector<SitePip> solution_storage;
    std::vector<SiteWire> solution_sinks;
    std::vector<uint8_t> inverted;
    std::vector<uint8_t> can_invert;
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

    unsigned int hash() const
    {
        unsigned int seed = 0;
        seed = mkhash(seed, tile_type);
        seed = mkhash(seed, site);
        seed = mkhash(seed, int(net_type));
        seed = mkhash(seed, int(driver_type));
        seed = mkhash(seed, driver_index);
        seed = mkhash(seed, user_types.size());
        for (auto t : user_types)
            seed = mkhash(seed, int(t));
        seed = mkhash(seed, user_indicies.size());
        for (auto i : user_indicies)
            seed = mkhash(seed, i);
        return seed;
    }
};

// Provides an LRU cache for site routing solutions.
class SiteRoutingCache
{
  public:
    bool get_solution(const SiteArch *ctx, const SiteNetInfo &net, SiteRoutingSolution *solution) const;
    void add_solutions(const SiteArch *ctx, const SiteNetInfo &net, const SiteRoutingSolution &solution);
    void clear();

  private:
    dict<SiteRoutingKey, SiteRoutingSolution> cache_;
};

NEXTPNR_NAMESPACE_END

#endif /* SITE_ROUTING_CACHE_H */
