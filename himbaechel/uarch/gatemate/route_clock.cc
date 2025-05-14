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

    auto feeds_clk_port = [](PortRef &port) {
        return port.cell->type.in(id_CPE_HALF, id_CPE_HALF_L, id_CPE_HALF_U) && port.port.in(id_CLK);
    };

    auto pip_plane = [&](PipId pip) {
        const auto &extra_data =
                *reinterpret_cast<const GateMatePipExtraDataPOD *>(chip_pip_info(ctx->chip_info, pip).extra_data.get());
        if (extra_data.type != PipExtra::PIP_EXTRA_MUX)
            return uint8_t{0};
        return extra_data.plane;
    };

    log_info("Routing clock nets...\n");

    dict<WireId, IdString> reserved_wires;

    for (auto &net : ctx->nets) {
        NetInfo *glb_net = net.second.get();
        if (!glb_net->driver.cell || glb_net->driver.cell->type != id_BUFG)
            continue;

        if (ctx->debug)
            log_info("    reserving net '%s'\n", glb_net->name.c_str(ctx));

        bool is_global_clk = false;
        for (auto &usr : glb_net->users) {
            if (!feeds_clk_port(usr))
                continue;

            is_global_clk = true;

            auto sink_wire = ctx->getNetinfoSinkWire(glb_net, usr, 0);
            if (ctx->debug) {
                auto sink_wire_name = "(uninitialized)";
                if (sink_wire != WireId())
                    sink_wire_name = ctx->nameOfWire(sink_wire);
                log_info("        reserving wire %s\n", sink_wire_name);
            }

            reserved_wires.insert({sink_wire, glb_net->name});
        }

        if (is_global_clk)
            clk_nets.push_back(glb_net);
    }

    for (auto glb_net : clk_nets) {
        log_info("    routing net '%s'\n", glb_net->name.c_str(ctx));
        ctx->bindWire(ctx->getNetinfoSourceWire(glb_net), glb_net, STRENGTH_LOCKED);

        auto bufg_idx = ctx->getBelLocation(glb_net->driver.cell->bel).z;

        for (auto &usr : glb_net->users) {
            std::priority_queue<QueuedWire, std::vector<QueuedWire>, std::greater<QueuedWire>> visit;
            dict<WireId, PipId> backtrace;
            WireId dest = WireId();
            // skip arcs that are not part of lowskew routing
            if (!feeds_clk_port(usr))
                continue;

            auto cpe_loc = ctx->getBelLocation(usr.cell->bel);

            auto sink_wire = ctx->getNetinfoSinkWire(glb_net, usr, 0);
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
                if (curr.wire == ctx->getNetinfoSourceWire(glb_net)) {
                    if (ctx->debug)
                        log_info("            (%.3fns)\n", curr.delay);
                    dest = curr.wire;
                    break;
                }

                PipId bound_pip;
                auto fnd_wire = glb_net->wires.find(curr.wire);
                if (fnd_wire != glb_net->wires.end()) {
                    bound_pip = fnd_wire->second.pip;
                }

                for (auto uh : ctx->getPipsUphill(curr.wire)) {
                    if (!ctx->checkPipAvailForNet(uh, glb_net))
                        continue;
                    WireId src = ctx->getPipSrcWire(uh);
                    if (backtrace.count(src))
                        continue;
                    if (!ctx->checkWireAvail(src) && ctx->getBoundWireNet(src) != glb_net)
                        continue;
                    if (bound_pip != PipId() && uh != bound_pip)
                        continue;
                    // Has this wire been reserved for another net?
                    auto reserved = reserved_wires.find(src);
                    if (reserved != reserved_wires.end() && reserved->second != glb_net->name)
                        continue;
                    auto pip_loc = ctx->getPipLocation(uh);
                    // Use only a specific plane to minimise congestion.
                    if ((pip_loc.x != cpe_loc.x || pip_loc.y != cpe_loc.y) && pip_plane(uh) != (9 + bufg_idx))
                        continue;
                    backtrace[src] = uh;
                    auto delay = ctx->getDelayNS(ctx->getPipDelay(uh).maxDelay() + ctx->getWireDelay(src).maxDelay() +
                                                 ctx->getDelayEpsilon());
                    visit.push(QueuedWire(src, curr.delay + delay));
                }
            }
            if (dest == WireId()) {
                log_info("            failed to find a route using dedicated resources. %s -> %s\n",
                         glb_net->driver.cell->name.c_str(ctx), usr.cell->name.c_str(ctx));
            }
            while (backtrace.count(dest)) {
                auto uh = backtrace[dest];
                dest = ctx->getPipDstWire(uh);
                if (ctx->getBoundWireNet(dest) == glb_net) {
                    NPNR_ASSERT(glb_net->wires.at(dest).pip == uh);
                    if (ctx->debug)
                        log_info("                 pip %s --> %s (plane %hhd)\n", ctx->nameOfPip(uh),
                                 ctx->nameOfWire(dest), pip_plane(uh));
                } else if (ctx->getBoundWireNet(dest) == nullptr) {
                    ctx->bindPip(uh, glb_net, STRENGTH_LOCKED);
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
