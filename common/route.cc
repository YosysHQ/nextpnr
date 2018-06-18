/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
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

#include <cmath>
#include <queue>

#include "log.h"
#include "route.h"

namespace {

USING_NEXTPNR_NAMESPACE

struct QueuedWire
{
    WireId wire;
    PipId pip;

    delay_t delay = 0, togo = 0;

    struct Greater
    {
        bool operator()(const QueuedWire &lhs, const QueuedWire &rhs) const
                noexcept
        {
            return (lhs.delay + lhs.togo) > (rhs.delay + rhs.togo);
        }
    };
};

void ripup_net(Design *design, IdString net_name)
{
    auto &chip = design->chip;
    auto net_info = design->nets.at(net_name);

    for (auto &it : net_info->wires) {
        if (it.second != PipId())
            chip.unbindPip(it.second);
        chip.unbindWire(it.first);
    }

    net_info->wires.clear();
}

struct Router
{
    std::unordered_set<IdString> rippedNets;
    int visitCnt = 0, revisitCnt = 0;
    bool routedOkay = false;
    delay_t maxDelay = 0.0;
    WireId failedDest;

    Router(Design *design, IdString net_name, bool verbose, bool ripup = false,
           delay_t ripup_penalty = 0)
    {
        auto &chip = design->chip;
        auto net_info = design->nets.at(net_name);

        if (verbose)
            log("Routing net %s.\n", net_name.c_str());

        if (verbose)
            log("  Source: %s.%s.\n", net_info->driver.cell->name.c_str(),
                net_info->driver.port.c_str());

        auto src_bel = net_info->driver.cell->bel;

        if (src_bel == BelId())
            log_error("Source cell %s (%s) is not mapped to a bel.\n",
                      net_info->driver.cell->name.c_str(),
                      net_info->driver.cell->type.c_str());

        if (verbose)
            log("    Source bel: %s\n", chip.getBelName(src_bel).c_str());

        IdString driver_port = net_info->driver.port;

        auto driver_port_it = net_info->driver.cell->pins.find(driver_port);
        if (driver_port_it != net_info->driver.cell->pins.end())
            driver_port = driver_port_it->second;

        auto src_wire = chip.getWireBelPin(src_bel, portPinFromId(driver_port));

        if (src_wire == WireId())
            log_error("No wire found for port %s (pin %s) on source cell %s "
                      "(bel %s).\n",
                      net_info->driver.port.c_str(), driver_port.c_str(),
                      net_info->driver.cell->name.c_str(),
                      chip.getBelName(src_bel).c_str());

        if (verbose)
            log("    Source wire: %s\n", chip.getWireName(src_wire).c_str());

        std::unordered_map<WireId, DelayInfo> src_wires;
        src_wires[src_wire] = DelayInfo();
        net_info->wires[src_wire] = PipId();
        chip.bindWire(src_wire, net_name);

        for (auto &user_it : net_info->users) {
            if (verbose)
                log("  Route to: %s.%s.\n", user_it.cell->name.c_str(),
                    user_it.port.c_str());

            auto dst_bel = user_it.cell->bel;

            if (dst_bel == BelId())
                log_error("Destination cell %s (%s) is not mapped to a bel.\n",
                          user_it.cell->name.c_str(),
                          user_it.cell->type.c_str());

            if (verbose)
                log("    Destination bel: %s\n",
                    chip.getBelName(dst_bel).c_str());

            IdString user_port = user_it.port;

            auto user_port_it = user_it.cell->pins.find(user_port);

            if (user_port_it != user_it.cell->pins.end())
                user_port = user_port_it->second;

            auto dst_wire =
                    chip.getWireBelPin(dst_bel, portPinFromId(user_port));

            if (dst_wire == WireId())
                log_error("No wire found for port %s (pin %s) on destination "
                          "cell %s (bel %s).\n",
                          user_it.port.c_str(), user_port.c_str(),
                          user_it.cell->name.c_str(),
                          chip.getBelName(dst_bel).c_str());

            if (verbose) {
                log("    Destination wire: %s\n",
                    chip.getWireName(dst_wire).c_str());
                log("    Path delay estimate: %.2f\n",
                    float(chip.estimateDelay(src_wire, dst_wire)));
            }

            std::unordered_map<WireId, QueuedWire> visited;
            std::priority_queue<QueuedWire, std::vector<QueuedWire>,
                                QueuedWire::Greater>
                    queue;

            for (auto &it : src_wires) {
                QueuedWire qw;
                qw.wire = it.first;
                qw.pip = PipId();
                qw.delay = it.second.avgDelay();
                qw.togo = chip.estimateDelay(qw.wire, dst_wire);

                queue.push(qw);
                visited[qw.wire] = qw;
            }

            while (!queue.empty() && !visited.count(dst_wire)) {
                QueuedWire qw = queue.top();
                queue.pop();

                for (auto pip : chip.getPipsDownhill(qw.wire)) {
                    delay_t next_delay = qw.delay;
                    IdString ripupNet = net_name;
                    visitCnt++;

                    if (!chip.checkPipAvail(pip)) {
                        if (!ripup)
                            continue;
                        ripupNet = chip.getPipNet(pip, true);
                        if (ripupNet == net_name)
                            continue;
                    }

                    WireId next_wire = chip.getPipDstWire(pip);
                    next_delay += chip.getPipDelay(pip).avgDelay();

                    if (!chip.checkWireAvail(next_wire)) {
                        if (!ripup)
                            continue;
                        ripupNet = chip.getWireNet(next_wire, true);
                        if (ripupNet == net_name)
                            continue;
                    }

                    if (ripupNet != net_name)
                        next_delay += ripup_penalty;
                    assert(next_delay >= 0);

                    if (visited.count(next_wire)) {
                        if (visited.at(next_wire).delay <= next_delay + 1e-3)
                            continue;
#if 0 // FIXME
                        if (verbose)
                            log("Found better route to %s. Old vs new delay "
                                "estimate: %.2f %.2f\n",
                                chip.getWireName(next_wire).c_str(),
                                float(visited.at(next_wire).delay),
                                float(next_delay));
#endif
                        revisitCnt++;
                    }

                    QueuedWire next_qw;
                    next_qw.wire = next_wire;
                    next_qw.pip = pip;
                    next_qw.delay = next_delay;
                    next_qw.togo = chip.estimateDelay(next_wire, dst_wire);
                    visited[next_qw.wire] = next_qw;
                    queue.push(next_qw);
                }
            }

            if (visited.count(dst_wire) == 0) {
                if (verbose)
                    log("Failed to route %s -> %s.\n",
                        chip.getWireName(src_wire).c_str(),
                        chip.getWireName(dst_wire).c_str());
                else if (ripup)
                    log_info("Failed to route %s -> %s.\n",
                             chip.getWireName(src_wire).c_str(),
                             chip.getWireName(dst_wire).c_str());
                ripup_net(design, net_name);
                failedDest = dst_wire;
                return;
            }

            if (verbose)
                log("    Final path delay: %.2f\n",
                    float(visited[dst_wire].delay));
            maxDelay = fmaxf(maxDelay, visited[dst_wire].delay);

            if (verbose)
                log("    Route (from destination to source):\n");

            WireId cursor = dst_wire;

            while (1) {
                if (verbose)
                    log("    %8.2f %s\n", float(visited[cursor].delay),
                        chip.getWireName(cursor).c_str());

                if (src_wires.count(cursor))
                    break;

                IdString conflicting_net = chip.getWireNet(cursor, true);

                if (conflicting_net != IdString()) {
                    assert(ripup);
                    assert(conflicting_net != net_name);
                    ripup_net(design, conflicting_net);
                    rippedNets.insert(conflicting_net);
                }

                conflicting_net = chip.getPipNet(visited[cursor].pip, true);

                if (conflicting_net != IdString()) {
                    assert(ripup);
                    assert(conflicting_net != net_name);
                    ripup_net(design, conflicting_net);
                    rippedNets.insert(conflicting_net);
                }

                net_info->wires[cursor] = visited[cursor].pip;
                chip.bindWire(cursor, net_name);
                chip.bindPip(visited[cursor].pip, net_name);

                src_wires[cursor] = chip.getPipDelay(visited[cursor].pip);
                cursor = chip.getPipSrcWire(visited[cursor].pip);
            }
        }

        routedOkay = true;
    }
};

} // namespace

