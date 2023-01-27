/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2022  YRabbit <rabbit@yrabbit.cyou>
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

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <queue>
#include "cells.h"
#include "log.h"
#include "nextpnr.h"
#include "place_common.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

bool GowinGlobalRouter::is_clock_port(PortRef const &user)
{
    if ((user.cell->type.in(id_SLICE, id_ODDR, id_ODDRC)) && user.port == id_CLK) {
        return true;
    }
    return false;
}

std::pair<WireId, BelId> GowinGlobalRouter::clock_src(Context *ctx, PortRef const &driver)
{
    if (driver.cell == nullptr) {
        return std::make_pair(WireId(), BelId());
    }

    BelInfo &bel = ctx->bel_info(driver.cell->bel);
    WireId wire;
    if (driver.cell->type == id_IOB) {
        if (ctx->is_GCLKT_iob(driver.cell)) {
            wire = bel.pins[id_O].wire;
            return std::make_pair(wire, bel.name);
        }
        return std::make_pair(WireId(), BelId());
    }
    if (driver.cell->type == id_rPLL || driver.cell->type == id_PLLVR) {
        if (driver.port == id_CLKOUT || driver.port == id_CLKOUTP || driver.port == id_CLKOUTD ||
            driver.port == id_CLKOUTD3) {
            wire = bel.pins[driver.port].wire;
            return std::make_pair(wire, bel.name);
        }
        return std::make_pair(WireId(), BelId());
    }
    return std::make_pair(WireId(), BelId());
}

// gather the clock nets
void GowinGlobalRouter::gather_clock_nets(Context *ctx, std::vector<globalnet_t> &clock_nets)
{
    for (auto const &net : ctx->nets) {
        NetInfo const *ni = net.second.get();
        auto new_clock = clock_nets.end();
        auto clock_wire_bel = clock_src(ctx, ni->driver);
        if (clock_wire_bel.first != WireId()) {
            clock_nets.emplace_back(net.first);
            new_clock = --clock_nets.end();
            new_clock->clock_wire = clock_wire_bel.first;
            new_clock->clock_bel = clock_wire_bel.second;
        }
        for (auto const &user : ni->users) {
            if (is_clock_port(user)) {
                if (new_clock == clock_nets.end()) {
                    clock_nets.emplace_back(net.first);
                    new_clock = --clock_nets.end();
                }
                ++(new_clock->clock_ports);
            }
        }
    }
    // need to prioritize the nets
    std::sort(clock_nets.begin(), clock_nets.end());

    if (ctx->verbose) {
        for (auto const &net : clock_nets) {
            log_info("  Net:%s, ports:%d, clock source:%s\n", net.name.c_str(ctx), net.clock_ports,
                     net.clock_wire == WireId() ? "No" : net.clock_wire.c_str(ctx));
        }
    }
}

// non clock port
// returns GB pip
IdString GowinGlobalRouter::route_to_non_clock_port(Context *ctx, WireId const dstWire, int clock,
                                                    pool<IdString> &used_pips, pool<IdString> &undo_wires)
{
    static std::vector<IdString> one_hop_0 = {id_W111, id_W121, id_E111, id_E121};
    static std::vector<IdString> one_hop_4 = {id_S111, id_S121, id_N111, id_N121};
    // uphill pips
    for (auto const uphill : ctx->getPipsUphill(dstWire)) {
        WireId srcWire = ctx->getPipSrcWire(uphill);
        bool found;
        if (clock < 4) {
            found = find(one_hop_0.begin(), one_hop_0.end(), ctx->wire_info(ctx->getPipSrcWire(uphill)).type) !=
                    one_hop_0.end();
        } else {
            found = find(one_hop_4.begin(), one_hop_4.end(), ctx->wire_info(ctx->getPipSrcWire(uphill)).type) !=
                    one_hop_4.end();
        }
        if (found) {
            // found one hop pip
            if (used_wires.count(srcWire)) {
                if (used_wires[srcWire] != clock) {
                    continue;
                }
            }
            WireInfo wi = ctx->wire_info(srcWire);
            std::string wire_alias = srcWire.str(ctx).substr(srcWire.str(ctx).rfind("_") + 1);
            IdString gb = ctx->idf("R%dC%d_GB%d0_%s", wi.y + 1, wi.x + 1, clock, wire_alias.c_str());
            if (ctx->verbose) {
                log_info("    1-hop gb:%s\n", gb.c_str(ctx));
            }
            // sanity
            NPNR_ASSERT(find(ctx->getPipsUphill(srcWire).begin(), ctx->getPipsUphill(srcWire).end(), gb) !=
                        ctx->getPipsUphill(srcWire).end());
            auto up_pips = ctx->getPipsUphill(srcWire);
            if (find(up_pips.begin(), up_pips.end(), gb) != up_pips.end()) {
                if (!used_wires.count(srcWire)) {
                    used_wires.insert(std::make_pair(srcWire, clock));
                    undo_wires.insert(srcWire);
                }
                used_pips.insert(uphill);
                if (ctx->verbose) {
                    log_info("    1-hop Pip:%s\n", uphill.c_str(ctx));
                }
                return gb;
            }
        }
    }
    return IdString();
}

