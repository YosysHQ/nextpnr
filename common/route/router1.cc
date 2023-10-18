/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
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

#include <chrono>
#include <cmath>
#include <queue>

#include "log.h"
#include "router1.h"
#include "scope_lock.h"
#include "timing.h"

namespace {

USING_NEXTPNR_NAMESPACE

struct arc_key
{
    NetInfo *net_info;
    // logical user cell port index
    store_index<PortRef> user_idx;
    // physical index into cell->bel pin mapping (usually 0)
    unsigned phys_idx;

    bool operator==(const arc_key &other) const
    {
        return (net_info == other.net_info) && (user_idx == other.user_idx) && (phys_idx == other.phys_idx);
    }
    bool operator<(const arc_key &other) const
    {
        return net_info == other.net_info
                       ? (user_idx == other.user_idx ? phys_idx < other.phys_idx : user_idx < other.user_idx)
                       : net_info->name < other.net_info->name;
    }

    unsigned int hash() const
    {
        std::size_t seed = std::hash<NetInfo *>()(net_info);
        seed ^= user_idx.hash() + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= std::hash<int>()(phys_idx) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct arc_entry
{
    arc_key arc;
    delay_t pri;
    int randtag = 0;

    struct Less
    {
        bool operator()(const arc_entry &lhs, const arc_entry &rhs) const noexcept
        {
            if (lhs.pri != rhs.pri)
                return lhs.pri < rhs.pri;
            return lhs.randtag < rhs.randtag;
        }
    };
};

struct QueuedWire
{
    WireId wire;
    PipId pip;

    delay_t delay = 0, penalty = 0, bonus = 0, togo = 0;
    int randtag = 0;

    struct Greater
    {
        bool operator()(const QueuedWire &lhs, const QueuedWire &rhs) const noexcept
        {
            delay_t l = lhs.delay + lhs.penalty + lhs.togo;
            delay_t r = rhs.delay + rhs.penalty + rhs.togo;
            NPNR_ASSERT(l >= 0);
            NPNR_ASSERT(r >= 0);
            l -= lhs.bonus;
            r -= rhs.bonus;
            return l == r ? lhs.randtag > rhs.randtag : l > r;
        }
    };
};

struct Router1
{
    Context *ctx;
    const Router1Cfg &cfg;

    std::priority_queue<arc_entry, std::vector<arc_entry>, arc_entry::Less> arc_queue;
    dict<WireId, pool<arc_key>> wire_to_arcs;
    dict<arc_key, pool<WireId>> arc_to_wires;
    pool<arc_key> queued_arcs;

    std::priority_queue<QueuedWire, std::vector<QueuedWire>, QueuedWire::Greater> queue;

    dict<WireId, int> wireScores;
    dict<NetInfo *, int, hash_ptr_ops> netScores;

    int arcs_with_ripup = 0;
    int arcs_without_ripup = 0;
    bool ripup_flag;

    TimingAnalyser tmg;

    bool timing_driven = true;

    Router1(Context *ctx, const Router1Cfg &cfg) : ctx(ctx), cfg(cfg), tmg(ctx)
    {
        timing_driven = ctx->setting<bool>("timing_driven");
        tmg.setup();
        tmg.run();
    }

    void arc_queue_insert(const arc_key &arc, WireId src_wire, WireId dst_wire)
    {
        if (queued_arcs.count(arc))
            return;

        delay_t pri = (arc.net_info->constant_value == IdString())
                              ? (ctx->estimateDelay(src_wire, dst_wire) *
                                 (100 * tmg.get_criticality(CellPortKey(arc.net_info->users.at(arc.user_idx)))))
                              : 0;

        arc_entry entry;
        entry.arc = arc;
        entry.pri = pri;
        entry.randtag = ctx->rng();

#if 0
        if (ctx->debug)
            log("[arc_queue_insert] %s (%d) %s %s [%d %d]\n", ctx->nameOf(entry.arc.net_info), entry.arc.user_idx,
                ctx->nameOfWire(src_wire), ctx->nameOfWire(dst_wire), (int)entry.pri, entry.randtag);
#endif

        arc_queue.push(entry);
        queued_arcs.insert(arc);
    }

    void arc_queue_insert(const arc_key &arc)
    {
        if (queued_arcs.count(arc))
            return;

        NetInfo *net_info = arc.net_info;
        auto user_idx = arc.user_idx;
        unsigned phys_idx = arc.phys_idx;

        auto src_wire = ctx->getNetinfoSourceWire(net_info);
        auto dst_wire = ctx->getNetinfoSinkWire(net_info, net_info->users[user_idx], phys_idx);

        arc_queue_insert(arc, src_wire, dst_wire);
    }

    arc_key arc_queue_pop()
    {
        arc_entry entry = arc_queue.top();

#if 0
        if (ctx->debug)
            log("[arc_queue_pop] %s (%d) [%d %d]\n", ctx->nameOf(entry.arc.net_info), entry.arc.user_idx,
                (int)entry.pri, entry.randtag);
#endif

        arc_queue.pop();
        queued_arcs.erase(entry.arc);
        return entry.arc;
    }

    void ripup_net(NetInfo *net)
    {
        if (ctx->debug)
            log("      ripup net %s\n", ctx->nameOf(net));

        netScores[net]++;

        std::vector<WireId> wires;
        for (auto &it : net->wires)
            wires.push_back(it.first);

        ctx->sorted_shuffle(wires);

        for (WireId w : wires) {
            std::vector<arc_key> arcs;
            for (auto &it : wire_to_arcs[w]) {
                arc_to_wires[it].erase(w);
                arcs.push_back(it);
            }
            wire_to_arcs[w].clear();

            ctx->sorted_shuffle(arcs);

            for (auto &it : arcs)
                arc_queue_insert(it);

            if (ctx->debug)
                log("        unbind wire %s\n", ctx->nameOfWire(w));

            ctx->unbindWire(w);
            wireScores[w]++;
        }

        ripup_flag = true;
    }

    void ripup_wire(WireId wire, int extra_indent = 0)
    {
        if (ctx->debug)
            log("    ripup wire %s\n", ctx->nameOfWire(wire));

        WireId w = ctx->getConflictingWireWire(wire);

        if (w == WireId()) {
            NetInfo *n = ctx->getConflictingWireNet(wire);
            if (n != nullptr)
                ripup_net(n);
        } else {
            std::vector<arc_key> arcs;
            for (auto &it : wire_to_arcs[w]) {
                arc_to_wires[it].erase(w);
                arcs.push_back(it);
            }
            wire_to_arcs[w].clear();

            ctx->sorted_shuffle(arcs);

            for (auto &it : arcs)
                arc_queue_insert(it);

            if (ctx->debug)
                log("      unbind wire %s\n", ctx->nameOfWire(w));

            ctx->unbindWire(w);
            wireScores[w]++;
        }

        ripup_flag = true;
    }

    void ripup_pip(PipId pip)
    {
        if (ctx->debug)
            log("    ripup pip %s\n", ctx->nameOfPip(pip));

        WireId w = ctx->getConflictingPipWire(pip);

        if (w == WireId()) {
            NetInfo *n = ctx->getConflictingPipNet(pip);
            if (n != nullptr)
                ripup_net(n);
        } else {
            std::vector<arc_key> arcs;
            for (auto &it : wire_to_arcs[w]) {
                arc_to_wires[it].erase(w);
                arcs.push_back(it);
            }
            wire_to_arcs[w].clear();

            ctx->sorted_shuffle(arcs);

            for (auto &it : arcs)
                arc_queue_insert(it);

            if (ctx->debug)
                log("      unbind wire %s\n", ctx->nameOfWire(w));

            ctx->unbindWire(w);
            wireScores[w]++;
        }

        ripup_flag = true;
    }

    bool skip_net(NetInfo *net_info)
    {
#ifdef ARCH_ECP5
        // ECP5 global nets currently appear part-unrouted due to arch database limitations
        // Don't touch them in the router
        if (net_info->is_global)
            return true;
#endif
        if (net_info->driver.cell == nullptr && net_info->constant_value == IdString())
            return true;

        return false;
    }

    void check()
    {
        pool<arc_key> valid_arcs;

        for (auto &net_it : ctx->nets) {
            NetInfo *net_info = net_it.second.get();
            pool<WireId> valid_wires_for_net;

            if (skip_net(net_info))
                continue;

#if 0
            if (ctx->debug)
                log("[check] net: %s\n", ctx->nameOf(net_info));
#endif

            auto src_wire = ctx->getNetinfoSourceWire(net_info);
            log_assert(src_wire != WireId());

            for (auto user : net_info->users.enumerate()) {
                unsigned phys_idx = 0;
                for (auto dst_wire : ctx->getNetinfoSinkWires(net_info, user.value)) {
                    log_assert(dst_wire != WireId());

                    arc_key arc;
                    arc.net_info = net_info;
                    arc.user_idx = user.index;
                    arc.phys_idx = phys_idx++;
                    valid_arcs.insert(arc);
#if 0
                    if (ctx->debug)
                    log("[check]   arc: %s %s\n", ctx->nameOfWire(src_wire), ctx->nameOfWire(dst_wire));
#endif

                    for (WireId wire : arc_to_wires[arc]) {
#if 0
                        if (ctx->debug)
                        log("[check]     wire: %s\n", ctx->nameOfWire(wire));
#endif
                        valid_wires_for_net.insert(wire);
                        log_assert(wire_to_arcs[wire].count(arc));
                        log_assert(net_info->wires.count(wire));
                    }
                }
            }

            for (auto &it : net_info->wires) {
                WireId w = it.first;
                log_assert(valid_wires_for_net.count(w));
            }
        }

        for (auto &it : wire_to_arcs) {
            for (auto &arc : it.second)
                log_assert(valid_arcs.count(arc));
        }

        for (auto &it : arc_to_wires) {
            log_assert(valid_arcs.count(it.first));
        }
    }

    void setup()
    {
        dict<WireId, NetInfo *> src_to_net;
        dict<WireId, arc_key> dst_to_arc;

        std::vector<IdString> net_names;
        for (auto &net_it : ctx->nets)
            net_names.push_back(net_it.first);

        ctx->sorted_shuffle(net_names);

        for (IdString net_name : net_names) {
            NetInfo *net_info = ctx->nets.at(net_name).get();

            if (skip_net(net_info))
                continue;

            auto src_wire = ctx->getNetinfoSourceWire(net_info);

            if (src_wire == WireId() && net_info->constant_value == IdString())
                log_error("No wire found for port %s on source cell %s.\n", ctx->nameOf(net_info->driver.port),
                          ctx->nameOf(net_info->driver.cell));

            if (src_to_net.count(src_wire))
                log_error("Found two nets with same source wire %s: %s vs %s\n", ctx->nameOfWire(src_wire),
                          ctx->nameOf(net_info), ctx->nameOf(src_to_net.at(src_wire)));

            if (dst_to_arc.count(src_wire))
                log_error("Wire %s is used as source and sink in different nets: %s vs %s (%d)\n",
                          ctx->nameOfWire(src_wire), ctx->nameOf(net_info),
                          ctx->nameOf(dst_to_arc.at(src_wire).net_info), dst_to_arc.at(src_wire).user_idx.idx());

            for (auto user : net_info->users.enumerate()) {
                unsigned phys_idx = 0;
                for (auto dst_wire : ctx->getNetinfoSinkWires(net_info, user.value)) {
                    arc_key arc;
                    arc.net_info = net_info;
                    arc.user_idx = user.index;
                    arc.phys_idx = phys_idx++;

                    if (dst_wire == WireId())
                        log_error("No wire found for port %s on destination cell %s.\n", ctx->nameOf(user.value.port),
                                  ctx->nameOf(user.value.cell));

                    if (dst_to_arc.count(dst_wire)) {
                        if (dst_to_arc.at(dst_wire).net_info == net_info)
                            continue;
                        log_error("Found two arcs with same sink wire %s: %s (%d) vs %s (%d)\n",
                                  ctx->nameOfWire(dst_wire), ctx->nameOf(net_info), user.index.idx(),
                                  ctx->nameOf(dst_to_arc.at(dst_wire).net_info),
                                  dst_to_arc.at(dst_wire).user_idx.idx());
                    }

                    if (src_to_net.count(dst_wire))
                        log_error("Wire %s is used as source and sink in different nets: %s vs %s (%d)\n",
                                  ctx->nameOfWire(dst_wire), ctx->nameOf(src_to_net.at(dst_wire)),
                                  ctx->nameOf(net_info), user.index.idx());

                    dst_to_arc[dst_wire] = arc;

                    if (net_info->wires.count(dst_wire) == 0) {
                        arc_queue_insert(arc, src_wire, dst_wire);
                        continue;
                    }

                    WireId cursor = dst_wire;
                    wire_to_arcs[cursor].insert(arc);
                    arc_to_wires[arc].insert(cursor);

                    while (src_wire != cursor && (net_info->constant_value == IdString() ||
                                                  ctx->getWireConstantValue(cursor) != net_info->constant_value)) {
                        auto it = net_info->wires.find(cursor);
                        if (it == net_info->wires.end()) {
                            arc_queue_insert(arc, src_wire, dst_wire);
                            break;
                        }

                        NPNR_ASSERT(it->second.pip != PipId());
                        cursor = ctx->getPipSrcWire(it->second.pip);
                        wire_to_arcs[cursor].insert(arc);
                        arc_to_wires[arc].insert(cursor);
                    }
                }
                // TODO: this matches the situation before supporting multiple cell->bel pins, but do we want to keep
                // this invariant?
                if (phys_idx == 0)
                    log_warning("No wires found for port %s on destination cell %s.\n", ctx->nameOf(user.value.port),
                                ctx->nameOf(user.value.cell));
            }

            src_to_net[src_wire] = net_info;

            std::vector<WireId> unbind_wires;

            for (auto &it : net_info->wires)
                if (it.second.strength < STRENGTH_LOCKED && wire_to_arcs.count(it.first) == 0)
                    unbind_wires.push_back(it.first);

            for (auto it : unbind_wires)
                ctx->unbindWire(it);
        }
    }

    bool route_arc(const arc_key &arc, bool ripup)
    {

        NetInfo *net_info = arc.net_info;
        auto user_idx = arc.user_idx;

        auto src_wire = ctx->getNetinfoSourceWire(net_info);
        auto dst_wire = ctx->getNetinfoSinkWire(net_info, net_info->users[user_idx], arc.phys_idx);
        ripup_flag = false;

        float crit = tmg.get_criticality(CellPortKey(net_info->users.at(user_idx)));

        if (ctx->debug) {
            log("Routing arc %d on net %s (%d arcs total):\n", user_idx.idx(), ctx->nameOf(net_info),
                int(net_info->users.capacity()));
            log("  source ... %s\n", ctx->nameOfWire(src_wire));
            log("  sink ..... %s\n", ctx->nameOfWire(dst_wire));
        }

        // unbind wires that are currently used exclusively by this arc

        pool<WireId> old_arc_wires;
        old_arc_wires.swap(arc_to_wires[arc]);

        for (WireId wire : old_arc_wires) {
            auto &arc_wires = wire_to_arcs.at(wire);
            NPNR_ASSERT(arc_wires.count(arc));
            arc_wires.erase(arc);
            if (arc_wires.empty()) {
                if (ctx->debug)
                    log("  unbind %s\n", ctx->nameOfWire(wire));
                ctx->unbindWire(wire);
            }
        }

        // special case

        if (src_wire == dst_wire) {
            NetInfo *bound = ctx->getBoundWireNet(src_wire);
            if (bound != nullptr)
                NPNR_ASSERT(bound == net_info);
            else {
                ctx->bindWire(src_wire, net_info, STRENGTH_WEAK);
            }
            arc_to_wires[arc].insert(src_wire);
            wire_to_arcs[src_wire].insert(arc);
            return true;
        }

        // reset wire queue

        if (!queue.empty()) {
            std::priority_queue<QueuedWire, std::vector<QueuedWire>, QueuedWire::Greater> new_queue;
            queue.swap(new_queue);
        }
        dict<WireId, QueuedWire> visited;

        // A* main loop

        int visitCnt = 0;
        int maxVisitCnt = INT_MAX;
        delay_t best_est = 0;
        delay_t best_score = -1;

        {
            QueuedWire qw;
            qw.wire = src_wire;
            qw.pip = PipId();
            qw.delay = ctx->getWireDelay(qw.wire).maxDelay();
            qw.penalty = 0;
            qw.bonus = 0;
            if (cfg.useEstimate) {
                qw.togo = ctx->estimateDelay(qw.wire, dst_wire);
                best_est = qw.delay + qw.togo;
            }
            qw.randtag = ctx->rng();

            queue.push(qw);
            visited[qw.wire] = qw;
        }

        while (visitCnt++ < maxVisitCnt && !queue.empty()) {
            QueuedWire qw = queue.top();
            queue.pop();

            for (auto pip : ctx->getPipsDownhill(qw.wire)) {
                delay_t next_delay = qw.delay + ctx->getPipDelay(pip).maxDelay();
                delay_t next_penalty = qw.penalty;
                delay_t next_bonus = qw.bonus;
                delay_t penalty_delta = 0;

                WireId next_wire = ctx->getPipDstWire(pip);
                next_delay += ctx->getWireDelay(next_wire).maxDelay();

                WireId conflictWireWire = WireId(), conflictPipWire = WireId();
                NetInfo *conflictWireNet = nullptr, *conflictPipNet = nullptr;

                if (net_info->wires.count(next_wire) && net_info->wires.at(next_wire).pip == pip) {
                    next_bonus += cfg.reuseBonus * (1.0 - crit);
                } else {
                    if (!ctx->checkWireAvail(next_wire)) {
                        if (!ripup)
                            continue;
                        conflictWireWire = ctx->getConflictingWireWire(next_wire);
                        if (conflictWireWire == WireId()) {
                            conflictWireNet = ctx->getConflictingWireNet(next_wire);
                            if (conflictWireNet == nullptr)
                                continue;
                            else {
                                if (conflictWireNet->wires.count(next_wire) &&
                                    conflictWireNet->wires.at(next_wire).strength > STRENGTH_STRONG)
                                    continue;
                            }
                        } else {
                            NetInfo *conflicting = ctx->getBoundWireNet(conflictWireWire);
                            if (conflicting != nullptr) {
                                if (conflicting->wires.count(conflictWireWire) &&
                                    conflicting->wires.at(conflictWireWire).strength > STRENGTH_STRONG)
                                    continue;
                            }
                        }
                    }

                    if (!ctx->checkPipAvail(pip)) {
                        if (!ripup)
                            continue;
                        conflictPipWire = ctx->getConflictingPipWire(pip);
                        if (conflictPipWire == WireId()) {
                            conflictPipNet = ctx->getConflictingPipNet(pip);
                            if (conflictPipNet == nullptr)
                                continue;
                            else {
                                if (conflictPipNet->wires.count(next_wire) &&
                                    conflictPipNet->wires.at(next_wire).strength > STRENGTH_STRONG)
                                    continue;
                            }
                        } else {
                            NetInfo *conflicting = ctx->getBoundWireNet(conflictPipWire);
                            if (conflicting != nullptr) {
                                if (conflicting->wires.count(conflictPipWire) &&
                                    conflicting->wires.at(conflictPipWire).strength > STRENGTH_STRONG)
                                    continue;
                            }
                        }
                    }

                    if (conflictWireNet != nullptr && conflictPipWire != WireId() &&
                        conflictWireNet->wires.count(conflictPipWire))
                        conflictPipWire = WireId();

                    if (conflictPipNet != nullptr && conflictWireWire != WireId() &&
                        conflictPipNet->wires.count(conflictWireWire))
                        conflictWireWire = WireId();

                    if (conflictWireWire == conflictPipWire)
                        conflictWireWire = WireId();

                    if (conflictWireNet == conflictPipNet)
                        conflictWireNet = nullptr;

                    if (conflictWireWire != WireId()) {
                        auto scores_it = wireScores.find(conflictWireWire);
                        if (scores_it != wireScores.end())
                            penalty_delta += scores_it->second * cfg.wireRipupPenalty;
                        penalty_delta += cfg.wireRipupPenalty;
                    }

                    if (conflictPipWire != WireId()) {
                        auto scores_it = wireScores.find(conflictPipWire);
                        if (scores_it != wireScores.end())
                            penalty_delta += scores_it->second * cfg.wireRipupPenalty;
                        penalty_delta += cfg.wireRipupPenalty;
                    }

                    if (conflictWireNet != nullptr) {
                        auto scores_it = netScores.find(conflictWireNet);
                        if (scores_it != netScores.end())
                            penalty_delta += scores_it->second * cfg.netRipupPenalty;
                        penalty_delta += cfg.netRipupPenalty;
                        penalty_delta += conflictWireNet->wires.size() * cfg.wireRipupPenalty;
                    }

                    if (conflictPipNet != nullptr) {
                        auto scores_it = netScores.find(conflictPipNet);
                        if (scores_it != netScores.end())
                            penalty_delta += scores_it->second * cfg.netRipupPenalty;
                        penalty_delta += cfg.netRipupPenalty;
                        penalty_delta += conflictPipNet->wires.size() * cfg.wireRipupPenalty;
                    }
                }

                next_penalty += penalty_delta * (timing_driven ? std::max(0.05, (1.0 - crit)) : 1);

                delay_t next_score = next_delay + next_penalty;
                NPNR_ASSERT(next_score >= 0);

                if ((best_score >= 0) && (next_score - next_bonus - cfg.estimatePrecision > best_score))
                    continue;

                auto old_visited_it = visited.find(next_wire);
                if (old_visited_it != visited.end()) {
                    delay_t old_delay = old_visited_it->second.delay;
                    delay_t old_score = old_delay + old_visited_it->second.penalty;
                    NPNR_ASSERT(old_score >= 0);

                    if (next_score + ctx->getDelayEpsilon() >= old_score)
                        continue;

#if 0
                    if (ctx->debug)
                        log("Found better route to %s. Old vs new delay estimate: %.3f (%.3f) %.3f (%.3f)\n",
                            ctx->nameOfWire(next_wire),
                            ctx->getDelayNS(old_score),
                            ctx->getDelayNS(old_visited_it->second.delay),
                            ctx->getDelayNS(next_score),
                            ctx->getDelayNS(next_delay));
#endif
                }

                QueuedWire next_qw;
                next_qw.wire = next_wire;
                next_qw.pip = pip;
                next_qw.delay = next_delay;
                next_qw.penalty = next_penalty;
                next_qw.bonus = next_bonus;
                if (cfg.useEstimate) {
                    next_qw.togo = ctx->estimateDelay(next_wire, dst_wire);
                    delay_t this_est = next_qw.delay + next_qw.togo;
                    if (this_est / 2 - cfg.estimatePrecision > best_est)
                        continue;
                    if (best_est > this_est)
                        best_est = this_est;
                }
                next_qw.randtag = ctx->rng();

#if 0
                if (ctx->debug)
                    log("%s -> %s: %.3f (%.3f)\n",
                        ctx->nameOfWire(qw.wire),
                        ctx->nameOfWire(next_wire),
                        ctx->getDelayNS(next_score),
                        ctx->getDelayNS(next_delay));
#endif

                visited[next_qw.wire] = next_qw;
                queue.push(next_qw);

                if (next_wire == dst_wire) {
                    maxVisitCnt = std::min(maxVisitCnt, 2 * visitCnt + (next_qw.penalty > 0 ? 100 : 0));
                    best_score = next_score - next_bonus;
                }
            }
        }

        if (ctx->debug)
            log("  total number of visited nodes: %d\n", visitCnt);

        if (visited.count(dst_wire) == 0) {
            if (ctx->debug)
                log("  no route found for this arc\n");
            return false;
        }

        if (ctx->debug) {
            log("  final route delay:   %8.2f\n", ctx->getDelayNS(visited[dst_wire].delay));
            log("  final route penalty: %8.2f\n", ctx->getDelayNS(visited[dst_wire].penalty));
            log("  final route bonus:   %8.2f\n", ctx->getDelayNS(visited[dst_wire].bonus));
        }

        // bind resulting route (and maybe unroute other nets)

        pool<WireId> unassign_wires = arc_to_wires[arc];

        WireId cursor = dst_wire;
        delay_t accumulated_path_delay = 0;
        delay_t last_path_delay_delta = 0;
        while (1) {
            auto pip = visited[cursor].pip;

            if (ctx->debug) {
                delay_t path_delay_delta = ctx->estimateDelay(cursor, dst_wire) - accumulated_path_delay;

                log("  node %s (%+.2f %+.2f)\n", ctx->nameOfWire(cursor), ctx->getDelayNS(path_delay_delta),
                    ctx->getDelayNS(path_delay_delta - last_path_delay_delta));

                last_path_delay_delta = path_delay_delta;

                if (pip != PipId())
                    accumulated_path_delay += ctx->getPipDelay(pip).maxDelay();
                accumulated_path_delay += ctx->getWireDelay(cursor).maxDelay();
            }

            if (pip == PipId())
                NPNR_ASSERT(cursor == src_wire);

            if (!net_info->wires.count(cursor) || net_info->wires.at(cursor).pip != pip) {
                if (!ctx->checkWireAvail(cursor)) {
                    ripup_wire(cursor);
                    NPNR_ASSERT(ctx->checkWireAvail(cursor));
                }

                if (pip != PipId() && !ctx->checkPipAvail(pip)) {
                    ripup_pip(pip);
                    NPNR_ASSERT(ctx->checkPipAvail(pip));
                }

                if (pip == PipId()) {
                    if (ctx->debug)
                        log("    bind wire %s\n", ctx->nameOfWire(cursor));
                    ctx->bindWire(cursor, net_info, STRENGTH_WEAK);
                } else {
                    if (ctx->debug)
                        log("    bind pip %s\n", ctx->nameOfPip(pip));
                    ctx->bindPip(pip, net_info, STRENGTH_WEAK);
                }
            }

            wire_to_arcs[cursor].insert(arc);
            arc_to_wires[arc].insert(cursor);

            if (pip == PipId())
                break;

            cursor = ctx->getPipSrcWire(pip);
        }

        if (ripup_flag)
            arcs_with_ripup++;
        else
            arcs_without_ripup++;

        return true;
    }

    bool route_const_arc(const arc_key &arc, bool ripup)
    {

        NetInfo *net_info = arc.net_info;
        auto user_idx = arc.user_idx;

        auto dst_wire = ctx->getNetinfoSinkWire(net_info, net_info->users[user_idx], arc.phys_idx);
        ripup_flag = false;

        if (ctx->debug) {
            log("Routing constant arc %d on net %s (%d arcs total):\n", user_idx.idx(), ctx->nameOf(net_info),
                int(net_info->users.capacity()));
            log("  value ... %s\n", ctx->nameOf(net_info->constant_value));
            log("  sink ..... %s\n", ctx->nameOfWire(dst_wire));
        }

        // unbind wires that are currently used exclusively by this arc

        pool<WireId> old_arc_wires;
        old_arc_wires.swap(arc_to_wires[arc]);

        for (WireId wire : old_arc_wires) {
            auto &arc_wires = wire_to_arcs.at(wire);
            NPNR_ASSERT(arc_wires.count(arc));
            arc_wires.erase(arc);
            if (arc_wires.empty()) {
                if (ctx->debug)
                    log("  unbind %s\n", ctx->nameOfWire(wire));
                ctx->unbindWire(wire);
            }
        }

        // special case

        if (ctx->getWireConstantValue(dst_wire) == net_info->constant_value) {
            NetInfo *bound = ctx->getBoundWireNet(dst_wire);
            if (bound != nullptr)
                NPNR_ASSERT(bound == net_info);
            else {
                ctx->bindWire(dst_wire, net_info, STRENGTH_WEAK);
            }
            arc_to_wires[arc].insert(dst_wire);
            wire_to_arcs[dst_wire].insert(arc);
            return true;
        }

        // reset wire queue

        if (!queue.empty()) {
            std::priority_queue<QueuedWire, std::vector<QueuedWire>, QueuedWire::Greater> new_queue;
            queue.swap(new_queue);
        }
        dict<WireId, QueuedWire> visited;

        // A* main loop

        int visitCnt = 0;
        int maxVisitCnt = INT_MAX;
        delay_t best_score = -1;
        WireId best_src;

        {
            QueuedWire qw;
            qw.wire = dst_wire;
            qw.pip = PipId();
            qw.delay = ctx->getWireDelay(qw.wire).maxDelay();
            qw.penalty = 0;
            qw.bonus = 0;
            qw.randtag = ctx->rng();

            queue.push(qw);
            visited[qw.wire] = qw;
        }

        while (visitCnt++ < maxVisitCnt && !queue.empty()) {
            QueuedWire qw = queue.top();
            queue.pop();

            for (auto pip : ctx->getPipsUphill(qw.wire)) {
                delay_t next_delay = qw.delay + ctx->getPipDelay(pip).maxDelay();
                delay_t next_penalty = qw.penalty;
                delay_t next_bonus = qw.bonus;
                delay_t penalty_delta = 0;

                WireId next_wire = ctx->getPipSrcWire(pip);
                next_delay += ctx->getWireDelay(next_wire).maxDelay();

                WireId conflictWireWire = WireId(), conflictPipWire = WireId();
                NetInfo *conflictWireNet = nullptr, *conflictPipNet = nullptr;

                if (net_info->wires.count(qw.wire)) {
                    if (net_info->wires.at(qw.wire).pip != pip)
                        continue;
                    next_bonus += cfg.reuseBonus;
                } else {
                    if (!ctx->checkWireAvail(next_wire)) {
                        if (!ripup)
                            continue;
                        conflictWireWire = ctx->getConflictingWireWire(next_wire);
                        if (conflictWireWire == WireId()) {
                            conflictWireNet = ctx->getConflictingWireNet(next_wire);
                            if (conflictWireNet == nullptr)
                                continue;
                            else {
                                if (conflictWireNet->wires.count(next_wire) &&
                                    conflictWireNet->wires.at(next_wire).strength > STRENGTH_STRONG)
                                    continue;
                            }
                        } else {
                            NetInfo *conflicting = ctx->getBoundWireNet(conflictWireWire);
                            if (conflicting != nullptr) {
                                if (conflicting->wires.count(conflictWireWire) &&
                                    conflicting->wires.at(conflictWireWire).strength > STRENGTH_STRONG)
                                    continue;
                            }
                        }
                    }

                    if (!ctx->checkPipAvail(pip)) {
                        if (!ripup)
                            continue;
                        conflictPipWire = ctx->getConflictingPipWire(pip);
                        if (conflictPipWire == WireId()) {
                            conflictPipNet = ctx->getConflictingPipNet(pip);
                            if (conflictPipNet == nullptr)
                                continue;
                            else {
                                if (conflictPipNet->wires.count(next_wire) &&
                                    conflictPipNet->wires.at(next_wire).strength > STRENGTH_STRONG)
                                    continue;
                            }
                        } else {
                            NetInfo *conflicting = ctx->getBoundWireNet(conflictPipWire);
                            if (conflicting != nullptr) {
                                if (conflicting->wires.count(conflictPipWire) &&
                                    conflicting->wires.at(conflictPipWire).strength > STRENGTH_STRONG)
                                    continue;
                            }
                        }
                    }

                    if (conflictWireNet != nullptr && conflictPipWire != WireId() &&
                        conflictWireNet->wires.count(conflictPipWire))
                        conflictPipWire = WireId();

                    if (conflictPipNet != nullptr && conflictWireWire != WireId() &&
                        conflictPipNet->wires.count(conflictWireWire))
                        conflictWireWire = WireId();

                    if (conflictWireWire == conflictPipWire)
                        conflictWireWire = WireId();

                    if (conflictWireNet == conflictPipNet)
                        conflictWireNet = nullptr;

                    if (conflictWireWire != WireId()) {
                        auto scores_it = wireScores.find(conflictWireWire);
                        if (scores_it != wireScores.end())
                            penalty_delta += scores_it->second * cfg.wireRipupPenalty;
                        penalty_delta += cfg.wireRipupPenalty;
                    }

                    if (conflictPipWire != WireId()) {
                        auto scores_it = wireScores.find(conflictPipWire);
                        if (scores_it != wireScores.end())
                            penalty_delta += scores_it->second * cfg.wireRipupPenalty;
                        penalty_delta += cfg.wireRipupPenalty;
                    }

                    if (conflictWireNet != nullptr) {
                        auto scores_it = netScores.find(conflictWireNet);
                        if (scores_it != netScores.end())
                            penalty_delta += scores_it->second * cfg.netRipupPenalty;
                        penalty_delta += cfg.netRipupPenalty;
                        penalty_delta += conflictWireNet->wires.size() * cfg.wireRipupPenalty;
                    }

                    if (conflictPipNet != nullptr) {
                        auto scores_it = netScores.find(conflictPipNet);
                        if (scores_it != netScores.end())
                            penalty_delta += scores_it->second * cfg.netRipupPenalty;
                        penalty_delta += cfg.netRipupPenalty;
                        penalty_delta += conflictPipNet->wires.size() * cfg.wireRipupPenalty;
                    }
                }

                next_penalty += penalty_delta;

                delay_t next_score = next_delay + next_penalty;
                NPNR_ASSERT(next_score >= 0);

                if ((best_score >= 0) && (next_score - next_bonus - cfg.estimatePrecision > best_score))
                    continue;

                auto old_visited_it = visited.find(next_wire);
                if (old_visited_it != visited.end()) {
                    continue;
                }

                QueuedWire next_qw;
                next_qw.wire = next_wire;
                next_qw.pip = pip;
                next_qw.delay = next_delay;
                next_qw.penalty = next_penalty;
                next_qw.bonus = next_bonus;
                next_qw.randtag = ctx->rng();

                visited[next_qw.wire] = next_qw;
                queue.push(next_qw);

                if (ctx->getWireConstantValue(next_wire) == net_info->constant_value) {
                    maxVisitCnt = std::min(maxVisitCnt, 2 * visitCnt + (next_qw.penalty > 0 ? 100 : 0));
                    if (best_src == WireId() || next_score < best_score) {
                        best_src = next_wire;
                    }
                    best_score = next_score - next_bonus;
                }
            }
        }

        if (ctx->debug)
            log("  total number of visited nodes: %d\n", visitCnt);

        if (best_src == WireId()) {
            if (ctx->debug)
                log("  no route found for this arc\n");
            return false;
        }

        if (ctx->debug) {
            log("  final route delay:   %8.2f\n", ctx->getDelayNS(visited[dst_wire].delay));
            log("  final route penalty: %8.2f\n", ctx->getDelayNS(visited[dst_wire].penalty));
            log("  final route bonus:   %8.2f\n", ctx->getDelayNS(visited[dst_wire].bonus));
        }

        // bind resulting route (and maybe unroute other nets)

        pool<WireId> unassign_wires = arc_to_wires[arc];

        WireId cursor = best_src;

        if (!net_info->wires.count(cursor)) {
            if (!ctx->checkWireAvail(cursor)) {
                ripup_wire(cursor);
                NPNR_ASSERT(ctx->checkWireAvail(cursor));
            }
            ctx->bindWire(cursor, net_info, STRENGTH_WEAK);
        }

        wire_to_arcs[cursor].insert(arc);
        arc_to_wires[arc].insert(cursor);

        while (1) {
            auto pip = visited[cursor].pip;

            if (pip == PipId()) {
                NPNR_ASSERT(cursor == dst_wire);
                break;
            }

            WireId next = ctx->getPipDstWire(pip);

            if (!net_info->wires.count(next) || (net_info->wires.count(next) && net_info->wires.at(next).pip != pip)) {
                if (!ctx->checkWireAvail(next)) {
                    ripup_wire(next);
                    NPNR_ASSERT(ctx->checkWireAvail(next));
                }

                if (!ctx->checkPipAvail(pip)) {
                    ripup_pip(pip);
                    NPNR_ASSERT(ctx->checkPipAvail(pip));
                }

                if (ctx->debug)
                    log("    bind pip %s\n", ctx->nameOfPip(pip));
                ctx->bindPip(pip, net_info, STRENGTH_WEAK);
            }

            wire_to_arcs[next].insert(arc);
            arc_to_wires[arc].insert(next);

            cursor = next;
        }

        if (ripup_flag)
            arcs_with_ripup++;
        else
            arcs_without_ripup++;

        return true;
    }

    delay_t find_slack_thresh()
    {
        // If more than 5% of arcs have negative slack; use the 5% threshold as a ripup criteria
        int arc_count = 0;
        int failed_count = 0;
        delay_t default_thresh = ctx->getDelayEpsilon();

        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            if (skip_net(ni))
                continue;
            for (auto &usr : ni->users) {
                ++arc_count;
                delay_t slack = tmg.get_setup_slack(CellPortKey(usr));
                if (slack == std::numeric_limits<delay_t>::lowest())
                    continue;
                if (slack < default_thresh)
                    ++failed_count;
            }
        }

        if (arc_count < 50 || (failed_count < (0.05 * arc_count))) {
            return default_thresh;
        }

        std::vector<delay_t> slacks;
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            if (skip_net(ni))
                continue;
            for (auto &usr : ni->users) {
                delay_t slack = tmg.get_setup_slack(CellPortKey(usr));
                if (slack == std::numeric_limits<delay_t>::lowest())
                    continue;
                slacks.push_back(slack);
            }
        }
        std::sort(slacks.begin(), slacks.end());
        delay_t thresh = slacks.at(int(slacks.size() * 0.05));
        log_warning("%.f%% of arcs have failing slack; using %.2fns as ripup threshold. Consider a reduced Fmax "
                    "constraint.\n",
                    (100.0 * failed_count) / arc_count, ctx->getDelayNS(thresh));
        return thresh;
    }
};

} // namespace

