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

    bool global_pip_available(PipId pip) const { return gwu.is_global_pip(pip) || ctx->checkPipAvail(pip); };

    // allow io->global, global->global and global->tile clock
    bool global_pip_filter(PipId pip) const
    {
        auto is_local = [&](IdString wire_type) {
            return !wire_type.in(id_GLOBAL_CLK, id_IO_O, id_IO_I, id_PLL_O, id_PLL_I, id_TILE_CLK);
        };

        IdString src_type = ctx->getWireType(ctx->getPipSrcWire(pip));
        IdString dst_type = ctx->getWireType(ctx->getPipDstWire(pip));
        bool src_valid = src_type.in(id_GLOBAL_CLK, id_IO_O, id_PLL_O, id_HCLK);
        bool dst_valid = dst_type.in(id_GLOBAL_CLK, id_TILE_CLK, id_PLL_I, id_IO_I, id_HCLK);

        bool res = (src_valid && dst_valid) || (src_valid && is_local(dst_type)) || (is_local(src_type) && dst_valid);
        if (ctx->debug && false /*&& res*/) {
            log_info("%s <- %s [%s <- %s]\n", ctx->getWireName(ctx->getPipDstWire(pip)).str(ctx).c_str(),
                     ctx->getWireName(ctx->getPipSrcWire(pip)).str(ctx).c_str(), dst_type.c_str(ctx),
                     src_type.c_str(ctx));
            log_info("res:%d, src_valid:%d, dst_valid:%d, src local:%d, dst local:%d\n", res, src_valid, dst_valid,
                     is_local(src_type), is_local(dst_type));
        }
        return res;
    }

    bool is_relaxed_sink(const PortRef &sink) const { return false; }

    // Dedicated backwards BFS routing for global networks
    template <typename Tfilt>
    bool backwards_bfs_route(NetInfo *net, WireId src, WireId dst, int iter_limit, bool strict, Tfilt pip_filter)
    {
        // log_info("route arc %s:%s->%s\n", net->name.c_str(ctx), ctx->nameOfWire(src), ctx->nameOfWire(dst));
        // Queue of wires to visit
        std::queue<WireId> visit;
        // Wire -> upstream pip
        dict<WireId, PipId> backtrace;

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
                if (!global_pip_available(pip) && ctx->getBoundPipNet(pip) != net) {
                    continue;
                }
                WireId prev = ctx->getPipSrcWire(pip);
                // Ditto for the upstream wire
                if (!ctx->checkWireAvail(prev) && ctx->getBoundWireNet(prev) != net) {
                    continue;
                }
                // Skip already visited wires
                if (backtrace.count(prev)) {
                    continue;
                }
                // Apply our custom pip filter
                if (!pip_filter(pip)) {
                    continue;
                }
                // Add to the queue
                visit.push(prev);
                backtrace[prev] = pip;
                // Check if we are done yet
                if (prev == src) {
                    goto done;
                }
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
                if (pip == PipId()) {
                    break;
                }
                pips.push_back(pip);
                cursor = ctx->getPipDstWire(pip);
                // log_info(">> %s:%s\n", ctx->getPipName(pip).str(ctx).c_str(), ctx->nameOfWire(cursor));
            }
            // Reverse that list
            std::reverse(pips.begin(), pips.end());
            // Bind pips until we hit already-bound routing
            for (PipId pip : pips) {
                WireId dst = ctx->getPipDstWire(pip);
                // log_info("bind pip %s:%s\n", ctx->getPipName(pip).str(ctx).c_str(), ctx->nameOfWire(dst));
                if (ctx->getBoundWireNet(dst) == net) {
                    break;
                }
                ctx->bindPip(pip, net, STRENGTH_LOCKED);
            }
            return true;
        } else {
            if (strict) {
                log_error("Failed to route net '%s' from %s to %s using dedicated routing.\n", ctx->nameOf(net),
                          ctx->nameOfWire(src), ctx->nameOfWire(dst));
            } else {
                log_warning("Failed to route net '%s' from %s to %s using dedicated routing.\n", ctx->nameOf(net),
                            ctx->nameOfWire(src), ctx->nameOfWire(dst));
                return false;
            }
        }
    }

    enum RouteResult
    {
        NOT_ROUTED = 0,
        ROUTED_PARTIALLY,
        ROUTED_ALL
    };

    RouteResult route_direct_net(NetInfo *net)
    {
        // Lookup source and destination wires
        WireId src = ctx->getNetinfoSourceWire(net);
        if (src == WireId())
            log_error("Net '%s' has an invalid source port %s.%s\n", ctx->nameOf(net), ctx->nameOf(net->driver.cell),
                      ctx->nameOf(net->driver.port));

        if (ctx->getBoundWireNet(src) != net) {
            ctx->bindWire(src, net, STRENGTH_LOCKED);
        }

        RouteResult routed = NOT_ROUTED;
        for (auto usr : net->users.enumerate()) {
            WireId dst = ctx->getNetinfoSinkWire(net, net->users.at(usr.index), 0);
            if (dst == WireId()) {
                log_error("Net '%s' has an invalid sink port %s.%s\n", ctx->nameOf(net),
                          ctx->nameOf(net->users.at(usr.index).cell), ctx->nameOf(net->users.at(usr.index).port));
            }
            if (backwards_bfs_route(net, src, dst, 1000000, false, [&](PipId pip) {
                    return (is_relaxed_sink(usr.value) || global_pip_filter(pip));
                })) {
                routed = routed == ROUTED_PARTIALLY ? routed : ROUTED_ALL;
            } else {
                routed = routed == NOT_ROUTED ? routed : ROUTED_PARTIALLY;
            }
        }
        if (routed == NOT_ROUTED) {
            ctx->unbindWire(src);
        }
        return routed;
    }

    void route_buffered_net(NetInfo *net)
    {
        // a) route net after buf using the buf input as source
        CellInfo *buf_ci = net->driver.cell;
        WireId src = ctx->getBelPinWire(buf_ci->bel, id_I);

        NetInfo *net_before_buf = buf_ci->getPort(id_I);
        NPNR_ASSERT(net_before_buf != nullptr);

        if (src == WireId()) {
            log_error("Net '%s' has an invalid source port %s.%s\n", ctx->nameOf(net), ctx->nameOf(net->driver.cell),
                      ctx->nameOf(net->driver.port));
        }
        ctx->bindWire(src, net, STRENGTH_LOCKED);

        for (auto usr : net->users.enumerate()) {
            WireId dst = ctx->getNetinfoSinkWire(net, net->users.at(usr.index), 0);
            if (dst == WireId()) {
                log_error("Net '%s' has an invalid sink port %s.%s\n", ctx->nameOf(net),
                          ctx->nameOf(net->users.at(usr.index).cell), ctx->nameOf(net->users.at(usr.index).port));
            }
            // log_info(" usr wire: %s\n", ctx->nameOfWire(dst));
            backwards_bfs_route(net, src, dst, 1000000, true,
                                [&](PipId pip) { return (is_relaxed_sink(usr.value) || global_pip_filter(pip)); });
        }

        // b) route net before buf from whatever to the buf input
        WireId dst = src;
        CellInfo *true_src_ci = net_before_buf->driver.cell;
        src = ctx->getBelPinWire(true_src_ci->bel, net_before_buf->driver.port);
        ctx->bindWire(src, net, STRENGTH_LOCKED);
        ctx->unbindWire(dst);
        backwards_bfs_route(net, src, dst, 1000000, false, [&](PipId pip) { return true; });
        // remove net
        buf_ci->movePortTo(id_O, true_src_ci, net_before_buf->driver.port);
        net_before_buf->driver.cell = nullptr;
    }

    void route_clk_net(NetInfo *net)
    {
        RouteResult res = route_direct_net(net);
        if (res) {
            log_info("    routed net '%s' using global resources %s.\n", ctx->nameOf(net),
                     res == ROUTED_ALL ? "only" : "partially");
        }
    }

    bool driver_is_buf(const PortRef &driver) { return CellTypePort(driver) == CellTypePort(id_BUFG, id_O); }

    bool driver_is_clksrc(const PortRef &driver)
    {
        // dedicated pins
        if (CellTypePort(driver) == CellTypePort(id_IBUF, id_O)) {

            NPNR_ASSERT(driver.cell->bel != BelId());
            IdStringList pin_func = gwu.get_pin_funcs(driver.cell->bel);
            for (size_t i = 0; i < pin_func.size(); ++i) {
                if (ctx->debug) {
                    log_info("bel:%s, pin func: %zu:%s\n", ctx->nameOfBel(driver.cell->bel), i,
                             pin_func[i].str(ctx).c_str());
                }
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

        std::vector<IdString> routed_nets;
        // buffered nets first
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            CellInfo *drv = ni->driver.cell;
            if (drv == nullptr || ni->users.empty()) {
                if (ctx->debug) {
                    log_info("skip empty or driverless net:%s\n", ctx->nameOf(ni));
                }
                continue;
            }
            if (driver_is_buf(ni->driver)) {
                if (ctx->verbose) {
                    log_info("route buffered net '%s'\n", ctx->nameOf(ni));
                }
                route_buffered_net(ni);
                routed_nets.push_back(net.first);
                continue;
            }
        }
        for (auto &net : ctx->nets) {
            if (std::find(routed_nets.begin(), routed_nets.end(), net.first) != routed_nets.end()) {
                if (ctx->debug) {
                    log_info("skip already routed net:%s\n", net.first.c_str(ctx));
                }
                continue;
            }
            NetInfo *ni = net.second.get();
            CellInfo *drv = ni->driver.cell;
            if (drv == nullptr || ni->users.empty()) {
                if (ctx->debug) {
                    log_info("skip empty or driverless net:%s\n", ctx->nameOf(ni));
                }
                continue;
            }
            if (driver_is_clksrc(ni->driver)) {
                if (ctx->verbose) {
                    log_info("route clock net '%s'\n", ctx->nameOf(ni));
                }
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
