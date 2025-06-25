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
#include "util.h"

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

void GateMateImpl::route_mults()
{
    auto mult_nets = std::vector<NetInfo *>{};
    auto reserved_wires = dict<WireId, IdString>{};

    auto feeds_multiplier = [](PortRef &port) {
        return port.cell->type.in(id_CPE_HALF, id_CPE_HALF_L, id_CPE_HALF_U) && int_or_default(port.cell->params, id_C_FUNCTION) == C_MULT;
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

    log_info("Routing multiplier connections...\n");

    for (auto &net_pair : ctx->nets) {
        NetInfo *net = net_pair.second.get();
        if (!net->driver.cell)
            continue;

        bool is_mult_net = false;
        for (auto &usr : net->users) {
            if (feeds_multiplier(usr)) {
                is_mult_net = true;

                for (auto sink_wire : ctx->getNetinfoSinkWires(net, usr))
                    reserve(sink_wire, net);
            }
        }

        if (is_mult_net)
            mult_nets.push_back(net);
    }

    for (auto mult_net : mult_nets) {
        log_info("    routing net '%s'\n", mult_net->name.c_str(ctx));
        ctx->bindWire(ctx->getNetinfoSourceWire(mult_net), mult_net, STRENGTH_LOCKED);

        for (auto &usr : mult_net->users) {
            std::priority_queue<QueuedWire, std::vector<QueuedWire>, std::greater<QueuedWire>> visit;
            dict<WireId, PipId> backtrace;
            WireId dest = WireId();

            if (!feeds_multiplier(usr))
                continue;

            auto sink_wire = ctx->getNetinfoSinkWire(mult_net, usr, 0);
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
                if (curr.wire == ctx->getNetinfoSourceWire(mult_net)) {
                    if (ctx->debug)
                        log_info("            (%.3fns)\n", curr.delay);
                    dest = curr.wire;
                    break;
                }

                PipId bound_pip;
                auto fnd_wire = mult_net->wires.find(curr.wire);
                if (fnd_wire != mult_net->wires.end()) {
                    bound_pip = fnd_wire->second.pip;
                }

                for (auto uh : ctx->getPipsUphill(curr.wire)) {
                    if (!ctx->checkPipAvailForNet(uh, mult_net))
                        continue;
                    WireId src = ctx->getPipSrcWire(uh);
                    if (backtrace.count(src))
                        continue;
                    if (!ctx->checkWireAvail(src) && ctx->getBoundWireNet(src) != mult_net)
                        continue;
                    if (bound_pip != PipId() && uh != bound_pip)
                        continue;
                    // Has this wire been reserved for another net?
                    auto reserved = reserved_wires.find(src);
                    if (reserved != reserved_wires.end() && reserved->second != mult_net->name)
                        continue;
                    backtrace[src] = uh;
                    auto delay = ctx->getDelayNS(ctx->getPipDelay(uh).maxDelay() + ctx->getWireDelay(src).maxDelay() +
                                                 ctx->getDelayEpsilon());
                    visit.push(QueuedWire(src, curr.delay + delay));
                }
            }
            if (dest == WireId()) {
                log_info("            failed to find a route using dedicated resources. %s -> %s\n",
                         mult_net->driver.cell->name.c_str(ctx), usr.cell->name.c_str(ctx));
            }
            while (backtrace.count(dest)) {
                auto uh = backtrace[dest];
                dest = ctx->getPipDstWire(uh);
                if (ctx->getBoundWireNet(dest) == mult_net) {
                    NPNR_ASSERT(mult_net->wires.at(dest).pip == uh);
                    if (ctx->debug)
                        log_info("                 pip %s --> %s\n", ctx->nameOfPip(uh),
                                 ctx->nameOfWire(dest));
                } else if (ctx->getBoundWireNet(dest) == nullptr) {
                    ctx->bindPip(uh, mult_net, STRENGTH_LOCKED);
                    if (ctx->debug)
                        log_info("            bind pip %s --> %s\n", ctx->nameOfPip(uh),
                                 ctx->nameOfWire(dest));
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
