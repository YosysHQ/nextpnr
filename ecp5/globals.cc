/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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
#include <queue>
#include "nextpnr.h"
#include "cells.h"

#include "log.h"

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
        if (user.cell->type == ctx->id("TRELLIS_LC") && user.port == ctx->id("CLK"))
            return true;
        return false;
    }

    std::vector<NetInfo *> get_clocks()
    {
        std::unordered_map<IdString, int> clockCount;
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            clockCount[ni->name] = 0;
            for (const auto &user : ni->users) {
                if (is_clock_port(user))
                    clockCount[ni->name]++;
            }
        }
        std::vector<NetInfo *> clocks;
        while (clocks.size() < 16) {
            auto max = std::max_element(clockCount.begin(), clockCount.end(),
                                        [](const decltype(clockCount)::value_type &a,
                                           const decltype(clockCount)::value_type &b) { return a.second < b.second; });
            if (max == clockCount.end() || max->second < 3)
                break;
            clocks.push_back(ctx->nets.at(max->first).get());
            clockCount.erase(max->first);
        }
        return clocks;
    }

    PipId find_tap_pip(WireId tile_glb)
    {
        std::string wireName = ctx->getWireBasename(tile_glb).str(ctx);
        std::string glbName = wireName.substr(2);
        TapDirection td = ctx->globalInfoAtLoc(tile_glb.location).tap_dir;
        WireId tap_wire;
        Location tap_loc;
        tap_loc.x = ctx->globalInfoAtLoc(tile_glb.location).tap_col;
        tap_loc.y = tile_glb.location.y;
        if (td == TAP_DIR_LEFT) {
            tap_wire = ctx->getWireByLocAndBasename(tap_loc, "L_" + glbName);
        } else {
            tap_wire = ctx->getWireByLocAndBasename(tap_loc, "R_" + glbName);
        }
        return *(ctx->getPipsUphill(tap_wire).begin());
    }

    void route_logic_tile_global(NetInfo *net, int global_index, PortRef user)
    {
        WireId userWire = ctx->getBelPinWire(user.cell->bel, ctx->portPinFromId(user.port));
        WireId globalWire;
        IdString global_name = ctx->id(fmt_str("G_HPBX" << std::setw(2) << std::setfill('0') << global_index << "00"));
        std::queue<WireId> upstream;
        std::unordered_map<WireId, PipId> backtrace;
        upstream.push(userWire);
        bool already_routed = false;
        // Search back from the pin until we reach the global network
        while (true) {
            WireId next = upstream.front();
            upstream.pop();

            if (ctx->getBoundWireNet(next) == net) {
                already_routed = true;
                globalWire = next;
                break;
            }

            if (ctx->getWireBasename(next) == global_name) {
                globalWire = next;
                break;
            }
            if (ctx->checkWireAvail(next)) {
                for (auto pip : ctx->getPipsUphill(next)) {
                    WireId src = ctx->getPipSrcWire(pip);
                    backtrace[src] = pip;
                    upstream.push(src);
                }
            }
            if (upstream.size() > 3000) {
                log_error("failed to route HPBX%02d00 to %s.%s\n", global_index,
                          ctx->getBelName(user.cell->bel).c_str(ctx), user.port.c_str(ctx));
            }
        }
        // Set all the pips we found along the way
        WireId cursor = userWire;
        while (true) {
            auto fnd = backtrace.find(cursor);
            if (fnd == backtrace.end())
                break;
            ctx->bindPip(fnd->second, net, STRENGTH_LOCKED);
            cursor = ctx->getPipSrcWire(fnd->second);
        }
        // If the global network inside the tile isn't already set up,
        // we also need to bind the buffers along the way
        if (!already_routed) {
            ctx->bindWire(cursor, net, STRENGTH_LOCKED);
            PipId tap_pip = find_tap_pip(cursor);
            NetInfo *tap_net = ctx->getBoundPipNet(tap_pip);
            if (tap_net == nullptr) {
                ctx->bindPip(tap_pip, net, STRENGTH_LOCKED);
                // TODO: SPINE
            } else {
                NPNR_ASSERT(tap_net == net);
            }
        }
    }

    bool is_global_io(CellInfo *io, std::string &glb_name)
    {
        std::string func_name = ctx->getPioFunctionName(io->bel);
        if (func_name.substr(0, 5) == "PCLKT") {
            func_name.erase(func_name.find('_'), 1);
            glb_name = "G_" + func_name;
            return true;
        }
        return false;
    }

    WireId get_global_wire(GlobalQuadrant quad, int network)
    {
        return ctx->getWireByLocAndBasename(Location(0, 0), get_quad_name(quad) + "PCLK" + std::to_string(network));
    }

    void simple_router(NetInfo *net, WireId src, WireId dst)
    {
        std::queue<WireId> visit;
        std::unordered_map<WireId, PipId> backtrace;
        visit.push(src);
        WireId cursor;
        while (true) {
            if (visit.empty() || visit.size() > 50000) {
                log_error("cannot route global from %s to %s.\n", ctx->getWireName(src).c_str(ctx),
                          ctx->getWireName(dst).c_str(ctx));
            }
            cursor = visit.back();
            visit.pop();
            NetInfo *bound = ctx->getBoundWireNet(cursor);
            if (bound == net) {
                break;
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
            ctx->bindPip(fnd->second, net, STRENGTH_LOCKED);
            cursor = ctx->getPipSrcWire(fnd->second);
        }
    }

    void route_onto_global(NetInfo *net, int network)
    {
        WireId glb_src;
        if (net->driver.cell->type == ctx->id("TRELLIS_IO")) {
            std::string ioglb;
            if (!is_global_io(net->driver.cell, ioglb))
                goto non_dedicated;
            glb_src = ctx->getWireByLocAndBasename(Location(0, 0), ioglb);
        }
        for (int quad = QUAD_UL; quad < QUAD_LR + 1; quad++) {
            WireId glb_dst = get_global_wire(GlobalQuadrant(quad), network);
            simple_router(net, glb_src, glb_dst);
        }
        if (false) {
        non_dedicated:
            log_error("FIXME: currenly global networks can only be driven by dedicated global input pins");
        }
    }


    // Insert a DCC into a net to promote it to a global
    NetInfo *insert_dcc(NetInfo *net)
    {
        auto dcc = create_ecp5_cell(ctx, ctx->id("DCCA"), "$gbuf$" + net->name.str(ctx));

        std::unique_ptr<NetInfo> glbnet = std::unique_ptr<NetInfo>(new NetInfo);
        glbnet->name = ctx->id("$glbnet$" + net->name.str(ctx));
        glbnet->driver.cell = dcc.get();
        glbnet->driver.port = ctx->id("CLKO");

        for (auto user : net->users) {
            user.cell->ports.at(user.port).net = glbnet.get();
        }
        net->users.clear();

        dcc->ports[ctx->id("CLKI")].net = net;
        PortRef clki_pr;
        clki_pr.port = ctx->id("CLKI");
        clki_pr.cell = dcc.get();
        net->users.push_back(clki_pr);

        ctx->cells[dcc->name] = std::move(dcc);
        NetInfo *glbptr = glbnet.get();
        ctx->nets[glbnet->name] = std::move(glbnet);
        return glbptr;
    }

    Context *ctx;
};

void route_ecp5_globals(Context *ctx);

NEXTPNR_NAMESPACE_END
