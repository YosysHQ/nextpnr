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
        WireId src, dst;
        src = ctx->getPipSrcWire(pip);
        dst = ctx->getPipDstWire(pip);
        IdString dst_name = ctx->getWireName(dst)[1];
        bool not_dsc_pip = dst_name != id_CLKOUT;
        IdString src_type = ctx->getWireType(src);
        IdString dst_type = ctx->getWireType(dst);
        bool src_valid = not_dsc_pip && src_type.in(id_GLOBAL_CLK, id_IO_O, id_PLL_O, id_HCLK);
        bool dst_valid = not_dsc_pip && dst_type.in(id_GLOBAL_CLK, id_TILE_CLK, id_PLL_I, id_IO_I, id_HCLK);

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

    bool global_DQCE_pip_filter(PipId pip) const
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
        bool not_dsc_pip = dst_name != id_CLKOUT && !is_dcs_input(src_name);
        IdString src_type = ctx->getWireType(src);
        IdString dst_type = ctx->getWireType(dst);
        bool src_valid = not_dsc_pip && src_type.in(id_GLOBAL_CLK, id_IO_O, id_PLL_O, id_HCLK);
        bool dst_valid = not_dsc_pip && dst_type.in(id_GLOBAL_CLK, id_TILE_CLK, id_PLL_I, id_IO_I, id_HCLK);

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

    bool global_DCS_pip_filter(PipId pip) const
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

    bool driver_is_buf(const PortRef &driver) { return CellTypePort(driver) == CellTypePort(id_BUFG, id_O); }
    bool driver_is_dqce(const PortRef &driver) { return CellTypePort(driver) == CellTypePort(id_DQCE, id_CLKOUT); }
    bool driver_is_dcs(const PortRef &driver) { return CellTypePort(driver) == CellTypePort(id_DCS, id_CLKOUT); }
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
        // HCLK outputs
        if (driver.cell->type.in(id_CLKDIV, id_CLKDIV2)) {
            if (driver.port.in(id_CLKOUT)) {
                if (ctx->debug) {
                    log_info("%s out:%s:%s\n", driver.cell->type.c_str(ctx),
                             ctx->getBelName(driver.cell->bel).str(ctx).c_str(), driver.port.c_str(ctx));
                }
                return true;
            }
        }
        return false;
    }

    enum RouteResult
    {
        NOT_ROUTED = 0,
        ROUTED_PARTIALLY,
        ROUTED_ALL
    };

    RouteResult route_direct_net(NetInfo *net, WireId aux_src = WireId(), bool DCS_pips = false, bool DQCE_pips = false)
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
            if (DCS_pips) {
                bfs_res = backwards_bfs_route(net, src, dst, 1000000, false, [&](PipId pip) {
                    return (is_relaxed_sink(usr) || global_DCS_pip_filter(pip));
                });
            } else {
                if (DQCE_pips) {
                    bfs_res = backwards_bfs_route(net, src, dst, 1000000, false, [&](PipId pip) {
                        return (is_relaxed_sink(usr) || global_DQCE_pip_filter(pip));
                    });
                } else {
                    bfs_res = backwards_bfs_route(net, src, dst, 1000000, false, [&](PipId pip) {
                        return (is_relaxed_sink(usr) || global_pip_filter(pip));
                    });
                }
            }
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
        NPNR_ASSERT_MSG(driver_is_buf(driver) || driver_is_clksrc(driver),
                        stringf("The input source for %s is not a clock.", ctx->nameOf(dqce_ci)).c_str());
        WireId src;
        // use BUF input if there is one
        if (driver_is_buf(driver)) {
            src = ctx->getBelPinWire(driver.cell->bel, id_I);
        } else {
            src = ctx->getBelPinWire(driver.cell->bel, driver.port);
        }

        RouteResult route_result = route_direct_net(net, src, false, true);
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
        // Since CLKOUT is responsible for only one quadrant, we will do
        // routing not from it, but from any CLK0-3 input actually connected to
        // the clock source.
        CellInfo *dcs_ci = net->driver.cell;
        NetInfo *net_before_dcs;
        PortRef driver;
        for (int i = 0; i < 4; ++i) {
            net_before_dcs = dcs_ci->getPort(ctx->idf("CLK%d", i));
            if (net_before_dcs == nullptr) {
                continue;
            }
            driver = net_before_dcs->driver;
            if (driver_is_buf(driver) || driver_is_clksrc(driver)) {
                break;
            }
            net_before_dcs = nullptr;
        }
        NPNR_ASSERT_MSG(net_before_dcs != nullptr, stringf("No clock inputs for %s.", ctx->nameOf(dcs_ci)).c_str());

        WireId src;
        // use BUF input if there is one
        if (driver_is_buf(driver)) {
            src = ctx->getBelPinWire(driver.cell->bel, id_I);
        } else {
            src = ctx->getBelPinWire(driver.cell->bel, driver.port);
        }

        RouteResult route_result = route_direct_net(net, src, true);
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
            dcs_ci->copyPortBusTo(id_CLK, 0, false, hw_dcs, id_CLK, 0, false, 4);
            dcs_ci->copyPortBusTo(id_CLKSEL, 0, true, hw_dcs, id_CLKSEL, 0, false, 4);
        }

        // remove the virtual DCS
        dcs_ci->disconnectPort(id_SELFORCE);
        dcs_ci->disconnectPort(id_CLKOUT);
        for (int i = 0; i < 4; ++i) {
            dcs_ci->disconnectPort(ctx->idf("CLKSEL[%d]", i));
            dcs_ci->disconnectPort(ctx->idf("CLK%d", i));
        }
        log_info("    '%s' net was routed.\n", ctx->nameOf(net));
        ctx->cells.erase(dcs_ci->name);
    }

    void route_buffered_net(NetInfo *net)
    {
        // a) route net after buf using the buf input as source
        CellInfo *buf_ci = net->driver.cell;
        WireId src = ctx->getBelPinWire(buf_ci->bel, id_I);

        NetInfo *net_before_buf = buf_ci->getPort(id_I);
        NPNR_ASSERT(net_before_buf != nullptr);

        RouteResult route_result = route_direct_net(net, src);
        if (route_result == NOT_ROUTED || route_result == ROUTED_PARTIALLY) {
            log_error("Can't route the %s net. It might be worth removing the BUFG buffer flag.\n", ctx->nameOf(net));
        }

        // b) route net before buf from whatever to the buf input
        WireId dst = src;
        CellInfo *true_src_ci = net_before_buf->driver.cell;
        src = ctx->getBelPinWire(true_src_ci->bel, net_before_buf->driver.port);
        ctx->bindWire(src, net, STRENGTH_LOCKED);
        backwards_bfs_route(net, src, dst, 1000000, false, [&](PipId pip) { return true; });
        // remove net
        buf_ci->movePortTo(id_O, true_src_ci, net_before_buf->driver.port);
        net_before_buf->driver.cell = nullptr;

        log_info("    '%s' net was routed.\n", ctx->nameOf(net));
    }

    void route_clk_net(NetInfo *net)
    {
        RouteResult route_result = route_direct_net(net);
        if (route_result != NOT_ROUTED) {
            log_info("    '%s' net was routed  using global resources %s.\n", ctx->nameOf(net),
                     route_result == ROUTED_ALL ? "only" : "partially");
        }
    }

    void run(void)
    {
        log_info("Routing globals...\n");

        std::vector<IdString> dqce_nets, dcs_nets, buf_nets, clk_nets;

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
            if (driver_is_buf(ni->driver)) {
                buf_nets.push_back(net.first);
            } else {
                if (driver_is_clksrc(ni->driver)) {
                    clk_nets.push_back(net.first);
                } else {
                    if (driver_is_dqce(ni->driver)) {
                        dqce_nets.push_back(net.first);
                    } else {
                        if (driver_is_dcs(ni->driver)) {
                            dcs_nets.push_back(net.first);
                        }
                    }
                }
            }
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
                log_info("route clock net '%s'\n", ctx->nameOf(ni));
            }
            route_clk_net(ni);
        }
    }
};

void gowin_route_globals(Context *ctx)
{
    GowinGlobalRouter router(ctx);
    router.run();
}

NEXTPNR_NAMESPACE_END
