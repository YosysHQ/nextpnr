/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
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

#include <functional>
#include <queue>

#include "gatemate.h"
#include "log.h"
#include "nextpnr_assertions.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {

struct QueuedWire
{
    explicit QueuedWire(WireId wire, float delay = 0.0) : wire{wire}, delay{delay} {};

    WireId wire;
    float delay;

    bool operator>(const QueuedWire &rhs) const { return this->delay > rhs.delay; }
};

} // namespace

void GateMateImpl::route_clock()
{
    auto clk_nets = std::vector<NetInfo *>{};
    auto reserved_wires = dict<WireId, IdString>{};

    auto feeds_clk_port = [](PortRef &port) {
        return port.cell->type.in(id_CPE_LT, id_CPE_LT_L, id_CPE_LT_U) && port.port.in(id_CLK);
    };

    auto feeds_ddr_port = [&](NetInfo *net, PortRef &port) {
        return this->ddr_nets.find(net->name) != this->ddr_nets.end() && port.port == id_IN1;
    };

    auto pip_plane = [&](PipId pip) {
        const auto &extra_data =
                *reinterpret_cast<const GateMatePipExtraDataPOD *>(chip_pip_info(ctx->chip_info, pip).extra_data.get());
        if (extra_data.type != PipExtra::PIP_EXTRA_MUX)
            return uint8_t{0};
        return extra_data.plane;
    };

    auto reserve = [&](WireId wire, NetInfo *net) {
        if (ctx->debug) {
            auto wire_name = "(uninitialized)";
            if (wire != WireId())
                wire_name = ctx->nameOfWire(wire);
            log_info("        reserving wire %s\n", wire_name);
        }
        reserved_wires.insert({wire, net->name});
    };

    log_info("Routing clock nets...\n");

    for (auto &net_pair : ctx->nets) {
        NetInfo *net = net_pair.second.get();
        if (!net->driver.cell)
            continue;

        bool is_clk_net = false;
        for (auto &usr : net->users) {
            if (feeds_clk_port(usr) || feeds_ddr_port(net, usr)) {
                is_clk_net = true;

                for (auto clk_sink_wire : ctx->getNetinfoSinkWires(net, usr))
                    reserve(clk_sink_wire, net);

                auto reserve_port_if_needed = [&](IdString port_name) {
                    auto port = usr.cell->ports.find(port_name);
                    if (port != usr.cell->ports.end() && port->second.net != nullptr) {
                        auto sink_wire = ctx->getNetinfoSinkWire(port->second.net,
                                                                 port->second.net->users.at(port->second.user_idx), 0);
                        reserve(sink_wire, port->second.net);
                    }
                };

                reserve_port_if_needed(id_EN);
                reserve_port_if_needed(id_SR);
            }
        }

        if (is_clk_net)
            clk_nets.push_back(net);
    }

    for (auto clk_net : clk_nets) {
        log_info("    routing net '%s'\n", clk_net->name.c_str(ctx));
        ctx->bindWire(ctx->getNetinfoSourceWire(clk_net), clk_net, STRENGTH_LOCKED);

        auto clk_plane = 0;
        switch (clk_net->driver.port.index) {
        case id_GLB0.index:
            clk_plane = 9;
            break;
        case id_GLB1.index:
            clk_plane = 10;
            break;
        case id_GLB2.index:
            clk_plane = 11;
            break;
        case id_GLB3.index:
            clk_plane = 12;
            break;
        }

        for (auto &usr : clk_net->users) {
            std::priority_queue<QueuedWire, std::vector<QueuedWire>, std::greater<QueuedWire>> visit;
            dict<WireId, PipId> backtrace;
            WireId dest = WireId();

            if (!feeds_clk_port(usr) && !feeds_ddr_port(clk_net, usr))
                continue;

            auto cpe_loc = ctx->getBelLocation(usr.cell->bel);
            auto is_glb_clk = clk_net->driver.cell->type == id_GLBOUT;

            auto sink_wire = ctx->getNetinfoSinkWire(clk_net, usr, 0);
            if (ctx->debug) {
                auto sink_wire_name = "(uninitialized)";
                if (sink_wire != WireId())
                    sink_wire_name = ctx->nameOfWire(sink_wire);
                log_info("        routing arc to %s.%s (wire %s):\n", usr.cell->name.c_str(ctx), usr.port.c_str(ctx),
                         sink_wire_name);
            }
            visit.push(QueuedWire(sink_wire));
            while (!visit.empty()) {
                QueuedWire curr = visit.top();
                visit.pop();
                if (curr.wire == ctx->getNetinfoSourceWire(clk_net)) {
                    if (ctx->debug)
                        log_info("            (%.3fns)\n", curr.delay);
                    dest = curr.wire;
                    break;
                }

                PipId bound_pip;
                auto fnd_wire = clk_net->wires.find(curr.wire);
                if (fnd_wire != clk_net->wires.end()) {
                    bound_pip = fnd_wire->second.pip;
                }

                for (auto uh : ctx->getPipsUphill(curr.wire)) {
                    if (!ctx->checkPipAvailForNet(uh, clk_net))
                        continue;
                    WireId src = ctx->getPipSrcWire(uh);
                    if (backtrace.count(src))
                        continue;
                    if (!ctx->checkWireAvail(src) && ctx->getBoundWireNet(src) != clk_net)
                        continue;
                    if (bound_pip != PipId() && uh != bound_pip)
                        continue;
                    // Has this wire been reserved for another net?
                    auto reserved = reserved_wires.find(src);
                    if (reserved != reserved_wires.end() && reserved->second != clk_net->name)
                        continue;
                    auto pip_loc = ctx->getPipLocation(uh);
                    // Use only a specific plane to minimise congestion for global clocks.
                    if (is_glb_clk && (pip_loc.x != cpe_loc.x || pip_loc.y != cpe_loc.y)) {
                        // Plane 9 is the clock plane, so it should only ever use itself.
                        if (clk_plane == 9 && pip_plane(uh) != 9)
                            continue;
                        // Plane 10 is the enable plane.
                        // When there's a set/reset, we want to use the switchbox X23 pip to change directly to plane 9.
                        if (clk_plane == 10 && pip_plane(uh) != 9 && pip_plane(uh) != 10)
                            continue;
                        // Plane 11 is the set/reset plane; we want to use the switchbox X14 pip to go to plane 12, then
                        // use the IM to switch to plane 9.
                        if (clk_plane == 11 && pip_plane(uh) == 10)
                            continue;
                        // Plane 12 is the spare plane; we can use the IM to change directly to plane 9.
                        if (clk_plane == 12 && pip_plane(uh) != 9 && pip_plane(uh) != 12)
                            continue;
                    }
                    backtrace[src] = uh;
                    auto delay = ctx->getDelayNS(ctx->getPipDelay(uh).maxDelay() + ctx->getWireDelay(src).maxDelay() +
                                                 ctx->getDelayEpsilon());
                    visit.push(QueuedWire(src, curr.delay + delay));
                }
            }
            if (dest == WireId()) {
                log_info("            failed to find a route using dedicated resources. %s -> %s\n",
                         clk_net->driver.cell->name.c_str(ctx), usr.cell->name.c_str(ctx));
            }
            while (backtrace.count(dest)) {
                auto uh = backtrace[dest];
                dest = ctx->getPipDstWire(uh);
                if (ctx->getBoundWireNet(dest) == clk_net) {
                    NPNR_ASSERT(clk_net->wires.at(dest).pip == uh);
                    if (ctx->debug)
                        log_info("                 pip %s --> %s (plane %hhd)\n", ctx->nameOfPip(uh),
                                 ctx->nameOfWire(dest), pip_plane(uh));
                } else if (ctx->getBoundWireNet(dest) == nullptr) {
                    ctx->bindPip(uh, clk_net, STRENGTH_LOCKED);
                    if (ctx->debug)
                        log_info("            bind pip %s --> %s (plane %hhd)\n", ctx->nameOfPip(uh),
                                 ctx->nameOfWire(dest), pip_plane(uh));
                } else {
                    log_error("Can't bind pip %s because wire %s is already bound\n", ctx->nameOfPip(uh),
                              ctx->nameOfWire(dest));
                }
                if (dest == sink_wire)
                    break;
            }
        }
    }
}

NEXTPNR_NAMESPACE_END