NEXTPNR_NAMESPACE_BEGIN

bool route_design(Design *design, bool verbose)
{
    auto &chip = design->chip;
    delay_t ripup_penalty = 5;

    log_info("Routing..\n");

    std::unordered_set<IdString> netsQueue;

    for (auto &net_it : design->nets) {
        auto net_name = net_it.first;
        auto net_info = net_it.second;

        if (net_info->driver.cell == nullptr)
            continue;

        if (!net_info->wires.empty())
            continue;

        netsQueue.insert(net_name);
    }

    if (netsQueue.empty()) {
        log_info("found no unrouted nets. no routing necessary.\n");
        return true;
    }

    log_info("found %d unrouted nets. starting routing procedure.\n",
             int(netsQueue.size()));

    delay_t estimatedTotalDelay = 0.0;
    int estimatedTotalDelayCnt = 0;

    for (auto net_name : netsQueue) {
        auto net_info = design->nets.at(net_name);

        auto src_bel = net_info->driver.cell->bel;

        if (src_bel == BelId())
            continue;

        IdString driver_port = net_info->driver.port;

        auto driver_port_it = net_info->driver.cell->pins.find(driver_port);
        if (driver_port_it != net_info->driver.cell->pins.end())
            driver_port = driver_port_it->second;

        auto src_wire = chip.getWireBelPin(src_bel, portPinFromId(driver_port));

        if (src_wire == WireId())
            continue;

        for (auto &user_it : net_info->users) {
            auto dst_bel = user_it.cell->bel;

            if (dst_bel == BelId())
                continue;

            IdString user_port = user_it.port;

            auto user_port_it = user_it.cell->pins.find(user_port);

            if (user_port_it != user_it.cell->pins.end())
                user_port = user_port_it->second;

            auto dst_wire =
                    chip.getWireBelPin(dst_bel, portPinFromId(user_port));

            if (dst_wire == WireId())
                continue;

            estimatedTotalDelay += chip.estimateDelay(src_wire, dst_wire);
            estimatedTotalDelayCnt++;
        }
    }

    log_info("estimated total wire delay: %.2f (avg %.2f)\n",
             float(estimatedTotalDelay),
             float(estimatedTotalDelay) / estimatedTotalDelayCnt);

    int iterCnt = 0;

    while (!netsQueue.empty()) {
        if (iterCnt == 200) {
            log_info("giving up after %d iterations.\n", iterCnt);
            return false;
        }
        log_info("-- %d --\n", ++iterCnt);

        int visitCnt = 0, revisitCnt = 0, netCnt = 0;

        std::unordered_set<IdString> ripupQueue;

        log_info("routing queue contains %d nets.\n", int(netsQueue.size()));
        bool printNets = netsQueue.size() < 10;

        for (auto net_name : netsQueue) {
            if (printNets)
                log_info("  routing net %s. (%d users)\n", net_name.c_str(),
                         int(design->nets.at(net_name)->users.size()));

            Router router(design, net_name, verbose, false);

            netCnt++;
            visitCnt += router.visitCnt;
            revisitCnt += router.revisitCnt;

            if (!router.routedOkay) {
                if (printNets)
                    log_info("    failed to route to %s.\n",
                             chip.getWireName(router.failedDest).c_str());
                ripupQueue.insert(net_name);
            }

            if (!printNets && netCnt % 100 == 0)
                log_info("  processed %d nets. (%d routed, %d failed)\n",
                         netCnt, netCnt - int(ripupQueue.size()),
                         int(ripupQueue.size()));
        }

        netsQueue.clear();

        if (netCnt % 100 != 0)
            log_info("  processed %d nets. (%d routed, %d failed)\n", netCnt,
                     netCnt - int(ripupQueue.size()), int(ripupQueue.size()));
        log_info("  routing pass visited %d PIPs (%.2f%% revisits).\n",
                 visitCnt, (100.0 * revisitCnt) / visitCnt);

        if (!ripupQueue.empty()) {
            log_info("failed to route %d nets. re-routing in ripup mode.\n",
                     int(ripupQueue.size()));

            printNets = ripupQueue.size() < 10;

            visitCnt = 0;
            revisitCnt = 0;
            netCnt = 0;
            int ripCnt = 0;

            for (auto net_name : ripupQueue) {
                if (printNets)
                    log_info("  routing net %s. (%d users)\n", net_name.c_str(),
                             int(design->nets.at(net_name)->users.size()));

                Router router(design, net_name, verbose, true,
                              ripup_penalty * (iterCnt - 1));

                netCnt++;
                visitCnt += router.visitCnt;
                revisitCnt += router.revisitCnt;

                if (!router.routedOkay)
                    log_error("Net %s is impossible to route.\n",
                              net_name.c_str());

                for (auto it : router.rippedNets)
                    netsQueue.insert(it);

                if (printNets) {
                    if (router.rippedNets.size() < 10) {
                        log_info("    ripped up %d other nets:\n",
                                 int(router.rippedNets.size()));
                        for (auto n : router.rippedNets)
                            log_info("      %s (%d users)\n", n.c_str(),
                                     int(design->nets.at(n)->users.size()));
                    } else {
                        log_info("    ripped up %d other nets.\n",
                                 int(router.rippedNets.size()));
                    }
                }

                ripCnt += router.rippedNets.size();

                if (!printNets && netCnt % 100 == 0)
                    log_info("  routed %d nets, ripped %d nets.\n", netCnt,
                             ripCnt);
            }

            if (netCnt % 100 != 0)
                log_info("  routed %d nets, ripped %d nets.\n", netCnt, ripCnt);

            log_info("  routing pass visited %d PIPs (%.2f%% revisits).\n",
                     visitCnt, (100.0 * revisitCnt) / visitCnt);

            if (!netsQueue.empty())
                log_info("  ripped up %d previously routed nets. continue "
                         "routing.\n",
                         int(netsQueue.size()));
        }
    }

    log_info("routing complete after %d iterations.\n", iterCnt);
    return true;
}

NEXTPNR_NAMESPACE_END
