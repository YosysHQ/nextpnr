/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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
#include "router1.h"
#include "timing.h"

namespace {

USING_NEXTPNR_NAMESPACE

struct arc_key
{
    NetInfo *net_info;
    int user_idx;

    bool operator==(const arc_key &other) const {
        return (net_info == other.net_info) && (user_idx == other.user_idx);
    }

    struct Hash
    {
        std::size_t operator()(const arc_key &arg) const noexcept
        {
            std::size_t seed = std::hash<NetInfo*>()(arg.net_info);
            seed ^= std::hash<int>()(arg.user_idx) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
};

struct arc_entry
{
    arc_key arc;
    delay_t pri;

    struct Greater
    {
        bool operator()(const arc_entry &lhs, const arc_entry &rhs) const noexcept
        {
            return lhs.pri > rhs.pri;
        }
    };
};

struct QueuedWire
{
    WireId wire;
    PipId pip;

    delay_t delay = 0, penalty = 0, togo = 0;
    int randtag = 0;

    struct Greater
    {
        bool operator()(const QueuedWire &lhs, const QueuedWire &rhs) const noexcept
        {
            delay_t l = lhs.delay + lhs.penalty + lhs.togo;
            delay_t r = rhs.delay + rhs.penalty + rhs.togo;
            return l == r ? lhs.randtag > rhs.randtag : l > r;
        }
    };
};

struct Router1
{
    Context *ctx;
    const Router1Cfg &cfg;

    std::priority_queue<arc_entry, std::vector<arc_entry>, arc_entry::Greater> arc_queue;
    std::unordered_map<WireId, std::unordered_set<arc_key, arc_key::Hash>> wire_to_arc;
    std::unordered_set<arc_key, arc_key::Hash> queued_arcs;

    std::unordered_map<WireId, QueuedWire> visited;
    std::priority_queue<QueuedWire, std::vector<QueuedWire>, QueuedWire::Greater> queue;

    std::unordered_map<WireId, int> wireScores;
    std::unordered_map<PipId, int> pipScores;

    int arcs_with_ripup = 0;
    int arcs_without_ripup = 0;
    bool ripup_flag;

    Router1(Context *ctx, const Router1Cfg &cfg) : ctx(ctx), cfg(cfg) { }

    void arc_queue_insert(const arc_key &arc, WireId src_wire, WireId dst_wire)
    {
        if (queued_arcs.count(arc))
            return;

        delay_t pri = ctx->estimateDelay(src_wire, dst_wire) - arc.net_info->users[arc.user_idx].budget;

        arc_entry entry;
        entry.arc = arc;
        entry.pri = pri;

        arc_queue.push(entry);
        queued_arcs.insert(arc);
    }

    void arc_queue_insert(const arc_key &arc)
    {
        NetInfo *net_info = arc.net_info;
        int user_idx = arc.user_idx;

        auto src_wire = ctx->getNetinfoSourceWire(net_info);
        auto dst_wire = ctx->getNetinfoSinkWire(net_info, net_info->users[user_idx]);

        arc_queue_insert(arc, src_wire, dst_wire);
    }

    void ripup_net(NetInfo *net)
    {
        if (ctx->debug)
            log("    ripup net %s\n", net->name.c_str(ctx));

        auto net_wires_copy = net->wires;
        for (auto &it : net_wires_copy) {
            if (it.second.pip == PipId())
                ripup_wire(it.first);
            else
                ripup_pip(it.second.pip);
        }

        ripup_flag = true;
    }

    void ripup_wire(WireId wire)
    {
        if (ctx->debug)
            log("    ripup wire %s\n", ctx->getWireName(wire).c_str(ctx));

        wireScores[wire]++;

        if (ctx->getBoundWireNet(wire)) {
            for (auto &it : wire_to_arc[wire])
                arc_queue_insert(it);
            wire_to_arc[wire].clear();
            ctx->unbindWire(wire);
        }

        NetInfo *net = ctx->getConflictingWireNet(wire);
        if (net != nullptr) {
            wireScores[wire] += net->wires.size();
            ripup_net(net);
        }

        ripup_flag = true;
    }