NEXTPNR_NAMESPACE_BEGIN

Router1Cfg::Router1Cfg(Context *ctx)
{
    maxIterCnt = ctx->setting<int>("router1/maxIterCnt", 200);
    cleanupReroute = ctx->setting<bool>("router1/cleanupReroute", true);
    fullCleanupReroute = ctx->setting<bool>("router1/fullCleanupReroute", true);
    useEstimate = ctx->setting<bool>("router1/useEstimate", true);

    wireRipupPenalty = ctx->getRipupDelayPenalty();
    netRipupPenalty = 10 * ctx->getRipupDelayPenalty();
    reuseBonus = wireRipupPenalty / 2;

    estimatePrecision = 100 * ctx->getRipupDelayPenalty();
}

bool router1(Context *ctx, const Router1Cfg &cfg)
{
    try {
        log_break();
        log_info("Routing..\n");
        ScopeLock<Context> lock(ctx);
        auto rstart = std::chrono::high_resolution_clock::now();

        log_info("Setting up routing queue.\n");

        Router1 router(ctx, cfg);
        router.setup();
#ifndef NDEBUG
        router.check();
#endif

        log_info("Routing %d arcs.\n", int(router.arc_queue.size()));

        int iter_cnt = 0;
        int last_arcs_with_ripup = 0;
        int last_arcs_without_ripup = 0;
        int timing_fail_count = 0;
        bool timing_ripup = ctx->setting<bool>("router/tmg_ripup", false);
        delay_t ripup_slack = 0;

        log_info("           |   (re-)routed arcs  |   delta    | remaining|       time spent     |\n");
        log_info("   IterCnt |  w/ripup   wo/ripup |  w/r  wo/r |      arcs| batch(sec) total(sec)|\n");

        auto prev_time = rstart;
        while (!router.arc_queue.empty()) {
            if (++iter_cnt % 1000 == 0) {
                auto curr_time = std::chrono::high_resolution_clock::now();
                log_info("%10d | %8d %10d | %4d %5d | %9d| %10.02f %10.02f|\n", iter_cnt, router.arcs_with_ripup,
                         router.arcs_without_ripup, router.arcs_with_ripup - last_arcs_with_ripup,
                         router.arcs_without_ripup - last_arcs_without_ripup, int(router.arc_queue.size()),
                         std::chrono::duration<float>(curr_time - prev_time).count(),
                         std::chrono::duration<float>(curr_time - rstart).count());
                prev_time = curr_time;
                last_arcs_with_ripup = router.arcs_with_ripup;
                last_arcs_without_ripup = router.arcs_without_ripup;
                ctx->yield();
#ifndef NDEBUG
                router.check();
#endif
            }

            if (ctx->debug)
                log("-- %d --\n", iter_cnt);

            arc_key arc = router.arc_queue_pop();
            if (arc.net_info->constant_value != IdString()) {
                if (!router.route_const_arc(arc, true)) {
                    log_warning("Failed to find a route for arc %d of net %s.\n", arc.user_idx.idx(),
                                ctx->nameOf(arc.net_info));
#ifndef NDEBUG
                    router.check();
                    ctx->check();
#endif
                    return false;
                }
            } else {
                if (!router.route_arc(arc, true)) {
                    log_warning("Failed to find a route for arc %d of net %s.\n", arc.user_idx.idx(),
                                ctx->nameOf(arc.net_info));
#ifndef NDEBUG
                    router.check();
                    ctx->check();
#endif
                    return false;
                }
            }
            // Timing driven ripup
            if (timing_ripup && router.arc_queue.empty() && timing_fail_count < 50) {
                ++timing_fail_count;
                router.tmg.run();
                delay_t wns = 0, tns = 0;
                if (timing_fail_count == 1)
                    ripup_slack = router.find_slack_thresh();
                for (auto &net : ctx->nets) {
                    NetInfo *ni = net.second.get();
                    if (router.skip_net(ni))
                        continue;
                    bool is_locked = false;
                    for (auto &wire : ni->wires) {
                        if (wire.second.strength > STRENGTH_STRONG)
                            is_locked = true;
                    }
                    if (is_locked)
                        continue;
                    for (auto &usr : ni->users) {
                        delay_t slack = router.tmg.get_setup_slack(CellPortKey(usr));
                        if (slack == std::numeric_limits<delay_t>::lowest())
                            continue;
                        if (slack < 0) {
                            wns = std::min(wns, slack);
                            tns += slack;
                        }
                        if (slack <= ripup_slack) {
                            for (WireId w : ctx->getNetinfoSinkWires(ni, usr)) {
                                if (ctx->checkWireAvail(w))
                                    continue;
                                router.ripup_wire(w);
                            }
                        }
                    }
                }
                log_info("    %d arcs ripped up due to negative slack WNS=%.02fns TNS=%.02fns.\n",
                         int(router.arc_queue.size()), ctx->getDelayNS(wns), ctx->getDelayNS(tns));
                iter_cnt = 0;
                router.wireScores.clear();
                router.netScores.clear();
            }
        }
        auto rend = std::chrono::high_resolution_clock::now();
        log_info("%10d | %8d %10d | %4d %5d | %9d| %10.02f %10.02f|\n", iter_cnt, router.arcs_with_ripup,
                 router.arcs_without_ripup, router.arcs_with_ripup - last_arcs_with_ripup,
                 router.arcs_without_ripup - last_arcs_without_ripup, int(router.arc_queue.size()),
                 std::chrono::duration<float>(rend - prev_time).count(),
                 std::chrono::duration<float>(rend - rstart).count());
        log_info("Routing complete.\n");
        ctx->yield();
        log_info("Router1 time %.02fs\n", std::chrono::duration<float>(rend - rstart).count());

#ifndef NDEBUG
        router.check();
        ctx->check();
        log_assert(ctx->checkRoutedDesign());
#endif

        log_info("Checksum: 0x%08x\n", ctx->checksum());
        timing_analysis(ctx, true /* slack_histogram */, true /* print_fmax */, true /* print_path */,
                        true /* warn_on_failure */, true /* update_results */);

        return true;
    } catch (log_execution_error_exception) {
#ifndef NDEBUG
        ctx->lock();
        ctx->check();
        ctx->unlock();
#endif
        return false;
    }
}

