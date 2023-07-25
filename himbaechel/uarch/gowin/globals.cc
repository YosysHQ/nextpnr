/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

#define HIMBAECHEL_CONSTIDS "uarch/gowin/constids.inc"
#include "himbaechel_constids.h"
#include "himbaechel_helpers.h"

#include "globals.h"
#include "gowin.h"
#include "gowin_utils.h"

NEXTPNR_NAMESPACE_BEGIN

struct GowinGlobalRouter
{
    Context *ctx;
    GowinUtils gwu;

    GowinGlobalRouter(Context *ctx) : ctx(ctx) { gwu.init(ctx); };

    // allow io->global, global->global and global->tile clock
    bool global_pip_filter(PipId pip) const
    {
        auto is_local = [&](IdString wire_type) {
            return !wire_type.in(id_GLOBAL_CLK, id_IO_O, id_IO_I, id_PLL_O, id_PLL_I, id_TILE_CLK);
        };

        IdString src_type = ctx->getWireType(ctx->getPipSrcWire(pip));
        IdString dst_type = ctx->getWireType(ctx->getPipDstWire(pip));
        bool src_valid = src_type.in(id_GLOBAL_CLK, id_IO_O, id_PLL_O);
        bool dst_valid = dst_type.in(id_GLOBAL_CLK, id_TILE_CLK, id_PLL_I, id_IO_I);

        if (ctx->debug && false) {
            log_info("%s <- %s [%s <- %s]\n", ctx->getWireName(ctx->getPipDstWire(pip)).str(ctx).c_str(),
                     ctx->getWireName(ctx->getPipSrcWire(pip)).str(ctx).c_str(), dst_type.c_str(ctx),
                     src_type.c_str(ctx));
        }
        return (src_valid && dst_valid) || (src_valid && is_local(dst_type)) || (is_local(src_type) && dst_valid);
    }

    bool is_relaxed_sink(const PortRef &sink) const { return false; }

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

    void route_clk_net(NetInfo *net)
    {
        bool routed = false;
        for (auto usr : net->users.enumerate()) {
            routed = backwards_bfs_route(net, usr.index, 1000000, true, [&](PipId pip) {
                return (is_relaxed_sink(usr.value) || global_pip_filter(pip));
            });
            if (!routed) {
                break;
            }
        }

        if (routed) {
            log_info("    routed net '%s' using global resources\n", ctx->nameOf(net));
        }
    }

    bool driver_is_clksrc(const PortRef &driver)
    {
        // dedicated pins
        if (CellTypePort(driver) == CellTypePort(id_IBUF, id_O)) {

            NPNR_ASSERT(driver.cell->bel != BelId());
            IdStringList pin_func = gwu.get_pin_funcs(driver.cell->bel);
            for (size_t i = 0; i < pin_func.size(); ++i) {
                if (pin_func[i].str(ctx).rfind("GCLKT", 0) == 0) {
                    if (ctx->debug) {
                        log_info("Clock pin:%s:%s\n", ctx->getBelName(driver.cell->bel).str(ctx).c_str(),
                                 pin_func[i].c_str(ctx));
                    }
                    return true;
                }
            }
        }
        // PLL outputs
        if (driver.cell->type.in(id_rPLL, id_PLLVR)) {
            if (driver.port.in(id_CLKOUT, id_CLKOUTD, id_CLKOUTD3, id_CLKOUTP)) {
                if (ctx->debug) {
                    log_info("PLL out:%s:%s\n", ctx->getBelName(driver.cell->bel).str(ctx).c_str(),
                             driver.port.c_str(ctx));
                }
                return true;
            }
        }
        return false;
    }

    void run(void)
    {
        log_info("Routing globals...\n");
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            CellInfo *drv = ni->driver.cell;
            if (drv == nullptr)
                continue;
            if (driver_is_clksrc(ni->driver)) {
                route_clk_net(ni);
                continue;
            }
        }
    }
};

void gowin_route_globals(Context *ctx)
{
    GowinGlobalRouter router(ctx);
    router.run();
}

NEXTPNR_NAMESPACE_END
