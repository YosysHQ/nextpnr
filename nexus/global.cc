/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  David Shah <dave@ds0.me>
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

    // Dedicated backwards BFS routing for global networks
    template <typename Tfilt>
    bool backwards_bfs_route(NetInfo *net, size_t user_idx, int iter_limit, bool strict, Tfilt pip_filter)
    {
        // Queue of wires to visit
        std::queue<WireId> visit;
        // Wire -> upstream pip
        std::unordered_map<WireId, PipId> backtrace;

        // Lookup source and destination wires
        WireId src = ctx->getNetinfoSourceWire(net);
        WireId dst = ctx->getNetinfoSinkWire(net, net->users.at(user_idx));

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

    void route_clk_net(NetInfo *net)
    {
        for (size_t i = 0; i < net->users.size(); i++)
            backwards_bfs_route(net, i, 1000000, true, [&](PipId pip) { return global_pip_filter(pip); });
        log_info("    routed net '%s' using global resources\n", ctx->nameOf(net));
    }

    void operator()()
    {
        log_info("Routing globals...\n");
        for (auto net : sorted(ctx->nets)) {
            NetInfo *ni = net.second;
            CellInfo *drv = ni->driver.cell;
            if (drv == nullptr)
                continue;
            if (drv->type == id_DCC) {
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
