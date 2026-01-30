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

    bool segment_wire_filter(PipId pip) const { return !gwu.is_segment_pip(pip); }

    // To avoid a cycle where we connect the clock wire to the gate in
    // the global clock system and it ends up in the global clock MUX
    // again, we only allow connections from general-purpose wires.
    bool clock_gate_wire_filter(PipId pip) const
    {
        WireId dst = ctx->getPipDstWire(pip);
        IdString src_type = ctx->getWireType(ctx->getPipSrcWire(pip));
        return !(gwu.wire_is_clock_gate(dst) && src_type.in(id_GLOBAL_CLK, id_TILE_CLK));
    }

    bool dcs_input_filter(PipId pip) const
    {
        return !ctx->getWireName(ctx->getPipSrcWire(pip))[1].in(
                id_P16A, id_P16B, id_P16C, id_P16D, id_P17A, id_P17B, id_P17C, id_P17D, id_P26A, id_P26B, id_P26C,
                id_P26D, id_P27A, id_P27B, id_P27C, id_P27D, id_P36A, id_P36B, id_P36C, id_P36D, id_P37A, id_P37B,
                id_P37C, id_P37D, id_P46A, id_P46B, id_P46C, id_P46D, id_P47A, id_P47B, id_P47C, id_P47D);
    }

    // allow io->global, global->global and global->tile clock
    bool global_pip_filter(PipId pip, WireId src_wire) const
    {
        auto is_local = [&](IdString wire_type) {
            return !wire_type.in(id_GLOBAL_CLK, id_IO_O, id_IO_I, id_PLL_O, id_PLL_I, id_TILE_CLK);
        };
        WireId src, dst;
        src = ctx->getPipSrcWire(pip);
        dst = ctx->getPipDstWire(pip);
        IdString dst_name = ctx->getWireName(dst)[1];
        bool not_dcs_pip = dst_name != id_CLKOUT;
        IdString src_type = ctx->getWireType(src);
        IdString dst_type = ctx->getWireType(dst);
        bool src_is_outpin = src_type.in(id_IO_O, id_PLL_O, id_HCLK, id_DLLDLY, id_OSCOUT);
        bool src_valid = not_dcs_pip && (src_type == id_GLOBAL_CLK || src_is_outpin);
        bool dst_valid = not_dcs_pip && dst_type.in(id_GLOBAL_CLK, id_TILE_CLK, id_PLL_I, id_PLL_O, id_IO_I, id_HCLK);

        bool res;
        if (src == src_wire && (src_type == id_PLL_O || (!src_is_outpin))) {
            bool dst_is_spine = dst_name.str(ctx).rfind("SPINE", 0) == 0;
            res = src_valid && (dst_is_spine || dst_type == id_PLL_O);
        } else {
            res = (src_valid && dst_valid) || (src_valid && is_local(dst_type)) || (is_local(src_type) && dst_valid);
        }
        if (ctx->debug && false /*&& res*/) {
            log_info("%s <- %s [%s <- %s]\n", ctx->nameOfWire(ctx->getPipDstWire(pip)),
                     ctx->nameOfWire(ctx->getPipSrcWire(pip)), dst_type.c_str(ctx), src_type.c_str(ctx));
            log_info("  res:%d, src_valid:%d, dst_valid:%d, src local:%d, dst local:%d, dst gate:%d\n", res, src_valid,
                     dst_valid, is_local(src_type), is_local(dst_type), gwu.wire_is_clock_gate(dst));
        }
        return res;
    }

    bool global_DQCE_pip_filter(PipId pip, WireId src_wire) const
    {
        auto is_local = [&](IdString wire_type) {
            return !wire_type.in(id_GLOBAL_CLK, id_IO_O, id_IO_I, id_PLL_O, id_PLL_I, id_TILE_CLK);
        };
        auto is_dcs_input = [&](IdString wire_name) {
            return wire_name.in(id_P16A, id_P16B, id_P16C, id_P16D, id_P17A, id_P17B, id_P17C, id_P17D, id_P26A,
                                id_P26B, id_P26C, id_P26D, id_P27A, id_P27B, id_P27C, id_P27D, id_P36A, id_P36B,
                                id_P36C, id_P36D, id_P37A, id_P37B, id_P37C, id_P37D, id_P46A, id_P46B, id_P46C,
                                id_P46D, id_P47A, id_P47B, id_P47C, id_P47D);
        };

        WireId src, dst;
        src = ctx->getPipSrcWire(pip);
        dst = ctx->getPipDstWire(pip);
        IdString src_name = ctx->getWireName(dst)[1];
        IdString dst_name = ctx->getWireName(dst)[1];
        bool not_dcs_pip = dst_name != id_CLKOUT && !is_dcs_input(src_name);
        IdString src_type = ctx->getWireType(src);
        IdString dst_type = ctx->getWireType(dst);
        bool src_valid = not_dcs_pip && src_type.in(id_GLOBAL_CLK, id_IO_O, id_PLL_O, id_HCLK);
        bool dst_valid = not_dcs_pip && dst_type.in(id_GLOBAL_CLK, id_TILE_CLK, id_PLL_I, id_IO_I, id_HCLK);

        // If DQCE is used, then the source can only connect to SPINEs as only they can be switched off/on.
        bool res;
        if (src == src_wire) {
            bool dst_is_spine = (dst_name.str(ctx).rfind("SPINE", 0) == 0 || dst_name.str(ctx).rfind("PCLK", 0) == 0 ||
                                 dst_name.str(ctx).rfind("LWSPINE", 0) == 0);
            res = src_valid && dst_is_spine;
        } else {
            res = (src_valid && dst_valid) || (src_valid && is_local(dst_type)) || (is_local(src_type) && dst_valid);
        }
        if (ctx->debug && false /*res*/) {
            log_info("%s <- %s [%s <- %s]\n", ctx->nameOfWire(ctx->getPipDstWire(pip)),
                     ctx->nameOfWire(ctx->getPipSrcWire(pip)), dst_type.c_str(ctx), src_type.c_str(ctx));
            log_info("  res:%d, src_valid:%d, dst_valid:%d, src local:%d, dst local:%d\n", res, src_valid, dst_valid,
                     is_local(src_type), is_local(dst_type));
        }
        return res;
    }

    bool global_DCS_pip_filter(PipId pip, WireId src_wire) const
    {
        auto is_local = [&](IdString wire_type) {
            return !wire_type.in(id_GLOBAL_CLK, id_IO_O, id_IO_I, id_PLL_O, id_PLL_I, id_TILE_CLK);
        };

        WireId src = ctx->getPipSrcWire(pip);
        IdString src_type = ctx->getWireType(src);
        IdString src_name = ctx->getWireName(src)[1];
        bool src_is_spine = src_name.str(ctx).rfind("SPINE", 0) == 0;
        IdString dst_type = ctx->getWireType(ctx->getPipDstWire(pip));
        bool src_valid = ((!src_is_spine) && src_type.in(id_GLOBAL_CLK, id_IO_O, id_PLL_O, id_HCLK)) ||
                         src_name.in(id_SPINE6, id_SPINE7, id_SPINE14, id_SPINE15, id_SPINE22, id_SPINE23, id_SPINE30,
                                     id_SPINE31);
        bool dst_valid = dst_type.in(id_GLOBAL_CLK, id_TILE_CLK, id_PLL_I, id_PLL_O, id_IO_I, id_HCLK);

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
    bool backwards_bfs_route(NetInfo *net, WireId src, WireId dst, int iter_limit, bool strict, Tfilt pip_filter,
                             std::vector<PipId> *path = nullptr)
    {
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
                if (!pip_filter(pip, src)) {
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
            }
            // Reverse that list
            std::reverse(pips.begin(), pips.end());
            // Bind pips until we hit already-bound routing
            for (PipId pip : pips) {
                WireId dst = ctx->getPipDstWire(pip);
                if (ctx->getBoundWireNet(dst) == net) {
                    break;
                }
                ctx->bindPip(pip, net, STRENGTH_LOCKED);
                if (path != nullptr) {
                    path->push_back(pip);
                }
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

    template <typename Tfilter>
    RouteResult route_direct_net(NetInfo *net, Tfilter pip_filter, WireId aux_src = WireId(),
                                 std::vector<PipId> *path = nullptr)
    {
        WireId src;
        src = aux_src == WireId() ? ctx->getNetinfoSourceWire(net) : aux_src;
        if (src == WireId()) {
            log_error("Net '%s' has an invalid source port %s.%s\n", ctx->nameOf(net), ctx->nameOf(net->driver.cell),
                      ctx->nameOf(net->driver.port));
        }

        if (aux_src == WireId() && ctx->getBoundWireNet(src) != net) {
            ctx->bindWire(src, net, STRENGTH_LOCKED);
        }

        RouteResult routed = NOT_ROUTED;
        for (auto usr : net->users) {
            WireId dst = ctx->getNetinfoSinkWire(net, usr, 0);
            if (dst == WireId()) {
                log_error("Net '%s' has an invalid sink port %s.%s\n", ctx->nameOf(net), ctx->nameOf(usr.cell),
                          ctx->nameOf(usr.port));
            }
            bool bfs_res;
            bfs_res = backwards_bfs_route(
                    net, src, dst, 1000000, false,
                    [&](PipId pip, WireId src_wire) { return (is_relaxed_sink(usr) || pip_filter(pip, src)); }, path);
            if (bfs_res) {
                routed = routed == ROUTED_PARTIALLY ? routed : ROUTED_ALL;
            } else {
                routed = routed == NOT_ROUTED ? routed : ROUTED_PARTIALLY;
            }
        }
        if (routed == NOT_ROUTED) {
            if (aux_src == WireId()) {
                ctx->unbindWire(src);
            }
        }
        return routed;
    }

    void route_dqce_net(NetInfo *net)
    {
        // route net after dqce using source of CLKIN net
        CellInfo *dqce_ci = net->driver.cell;

        NetInfo *net_before_dqce = dqce_ci->getPort(id_CLKIN);
        NPNR_ASSERT(net_before_dqce != nullptr);

        PortRef driver = net_before_dqce->driver;
        NPNR_ASSERT_MSG(gwu.driver_is_buf(driver) || gwu.driver_is_clksrc(driver),
                        stringf("The input source for %s is not a clock.", ctx->nameOf(dqce_ci)).c_str());
        WireId src;
        // use BUF input if there is one
        if (gwu.driver_is_buf(driver)) {
            src = ctx->getBelPinWire(driver.cell->bel, id_I);
        } else {
            src = ctx->getBelPinWire(driver.cell->bel, driver.port);
        }

        RouteResult route_result = route_direct_net(
                net,
                [&](PipId pip, WireId src_wire) {
                    return global_DQCE_pip_filter(pip, src) && segment_wire_filter(pip) && dcs_input_filter(pip);
                },
                src);
        if (route_result == NOT_ROUTED) {
            log_error("Can't route the %s network.\n", ctx->nameOf(net));
        }
        if (route_result == ROUTED_PARTIALLY) {
            log_error("It was not possible to completely route the %s net using only global resources. This is not "
                      "allowed for DQCE managed networks.\n",
                      ctx->nameOf(net));
        }

        // In networks controlled by DQCE, the source can only connect to the
        // "spine" wires. Here we not only check this fact, but also find out
        // how many and what kind of "spine" wires were used for network
        // roaming.
        for (PipId pip : ctx->getPipsDownhill(src)) {
            if (ctx->getBoundPipNet(pip) == nullptr) {
                continue;
            }
            WireId dst = ctx->getPipDstWire(pip);
            IdString dst_name = ctx->getWireName(dst)[1];
            if (dst_name.str(ctx).rfind("PCLK", 0) == 0 || dst_name.str(ctx).rfind("LWSPINE", 0) == 0) {
                // step over dummy pip
                for (PipId next_pip : ctx->getPipsDownhill(dst)) {
                    if (ctx->getBoundPipNet(next_pip) != nullptr) {
                        ctx->unbindPip(pip);
                        src = dst;
                        break;
                    }
                }
                if (src == dst) {
                    break;
                }
            }
        }
        for (PipId pip : ctx->getPipsDownhill(src)) {
            if (ctx->getBoundPipNet(pip) == nullptr) {
                continue;
            }
            WireId dst = ctx->getPipDstWire(pip);
            BelId dqce_bel = gwu.get_dqce_bel(ctx->getWireName(dst)[1]);
            NPNR_ASSERT(dqce_bel != BelId());

            // One pseudo DQCE (either logical or custom, whatever you like)
            // can be implemented as several hardware dqce - this is because
            // each hardware dqce can control only one "spine", that is, a bus
            // within one quadrant. Here we find suitable hardware dqces.
            CellInfo *hw_dqce = ctx->getBoundBelCell(dqce_bel);
            if (ctx->debug) {
                log_info("  use %s spine and %s bel for '%s' hw cell.\n", ctx->nameOfWire(dst),
                         ctx->nameOfBel(dqce_bel), ctx->nameOf(hw_dqce));
            }

            hw_dqce->setAttr(id_DQCE_PIP, Property(ctx->getPipName(pip).str(ctx)));
            ctx->unbindPip(pip);
            ctx->bindWire(dst, net, STRENGTH_LOCKED);

            // The control network must connect the CE inputs of all hardware dqces.
            dqce_ci->copyPortTo(id_CE, hw_dqce, id_CE);
        }
        net->driver.cell->disconnectPort(net->driver.port);

        // remove the virtual DQCE
        dqce_ci->disconnectPort(id_CLKIN);
        dqce_ci->disconnectPort(id_CE);
        ctx->cells.erase(dqce_ci->name);
    }

    void route_dcs_net(NetInfo *net)
    {
        IdString dcs_clock_input_prefix = gwu.get_dcs_prefix();
        const char *dcs_clock_input_prefix_str = dcs_clock_input_prefix.c_str(ctx);
        // Since CLKOUT is responsible for only one quadrant, we will do
        // routing not from it, but from any CLK0-3 input actually connected to
        // the clock source.
        CellInfo *dcs_ci = net->driver.cell;
        NetInfo *net_before_dcs;
        PortRef driver;
        for (int i = 0; i < 4; ++i) {
            net_before_dcs = dcs_ci->getPort(ctx->idf("%s%d", dcs_clock_input_prefix_str, i));
            if (net_before_dcs == nullptr) {
                continue;
            }
            driver = net_before_dcs->driver;
            if (gwu.driver_is_buf(driver) || gwu.driver_is_clksrc(driver)) {
                break;
            }
            net_before_dcs = nullptr;
        }
        NPNR_ASSERT_MSG(net_before_dcs != nullptr, stringf("No clock inputs for %s.", ctx->nameOf(dcs_ci)).c_str());

        WireId src;
        // use BUF input if there is one
        if (gwu.driver_is_buf(driver)) {
            src = ctx->getBelPinWire(driver.cell->bel, id_I);
        } else {
            src = ctx->getBelPinWire(driver.cell->bel, driver.port);
        }

        RouteResult route_result = route_direct_net(
                net,
                [&](PipId pip, WireId src_wire) { return global_DCS_pip_filter(pip, src) && segment_wire_filter(pip); },
                src);
        if (route_result == NOT_ROUTED) {
            log_error("Can't route the %s network.\n", ctx->nameOf(net));
        }
        if (route_result == ROUTED_PARTIALLY) {
            log_error("It was not possible to completely route the %s net using only global resources. This is not "
                      "allowed for DCS managed networks.\n",
                      ctx->nameOf(net));
        }

        // In networks controlled by DCS, the source can only connect to the
        // "spine" wires. Here we not only check this fact, but also find out
        // how many and what kind of "spine" wires were used for network
        // roaming.
        for (PipId pip : ctx->getPipsDownhill(src)) {
            if (ctx->getBoundPipNet(pip) == nullptr) {
                continue;
            }
            WireId dst = ctx->getPipDstWire(pip);
            IdString dst_name = ctx->getWireName(dst)[1];
            if (dst_name.str(ctx).rfind("PCLK", 0) == 0 || dst_name.str(ctx).rfind("LWSPINE", 0) == 0 ||
                dst_name.str(ctx).rfind("PLL") == 0) {
                // step over dummy pip
                for (PipId next_pip : ctx->getPipsDownhill(dst)) {
                    if (ctx->getBoundPipNet(next_pip) != nullptr) {
                        src = dst;
                        break;
                    }
                }
                if (src == dst) {
                    break;
                }
            }
        }
        for (PipId pip : ctx->getPipsDownhill(src)) {
            if (ctx->getBoundPipNet(pip) == nullptr) {
                continue;
            }
            WireId dst = ctx->getPipDstWire(pip);
            BelId dcs_bel = gwu.get_dcs_bel(ctx->getWireName(dst)[1]);
            NPNR_ASSERT(dcs_bel != BelId());

            // One pseudo DCS (either logical or custom, whatever you like)
            // can be implemented as several hardware dcs - this is because
            // each hardware dcs can control only one "spine", that is, a bus
            // within one quadrant. Here we find suitable hardware dcses.
            CellInfo *hw_dcs = ctx->getBoundBelCell(dcs_bel);
            if (ctx->debug) {
                log_info("  use %s spine and %s bel for '%s' hw cell.\n", ctx->nameOfWire(dst), ctx->nameOfBel(dcs_bel),
                         ctx->nameOf(hw_dcs));
            }
            if (dcs_ci->attrs.count(id_DCS_MODE) != 0) {
                hw_dcs->setAttr(id_DCS_MODE, dcs_ci->attrs.at(id_DCS_MODE));
            } else {
                hw_dcs->setAttr(id_DCS_MODE, Property("RISING"));
            }

            // Need to release the fake internal DCS PIP which is the only
            // downhill pip for DCS inputs
            PipId fake_pip = *ctx->getPipsDownhill(dst).begin();
            WireId clkout_wire = ctx->getPipDstWire(fake_pip);
            if (ctx->debug) {
                log_info("fake pip:%s, CLKOUT src:%s\n", ctx->nameOfPip(fake_pip), ctx->nameOfWire(clkout_wire));
            }
            ctx->unbindPip(fake_pip);
            ctx->bindWire(clkout_wire, net, STRENGTH_LOCKED);
            ctx->unbindWire(dst);

            // The input networks must bs same for all hardware dcs.
            dcs_ci->copyPortTo(id_SELFORCE, hw_dcs, id_SELFORCE);
            dcs_ci->copyPortBusTo(dcs_clock_input_prefix, 0, false, hw_dcs, dcs_clock_input_prefix, 0, false, 4);
            dcs_ci->copyPortBusTo(id_CLKSEL, 0, true, hw_dcs, id_CLKSEL, 0, false, 4);
        }

        // remove the virtual DCS
        dcs_ci->disconnectPort(id_SELFORCE);
        dcs_ci->disconnectPort(id_CLKOUT);
        for (int i = 0; i < 4; ++i) {
            dcs_ci->disconnectPort(ctx->idf("CLKSEL[%d]", i));
            dcs_ci->disconnectPort(ctx->idf("%s%d", dcs_clock_input_prefix_str, i));
        }
        log_info("    '%s' net was routed.\n", ctx->nameOf(net));
        ctx->cells.erase(dcs_ci->name);
    }

    void route_dhcen_net(NetInfo *net)
    {
        // route net after dhcen source of CLKIN net
        CellInfo *dhcen_ci = net->driver.cell;

        NetInfo *net_before_dhcen = dhcen_ci->getPort(id_CLKIN);
        NPNR_ASSERT(net_before_dhcen != nullptr);

        PortRef driver = net_before_dhcen->driver;
        NPNR_ASSERT_MSG(gwu.driver_is_buf(driver) || gwu.driver_is_clksrc(driver) || gwu.driver_is_mipi(driver),
                        stringf("The input source (%s:%s) for %s is not a clock.", ctx->nameOf(driver.cell),
                                driver.port.c_str(ctx), ctx->nameOf(dhcen_ci))
                                .c_str());

        IdString port;
        // use BUF input if there is one
        if (gwu.driver_is_buf(driver)) {
            port = id_I;
        } else {
            port = driver.port;
        }
        WireId src = ctx->getBelPinWire(driver.cell->bel, port);

        std::vector<PipId> path;
        RouteResult route_result;
        if (gwu.driver_is_mipi(driver)) {
            route_result = route_direct_net(
                    net, [&](PipId pip, WireId src_wire) { return segment_wire_filter(pip) && dcs_input_filter(pip); },
                    src, &path);
        } else {
            route_result = route_direct_net(
                    net,
                    [&](PipId pip, WireId src_wire) {
                        return global_pip_filter(pip, src) && segment_wire_filter(pip) && dcs_input_filter(pip);
                    },
                    src, &path);
        }

        if (route_result == NOT_ROUTED) {
            log_error("Can't route the %s network.\n", ctx->nameOf(net));
        }
        if (route_result == ROUTED_PARTIALLY) {
            log_error("It was not possible to completely route the %s net using only global resources. This is not "
                      "allowed for dhcen managed networks.\n",
                      ctx->nameOf(net));
        }

        // In networks controlled by dhcen we disable/enable only HCLK - if
        // there are ordinary cells among the sinks, then they are not affected
        // by this primitive.
        for (PipId pip : path) {
            // move to upper level net
            ctx->unbindPip(pip);
            ctx->bindPip(pip, net_before_dhcen, STRENGTH_LOCKED);

            WireId dst = ctx->getPipDstWire(pip);
            IdString side;
            BelId dhcen_bel = gwu.get_dhcen_bel(dst, side);
            if (dhcen_bel == BelId()) {
                continue;
            }

            // One pseudo dhcen can be implemented as several hardware dhcen.
            // Here we find suitable hardware dhcens.
            CellInfo *hw_dhcen = ctx->getBoundBelCell(dhcen_bel);
            if (ctx->debug) {
                log_info("  use %s wire and %s bel for '%s' hw cell.\n", ctx->nameOfWire(dst),
                         ctx->nameOfBel(dhcen_bel), ctx->nameOf(hw_dhcen));
            }

            // The control network must connect the CE inputs of all hardware dhcens.
            hw_dhcen->setAttr(id_DHCEN_USED, 1);
            dhcen_ci->copyPortTo(id_CE, hw_dhcen, id_CE);
        }
        if (gwu.driver_is_mipi(driver)) {
            ctx->bindWire(src, net_before_dhcen, STRENGTH_LOCKED);
        }

        // connect all users to upper level net
        std::vector<PortRef> users;
        for (auto &cell_port : net->users) {
            users.push_back(cell_port);
        }
        for (PortRef &user : users) {
            user.cell->disconnectPort(user.port);
            user.cell->connectPort(user.port, net_before_dhcen);
        }

        // remove the virtual dhcen
        dhcen_ci->disconnectPort(id_CLKOUT);
        dhcen_ci->disconnectPort(id_CLKIN);
        dhcen_ci->disconnectPort(id_CE);
        ctx->cells.erase(dhcen_ci->name);
    }

    void route_buffered_net(NetInfo *net)
    {
        // a) route net after buf using the buf input as source
        CellInfo *buf_ci = net->driver.cell;
        WireId src = ctx->getBelPinWire(buf_ci->bel, id_I);

        NetInfo *net_before_buf = buf_ci->getPort(id_I);
        NPNR_ASSERT(net_before_buf != nullptr);

        RouteResult route_result = route_direct_net(
                net,
                [&](PipId pip, WireId src_wire) {
                    return global_pip_filter(pip, src_wire) && segment_wire_filter(pip) && dcs_input_filter(pip);
                },
                src);
        if (route_result == NOT_ROUTED) {
            log_error("Can't route the %s net. It might be worth removing the BUFG buffer flag.\n", ctx->nameOf(net));
        }

        // b) route net before buf from whatever to the buf input
        WireId dst = src;
        CellInfo *true_src_ci = net_before_buf->driver.cell;
        src = ctx->getBelPinWire(true_src_ci->bel, net_before_buf->driver.port);
        ctx->bindWire(src, net, STRENGTH_LOCKED);
        backwards_bfs_route(net, src, dst, 1000000, false, [&](PipId pip, WireId src_wire) {
            return clock_gate_wire_filter(pip) && segment_wire_filter(pip) && dcs_input_filter(pip);
        });
        // remove net
        buf_ci->movePortTo(id_O, true_src_ci, net_before_buf->driver.port);
        net_before_buf->driver.cell = nullptr;

        log_info("    '%s' net was routed.\n", ctx->nameOf(net));
    }

    RouteResult route_clk_net(NetInfo *net)
    {
        RouteResult route_result = route_direct_net(net, [&](PipId pip, WireId src_wire) {
            return clock_gate_wire_filter(pip) && global_pip_filter(pip, src_wire) && segment_wire_filter(pip) &&
                   dcs_input_filter(pip);
        });
        if (route_result != NOT_ROUTED) {
            log_info("    '%s' net was routed using global resources %s.\n", ctx->nameOf(net),
                     route_result == ROUTED_ALL ? "only" : "partially");
        }
        return route_result;
    }

    // segmented wires
    enum SegmentRouteResult
    {
        SEG_NOT_ROUTED = 0,
        SEG_ROUTED_TO_ANOTHER_SEGMENT,
        SEG_ROUTED
    };
    // Step 0: route LBx1 -> sinks
    SegmentRouteResult route_segmented_step0(NetInfo *ni, Loc dst_loc, WireId dst_wire, int s_idx, int s_x,
                                             std::vector<PipId> &bound_pips)
    {
        bool routed = false;

        WireId lbo_wire = ctx->getWireByName(
                IdStringList::concat(ctx->idf("X%dY%d", s_x, dst_loc.y), ctx->idf("LBO%d", s_idx / 4)));
        if (ctx->debug) {
            log_info("      step 0: %s -> %s\n", ctx->nameOfWire(lbo_wire), ctx->nameOfWire(dst_wire));
        }
        // The DFF can currently only connect to a neighbouring LUT. Skip such networks.
        if (ctx->getWireName(dst_wire)[1].in(id_XD0, id_XD1, id_XD2, id_XD3, id_XD4, id_XD5)) {
            auto pips = ctx->getPipsUphill(dst_wire);
            auto pip_it = pips.begin();
            ++pip_it;
            NPNR_ASSERT_MSG(!(pip_it != pips.end()), "DFFs have been given the ability to connect independently of the "
                                                     "neighbouring LUT. Segment routing must be corrected.\n");
            // Connect LUT OUT to DFF IN
            PipId pip = *pips.begin();
            ctx->bindPip(pip, ni, STRENGTH_LOCKED);
            bound_pips.push_back(pip);
            return SEG_ROUTED_TO_ANOTHER_SEGMENT;
        }
        routed = backwards_bfs_route(
                ni, lbo_wire, dst_wire, 1000000, false, [&](PipId pip, WireId src) { return true; }, &bound_pips);
        return routed ? SEG_ROUTED : SEG_ROUTED_TO_ANOTHER_SEGMENT;
    }

    // Step 1: segment wire -> LBOx
    SegmentRouteResult route_segmented_step1(NetInfo *ni, Loc dst_loc, int s_idx, int s_x,
                                             std::vector<PipId> &bound_pips)
    {
        IdString tile = ctx->idf("X%dY%d", s_x, dst_loc.y);
        IdString lbo_wire_name = ctx->idf("LBO%d", s_idx > 3 ? 1 : 0);
        IdStringList pip_dst_name = IdStringList::concat(tile, lbo_wire_name);

        // if we used other wire
        IdStringList last_pip_src_name = ctx->getWireName(ctx->getPipSrcWire(bound_pips.back()));
        if (last_pip_src_name != pip_dst_name) {
            if (ctx->debug) {
                log_info("      step 1: Already joined the network in another segment at %s. Skip.\n",
                         last_pip_src_name.str(ctx).c_str());
            }
            return SEG_ROUTED_TO_ANOTHER_SEGMENT;
        }

        IdString lt_wire_name = ctx->idf("LT0%d", s_idx > 3 ? 4 : 1);
        PipId pip = ctx->getPipByName(IdStringList::concat(pip_dst_name, lt_wire_name));

        if (ctx->debug) {
            log_info("      step 1: %s -> %s\n", lt_wire_name.c_str(ctx), pip_dst_name.str(ctx).c_str());
        }
        NPNR_ASSERT(pip != PipId());

        NetInfo *pip_net = ctx->getBoundPipNet(pip);
        if (pip_net == nullptr) {
            ctx->bindPip(pip, ni, STRENGTH_LOCKED);
            bound_pips.push_back(pip);
        } else {
            if (pip_net != ni) {
                return SEG_NOT_ROUTED;
            }
        }
        return SEG_ROUTED;
    }

    // Step 2: gate wire -> segment wire
    SegmentRouteResult route_segmented_step2(NetInfo *ni, WireId segment_wire, WireId gate_wire,
                                             std::vector<PipId> &bound_pips)
    {
        PipId pip;
        for (PipId down_pip : ctx->getPipsDownhill(gate_wire)) {
            WireId dst = ctx->getPipDstWire(down_pip);
            if (dst == segment_wire) {
                pip = down_pip;
                break;
            }
        }

        NPNR_ASSERT(pip != PipId());
        if (ctx->debug) {
            log_info("      step 2: %s\n", ctx->nameOfPip(pip));
        }
        NetInfo *pip_net = ctx->getBoundPipNet(pip);
        if (pip_net == nullptr) {
            ctx->bindPip(pip, ni, STRENGTH_LOCKED);
            bound_pips.push_back(pip);
        } else {
            if (pip_net != ni) {
                return SEG_NOT_ROUTED;
            }
        }
        return SEG_ROUTED;
    }

    // Step 3: route src -> gate wires
    SegmentRouteResult route_segmented_step3(NetInfo *ni, pool<WireId> gate_wires, std::vector<PipId> &bound_pips,
                                             pool<WireId> &bound_wires)
    {
        bool routed = false;
        WireId src_wire = ctx->getNetinfoSourceWire(ni);
        if (ctx->debug) {
            log_info("    step 3: %s -> \n", ctx->nameOfWire(src_wire));
        }
        // Create a temporary small network where segment gates will be the sinks
        IdString gate_net_name = ctx->idf("%s$gate_net$", ni->name.c_str(ctx));
        NetInfo *gate_ni = ctx->createNet(gate_net_name);
        std::vector<PipId> gate_bound_pips;
        pool<WireId> gate_bound_wires;

        for (WireId gatewire : gate_wires) {
            if (ctx->debug) {
                log_info("      %s\n", ctx->nameOfWire(gatewire));
            }
            routed = backwards_bfs_route(
                    gate_ni, src_wire, gatewire, 1000000, false,
                    [&](PipId pip, WireId src) { return dcs_input_filter(pip) && !gwu.is_global_pip(pip); },
                    &gate_bound_pips);
            if (routed) {
                // bind src
                if (ctx->checkWireAvail(src_wire)) {
                    ctx->bindWire(src_wire, gate_ni, STRENGTH_LOCKED);
                    gate_bound_wires.insert(src_wire);
                }
            } else {
                break;
            }
        }

        // merge with original net
        if (routed) {
            for (PipId pip : gate_bound_pips) {
                ctx->unbindPip(pip);
                ctx->bindPip(pip, ni, STRENGTH_LOCKED);
                bound_pips.push_back(pip);
            }
            for (WireId wire : gate_bound_wires) {
                ctx->unbindWire(wire);
                ctx->bindWire(wire, ni, STRENGTH_LOCKED);
                bound_wires.insert(wire);
            }
        }

        return routed ? SEG_ROUTED : SEG_NOT_ROUTED;
    }

    void route_segmented(std::vector<IdString> &nets)
    {
        if (ctx->verbose) {
            log_info("routing segmented...\n");
        }

        struct selected_net
        {
            int sink_cnt;
            std::vector<int> segs;             // segments
            dict<int, WireId> gate_wires;      // from logic to segment
            dict<int, WireId> tb_wires;        // top or bottom segment wire
            dict<int, WireId> wire_to_isolate; // this wire should be disconnected to avoid conflict
        };

        dict<IdString, selected_net> selected_nets;
        NetInfo *vcc_net = ctx->nets.at(ctx->id("$PACKER_VCC")).get();
        NetInfo *vss_net = ctx->nets.at(ctx->id("$PACKER_GND")).get();

        auto get_port_loc = [&](PortRef &cell_wire) -> Loc {
            BelId bel = cell_wire.cell->bel;
            NPNR_ASSERT(bel != BelId());
            return ctx->getBelLocation(bel);
        };

        for (IdString net_name : nets) {
            NetInfo *ni = ctx->nets.at(net_name).get();

            // We restrict the considered networks from above because networks
            // with a large number of sinks have all chances to cross quadrant
            // boundaries and for such large global networks it is better to
            // use free clock wires.
            int sinks_num = ni->users.entries();
            if (ni->driver.cell == nullptr || sinks_num < 8 || sinks_num > 50 || ni == vcc_net || ni == vss_net) {
                continue;
            }

            // We cut off very compact networks because regular wires will
            // suffice for them, and using segmented ones takes up a whole
            // column in the bank at once.
            Loc src_loc = get_port_loc(ni->driver);
            if (ctx->debug) {
                log_info("    net:%s, src:(%d, %d) %s\n", ctx->nameOf(ni), src_loc.y, src_loc.x,
                         ni->driver.port.c_str(ctx));
            }
            int far_sink_cnt = 0;
            for (auto sink : ni->users) {
                Loc sink_loc = get_port_loc(sink);
                if (ctx->debug) {
                    log_info("      sink:(%d, %d) %s\n", sink_loc.y, sink_loc.x, sink.port.c_str(ctx));
                }
                if (std::abs(sink_loc.x - src_loc.x) > 4 || std::abs(sink_loc.y - src_loc.y) > 4) {
                    ++far_sink_cnt;
                }
            }
            if (far_sink_cnt > 10) {
                if (ctx->debug) {
                    log_info("    far sinks:%d, net is selected for processing.\n", far_sink_cnt);
                }
                selected_nets[net_name].sink_cnt = far_sink_cnt;
            }
        }
        // Now that we have selected candidate grids, let's put them into a
        // structure convenient for working with each grid cell of the chip
        // individually.
        // Each segment "serves" a rectangular area, the width and height of
        // which depends on the position of the tap from the horizontal
        // "spine" wire.
        // The areas of neighboring taps overlap, but not completely, so we'll
        // have to handle the sinks of the nets cell by cell.
        // Another reason why we have to work with each cell individually,
        // instead of using the total number of sinks of a particular network
        // in the whole rectangular area, is that it makes sense to connect the
        // sinks that are in the immediate neighborhood of the network source
        // with ordinary wires.
        struct grid_net
        {
            IdString net;
            int sink_cnt; // It is not currently used, but it may be useful if
                          // the network search algorithm is based on the
                          // number of sinks in the segment's service region.
        };
        // The largest Gowin chip to date (GW5A-138) contains 138000 LUTs,
        // which is a rough estimate without taking into account the placement
        // of a few LUTs in the cell gives 400 columns and 400 rows.  We use
        // the combination of row number << 16 and column number as a key.
        auto xy_to_key = [&](int x, int y) -> uint32_t { return (y << 16) | x; };
        std::unordered_multimap<uint32_t, grid_net> grid;
        int min_x = ctx->getGridDimX();
        int max_x = -1;
        int min_y = ctx->getGridDimY();
        int max_y = -1;

        for (auto &net : selected_nets) {
            IdString net_name = net.first;
            NetInfo *ni = ctx->nets.at(net_name).get();
            Loc src_loc = get_port_loc(ni->driver);
            for (auto sink : ni->users) {
                Loc sink_loc = get_port_loc(sink);
                min_x = std::min(min_x, sink_loc.x);
                max_x = std::max(max_x, sink_loc.x);
                min_y = std::min(min_y, sink_loc.y);
                max_y = std::max(max_y, sink_loc.y);

                if (std::abs(sink_loc.x - src_loc.x) > 4 || std::abs(sink_loc.y - src_loc.y) > 4) {
                    uint32_t key = xy_to_key(sink_loc.x, sink_loc.y);
                    bool found = false;
                    if (grid.count(key)) {
                        auto net_range = grid.equal_range(key);
                        for (auto it = net_range.first; it != net_range.second; ++it) {
                            if (it->second.net == net_name) {
                                found = true;
                                ++(it->second.sink_cnt);
                            }
                        }
                    }
                    if (!found) {
                        grid_net new_cell;
                        new_cell.net = net_name;
                        new_cell.sink_cnt = 1;
                        grid.insert(std::make_pair(key, new_cell));
                    }
                }
            }
        }
        if (ctx->debug) {
            log_info("Net grid. (%d, %d) <=> (%d, %d)\n", min_y, min_x, max_y, max_x);
            for (auto it = grid.begin(); it != grid.end(); ++it) {
                log_info(" (%d, %d): %s %d\n", it->first >> 16, it->first & 0xffff, it->second.net.c_str(ctx),
                         it->second.sink_cnt);
            }
        }

        // Net -> s_idx (0 <= s_idx < 8 -indices of vertical segments)
        dict<IdString, int> net_to_s_idx;

        // We search all segmental columns, ignoring those that do not fall
        // into the grid of networks
        for (int s_i = 0; s_i < gwu.get_segments_count(); ++s_i) {
            int s_x, s_idx, s_min_x, s_min_y, s_max_x, s_max_y;
            gwu.get_segment_region(s_i, s_idx, s_x, s_min_x, s_min_y, s_max_x, s_max_y);
            // skip empty (in sense of net sinks) segments
            if (s_max_x < min_x || s_min_x > max_x || s_max_y < min_y || s_min_y > max_y) {
                continue;
            }
            if (ctx->debug) {
                log_info("segment:%d/%d, x:%d, (%d, %d) <=> (%d, %d)\n", s_i, s_idx, s_x, s_min_y, s_min_x, s_max_y,
                         s_max_x);
            }
            // Selecting networks whose sinks fall in the served region.
            // Networks with an already assigned segment index are prioritized
            // over the rest, among which the network with the maximum number
            // of sinks is selected.
            bool found_net_with_index = false;
            IdString net;
            int sink_cnt = 0;
            for (int y = s_min_y; y <= s_max_y && (!found_net_with_index); ++y) {
                for (int x = s_min_x; x <= s_max_x && (!found_net_with_index); ++x) {
                    auto net_range = grid.equal_range(xy_to_key(x, y));
                    for (auto it = net_range.first; it != net_range.second; ++it) {
                        if (net_to_s_idx.count(it->second.net)) {
                            if (net_to_s_idx.at(it->second.net) == s_idx) {
                                // far network already use our segment index - reuse it
                                found_net_with_index = true;
                                net = it->second.net;
                                sink_cnt = selected_nets.at(it->second.net).sink_cnt;
                                break;
                            }
                            continue;
                        }
                        // new net, calculate maximum sinks
                        if (selected_nets.at(it->second.net).sink_cnt > sink_cnt) {
                            sink_cnt = selected_nets.at(it->second.net).sink_cnt;
                            net = it->second.net;
                        }
                    }
                }
            }
            // no suitable nets, segment is unused, skip
            if (sink_cnt == 0) {
                continue;
            }

            if (!found_net_with_index) {
                // new net
                if (ctx->debug) {
                    log_info("  new net: %s, index:%d\n", net.c_str(ctx), s_idx);
                }
                net_to_s_idx[net] = s_idx;
            } else {
                // old net
                if (ctx->debug) {
                    log_info("  old net: %s, index:%d\n", net.c_str(ctx), s_idx);
                }
            }
            selected_nets.at(net).segs.push_back(s_i);
        }
        // Sort in descending order of the number of segments used.
        std::multimap<int, IdString> sorted_nets;
        for (auto const &net_seg : net_to_s_idx) {
            sorted_nets.insert(std::make_pair(-selected_nets.at(net_seg.first).segs.size(), net_seg.first));
        }

        // Now that we have all the segments for the networks we need to
        // decide which end of the segment (upper or lower) to use
        // depending on the distance to the network source.
        // This is critical because the signal in a segment can propagate
        // from bottom to top or top to bottom and you need to know exactly
        // which end to isolate.
        for (auto const &net_seg : sorted_nets) {
            IdString net = net_seg.second;
            NetInfo *ni = ctx->nets.at(net).get();
            Loc src_loc = get_port_loc(ni->driver);
            if (ctx->debug) {
                log_info("net:%s, src:(%d, %d)\n", ctx->nameOf(ni), src_loc.y, src_loc.x);
            }
            std::string wires_to_isolate;
            for (int s_i : selected_nets.at(net).segs) {
                // distances to net source
                Loc top_loc, bottom_loc;
                gwu.get_segment_wires_loc(s_i, top_loc, bottom_loc);
                int top_to_src = std::abs(src_loc.x - top_loc.x) + std::abs(src_loc.y - top_loc.y);
                int bottom_to_src = std::abs(src_loc.x - bottom_loc.x) + std::abs(src_loc.y - bottom_loc.y);
                if (ctx->debug) {
                    log_info("  segment:%d, top:(%d, %d), bottom:(%d, %d) dists:%d %d\n", s_i, top_loc.y, top_loc.x,
                             bottom_loc.y, bottom_loc.x, top_to_src, bottom_to_src);
                }
                // By selecting the top or bottom end we also select a pair of
                // gate wires to use.
                WireId tb_wire, gate_wire, top_seg_wire, bottom_seg_wire, wire_to_isolate;
                gwu.get_segment_wires(s_i, top_seg_wire, bottom_seg_wire);
                tb_wire = top_seg_wire;
                if (top_to_src <= bottom_to_src) {
                    WireId gate_wire1;
                    gwu.get_segment_top_gate_wires(s_i, gate_wire, gate_wire1);
                    if (gate_wire == WireId()) {
                        gate_wire = gate_wire1;
                    }
                    if (gate_wire == WireId()) {
                        // This segment has no top gate wires, so we use one of the bottom ones.
                        gwu.get_segment_bottom_gate_wires(s_i, gate_wire, gate_wire1);
                        if (gate_wire == WireId()) {
                            gate_wire = gate_wire1;
                        }
                        tb_wire = bottom_seg_wire;
                        wire_to_isolate = top_seg_wire;
                        NPNR_ASSERT(gate_wire != WireId()); // Completely isolated segment. The chip base is damaged.
                    }
                } else {
                    WireId gate_wire1;
                    tb_wire = bottom_seg_wire;
                    wire_to_isolate = top_seg_wire;
                    gwu.get_segment_bottom_gate_wires(s_i, gate_wire, gate_wire1);
                    if (gate_wire == WireId()) {
                        gate_wire = gate_wire1;
                    }
                    if (gate_wire == WireId()) {
                        // This segment has no top gate wires, so we use one of the bottom ones.
                        gwu.get_segment_top_gate_wires(s_i, gate_wire, gate_wire1);
                        if (gate_wire == WireId()) {
                            gate_wire = gate_wire1;
                        }
                        tb_wire = top_seg_wire;
                        wire_to_isolate = WireId();
                        NPNR_ASSERT(gate_wire != WireId()); // Completely isolated segment. The chip base is damaged.
                    }
                }
                selected_nets.at(net).tb_wires[s_i] = tb_wire;
                selected_nets.at(net).gate_wires[s_i] = gate_wire;
                // store used wires for gowin_pack
                if (wire_to_isolate != WireId()) {
                    wires_to_isolate += ctx->getWireName(wire_to_isolate).str(ctx);
                    wires_to_isolate += ";";
                }
                if (ctx->debug) {
                    log_info("    wire:%s, gate wire:%s\n", ctx->nameOfWire(tb_wire), ctx->nameOfWire(gate_wire));
                }
            }
            // Laying out a route for the network.
            std::vector<PipId> bound_pips;
            pool<WireId> bound_wires;
            SegmentRouteResult routed = SEG_NOT_ROUTED;
            pool<WireId> gate_wires;

            if (ctx->debug) {
                log_info("  Route\n");
            }
            for (auto usr : ni->users) {
                BelId dst_bel = usr.cell->bel;
                NPNR_ASSERT(dst_bel != BelId());

                Loc dst_loc(ctx->getBelLocation(dst_bel));
                WireId dst_wire = ctx->getNetinfoSinkWire(ni, usr, 0);

                // find segment covers dest
                int s_idx = -1;
                int s_x, s_min_x, s_min_y, s_max_x, s_max_y;
                WireId tb_wire, gate_wire;
                for (int s_i : selected_nets.at(net).segs) {
                    int idx;
                    gwu.get_segment_region(s_i, idx, s_x, s_min_x, s_min_y, s_max_x, s_max_y);
                    if (dst_loc.x >= s_min_x && dst_loc.x <= s_max_x && dst_loc.y >= s_min_y && dst_loc.y <= s_max_y) {
                        s_idx = idx;
                        tb_wire = selected_nets.at(net).tb_wires.at(s_i);
                        gate_wire = selected_nets.at(net).gate_wires.at(s_i);
                        break;
                    }
                }
                if (ctx->debug) {
                    log_info("    segment index:%d, dst:%s\n", s_idx, ctx->nameOf(usr.cell));
                }
                // There may not be a suitable segment if the sink is close to
                // the source. In that case consider these sinks along with
                // gate wires.
                if (s_idx == -1) {
                    gate_wires.insert(dst_wire);
                } else {
                    // Step 0: LBx1 -> dest
                    routed = route_segmented_step0(ni, dst_loc, dst_wire, s_idx, s_x, bound_pips);
                    if (routed == SEG_NOT_ROUTED) {
                        break;
                    }
                    if (routed == SEG_ROUTED_TO_ANOTHER_SEGMENT) {
                        continue;
                    }
                    // Step 1: segment wire -> LBOx
                    routed = route_segmented_step1(ni, dst_loc, s_idx, s_x, bound_pips);
                    if (routed == SEG_NOT_ROUTED) {
                        break;
                    }
                    if (routed == SEG_ROUTED_TO_ANOTHER_SEGMENT) {
                        continue;
                    }
                    // Step 2: gate wire -> segment wire
                    routed = route_segmented_step2(ni, tb_wire, gate_wire, bound_pips);
                    if (routed == SEG_NOT_ROUTED) {
                        break;
                    }
                    // mark gate for step 3
                    gate_wires.insert(gate_wire);
                }
            }
            // Step 3: src -> gate wire
            routed = route_segmented_step3(ni, gate_wires, bound_pips, bound_wires);
            if (routed == SEG_NOT_ROUTED) {
                if (ctx->verbose || ctx->debug) {
                    log_warning("Can't route net %s using segments.\n", ctx->nameOf(ni));
                }
                // unbind pips and wires
                for (PipId pip : bound_pips) {
                    ctx->unbindPip(pip);
                }
                for (WireId wire : bound_wires) {
                    ctx->unbindWire(wire);
                }
            } else {
                // make list of wires for isolation
                if (!wires_to_isolate.empty()) {
                    ni->attrs[id_SEG_WIRES_TO_ISOLATE] = wires_to_isolate;
                }
                log_info("    '%s' is routed using segments.\n", ctx->nameOf(ni));
                if (ctx->debug) {
                    log_info("    routed\n");
                    for (PipId pip : bound_pips) {
                        log_info("      %s\n", ctx->nameOfPip(pip));
                    }
                    for (WireId wire : bound_wires) {
                        log_info("      %s\n", ctx->nameOfWire(wire));
                    }
                }
            }
        }
    }

    // Enable clocked spines by connecting magic wires to VCC/GND if necessary.
    void enable_spines(void)
    {
        if (ctx->verbose) {
            log_info("Check for spine select wires.\n");
        }

        NetInfo *vcc_net = ctx->nets.at(ctx->id("$PACKER_VCC")).get();
        NetInfo *vss_net = ctx->nets.at(ctx->id("$PACKER_GND")).get();

        std::unique_ptr<CellInfo> top_ci = gwu.create_cell(ctx->id("spine_select$top"), id_SPINE_SELECT);
        top_ci->pseudo_cell = std::make_unique<RegionPlug>(Loc(0, 0, 0));
        std::unique_ptr<CellInfo> bottom_ci = gwu.create_cell(ctx->id("spine_select$bottom"), id_SPINE_SELECT);
        bottom_ci->pseudo_cell = std::make_unique<RegionPlug>(Loc(0, 0, 0));

        pool<WireId> seen_spines;
        dict<IdString, int> top_connections;
        dict<IdString, int> bottom_connections;

        for (auto &net : ctx->nets) {
            const NetInfo *ni = net.second.get();
            for (auto &wire : ni->wires) {
                WireId spine = wire.first;
                IdString spine_name = ctx->getWireName(spine)[1];
                if (spine_name.str(ctx).rfind("SPINE", 0) == 0 && seen_spines.count(spine) == 0) {
                    seen_spines.insert(spine);
                    std::vector<std::pair<WireId, int>> wires;
                    if (gwu.get_spine_select_wire(spine, wires)) {
                        int sfx = 0; // To activate a single spine, it may be necessary to connect an unknown number of
                                     // wires.
                        CellInfo *select_cell = top_ci.get();
                        auto connections = &top_connections;
                        if (gwu.wire_in_bottom_half(spine)) {
                            select_cell = bottom_ci.get();
                            connections = &bottom_connections;
                        }

                        for (auto gate : wires) {
                            IdString port_name = ctx->idf("%s.%d", spine_name.c_str(ctx), sfx);

                            select_cell->addInput(port_name);

                            RegionPlug *rp = (RegionPlug *)select_cell->pseudo_cell.get();
                            rp->port_wires[port_name] = gate.first;
                            (*connections)[port_name] = gate.second;
                            ++sfx;
                            if (ctx->verbose) {
                                log_info("  %s->%s\n", port_name.c_str(ctx), ctx->nameOfWire(gate.first));
                            }
                        }
                    }
                }
            }
        }

        // realy connect nets
        if (!top_connections.empty()) {
            for (auto conn : top_connections) {
                top_ci->connectPort(conn.first, conn.second ? vcc_net : vss_net);
            }
            ctx->cells[top_ci->name] = std::move(top_ci);
        }
        if (!bottom_connections.empty()) {
            for (auto conn : bottom_connections) {
                bottom_ci->connectPort(conn.first, conn.second ? vcc_net : vss_net);
            }
            ctx->cells[bottom_ci->name] = std::move(bottom_ci);
        }
    }

    // Route all
    void run(void)
    {
        log_info("Routing globals...\n");

        std::vector<IdString> dhcen_nets, dqce_nets, dcs_nets, buf_nets, clk_nets, seg_nets;

        // Determining the priority of network routing
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            CellInfo *drv = ni->driver.cell;
            if (drv == nullptr || ni->users.empty()) {
                if (ctx->debug) {
                    log_info("skip empty or driverless net:%s\n", ctx->nameOf(ni));
                }
                continue;
            }
            if (gwu.driver_is_buf(ni->driver)) {
                buf_nets.push_back(net.first);
            } else {
                if (gwu.driver_is_clksrc(ni->driver)) {
                    clk_nets.push_back(net.first);
                } else {
                    if (gwu.driver_is_dqce(ni->driver)) {
                        dqce_nets.push_back(net.first);
                    } else {
                        if (gwu.driver_is_dcs(ni->driver)) {
                            dcs_nets.push_back(net.first);
                        } else {
                            if (gwu.driver_is_dhcen(ni->driver)) {
                                dhcen_nets.push_back(net.first);
                            } else {
                                seg_nets.push_back(net.first);
                            }
                        }
                    }
                }
            }
        }

        // nets with DHCEN
        for (IdString net_name : dhcen_nets) {
            NetInfo *ni = ctx->nets.at(net_name).get();
            if (ctx->verbose) {
                log_info("route dhcen net '%s'\n", ctx->nameOf(ni));
            }
            route_dhcen_net(ni);
        }

        // nets with DQCE
        for (IdString net_name : dqce_nets) {
            NetInfo *ni = ctx->nets.at(net_name).get();
            if (ctx->verbose) {
                log_info("route dqce net '%s'\n", ctx->nameOf(ni));
            }
            route_dqce_net(ni);
        }

        // nets with DCS
        for (IdString net_name : dcs_nets) {
            NetInfo *ni = ctx->nets.at(net_name).get();
            if (ctx->verbose) {
                log_info("route dcs net '%s'\n", ctx->nameOf(ni));
            }
            route_dcs_net(ni);
        }

        // buffered nets
        for (IdString net_name : buf_nets) {
            NetInfo *ni = ctx->nets.at(net_name).get();
            CellInfo *drv = ni->driver.cell;
            if (drv == nullptr || ni->users.empty()) {
                if (ctx->debug) {
                    log_info("skip empty or driverless net:%s\n", ctx->nameOf(ni));
                }
                continue;
            }
            if (ctx->verbose) {
                log_info("route buffered net '%s'\n", ctx->nameOf(ni));
            }
            route_buffered_net(ni);
        }

        // clock nets
        for (IdString net_name : clk_nets) {
            NetInfo *ni = ctx->nets.at(net_name).get();
            CellInfo *drv = ni->driver.cell;
            if (drv == nullptr || ni->users.empty()) {
                if (ctx->debug) {
                    log_info("skip empty or driverless net:%s\n", ctx->nameOf(ni));
                }
                continue;
            }
            if (ctx->verbose) {
                log_info("route clock net '%s', src:%s\n", ctx->nameOf(ni), ctx->nameOf(ni->driver.cell));
            }
            if (route_clk_net(ni) == NOT_ROUTED) {
                if (ctx->verbose) {
                    log_info("  will try to route it as a segmented network.\n");
                }
                seg_nets.push_back(net_name);
            }
        }

        // segmented nets
        if (gwu.get_segments_count() != 0) {
            route_segmented(seg_nets);
        }

        // In some GW5 series chips, in addition to the mechanism for
        // enabling/disabling individual clock spines using fuses, which is
        // invisible to nextpnr, it is necessary to enable them by connecting
        // some ports of the mysterious MUX to VSS/GND.
        if (gwu.has_spine_enable_nets()) {
            enable_spines();
        }
    }
};

void gowin_route_globals(Context *ctx)
{
    GowinGlobalRouter router(ctx);
    router.run();
}

NEXTPNR_NAMESPACE_END