bool Context::checkRoutedDesign() const
{
    const Context *ctx = getCtx();

    for (auto &net_it : ctx->nets) {
        NetInfo *net_info = net_it.second.get();

#ifdef ARCH_ECP5
        if (net_info->is_global)
            continue;
#endif

        if (ctx->debug)
            log("checking net %s\n", ctx->nameOf(net_info));

        if (net_info->users.empty()) {
            if (ctx->debug)
                log("  net without sinks\n");
            log_assert(net_info->wires.empty());
            continue;
        }

        bool found_unrouted = false;
        bool found_loop = false;
        bool found_stub = false;

        struct ExtraWireInfo
        {
            int order_num = 0;
            pool<WireId> children;
        };

        dict<WireId, std::unique_ptr<ExtraWireInfo>> db;

        for (auto &it : net_info->wires) {
            WireId w = it.first;
            PipId p = it.second.pip;

            if (p != PipId()) {
                log_assert(ctx->getPipDstWire(p) == w);
                db.emplace(ctx->getPipSrcWire(p), std::make_unique<ExtraWireInfo>()).first->second->children.insert(w);
            }
        }

        auto src_wire = ctx->getNetinfoSourceWire(net_info);
        if (net_info->constant_value == IdString()) {
            if (src_wire == WireId()) {
                log_assert(net_info->driver.cell == nullptr);
                if (ctx->debug)
                    log("  undriven and unrouted\n");
                continue;
            }

            if (net_info->wires.count(src_wire) == 0) {
                if (ctx->debug)
                    log("  source (%s) not bound to net\n", ctx->nameOfWire(src_wire));
                found_unrouted = true;
            }
        }

        dict<WireId, store_index<PortRef>> dest_wires;
        for (auto user : net_info->users.enumerate()) {
            for (auto dst_wire : ctx->getNetinfoSinkWires(net_info, user.value)) {
                log_assert(dst_wire != WireId());
                dest_wires[dst_wire] = user.index;

                if (net_info->wires.count(dst_wire) == 0) {
                    if (ctx->debug)
                        log("  sink %d (%s) not bound to net\n", user.index.idx(), ctx->nameOfWire(dst_wire));
                    found_unrouted = true;
                }
            }
        }

        std::function<void(WireId, int)> setOrderNum;
        pool<WireId> logged_wires;

        setOrderNum = [&](WireId w, int num) {
            auto &db_entry = *db.emplace(w, std::make_unique<ExtraWireInfo>()).first->second;
            if (db_entry.order_num != 0) {
                found_loop = true;
                log("  %*s=> loop\n", 2 * num, "");
                return;
            }
            db_entry.order_num = num;
            for (WireId child : db_entry.children) {
                if (ctx->debug) {
                    log("  %*s-> %s\n", 2 * num, "", ctx->nameOfWire(child));
                    logged_wires.insert(child);
                }
                setOrderNum(child, num + 1);
            }
            if (db_entry.children.empty()) {
                if (dest_wires.count(w) != 0) {
                    if (ctx->debug)
                        log("  %*s=> sink %d\n", 2 * num, "", dest_wires.at(w).idx());
                } else {
                    if (ctx->debug)
                        log("  %*s=> stub\n", 2 * num, "");
                    found_stub = true;
                }
            }
        };

        if (ctx->debug) {
            log("  driver: %s\n", ctx->nameOfWire(src_wire));
            logged_wires.insert(src_wire);
        }
        if (net_info->constant_value != IdString()) {
            for (const auto &wire : net_info->wires) {
                if (wire.second.pip == PipId() && ctx->getWireConstantValue(wire.first) == net_info->constant_value)
                    setOrderNum(wire.first, 1);
            }
        } else {
            setOrderNum(src_wire, 1);
        }

        pool<WireId> dangling_wires;

        for (auto &it : db) {
            auto &db_entry = *it.second;
            if (db_entry.order_num == 0)
                dangling_wires.insert(it.first);
        }

        if (ctx->debug) {
            if (dangling_wires.empty()) {
                log("  no dangling wires.\n");
            } else {
                pool<WireId> root_wires = dangling_wires;

                for (WireId w : dangling_wires) {
                    for (WireId c : db[w]->children)
                        root_wires.erase(c);
                }

                for (WireId w : root_wires) {
                    log("  dangling wire: %s\n", ctx->nameOfWire(w));
                    logged_wires.insert(w);
                    setOrderNum(w, 1);
                }

                for (WireId w : dangling_wires) {
                    if (logged_wires.count(w) == 0)
                        log("  loop: %s -> %s\n", ctx->nameOfWire(ctx->getPipSrcWire(net_info->wires.at(w).pip)),
                            ctx->nameOfWire(w));
                }
            }
        }

        bool fail = false;

        if (found_unrouted) {
            if (ctx->debug)
                log("check failed: found unrouted arcs\n");
            fail = true;
        }

        if (found_loop) {
            if (ctx->debug)
                log("check failed: found loops\n");
            fail = true;
        }

        if (found_stub) {
            if (ctx->debug)
                log("check failed: found stubs\n");
            fail = true;
        }

        if (!dangling_wires.empty()) {
            if (ctx->debug)
                log("check failed: found dangling wires\n");
            fail = true;
        }

        if (fail)
            return false;
    }

    return true;
}

bool Context::getActualRouteDelay(WireId src_wire, WireId dst_wire, delay_t *delay, dict<WireId, PipId> *route,
                                  bool useEstimate)
{
    // FIXME
    return false;
}

NEXTPNR_NAMESPACE_END
