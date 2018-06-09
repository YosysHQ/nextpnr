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

#include <queue>

#include "log.h"
#include "route.h"

struct QueuedWire
{
    WireId wire;
    PipId pip;
    DelayInfo delay;
};

namespace std {
template <> struct greater<QueuedWire>
{
    bool operator()(const QueuedWire &lhs, const QueuedWire &rhs) const noexcept
    {
        return lhs.delay.avgDelay() > rhs.delay.avgDelay();
    }
};
}

void route_design(Design *design)
{
    auto &chip = design->chip;

    for (auto &net_it : design->nets) {
        auto net_name = net_it.first;
        auto net_info = net_it.second;

        if (net_info->driver.cell == nullptr)
            continue;

        log("Routing net %s.\n", net_name.c_str());

        log("  Source: %s.%s.\n", net_info->driver.cell->name.c_str(),
            net_info->driver.port.c_str());

        auto src_bel = net_info->driver.cell->bel;

        if (src_bel == BelId())
            log_error("Source cell is not mapped to a bel.\n");

        log("    Source bel: %s\n", chip.getBelName(src_bel).c_str());

        auto src_wire = chip.getWireBelPin(
                src_bel, portPinFromId(net_info->driver.port));

        if (src_wire == WireId())
            log_error("No wire found for port %s on source bel.\n",
                      net_info->driver.port.c_str());

        log("    Source wire: %s\n", chip.getWireName(src_wire).c_str());

        dict<WireId, DelayInfo> src_wires;
        src_wires[src_wire] = DelayInfo();
        net_info->wires[src_wire] = PipId();
        chip.bindWire(src_wire, net_name);

        for (auto &user_it : net_info->users) {
            log("  Route to: %s.%s.\n", user_it.cell->name.c_str(),
                user_it.port.c_str());

            auto dst_bel = user_it.cell->bel;

            if (dst_bel == BelId())
                log_error("Destination cell is not mapped to a bel.\n");

            log("    Destination bel: %s\n", chip.getBelName(dst_bel).c_str());

            auto dst_wire =
                    chip.getWireBelPin(dst_bel, portPinFromId(user_it.port));

            if (dst_wire == WireId())
                log_error("No wire found for port %s on destination bel.\n",
                          user_it.port.c_str());

            log("    Destination wire: %s\n",
                chip.getWireName(dst_wire).c_str());

            dict<WireId, QueuedWire> visited;
            std::priority_queue<QueuedWire, std::vector<QueuedWire>,
                                std::greater<QueuedWire>>
                    queue;

            for (auto &it : src_wires) {
                QueuedWire qw;
                qw.wire = it.first;
                qw.pip = PipId();
                qw.delay = it.second;

                queue.push(qw);
                visited[qw.wire] = qw;
            }

            while (!queue.empty()) {
                QueuedWire qw = queue.top();
                queue.pop();

                for (auto pip : chip.getPipsDownhill(qw.wire)) {
                    if (!chip.checkPipAvail(pip))
                        continue;

                    WireId next_wire = chip.getPipDstWire(pip);

                    if (visited.count(next_wire) ||
                        !chip.checkWireAvail(next_wire))
                        continue;

                    QueuedWire next_qw;
                    next_qw.wire = next_wire;
                    next_qw.pip = pip;
                    next_qw.delay = qw.delay + chip.getPipDelay(pip);
                    visited[next_qw.wire] = next_qw;
                    queue.push(next_qw);

                    if (next_qw.wire == dst_wire) {
                        std::priority_queue<QueuedWire, std::vector<QueuedWire>,
                                            std::greater<QueuedWire>>
                                empty_queue;
                        std::swap(queue, empty_queue);
                        break;
                    }
                }
            }

            if (visited.count(dst_wire) == 0)
                log_error("Failed to route %s -> %s.\n",
                          chip.getWireName(src_wire).c_str(),
                          chip.getWireName(dst_wire).c_str());

            log("      Route (from destination to source):\n");

            WireId cursor = dst_wire;

            while (1) {
                log("      %8.2f %s\n", visited[cursor].delay.avgDelay(),
                    chip.getWireName(cursor).c_str());

                if (src_wires.count(cursor))
                    break;

                net_info->wires[cursor] = visited[cursor].pip;
                chip.bindWire(cursor, net_name);
                chip.bindPip(visited[cursor].pip, net_name);

                src_wires[cursor] = visited[cursor].delay;
                cursor = chip.getPipSrcWire(visited[cursor].pip);
            }
        }
    }
}
