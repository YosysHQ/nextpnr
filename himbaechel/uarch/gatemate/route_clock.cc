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
    explicit QueuedWire(WireId wire, delay_t delay = 0) : wire{wire}, delay{delay} {};

    WireId wire;
    delay_t delay;

    bool operator>(const QueuedWire &rhs) const { return this->delay > rhs.delay; }
};

} // namespace

void GateMateImpl::route_clock()
{
    log_info("Routing clock nets...\n");
    auto rstart = std::chrono::high_resolution_clock::now();

    auto clk_nets = std::vector<NetInfo *>{};
    auto reserved_wires = dict<WireId, IdString>{};

    auto feeds_clk_port = [&](PortRef &port) {
        return (ctx->getBelBucketForCellType(port.cell->type) == id_CPE_FF) && port.port.in(id_CLK);
    };

    auto feeds_ddr_port = [&](NetInfo *net, PortRef &port) {
        return this->ddr_nets.find(net->name) != this->ddr_nets.end() && port.port == id_IN1;
    };

    auto pip_plane = [&](PipId pip) {
        const auto &extra_data = *pip_extra_data(pip);
        if (extra_data.type != PipExtra::PIP_EXTRA_MUX)
            return uint8_t{0};
        return extra_data.plane;
    };

    auto reserve = [&](WireId wire, NetInfo *net) {
        for (auto pip : ctx->getPipsUphill(wire)) {
            wire = ctx->getPipSrcWire(pip);
            break;
        }
        if (ctx->debug) {
            auto wire_name = "(uninitialized)";
            if (wire != WireId())
                wire_name = ctx->nameOfWire(wire);
            log_info("        reserving wire %s\n", wire_name);
        }
        reserved_wires.insert({wire, net->name});
    };

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
        log_info("    routing net '%s' to %d users\n", clk_net->name.c_str(ctx), clk_net->users.entries());
        auto src_wire = ctx->getNetinfoSourceWire(clk_net);
        ctx->bindWire(src_wire, clk_net, STRENGTH_LOCKED);

        auto sink_wires = dict<WireId, PortRef>{};
        auto sink_wires_to_do = pool<WireId>{};
        for (auto &usr : clk_net->users) {
            if (!feeds_clk_port(usr) && !feeds_ddr_port(clk_net, usr))
                continue;

            auto sink_wire = ctx->getNetinfoSinkWire(clk_net, usr, 0);

            sink_wires.insert({sink_wire, usr});
            sink_wires_to_do.insert(sink_wire);
        }

        std::priority_queue<QueuedWire, std::vector<QueuedWire>, std::greater<QueuedWire>> visit;
        dict<WireId, PipId> backtrace;
        dict<WireId, delay_t> delay_map;

        auto is_glb_clk = clk_net->driver.cell->type == id_GLBOUT;

        visit.push(QueuedWire(src_wire));
        while (!visit.empty()) {
            QueuedWire curr = visit.top();
            visit.pop();
            if (sink_wires_to_do.count(curr.wire)) {
                if (ctx->debug) {
                    auto sink_wire_name = "(uninitialized)";
                    if (curr.wire != WireId())
                        sink_wire_name = ctx->nameOfWire(curr.wire);
                    log_info("            -> %s (%.3fns)\n", sink_wire_name, ctx->getDelayNS(curr.delay));
                }
                sink_wires_to_do.erase(curr.wire);
                if (sink_wires_to_do.empty())
                    break;
            }

            for (auto dh : ctx->getPipsDownhill(curr.wire)) {
                if (!ctx->checkPipAvailForNet(dh, clk_net))
                    continue;
                WireId dst = ctx->getPipDstWire(dh);
                if (!ctx->checkWireAvail(dst) && ctx->getBoundWireNet(dst) != clk_net)
                    continue;
                // Has this wire been reserved for another net?
                auto reserved = reserved_wires.find(dst);
                if (reserved != reserved_wires.end() && reserved->second != clk_net->name)
                    continue;
                auto delay = curr.delay + ctx->getPipDelay(dh).maxDelay() + ctx->getWireDelay(dst).maxDelay() +
                             ctx->getDelayEpsilon();
                if (backtrace.count(dst) && delay_map.at(dst) <= delay)
                    continue;
                delay_map[dst] = delay;
                backtrace[dst] = dh;
                visit.push(QueuedWire(dst, delay));
            }
        }
        for (auto sink_wire : sink_wires_to_do) {
            log_info("            failed to find a route using dedicated resources. %s -> %s\n",
                     clk_net->driver.cell->name.c_str(ctx), ctx->nameOfWire(sink_wire));
        }
        for (auto pair : sink_wires) {
            auto sink_wire = pair.first;
            auto usr = pair.second;
            auto src = sink_wire;

            if (ctx->debug) {
                auto sink_wire_name = "(uninitialized)";
                if (sink_wire != WireId())
                    sink_wire_name = ctx->nameOfWire(sink_wire);
                log_info("        routing arc to %s.%s (wire %s):\n", usr.cell->name.c_str(ctx), usr.port.c_str(ctx),
                         sink_wire_name);
            }

            while (backtrace.count(src)) {
                auto uh = backtrace[src];
                if (ctx->getBoundWireNet(src) == clk_net) {
                    if (ctx->debug)
                        log_info("                 pip %s --> %s (plane %hhd)\n", ctx->nameOfPip(uh),
                                 ctx->nameOfWire(src), pip_plane(uh));
                } else if (ctx->getBoundWireNet(src) == nullptr) {
                    if (ctx->debug)
                        log_info("            bind pip %s --> %s (plane %hhd)\n", ctx->nameOfPip(uh),
                                 ctx->nameOfWire(src), pip_plane(uh));
                    ctx->bindPip(uh, clk_net, is_glb_clk ? STRENGTH_LOCKED : STRENGTH_WEAK);
                } else {
                    log_error("Can't bind pip %s because wire %s is already bound\n", ctx->nameOfPip(uh),
                              ctx->nameOfWire(src));
                }
                if (src == src_wire)
                    break;
                src = ctx->getPipSrcWire(uh);
            }
        }
    }
    auto rend = std::chrono::high_resolution_clock::now();
    log_info("Clock router time %.02fs\n", std::chrono::duration<float>(rend - rstart).count());
}

NEXTPNR_NAMESPACE_END
