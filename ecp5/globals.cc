/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
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

#include "globals.h"
#include <algorithm>
#include <iomanip>
#include <queue>
#include "cells.h"
#include "log.h"
#include "nextpnr.h"
#include "place_common.h"
#include "util.h"
#define fmt_str(x) (static_cast<const std::ostringstream &>(std::ostringstream() << x).str())

NEXTPNR_NAMESPACE_BEGIN

static std::string get_quad_name(GlobalQuadrant quad)
{
    switch (quad) {
    case QUAD_UL:
        return "UL";
    case QUAD_UR:
        return "UR";
    case QUAD_LL:
        return "LL";
    case QUAD_LR:
        return "LR";
    }
    return "";
}

class Ecp5GlobalRouter
{
  public:
    Ecp5GlobalRouter(Context *ctx) : ctx(ctx){};

  private:
    bool is_clock_port(const PortRef &user)
    {
        if (user.cell->type == id_TRELLIS_FF && user.port == id_CLK)
            return true;
        if (user.cell->type == id_TRELLIS_COMB && user.port == id_WCK)
            return true;
        if (user.cell->type == id_DCUA &&
            (user.port.in(id_CH0_FF_RXI_CLK, id_CH1_FF_RXI_CLK, id_CH0_FF_TXI_CLK, id_CH1_FF_TXI_CLK)))
            return true;
        if ((user.cell->type.in(id_IOLOGIC, id_SIOLOGIC)) && (user.port == id_CLK))
            return true;
        return false;
    }

    bool is_logic_port(const PortRef &user)
    {
        if (user.cell->type == id_TRELLIS_FF && user.port != id_CLK)
            return true;
        if (user.cell->type == id_TRELLIS_COMB && user.port != id_WCK)
            return true;
        return false;
    }