// route one net
void GowinGlobalRouter::route_net(Context *ctx, globalnet_t const &net)
{
    // For failed routing undo
    pool<IdString> used_pips;
    pool<IdString> undo_wires;

    log_info("  Route net %s, use clock #%d.\n", net.name.c_str(ctx), net.clock);
    for (auto const &user : ctx->net_info(net.name).users) {
        // >>> port <- GB<clock>0
        WireId dstWire = ctx->getNetinfoSinkWire(&ctx->net_info(net.name), user, 0);
        if (ctx->verbose) {
            log_info("   Cell:%s, port:%s, wire:%s\n", user.cell->name.c_str(ctx), user.port.c_str(ctx),
                     dstWire.c_str(ctx));
        }

        char buf[30];
        PipId gb_pip_id;
        if (user.port == id_CLK || user.port == id_CLKIN) {
            WireInfo const wi = ctx->wire_info(dstWire);
            gb_pip_id =
                    ctx->idf("R%dC%d_GB%d0_%s", wi.y + 1, wi.x + 1, net.clock, ctx->wire_info(dstWire).type.c_str(ctx));
            // sanity
            NPNR_ASSERT(find(ctx->getPipsUphill(dstWire).begin(), ctx->getPipsUphill(dstWire).end(), gb_pip_id) !=
                        ctx->getPipsUphill(dstWire).end());
        } else {
            // Non clock port
            gb_pip_id = route_to_non_clock_port(ctx, dstWire, net.clock, used_pips, undo_wires);
            if (gb_pip_id == IdString()) {
                if (ctx->verbose) {
                    log_info("  Can't find route to %s, net %s will be routed in a standard way.\n", dstWire.c_str(ctx),
                             net.name.c_str(ctx));
                }
                for (IdString const undo : undo_wires) {
                    used_wires.erase(undo);
                }
                return;
            }
        }
        if (ctx->verbose) {
            log_info("    GB Pip:%s\n", gb_pip_id.c_str(ctx));
        }

        if (used_pips.count(gb_pip_id)) {
            if (ctx->verbose) {
                log_info("    ^routed already^\n");
            }
            continue;
        }
        used_pips.insert(gb_pip_id);

        // >>> GBOx <- GTx0
        dstWire = ctx->getPipSrcWire(gb_pip_id);
        WireInfo dstWireInfo = ctx->wire_info(dstWire);
        int branch_tap_idx = net.clock > 3 ? 1 : 0;
        PipId gt_pip_id =
                ctx->idf("R%dC%d_GT%d0_GBO%d", dstWireInfo.y + 1, dstWireInfo.x + 1, branch_tap_idx, branch_tap_idx);
        if (ctx->verbose) {
            log_info("     GT Pip:%s\n", gt_pip_id.c_str(ctx));
        }
        // sanity
        NPNR_ASSERT(find(ctx->getPipsUphill(dstWire).begin(), ctx->getPipsUphill(dstWire).end(), gt_pip_id) !=
                    ctx->getPipsUphill(dstWire).end());
        // if already routed
        if (used_pips.count(gt_pip_id)) {
            if (ctx->verbose) {
                log_info("     ^routed already^\n");
            }
            continue;
        }
        used_pips.insert(gt_pip_id);

        // >>> GTx0 <- SPINExx
        // XXX no optimization here, we need to store
        // the SPINE <-> clock# correspondence in the database. In the
        // meantime, we define in run-time in a completely suboptimal way.
        std::vector<std::string> clock_spine;
        dstWire = ctx->getPipSrcWire(gt_pip_id);
        for (auto const uphill_pip : ctx->getPipsUphill(dstWire)) {
            std::string name = ctx->wire_info(ctx->getPipSrcWire(uphill_pip)).type.str(ctx);
            if (name.rfind("SPINE", 0) == 0) {
                clock_spine.push_back(name);
            }
        }
        sort(clock_spine.begin(), clock_spine.end(), [](const std::string &a, const std::string &b) -> bool {
            return (a.size() < b.size()) || (a.size() == b.size() && a < b);
        });
        dstWireInfo = ctx->wire_info(dstWire);
        snprintf(buf, sizeof(buf), "R%dC%d_%s_GT%d0", dstWireInfo.y + 1, dstWireInfo.x + 1,
                 clock_spine[net.clock - branch_tap_idx * 4].c_str(), branch_tap_idx);
        PipId spine_pip_id = ctx->id(buf);
        if (ctx->verbose) {
            log_info("      Spine Pip:%s\n", buf);
        }
        // sanity
        NPNR_ASSERT(find(ctx->getPipsUphill(dstWire).begin(), ctx->getPipsUphill(dstWire).end(), spine_pip_id) !=
                    ctx->getPipsUphill(dstWire).end());
        // if already routed
        if (used_pips.count(spine_pip_id)) {
            if (ctx->verbose) {
                log_info("      ^routed already^\n");
            }
            continue;
        }
        used_pips.insert(spine_pip_id);

        // >>> SPINExx <- Src
        dstWire = ctx->getPipSrcWire(spine_pip_id);
        dstWireInfo = ctx->wire_info(dstWire);
        PipId src_pip_id = PipId();
        for (auto const uphill_pip : ctx->getPipsUphill(dstWire)) {
            if (ctx->getPipSrcWire(uphill_pip) == net.clock_wire) {
                src_pip_id = uphill_pip;
                break;
            }
        }
        if (ctx->verbose) {
            log_info("       Src Pip:%s\n", src_pip_id.c_str(ctx));
        }
        NPNR_ASSERT(src_pip_id != PipId());
        // if already routed
        if (used_pips.count(src_pip_id)) {
            if (ctx->verbose) {
                log_info("      ^routed already^\n");
            }
            continue;
        }
        used_pips.insert(src_pip_id);
    }
    log_info("  Net %s is routed.\n", net.name.c_str(ctx));
    if (!ctx->net_info(net.name).users.empty()) {
        for (auto const pip : used_pips) {
            ctx->bindPip(pip, &ctx->net_info(net.name), STRENGTH_LOCKED);
        }
        ctx->bindWire(net.clock_wire, &ctx->net_info(net.name), STRENGTH_LOCKED);
    }
}