    void ripup_pip(PipId pip)
    {
        if (ctx->debug)
            log("    ripup pip %s\n", ctx->getPipName(pip).c_str(ctx));

        WireId wire = ctx->getPipDstWire(pip);
        wireScores[wire]++;
        pipScores[pip]++;

        if (ctx->getBoundPipNet(pip)) {
            for (auto &it : wire_to_arc[wire])
                arc_queue_insert(it);
            wire_to_arc[wire].clear();
            ctx->unbindPip(pip);
        }

        NetInfo *net = ctx->getConflictingPipNet(pip);
        if (net != nullptr) {
            wireScores[wire] += net->wires.size();
            pipScores[pip] += net->wires.size();
            ripup_net(net);
        }

        ripup_flag = true;
    }

    void setup()
    {
        for (auto &net_it : ctx->nets)
        {
            NetInfo *net_info = net_it.second.get();

#ifdef ARCH_ECP5
            // ECP5 global nets currently appear part-unrouted due to arch database limitations
            // Don't touch them in the router
            if (net_info->is_global)
                return;
#endif
            if (net_info->driver.cell == nullptr)
                return;

            auto src_wire = ctx->getNetinfoSourceWire(net_info);

            if (src_wire == WireId())
                log_error("No wire found for port %s on source cell %s.\n", net_info->driver.port.c_str(ctx),
                          net_info->driver.cell->name.c_str(ctx));

            for (int user_idx = 0; user_idx < int(net_info->users.size()); user_idx++) {
                auto dst_wire = ctx->getNetinfoSinkWire(net_info, net_info->users[user_idx]);

                if (dst_wire == WireId())
                    log_error("No wire found for port %s on destination cell %s.\n", net_info->users[user_idx].port.c_str(ctx),
                              net_info->users[user_idx].cell->name.c_str(ctx));

                arc_key arc;
                arc.net_info = net_info;
                arc.user_idx = user_idx;

                WireId cursor = dst_wire;
                wire_to_arc[cursor].insert(arc);

                while (src_wire != cursor) {
                    auto it = net_info->wires.find(cursor);
                    if (it == net_info->wires.end()) {
                        arc_queue_insert(arc, src_wire, dst_wire);
                        break;
                    }

                    NPNR_ASSERT(it->second.pip != PipId());
                    cursor = ctx->getPipSrcWire(it->second.pip);
                    wire_to_arc[cursor].insert(arc);
                }
            }

            std::vector<WireId> unbind_wires;

            for (auto &it : net_info->wires)
                if (it.second.strength < STRENGTH_LOCKED && wire_to_arc.count(it.first) == 0)
                    unbind_wires.push_back(it.first);

            for (auto it : unbind_wires)
                ctx->unbindWire(it);
        }
    }

    arc_key arc_queue_pop()
    {
        arc_entry entry = arc_queue.top();
        arc_queue.pop();
        queued_arcs.erase(entry.arc);
        return entry.arc;
    }