    std::vector<NetInfo *> get_clocks()
    {
        dict<IdString, int> clockCount;
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            if (ni->name == ctx->id("$PACKER_GND_NET") || ni->name == ctx->id("$PACKER_VCC_NET") ||
                ni->driver.cell == nullptr)
                continue;
            clockCount[ni->name] = 0;
            for (const auto &user : ni->users) {
                if (is_clock_port(user)) {
                    clockCount[ni->name]++;
                    if (user.cell->type == id_DCUA)
                        clockCount[ni->name] += 100;
                    if (user.cell->type.in(id_IOLOGIC, id_SIOLOGIC))
                        clockCount[ni->name] += 10;
                }
            }
            // log_info("clkcount %s: %d\n", ni->name.c_str(ctx),clockCount[ni->name]);
        }
        // DCCAs must always drive globals
        std::vector<NetInfo *> clocks;
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type == id_DCCA) {
                NetInfo *glb = ci->ports.at(id_CLKO).net;
                if (glb != nullptr) {
                    clocks.push_back(glb);
                    clockCount.erase(glb->name);
                }
            }
        }
        while (clocks.size() < 16) {
            auto max = std::max_element(clockCount.begin(), clockCount.end(),
                                        [](const decltype(clockCount)::value_type &a,
                                           const decltype(clockCount)::value_type &b) { return a.second < b.second; });
            if (max == clockCount.end() || max->second < 5)
                break;
            clocks.push_back(ctx->nets.at(max->first).get());
            clockCount.erase(max->first);
        }
        return clocks;
    }

    PipId find_tap_pip(WireId tile_glb)
    {
        std::string wireName = ctx->get_wire_basename(tile_glb).str(ctx);
        std::string glbName = wireName.substr(2);
        TapDirection td = ctx->global_info_at_loc(tile_glb.location).tap_dir;
        WireId tap_wire;
        Location tap_loc;
        tap_loc.x = ctx->global_info_at_loc(tile_glb.location).tap_col;
        tap_loc.y = tile_glb.location.y;
        if (td == TAP_DIR_LEFT) {
            tap_wire = ctx->get_wire_by_loc_basename(tap_loc, "L_" + glbName);
        } else {
            tap_wire = ctx->get_wire_by_loc_basename(tap_loc, "R_" + glbName);
        }
        NPNR_ASSERT(tap_wire != WireId());
        return *(ctx->getPipsUphill(tap_wire).begin());
    }

    PipId find_spine_pip(WireId tap_wire)
    {
        std::string wireName = ctx->get_wire_basename(tap_wire).str(ctx);
        Location spine_loc;
        spine_loc.x = ctx->global_info_at_loc(tap_wire.location).spine_col;
        spine_loc.y = ctx->global_info_at_loc(tap_wire.location).spine_row;
        WireId spine_wire = ctx->get_wire_by_loc_basename(spine_loc, wireName);
        return *(ctx->getPipsUphill(spine_wire).begin());
    }

    void route_logic_tile_global(NetInfo *net, int global_index, PortRef user)
    {
        WireId userWire = ctx->getBelPinWire(user.cell->bel, user.port);
        WireId globalWire;
        IdString global_name = ctx->id(fmt_str("G_HPBX" << std::setw(2) << std::setfill('0') << global_index << "00"));
        std::queue<WireId> upstream;
        dict<WireId, PipId> backtrace;
        upstream.push(userWire);
        bool already_routed = false;
        WireId next;
        // Search back from the pin until we reach the global network
        while (true) {
            next = upstream.front();
            upstream.pop();

            if (ctx->getBoundWireNet(next) == net) {
                already_routed = true;
                globalWire = next;
                break;
            }

            if (ctx->get_wire_basename(next) == global_name) {
                globalWire = next;
                break;
            }
            if (ctx->checkWireAvail(next)) {
                for (auto pip : ctx->getPipsUphill(next)) {
                    WireId src = ctx->getPipSrcWire(pip);
                    if (backtrace.count(src))
                        continue;
                    backtrace[src] = pip;
                    upstream.push(src);
                }
            }
            if (upstream.size() > 30000) {
                log_error("failed to route HPBX%02d00 to %s.%s\n", global_index, ctx->nameOfBel(user.cell->bel),
                          user.port.c_str(ctx));
            }
        }
        // Set all the pips we found along the way
        WireId cursor = next;
        while (true) {
            auto fnd = backtrace.find(cursor);
            if (fnd == backtrace.end())
                break;
            ctx->bindPip(fnd->second, net, STRENGTH_LOCKED);
            cursor = ctx->getPipDstWire(fnd->second);
        }
        // If the global network inside the tile isn't already set up,
        // we also need to bind the buffers along the way
        if (!already_routed) {
            ctx->bindWire(next, net, STRENGTH_LOCKED);
            PipId tap_pip = find_tap_pip(next);
            NetInfo *tap_net = ctx->getBoundPipNet(tap_pip);
            if (tap_net == nullptr) {
                ctx->bindPip(tap_pip, net, STRENGTH_LOCKED);
                PipId spine_pip = find_spine_pip(ctx->getPipSrcWire(tap_pip));
                NetInfo *spine_net = ctx->getBoundPipNet(spine_pip);
                if (spine_net == nullptr) {
                    ctx->bindPip(spine_pip, net, STRENGTH_LOCKED);
                } else {
                    NPNR_ASSERT(spine_net == net);
                }
            } else {
                NPNR_ASSERT(tap_net == net);
            }
        }
    }

    bool is_global_io(CellInfo *io, std::string &glb_name)
    {
        std::string func_name = ctx->get_pio_function_name(io->bel);
        if (func_name.substr(0, 5) == "PCLKT") {
            func_name.erase(func_name.find('_'), 1);
            glb_name = "G_" + func_name;
            return true;
        }
        return false;
    }

    WireId get_global_wire(GlobalQuadrant quad, int network)
    {
        return ctx->get_wire_by_loc_basename(Location(0, 0),
                                             "G_" + get_quad_name(quad) + "PCLK" + std::to_string(network));
    }

    bool simple_router(NetInfo *net, WireId src, WireId dst, bool allow_fail = false)
    {
        std::queue<WireId> visit;
        dict<WireId, PipId> backtrace;
        visit.push(src);
        WireId cursor;
        while (true) {

            if (visit.empty() || visit.size() > 50000) {
                if (allow_fail)
                    return false;
                log_error("cannot route global from %s to %s.\n", ctx->nameOfWire(src), ctx->nameOfWire(dst));
            }
            cursor = visit.front();
            visit.pop();
            NetInfo *bound = ctx->getBoundWireNet(cursor);
            if (bound == net) {
            } else if (bound != nullptr) {
                continue;
            }
            if (cursor == dst)
                break;
            for (auto dh : ctx->getPipsDownhill(cursor)) {
                WireId pipDst = ctx->getPipDstWire(dh);
                if (backtrace.count(pipDst))
                    continue;
                backtrace[pipDst] = dh;
                visit.push(pipDst);
            }
        }
        while (true) {
            auto fnd = backtrace.find(cursor);
            if (fnd == backtrace.end())
                break;
            NetInfo *bound = ctx->getBoundWireNet(cursor);
            if (bound != nullptr) {
                NPNR_ASSERT(bound == net);
                break;
            }
            ctx->bindPip(fnd->second, net, STRENGTH_LOCKED);
            cursor = ctx->getPipSrcWire(fnd->second);
        }
        if (ctx->getBoundWireNet(src) == nullptr)
            ctx->bindWire(src, net, STRENGTH_LOCKED);
        return true;
    }

    bool route_onto_global(NetInfo *net, int network)
    {
        WireId glb_src;
        NPNR_ASSERT(net->driver.cell->type.in(id_DCCA, id_DCSC));
        glb_src = ctx->getNetinfoSourceWire(net);
        for (int quad = QUAD_UL; quad < QUAD_LR + 1; quad++) {
            WireId glb_dst = get_global_wire(GlobalQuadrant(quad), network);
            NPNR_ASSERT(glb_dst != WireId());
            bool routed = simple_router(net, glb_src, glb_dst);
            if (!routed)
                return false;
        }
        return true;
    }

    // Get DCC wirelength based on source
    wirelen_t get_dcc_wirelen(CellInfo *dcc, bool &dedicated_routing)
    {
        NetInfo *clki = dcc->ports.at((dcc->type == id_DCSC) ? id_CLK0 : id_CLKI).net;
        BelId drv_bel;
        const PortRef &drv = clki->driver;
        dedicated_routing = false;
        if (drv.cell == nullptr) {
            return 0;
        } else if (drv.cell->attrs.count(id_BEL)) {
            drv_bel = ctx->getBelByNameStr(drv.cell->attrs.at(id_BEL).as_string());
        } else {
            // Check if driver is a singleton
            BelId last_bel;
            bool singleton = true;
            for (auto bel : ctx->getBels()) {
                if (ctx->getBelType(bel) == drv.cell->type) {
                    if (last_bel != BelId()) {
                        singleton = false;
                        break;
                    }
                    last_bel = bel;
                }
            }
            if (singleton && last_bel != BelId()) {
                drv_bel = last_bel;
            }
        }
        if (drv_bel == BelId()) {
            // Driver is not locked. Use standard metric
            float tns;
            return get_net_metric(ctx, clki, MetricType::WIRELENGTH, tns);
        } else {
            // Check for dedicated routing
            if (has_short_route(ctx->getBelPinWire(drv_bel, drv.port), ctx->getBelPinWire(dcc->bel, id_CLKI))) {
                // log_info("dedicated route %s -> %s\n", ctx->nameOfWire(ctx->getBelPinWire(drv_bel,
                // drv.port)), ctx->nameOfWire(dcc->bel));
                dedicated_routing = true;
                return 0;
            }
            // Driver is locked
            Loc dcc_loc = ctx->getBelLocation(dcc->bel);
            Loc drv_loc = ctx->getBelLocation(drv_bel);
            return std::abs(dcc_loc.x - drv_loc.x) + std::abs(dcc_loc.y - drv_loc.y);
        }
    }

    // Return true if a short (<5) route exists between two wires
    bool has_short_route(WireId src, WireId dst, int thresh = 7)
    {
        std::queue<WireId> visit;
        dict<WireId, PipId> backtrace;
        visit.push(src);
        WireId cursor;
        while (true) {

            if (visit.empty() || visit.size() > 10000) {
                // log_info ("dist %s -> %s = inf\n", ctx->nameOfWire(src),
                // ctx->nameOfWire(dst));
                return false;
            }
            cursor = visit.front();
            visit.pop();

            if (cursor == dst)
                break;
            for (auto dh : ctx->getPipsDownhill(cursor)) {
                WireId pipDst = ctx->getPipDstWire(dh);
                if (backtrace.count(pipDst))
                    continue;
                backtrace[pipDst] = dh;
                visit.push(pipDst);
            }
        }
        int length = 0;
        while (true) {
            auto fnd = backtrace.find(cursor);
            if (fnd == backtrace.end())
                break;
            cursor = ctx->getPipSrcWire(fnd->second);
            length++;
        }
        // log_info ("dist %s -> %s = %d\n", ctx->nameOfWire(src), ctx->nameOfWire(dst),
        // length);
        return length < thresh;
    }

    pool<WireId> used_pclkcib;

    std::set<WireId> get_candidate_pclkcibs(BelId dcc)
    {
        std::set<WireId> candidates;
        WireId dcc_i = ctx->getBelPinWire(dcc, id_CLKI);
        WireId dcc_mux = ctx->getPipSrcWire(*(ctx->getPipsUphill(dcc_i).begin()));
        for (auto pip : ctx->getPipsUphill(dcc_mux)) {
            WireId src = ctx->getPipSrcWire(pip);
            std::string basename = ctx->nameOf(ctx->get_wire_basename(src));
            if (basename.find("QPCLKCIB") == std::string::npos)
                continue;
            candidates.insert(src);
        }
        return candidates;
    }

    // Attempt to place a DCC
    void place_dcc_dcs(CellInfo *dcc)
    {
        BelId best_bel;
        WireId best_bel_pclkcib;
        bool using_ce = dcc->getPort(id_CE) != nullptr;
        wirelen_t best_wirelen = 9999999;
        bool dedicated_routing = false;
        for (auto bel : ctx->getBels()) {
            if (ctx->getBelType(bel) == dcc->type && ctx->checkBelAvail(bel)) {
                std::string belname = ctx->loc_info(bel)->bel_data[bel.index].name.get();
                if (belname.at(0) == 'D' && using_ce)
                    continue; // don't allow DCCs with CE at center
                ctx->bindBel(bel, dcc, STRENGTH_LOCKED);
                if (!ctx->isBelLocationValid(bel)) {
                    ctx->unbindBel(bel);
                    continue;
                }
                wirelen_t wirelen = get_dcc_wirelen(dcc, dedicated_routing);
                if (wirelen < best_wirelen) {
                    if (dedicated_routing || dcc->type == id_DCSC) {
                        best_bel_pclkcib = WireId();
                    } else {
                        bool found_pclkcib = false;
                        for (WireId pclkcib : get_candidate_pclkcibs(bel)) {
                            if (used_pclkcib.count(pclkcib))
                                continue;
                            found_pclkcib = true;
                            best_bel_pclkcib = pclkcib;
                            break;
                        }
                        if (!found_pclkcib)
                            goto pclkcib_fail;
                    }
                    best_bel = bel;
                    best_wirelen = wirelen;
                }
            pclkcib_fail:
                ctx->unbindBel(bel);
            }
        }
        NPNR_ASSERT(best_bel != BelId());
        ctx->bindBel(best_bel, dcc, STRENGTH_LOCKED);
        if (best_bel_pclkcib != WireId()) {
            used_pclkcib.insert(best_bel_pclkcib);
            if (ctx->verbose)
                log_info("        preliminary allocation of PCLKCIB '%s' to DCC '%s' at '%s'\n",
                         ctx->nameOfWire(best_bel_pclkcib), ctx->nameOf(dcc), ctx->nameOfBel(best_bel));
        }
    }

    // Insert a DCC into a net to promote it to a global
    NetInfo *insert_dcc(NetInfo *net, CellInfo *dcs_cell = nullptr)
    {
        NetInfo *glbptr = nullptr;
        CellInfo *dccptr = nullptr;
        if (net->driver.cell != nullptr && (net->driver.cell->type.in(id_DCCA, id_DCSC))) {
            // Already have a DCC (such as clock gating)
            glbptr = net;
            dccptr = net->driver.cell;
        } else {
            auto dcc = create_ecp5_cell(ctx, id_DCCA, "$gbuf$" + net->name.str(ctx));
            glbptr = ctx->createNet(ctx->id("$glbnet$" + net->name.str(ctx)));
            glbptr->driver.cell = dcc.get();
            glbptr->driver.port = id_CLKO;
            dcc->ports[id_CLKO].net = glbptr;
            std::vector<PortRef> keep_users;
            for (auto user : net->users) {
                if (dcs_cell != nullptr && user.cell != dcs_cell) {
                    // DCS DCC insertion mode
                    keep_users.push_back(user);
                } else if (user.port == id_CLKFB) {
                    keep_users.push_back(user);
                } else if (net->driver.cell->type == id_EXTREFB && user.cell->type == id_DCUA) {
                    keep_users.push_back(user);
                } else if (is_logic_port(user)) {
                    keep_users.push_back(user);
                } else {
                    user.cell->ports.at(user.port).net = glbptr;
                    user.cell->ports.at(user.port).user_idx = glbptr->users.add(user);
                }
            }
            net->users.clear();
            for (auto &usr : keep_users)
                usr.cell->ports.at(usr.port).user_idx = net->users.add(usr);

            dcc->connectPort(id_CLKI, net);
            if (net->clkconstr) {
                glbptr->clkconstr = std::unique_ptr<ClockConstraint>(new ClockConstraint());
                glbptr->clkconstr->low = net->clkconstr->low;
                glbptr->clkconstr->high = net->clkconstr->high;
                glbptr->clkconstr->period = net->clkconstr->period;
            }
            dccptr = dcc.get();
            ctx->cells[dcc->name] = std::move(dcc);
        }
        glbptr->attrs[id_ECP5_IS_GLOBAL] = 1;
        if (str_or_default(dccptr->attrs, id_BEL, "") == "")
            place_dcc_dcs(dccptr);
        return glbptr;
    }

    int global_route_priority(const PortRef &load)
    {
        if (load.port.in(id_WCK, id_WRE))
            return 90;
        return 99;
    }

    Context *ctx;

  public:
    void promote_globals()
    {
        bool is_ooc = bool_or_default(ctx->settings, ctx->id("arch.ooc"));
        log_info("Promoting globals...\n");
        auto clocks = get_clocks();
        for (auto clock : clocks) {
            bool is_noglobal = bool_or_default(clock->attrs, id_noglobal, false) ||
                               bool_or_default(clock->attrs, id_ECP5_IS_GLOBAL, false);
            if (is_noglobal)
                continue;
            log_info("    promoting clock net %s to global network\n", clock->name.c_str(ctx));
            if (is_ooc) // Don't actually do anything in OOC mode, global routing will be done in the full design
                clock->is_global = true;
            else
                insert_dcc(clock);
        }
        // Insert DCCs on DCS inputs, too
        std::vector<CellInfo *> dcsc_cells;
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type == id_DCSC)
                dcsc_cells.push_back(ci);
        }
        for (auto ci : dcsc_cells) {
            for (auto port : {id_CLK0, id_CLK1}) {
                NetInfo *net = ci->getPort(port);
                if (net != nullptr)
                    insert_dcc(net, ci);
            }
        }
    }

    void route_globals()
    {
        log_info("Routing globals...\n");
        std::set<int> all_globals, fab_globals;
        for (int i = 0; i < 16; i++) {
            all_globals.insert(i);
            if (i < 8)
                fab_globals.insert(i);
        }
        std::vector<std::pair<PortRef *, int>> toroute;
        dict<int, NetInfo *> clocks;
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type.in(id_DCCA, id_DCSC)) {
                NetInfo *clock = ci->ports.at((ci->type == id_DCSC) ? id_DCSOUT : id_CLKO).net;
                NPNR_ASSERT(clock != nullptr);
                bool drives_fabric = false;
                for (auto &usr : clock->users) {
                    if (!is_clock_port(usr)) {
                        drives_fabric = true;
                        break;
                    }
                }
                int glbid;
                if (drives_fabric) {
                    if (fab_globals.empty())
                        continue;
                    glbid = *(fab_globals.begin());
                } else {
                    glbid = *(all_globals.begin());
                }
                all_globals.erase(glbid);
                fab_globals.erase(glbid);

                log_info("    routing clock net %s using global %d\n", clock->name.c_str(ctx), glbid);
                bool routed = route_onto_global(clock, glbid);
                NPNR_ASSERT(routed);

                // WCK must have routing priority
                for (auto &user : clock->users)
                    toroute.emplace_back(&user, glbid);
                clocks[glbid] = clock;
            }
        }
        std::sort(toroute.begin(), toroute.end(),
                  [this](const std::pair<PortRef *, int> &a, const std::pair<PortRef *, int> &b) {
                      return global_route_priority(*a.first) < global_route_priority(*b.first);
                  });
        for (const auto &user : toroute) {
            if (user.first->cell->type == id_DCSC && (user.first->port.in(id_CLK0, id_CLK1))) {
                // Special case, skips most of the typical global network
                NetInfo *net = clocks.at(user.second);
                simple_router(net, ctx->getNetinfoSourceWire(net), ctx->getNetinfoSinkWire(net, *(user.first), 0));
                continue;
            }
            route_logic_tile_global(clocks.at(user.second), user.second, *user.first);
        }
    }

    void route_eclk_sources()
    {
        // Try and use dedicated paths if possible
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type.in(id_ECLKSYNCB, id_TRELLIS_ECLKBUF, id_ECLKBRIDGECS)) {
                std::vector<IdString> pins;
                if (ci->type.in(id_ECLKSYNCB, id_TRELLIS_ECLKBUF)) {
                    pins.push_back(id_ECLKI);
                } else {
                    pins.push_back(id_CLK0);
                    pins.push_back(id_CLK1);
                }
                for (auto pin : pins) {
                    NetInfo *ni = ci->getPort(pin);
                    if (ni == nullptr)
                        continue;
                    log_info("    trying dedicated routing for edge clock source %s\n", ctx->nameOf(ni));
                    WireId src = ctx->getNetinfoSourceWire(ni);
                    WireId dst = ctx->getBelPinWire(ci->bel, pin);
                    std::queue<WireId> visit;
                    dict<WireId, PipId> backtrace;
                    visit.push(dst);
                    int iter = 0;
                    WireId cursor;
                    bool success = false;
                    // This is a best-effort pass, if it fails then still try general routing later
                    const int iter_max = 1000;
                    while (iter < iter_max && !visit.empty()) {
                        cursor = visit.front();
                        visit.pop();
                        ++iter;
                        NetInfo *bound = ctx->getBoundWireNet(cursor);
                        if (bound != nullptr) {
                            if (bound == ni) {
                                success = true;
                                break;
                            } else {
                                continue;
                            }
                        }
                        if (cursor == src) {
                            ctx->bindWire(cursor, ni, STRENGTH_LOCKED);
                            success = true;
                            break;
                        }
                        for (auto uh : ctx->getPipsUphill(cursor)) {
                            if (!ctx->checkPipAvail(uh))
                                continue;
                            WireId src = ctx->getPipSrcWire(uh);
                            if (backtrace.count(src))
                                continue;
                            IdString basename = ctx->get_wire_basename(src);
                            // "ECLKCIB" wires are the junction with general routing
                            if (basename.str(ctx).find("ECLKCIB") != std::string::npos)
                                continue;
                            visit.push(src);
                            backtrace[src] = uh;
                        }
                    }
                    if (success) {
                        while (cursor != dst) {
                            PipId pip = backtrace.at(cursor);
                            ctx->bindPip(pip, ni, STRENGTH_LOCKED);
                            cursor = ctx->getPipDstWire(pip);
                        }
                    } else {
                        log_info("        no route found, general routing will be used.\n");
                    }
                }
            }
        }
    }
};
void promote_ecp5_globals(Context *ctx) { Ecp5GlobalRouter(ctx).promote_globals(); }
void route_ecp5_globals(Context *ctx)
{
    Ecp5GlobalRouter router(ctx);
    router.route_globals();
    router.route_eclk_sources();
}

NEXTPNR_NAMESPACE_END
