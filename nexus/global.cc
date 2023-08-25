/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  gatecat <gatecat@ds0.me>
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

#include "log.h"
#include "nextpnr.h"
#include "util.h"

#include <queue>

NEXTPNR_NAMESPACE_BEGIN

namespace {
bool str_match(const std::string &s, const std::string &pattern)
{
    if (s.size() != pattern.size())
        return false;
    for (size_t i = 0; i < s.size(); i++)
        if (pattern.at(i) != '?' && s.at(i) != pattern.at(i))
            return false;
    return true;
}
} // namespace

struct NexusGlobalRouter
{
    Context *ctx;

    NexusGlobalRouter(Context *ctx) : ctx(ctx){};

    // When routing globals; we allow global->local for some tricky cases but never local->local
    bool global_pip_filter(PipId pip) const
    {
        IdString dest_basename(ctx->wire_data(ctx->getPipDstWire(pip)).name);
        const std::string &s = dest_basename.str(ctx);
        if (s.size() > 2 && (s[0] == 'H' || s[0] == 'V') && s[1] == '0')
            return false;
        return true;
    }

    // These rules make sure global->fabric connections are always routeable, as they won't be ripup-able by the general
    // router
    bool routeability_pip_filter(PipId pip) const
    {
        IdString dest_basename(ctx->wire_data(ctx->getPipDstWire(pip)).name);
        const std::string &s = dest_basename.str(ctx);
        if (str_match(s, "JDI?_DIMUX")) {
            IdString src_basename(ctx->wire_data(ctx->getPipSrcWire(pip)).name);
            return str_match(src_basename.str(ctx), "JM?_DIMUX");
        } else if (str_match(s, "JDL?_DRMUX")) {
            IdString src_basename(ctx->wire_data(ctx->getPipSrcWire(pip)).name);
            return str_match(src_basename.str(ctx), "JD?_DRMUX");
        }
        return true;
    }

    // Dedicated backwards BFS routing for global networks
    template <typename Tfilt>
    bool backwards_bfs_route(NetInfo *net, store_index<PortRef> user_idx, int iter_limit, bool strict, Tfilt pip_filter)
    {
        // Queue of wires to visit
        std::queue<WireId> visit;
        // Wire -> upstream pip
        dict<WireId, PipId> backtrace;

        // Lookup source and destination wires
        WireId src = ctx->getNetinfoSourceWire(net);
        WireId dst = ctx->getNetinfoSinkWire(net, net->users.at(user_idx), 0);

        if (src == WireId())
            log_error("Net '%s' has an invalid source port %s.%s\n", ctx->nameOf(net), ctx->nameOf(net->driver.cell),
                      ctx->nameOf(net->driver.port));

        if (dst == WireId())
            log_error("Net '%s' has an invalid sink port %s.%s\n", ctx->nameOf(net),
                      ctx->nameOf(net->users.at(user_idx).cell), ctx->nameOf(net->users.at(user_idx).port));

        if (ctx->getBoundWireNet(src) != net)
            ctx->bindWire(src, net, STRENGTH_LOCKED);

        if (src == dst) {
            // Nothing more to do
            return true;
        }

        visit.push(dst);
        backtrace[dst] = PipId();

        int iter = 0;

        while (!visit.empty() && (iter++ < iter_limit)) {
            WireId cursor = visit.front();
            visit.pop();
            // Search uphill pips
            for (PipId pip : ctx->getPipsUphill(cursor)) {
                // Skip pip if unavailable, and not because it's already used for this net
                if (!ctx->checkPipAvail(pip) && ctx->getBoundPipNet(pip) != net)
                    continue;
                WireId prev = ctx->getPipSrcWire(pip);
                // Ditto for the upstream wire
                if (!ctx->checkWireAvail(prev) && ctx->getBoundWireNet(prev) != net)
                    continue;
                // Skip already visited wires
                if (backtrace.count(prev))
                    continue;
                // Apply our custom pip filter
                if (!pip_filter(pip))
                    continue;
                // Add to the queue
                visit.push(prev);
                backtrace[prev] = pip;
                // Check if we are done yet
                if (prev == src)
                    goto done;
            }
            if (false) {
            done:
                break;
            }
        }

        if (backtrace.count(src)) {
            WireId cursor = src;
            std::vector<PipId> pips;
            // Create a list of pips on the routed path
            while (true) {
                PipId pip = backtrace.at(cursor);
                if (pip == PipId())
                    break;
                pips.push_back(pip);
                cursor = ctx->getPipDstWire(pip);
            }
            // Reverse that list
            std::reverse(pips.begin(), pips.end());
            // Bind pips until we hit already-bound routing
            for (PipId pip : pips) {
                WireId dst = ctx->getPipDstWire(pip);
                if (ctx->getBoundWireNet(dst) == net)
                    break;
                ctx->bindPip(pip, net, STRENGTH_LOCKED);
            }
            return true;
        } else {
            if (strict)
                log_error("Failed to route net '%s' from %s to %s using dedicated routing.\n", ctx->nameOf(net),
                          ctx->nameOfWire(src), ctx->nameOfWire(dst));
            return false;
        }
    }

    bool is_relaxed_sink(const PortRef &sink) const
    {
        // These DPHY clock ports can't be routed without going through some general routing
        if (sink.cell->type == id_DPHY_CORE && sink.port.in(id_URXCKINE, id_UCENCK, id_UTXCKE, id_U3TDE5CK))
            return true;
        // Cases where global clocks are driving fabric
        if ((sink.cell->type == id_OXIDE_COMB && sink.port != id_WCK) ||
            (sink.cell->type == id_OXIDE_FF && sink.port != id_CLK))
            return true;
        return false;
    }

    void route_clk_net(NetInfo *net)
    {
        for (auto usr : net->users.enumerate())
            backwards_bfs_route(net, usr.index, 1000000, true, [&](PipId pip) {
                return (is_relaxed_sink(usr.value) || global_pip_filter(pip)) && routeability_pip_filter(pip);
            });
        log_info("    routed net '%s' using global resources\n", ctx->nameOf(net));
    }

    void operator()()
    {
        log_info("Routing globals...\n");
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            CellInfo *drv = ni->driver.cell;
            if (drv == nullptr)
                continue;
            if (drv->type.in(id_DCC, id_DCS)) {
                route_clk_net(ni);
                continue;
            }
        }
    }
};

void Arch::route_globals()
{
    NexusGlobalRouter glb_router(getCtx());
    glb_router();
}

NEXTPNR_NAMESPACE_END