    bool route_arc(const arc_key &arc, bool ripup)
    {

        NetInfo *net_info = arc.net_info;
        int user_idx = arc.user_idx;

        auto src_wire = ctx->getNetinfoSourceWire(net_info);
        auto dst_wire = ctx->getNetinfoSinkWire(net_info, net_info->users[user_idx]);
        ripup_flag = false;

        if (ctx->debug) {
            log("Routing arc %d on net %s (%d arcs total):\n", user_idx, net_info->name.c_str(ctx), int(net_info->users.size()));
            log("  source ... %s\n", ctx->getWireName(src_wire).c_str(ctx));
            log("  sink ..... %s\n", ctx->getWireName(dst_wire).c_str(ctx));
        }

        // unbind wires that are currently used exclusively by this arc

        std::vector<WireId> unbind_wires;

        for (auto &wire_it : net_info->wires) {
            auto wire = wire_it.first;
            auto wire_to_arc_it = wire_to_arc.find(wire);
            NPNR_ASSERT(wire_to_arc_it != wire_to_arc.end());

            wire_to_arc_it->second.erase(arc);
            if (wire_to_arc_it->second.empty())
                unbind_wires.push_back(wire);
        }

        for (auto it : unbind_wires)
            ctx->unbindWire(it);

        // reset wire queue

        if (!queue.empty()) {
            std::priority_queue<QueuedWire, std::vector<QueuedWire>, QueuedWire::Greater> new_queue;
            queue.swap(new_queue);
        }
        visited.clear();

        int visitCnt = 0;
        int maxVisitCnt = INT_MAX;
        delay_t best_est = 0;

        {
            QueuedWire qw;
            qw.wire = src_wire;
            qw.pip = PipId();
            qw.delay = ctx->getWireDelay(qw.wire).maxDelay();
            qw.penalty = 0;
            if (cfg.useEstimate) {
                qw.togo = ctx->estimateDelay(qw.wire, dst_wire);
                best_est = qw.delay + qw.togo;
            }
            qw.randtag = ctx->rng();

            queue.push(qw);
            visited[qw.wire] = qw;
        }

        while (visitCnt++ < maxVisitCnt && !queue.empty())
        {
            QueuedWire qw = queue.top();
            queue.pop();

            for (auto pip : ctx->getPipsDownhill(qw.wire)) {
                delay_t next_delay = qw.delay + ctx->getPipDelay(pip).maxDelay();
                delay_t next_penalty = qw.penalty;

                WireId next_wire = ctx->getPipDstWire(pip);
                next_delay += ctx->getWireDelay(next_wire).maxDelay();

                if (!ctx->checkWireAvail(next_wire)) {
                    NetInfo *ripupWireNet = ctx->getConflictingWireNet(next_wire);

                    if (ripupWireNet == nullptr)
                        continue;

                    if (ripupWireNet == net_info) {
                        next_penalty += cfg.wireReusePenalty;
                    } else {
                        if (!ripup)
                            continue;

                        auto scores_it = wireScores.find(next_wire);
                        if (scores_it != wireScores.end())
                            next_penalty += scores_it->second * cfg.wireRipupPenalty;
                    }
                }

                if (!ctx->checkPipAvail(pip)) {
                    NetInfo *ripupPipNet = ctx->getConflictingPipNet(pip);

                    if (ripupPipNet == nullptr)
                        continue;

                    if (ripupPipNet == net_info) {
                        next_penalty += cfg.pipReusePenalty;
                    } else {
                        if (!ripup)
                            continue;

                        auto scores_it = pipScores.find(pip);
                        if (scores_it != pipScores.end())
                            next_penalty += scores_it->second * cfg.pipRipupPenalty;
                    }
                }

                if (visited.count(next_wire)) {
                    if (visited.at(next_wire).delay + visited.at(next_wire).penalty <= next_delay + next_penalty + ctx->getDelayEpsilon())
                        continue;
#if 0 // FIXME
                    if (ctx->debug)
                        log("Found better route to %s. Old vs new delay estimate: %.3f %.3f\n",
                            ctx->getWireName(next_wire).c_str(),
                            ctx->getDelayNS(visited.at(next_wire).delay),
                            ctx->getDelayNS(next_delay));
#endif
                }

                QueuedWire next_qw;
                next_qw.wire = next_wire;
                next_qw.pip = pip;
                next_qw.delay = next_delay;
                next_qw.penalty = next_penalty;
                if (cfg.useEstimate) {
                    next_qw.togo = ctx->estimateDelay(next_wire, dst_wire);
                    delay_t this_est = next_qw.delay + next_qw.togo;
                    if (this_est > best_est + cfg.estimatePrecision)
                        continue;
                    if (best_est > this_est)
                        best_est = this_est;
                }
                next_qw.randtag = ctx->rng();

                visited[next_qw.wire] = next_qw;
                queue.push(next_qw);

                if (maxVisitCnt == INT_MAX && next_wire == dst_wire)
                    maxVisitCnt = 2*visitCnt;
            }
        }

        if (ctx->debug)
            log("  total number of visited nodes: %d\n", visitCnt);

        if (visited.count(dst_wire) == 0) {
            if (ctx->debug)
                log("  no route found for this arc\n");
            return false;
        }

        WireId cursor = dst_wire;
        while (1) {
            if (ctx->debug)
                log("  node %s\n", ctx->getWireName(cursor).c_str(ctx));

            if (!ctx->checkWireAvail(cursor)) {
                NetInfo *ripupWireNet = ctx->getConflictingWireNet(cursor);
                NPNR_ASSERT(ripupWireNet != nullptr);

                if (ripupWireNet != net_info)
                    ripup_wire(cursor);
            }

            auto pip = visited[cursor].pip;

            if (pip == PipId()) {
                NPNR_ASSERT(cursor == src_wire);
            } else {
                if (!ctx->checkPipAvail(pip)) {
                    NetInfo *ripupPipNet = ctx->getConflictingPipNet(pip);
                    NPNR_ASSERT(ripupPipNet != nullptr);

                    if (ripupPipNet != net_info)
                        ripup_pip(pip);
                }
            }

            if (ctx->checkWireAvail(cursor)) {
                if (pip == PipId())
                    ctx->bindWire(cursor, net_info, STRENGTH_WEAK);
                else if (ctx->checkPipAvail(pip))
                    ctx->bindPip(pip, net_info, STRENGTH_WEAK);
            }

            wire_to_arc[cursor].insert(arc);

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
};

} // namespace

NEXTPNR_NAMESPACE_BEGIN

Router1Cfg::Router1Cfg(Context *ctx) : Settings(ctx)
{
    maxIterCnt = get<int>("router1/maxIterCnt", 200);
    cleanupReroute = get<bool>("router1/cleanupReroute", true);
    fullCleanupReroute = get<bool>("router1/fullCleanupReroute", true);
    useEstimate = get<bool>("router1/useEstimate", true);

    wireRipupPenalty = ctx->getRipupDelayPenalty();
    pipRipupPenalty = ctx->getRipupDelayPenalty();

    wireReusePenalty = -wireRipupPenalty/8;
    pipReusePenalty = -pipRipupPenalty/8;

    estimatePrecision = 100 * ctx->getRipupDelayPenalty();
}

bool router1(Context *ctx, const Router1Cfg &cfg)
{
    try {
        log_break();
        log_info("Routing..\n");
        ctx->lock();

        log_info("Setting up routing queue.\n");

        Router1 router(ctx, cfg);
        router.setup();

        log_info("Added %d arcs to routing queue.\n", int(router.arc_queue.size()));

        int iter_cnt = 0;
        int last_arcs_with_ripup = 0;
        int last_arcs_without_ripup = 0;

        while (!router.arc_queue.empty()) {
            if (++iter_cnt % 1000 == 0) {
                log_info("At iteration %d:\n", iter_cnt);
                log_info("  routed %d (%d) arcs with rip-up.\n", router.arcs_with_ripup, router.arcs_with_ripup - last_arcs_with_ripup);
                log_info("  routed %d (%d) arcs without rip-up.\n", router.arcs_without_ripup, router.arcs_without_ripup - last_arcs_without_ripup);
                log_info("  %d arcs remaining in routing queue.\n", int(router.arc_queue.size()));
                last_arcs_with_ripup = router.arcs_with_ripup;
                last_arcs_without_ripup = router.arcs_without_ripup;
            }

            arc_key arc = router.arc_queue_pop();

            if (!router.route_arc(arc, true)) {
                log_warning("Failed to find a route for arc %d of net %s.\n",
                        arc.user_idx, arc.net_info->name.c_str(ctx));
#ifndef NDEBUG
                ctx->check();
#endif
                ctx->unlock();
                return false;
            }
        }

        log_info("At iteration %d:\n", iter_cnt);
        log_info("  routed %d (%d) arcs with rip-up.\n", router.arcs_with_ripup, router.arcs_with_ripup - last_arcs_with_ripup);
        log_info("  routed %d (%d) arcs without rip-up.\n", router.arcs_without_ripup, router.arcs_without_ripup - last_arcs_without_ripup);
        log_info("  %d arcs remaining in routing queue.\n", int(router.arc_queue.size()));

        log_info("Routing finished after %d iterations.\n", iter_cnt);

        log_info("Checksum: 0x%08x\n", ctx->checksum());
#ifndef NDEBUG
        ctx->check();
#endif
        timing_analysis(ctx, true /* slack_histogram */, true /* print_path */);
        ctx->unlock();
        return true;
    } catch (log_execution_error_exception) {
#ifndef NDEBUG
        ctx->check();
#endif
        ctx->unlock();
        return false;
    }
}

bool Context::getActualRouteDelay(WireId src_wire, WireId dst_wire, delay_t *delay,
                                  std::unordered_map<WireId, PipId> *route, bool useEstimate)
{
    // FIXME
    return false;
}

NEXTPNR_NAMESPACE_END