void GowinGlobalRouter::route_globals(Context *ctx)
{
    log_info("Routing globals...\n");

    for (auto const &net : nets) {
        route_net(ctx, net);
    }
}

// Allocate networks that will be routed through the global system.
// Mark their driver cells as global buffers to exclude them from the analysis.
void GowinGlobalRouter::mark_globals(Context *ctx)
{
    log_info("Find global nets...\n");

    std::vector<globalnet_t> clock_nets;
    gather_clock_nets(ctx, clock_nets);
    // XXX we need to use the list of indexes of clocks from the database
    // use 6 clocks (XXX 3 for GW1NZ-1)
    int max_clock = ctx->max_clock, cur_clock = -1;
    for (auto &net : clock_nets) {
        // XXX only IO clock for now
        if (net.clock_wire == WireId()) {
            log_info(" Non clock source, skip %s.\n", net.name.c_str(ctx));
            continue;
        }
        if (++cur_clock >= max_clock) {
            log_info(" No more clock wires left, skip the remaining nets.\n");
            break;
        }
        if (ctx->net_info(net.name).users.empty()) {
            --cur_clock;
            net.clock = -1;
        } else {
            net.clock = cur_clock;
        }
        BelInfo &bi = ctx->bel_info(net.clock_bel);
        bi.gb = true;
        nets.emplace_back(net);
    }
}

NEXTPNR_NAMESPACE_END
