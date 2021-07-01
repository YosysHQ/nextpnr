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

#include "site_routing_cache.h"

#include "context.h"
#include "site_arch.impl.h"

NEXTPNR_NAMESPACE_BEGIN

void SiteRoutingSolution::store_solution(const SiteArch *ctx, const RouteNodeStorage *node_storage,
                                         const SiteWire &driver, std::vector<size_t> solutions)
{
    clear();

    solution_sinks.reserve(solutions.size());
    inverted.reserve(solutions.size());
    can_invert.reserve(solutions.size());

    for (size_t route : solutions) {
        bool sol_inverted = false;
        bool sol_can_invert = false;

        SiteWire wire = node_storage->get_node(route)->wire;
        solution_sinks.push_back(wire);

        solution_offsets.push_back(solution_storage.size());
        Node cursor = node_storage->get_node(route);
        while (cursor.has_parent()) {
            if (ctx->isInverting(cursor->pip) && !sol_can_invert) {
                sol_inverted = !sol_inverted;
            }
            if (ctx->canInvert(cursor->pip)) {
                sol_inverted = false;
                sol_can_invert = true;
            }

            solution_storage.push_back(cursor->pip);
            Node parent = cursor.parent();
            NPNR_ASSERT(ctx->getPipDstWire(cursor->pip) == cursor->wire);
            NPNR_ASSERT(ctx->getPipSrcWire(cursor->pip) == parent->wire);
            cursor = parent;
        }

        inverted.push_back(sol_inverted);
        can_invert.push_back(sol_can_invert);

        NPNR_ASSERT(cursor->wire == driver);
    }

    solution_offsets.push_back(solution_storage.size());
}

bool SiteRoutingSolution::verify(const SiteArch *ctx, const SiteNetInfo &net)
{
    pool<SiteWire> seen_users;
    for (size_t i = 0; i < num_solutions(); ++i) {
        SiteWire cursor = solution_sink(i);
        NPNR_ASSERT(net.users.count(cursor) == 1);
        seen_users.emplace(cursor);

        auto begin = solution_begin(i);
        auto end = solution_end(i);

        for (auto iter = begin; iter != end; ++iter) {
            SitePip pip = *iter;
            NPNR_ASSERT(ctx->getPipDstWire(pip) == cursor);
            cursor = ctx->getPipSrcWire(pip);
        }

        NPNR_ASSERT(net.driver == cursor);
    }

    return seen_users.size() == net.users.size();
}

SiteRoutingKey SiteRoutingKey::make(const SiteArch *ctx, const SiteNetInfo &site_net)
{
    SiteRoutingKey out;

    out.tile_type = ctx->site_info->tile_type;
    out.site = ctx->site_info->site;

    out.net_type = ctx->ctx->get_net_type(site_net.net);
    out.driver_type = site_net.driver.type;
    if (site_net.driver.type == SiteWire::SITE_WIRE) {
        out.driver_index = site_net.driver.wire.index;
    } else {
        NPNR_ASSERT(site_net.driver.type == SiteWire::OUT_OF_SITE_SOURCE);
        out.driver_index = -1;
    }

    out.user_types.reserve(site_net.users.size());
    out.user_indicies.reserve(site_net.users.size());

    std::vector<SiteWire> users;
    users.reserve(site_net.users.size());
    users.insert(users.begin(), site_net.users.begin(), site_net.users.end());

    std::sort(users.begin(), users.end());

    for (const SiteWire &user : users) {
        out.user_types.push_back(user.type);

        if (user.type == SiteWire::SITE_WIRE) {
            out.user_indicies.push_back(user.wire.index);
        } else {
            NPNR_ASSERT(user.type == SiteWire::OUT_OF_SITE_SINK);
            out.user_indicies.push_back(-1);
        }
    }

    return out;
}

bool SiteRoutingCache::get_solution(const SiteArch *ctx, const SiteNetInfo &net, SiteRoutingSolution *solution) const
{
    SiteRoutingKey key = SiteRoutingKey::make(ctx, net);
    auto iter = cache_.find(key);
    if (iter == cache_.end()) {
        return false;
    }

    *solution = iter->second;
    const auto &tile_type_data = ctx->site_info->chip_info().tile_types[ctx->site_info->tile_type];

    for (SiteWire &wire : solution->solution_sinks) {
        switch (wire.type) {
        case SiteWire::SITE_WIRE:
            wire.wire.tile = ctx->site_info->tile;
            break;
        case SiteWire::OUT_OF_SITE_SOURCE:
            wire.net = net.net;
            break;
        case SiteWire::OUT_OF_SITE_SINK:
            wire.net = net.net;
            break;
        case SiteWire::SITE_PORT_SINK: {
            const auto &pip_data = tile_type_data.pip_data[wire.pip.index];
            wire.pip.tile = ctx->site_info->tile;
            wire.wire = canonical_wire(&ctx->site_info->chip_info(), ctx->site_info->tile, pip_data.dst_index);
            break;
        }
        case SiteWire::SITE_PORT_SOURCE: {
            const auto &pip_data = tile_type_data.pip_data[wire.pip.index];
            wire.pip.tile = ctx->site_info->tile;
            wire.wire = canonical_wire(&ctx->site_info->chip_info(), ctx->site_info->tile, pip_data.src_index);
            break;
        }
        default:
            NPNR_ASSERT(false);
        }
    }

    for (SitePip &pip : solution->solution_storage) {
        pip.pip.tile = ctx->site_info->tile;
        switch (pip.type) {
        case SitePip::SITE_PIP:
            // Done!
            break;
        case SitePip::SITE_PORT:
            // Done!
            break;
        case SitePip::SOURCE_TO_SITE_PORT:
            NPNR_ASSERT(pip.wire.type == SiteWire::OUT_OF_SITE_SOURCE);
            pip.wire.net = net.net;
            break;
        case SitePip::SITE_PORT_TO_SINK:
            NPNR_ASSERT(pip.wire.type == SiteWire::OUT_OF_SITE_SINK);
            pip.wire.net = net.net;
            break;
        case SitePip::SITE_PORT_TO_SITE_PORT:
            pip.other_pip.tile = ctx->site_info->tile;
            break;
        default:
            NPNR_ASSERT(false);
        }
    }

    return solution->verify(ctx, net);
}

void SiteRoutingCache::add_solutions(const SiteArch *ctx, const SiteNetInfo &net, const SiteRoutingSolution &solution)
{
    SiteRoutingKey key = SiteRoutingKey::make(ctx, net);

    cache_[key] = solution;
}

void SiteRoutingCache::clear() { cache_.clear(); }

NEXTPNR_NAMESPACE_END
