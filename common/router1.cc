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

struct hash_id_wire
{
    std::size_t operator()(const std::pair<IdString, WireId> &arg) const noexcept
    {
        std::size_t seed = std::hash<IdString>()(arg.first);
        seed ^= std::hash<WireId>()(arg.second) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct hash_id_pip
{
    std::size_t operator()(const std::pair<IdString, PipId> &arg) const noexcept
    {
        std::size_t seed = std::hash<IdString>()(arg.first);
        seed ^= std::hash<PipId>()(arg.second) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct QueuedWire
{
    WireId wire;
    PipId pip;

    delay_t delay = 0, togo = 0;
    int randtag = 0;

    struct Greater
    {
        bool operator()(const QueuedWire &lhs, const QueuedWire &rhs) const noexcept
        {
            delay_t l = lhs.delay + lhs.togo, r = rhs.delay + rhs.togo;
            return l == r ? lhs.randtag > rhs.randtag : l > r;
        }
    };
};

struct RipupScoreboard
{
    std::unordered_map<WireId, int> wireScores;
    std::unordered_map<PipId, int> pipScores;
    std::unordered_map<std::pair<IdString, WireId>, int, hash_id_wire> netWireScores;
    std::unordered_map<std::pair<IdString, PipId>, int, hash_id_pip> netPipScores;
};

void ripup_net(Context *ctx, IdString net_name)
{
    if (ctx->debug)
        log("Ripping up all routing for net %s.\n", net_name.c_str(ctx));

    auto net_info = ctx->nets.at(net_name).get();
    std::vector<PipId> pips;
    std::vector<WireId> wires;

    pips.reserve(net_info->wires.size());
    wires.reserve(net_info->wires.size());

    for (auto &it : net_info->wires) {
        if (it.second.pip != PipId())
            pips.push_back(it.second.pip);
        else
            wires.push_back(it.first);
    }

    for (auto pip : pips)
        ctx->unbindPip(pip);

    for (auto wire : wires)
        ctx->unbindWire(wire);

    NPNR_ASSERT(net_info->wires.empty());
}

struct Router
{
    Context *ctx;
    RipupScoreboard scores;
    IdString net_name;

    bool ripup;
    delay_t ripup_penalty;

    std::unordered_set<IdString> rippedNets;
    std::unordered_map<WireId, QueuedWire> visited;
    int visitCnt = 0, revisitCnt = 0, overtimeRevisitCnt = 0;
    bool routedOkay = false;
    delay_t maxDelay = 0.0;
    WireId failedDest;

    void route(const std::unordered_map<WireId, delay_t> &src_wires, WireId dst_wire)
    {
        std::priority_queue<QueuedWire, std::vector<QueuedWire>, QueuedWire::Greater> queue;

        visited.clear();

        for (auto &it : src_wires) {
            QueuedWire qw;
            qw.wire = it.first;
            qw.pip = PipId();
            qw.delay = it.second;
            qw.togo = ctx->estimateDelay(qw.wire, dst_wire);
            qw.randtag = ctx->rng();

            queue.push(qw);
            visited[qw.wire] = qw;
        }

        int thisVisitCnt = 0;
        int thisVisitCntLimit = 0;

        while (!queue.empty() && (thisVisitCntLimit == 0 || thisVisitCnt < thisVisitCntLimit)) {
            QueuedWire qw = queue.top();
            queue.pop();

            if (thisVisitCntLimit == 0 && visited.count(dst_wire))
                thisVisitCntLimit = (thisVisitCnt * 3) / 2;

            for (auto pip : ctx->getPipsDownhill(qw.wire)) {
                delay_t next_delay = qw.delay + ctx->getPipDelay(pip).maxDelay();
                WireId next_wire = ctx->getPipDstWire(pip);
                bool foundRipupNet = false;
                thisVisitCnt++;

                next_delay += ctx->getWireDelay(next_wire).maxDelay();

                if (!ctx->checkWireAvail(next_wire)) {
                    if (!ripup)
                        continue;
                    IdString ripupWireNet = ctx->getConflictingWireNet(next_wire);
                    if (ripupWireNet == net_name || ripupWireNet == IdString())
                        continue;

                    auto it1 = scores.wireScores.find(next_wire);
                    if (it1 != scores.wireScores.end())
                        next_delay += (it1->second * ripup_penalty) / 8;

                    auto it2 = scores.netWireScores.find(std::make_pair(ripupWireNet, next_wire));
                    if (it2 != scores.netWireScores.end())
                        next_delay += it2->second * ripup_penalty;

                    foundRipupNet = true;
                }

                if (!ctx->checkPipAvail(pip)) {
                    if (!ripup)
                        continue;
                    IdString ripupPipNet = ctx->getConflictingPipNet(pip);
                    if (ripupPipNet == net_name || ripupPipNet == IdString())
                        continue;

                    auto it1 = scores.pipScores.find(pip);
                    if (it1 != scores.pipScores.end())
                        next_delay += (it1->second * ripup_penalty) / 8;

                    auto it2 = scores.netPipScores.find(std::make_pair(ripupPipNet, pip));
                    if (it2 != scores.netPipScores.end())
                        next_delay += it2->second * ripup_penalty;

                    foundRipupNet = true;
                }

                if (foundRipupNet)
                    next_delay += ripup_penalty;

                NPNR_ASSERT(next_delay >= 0);

                if (visited.count(next_wire)) {
                    if (visited.at(next_wire).delay <= next_delay + ctx->getDelayEpsilon())
                        continue;
#if 0 // FIXME
                    if (ctx->debug)
                        log("Found better route to %s. Old vs new delay "
                            "estimate: %.3f %.3f\n",
                            ctx->getWireName(next_wire).c_str(),
                            ctx->getDelayNS(visited.at(next_wire).delay),
                            ctx->getDelayNS(next_delay));
#endif
                    if (thisVisitCntLimit == 0)
                        revisitCnt++;
                    else
                        overtimeRevisitCnt++;
                }

                QueuedWire next_qw;
                next_qw.wire = next_wire;
                next_qw.pip = pip;
                next_qw.delay = next_delay;
                next_qw.togo = ctx->estimateDelay(next_wire, dst_wire);
                next_qw.randtag = ctx->rng();

                visited[next_qw.wire] = next_qw;
                queue.push(next_qw);
            }
        }

        visitCnt += thisVisitCnt;
    }

    Router(Context *ctx, RipupScoreboard &scores, WireId src_wire, WireId dst_wire, bool ripup = false,
           delay_t ripup_penalty = 0)
            : ctx(ctx), scores(scores), ripup(ripup), ripup_penalty(ripup_penalty)
    {
        std::unordered_map<WireId, delay_t> src_wires;
        src_wires[src_wire] = ctx->getWireDelay(src_wire).maxDelay();
        route(src_wires, dst_wire);
        routedOkay = visited.count(dst_wire);

        if (ctx->debug) {
            log("Route (from destination to source):\n");

            WireId cursor = dst_wire;

            while (1) {
                log("  %8.3f %s\n", ctx->getDelayNS(visited[cursor].delay), ctx->getWireName(cursor).c_str(ctx));

                if (cursor == src_wire)
                    break;

                cursor = ctx->getPipSrcWire(visited[cursor].pip);
            }
        }
    }

    Router(Context *ctx, RipupScoreboard &scores, IdString net_name, int user_idx = -1, bool reroute = false,
           bool ripup = false, delay_t ripup_penalty = 0)
            : ctx(ctx), scores(scores), net_name(net_name), ripup(ripup), ripup_penalty(ripup_penalty)
    {
        auto net_info = ctx->nets.at(net_name).get();

        if (ctx->debug)
            log("Routing net %s.\n", net_name.c_str(ctx));

        if (ctx->debug)
            log("  Source: %s.%s.\n", net_info->driver.cell->name.c_str(ctx), net_info->driver.port.c_str(ctx));

        auto src_wire = ctx->getNetinfoSourceWire(net_info);

        if (src_wire == WireId())
            log_error("No wire found for port %s on source cell %s.\n", net_info->driver.port.c_str(ctx),
                      net_info->driver.cell->name.c_str(ctx));

        if (ctx->debug)
            log("    Source wire: %s\n", ctx->getWireName(src_wire).c_str(ctx));

        std::unordered_map<WireId, delay_t> src_wires;
        std::vector<int> users_array;

        if (user_idx < 0) {
            // route all users
            for (int user_idx = 0; user_idx < int(net_info->users.size()); user_idx++)
                users_array.push_back(user_idx);
            ctx->shuffle(users_array);
        } else {
            // route only the selected user
            users_array.push_back(user_idx);
        }

        if (reroute) {
            // complete ripup
            ripup_net(ctx, net_name);
            ctx->bindWire(src_wire, net_name, STRENGTH_WEAK);
            src_wires[src_wire] = ctx->getWireDelay(src_wire).maxDelay();
        } else {
            // re-use existing routes as much as possible
            if (net_info->wires.count(src_wire) == 0)
                ctx->bindWire(src_wire, net_name, STRENGTH_WEAK);
            src_wires[src_wire] = ctx->getWireDelay(src_wire).maxDelay();

            for (int user_idx = 0; user_idx < int(net_info->users.size()); user_idx++) {
                auto dst_wire = ctx->getNetinfoSinkWire(net_info, user_idx);

                if (dst_wire == WireId())
                    log_error("No wire found for port %s on destination cell %s.\n",
                              net_info->users[user_idx].port.c_str(ctx),
                              net_info->users[user_idx].cell->name.c_str(ctx));

                std::function<delay_t(WireId)> register_existing_path =
                        [ctx, net_info, &src_wires, &register_existing_path](WireId wire) -> delay_t {
                    auto it = src_wires.find(wire);
                    if (it != src_wires.end())
                        return it->second;

                    PipId pip = net_info->wires.at(wire).pip;
                    delay_t delay = register_existing_path(ctx->getPipSrcWire(pip));
                    delay += ctx->getPipDelay(pip).maxDelay();
                    delay += ctx->getWireDelay(wire).maxDelay();
                    delay -= 2 * ctx->getDelayEpsilon();
                    src_wires[wire] = delay;

                    return delay;
                };

                WireId cursor = dst_wire;
                while (src_wires.count(cursor) == 0) {
                    auto it = net_info->wires.find(cursor);
                    if (it == net_info->wires.end())
                        goto check_next_user_for_existing_path;
                    NPNR_ASSERT(it->second.pip != PipId());
                    cursor = ctx->getPipSrcWire(it->second.pip);
                }

                register_existing_path(dst_wire);
            check_next_user_for_existing_path:;
            }

            std::vector<WireId> ripup_wires;
            for (auto &it : net_info->wires)
                if (src_wires.count(it.first) == 0)
                    ripup_wires.push_back(it.first);

            for (auto &it : ripup_wires) {
                if (ctx->debug)
                    log("  Unbind dangling wire for net %s: %s\n", net_name.c_str(ctx),
                        ctx->getWireName(it).c_str(ctx));
                ctx->unbindWire(it);
            }
        }

        for (int user_idx : users_array) {
            if (ctx->debug)
                log("  Route to: %s.%s.\n", net_info->users[user_idx].cell->name.c_str(ctx),
                    net_info->users[user_idx].port.c_str(ctx));

            auto dst_wire = ctx->getNetinfoSinkWire(net_info, user_idx);

            if (dst_wire == WireId())
                log_error("No wire found for port %s on destination cell %s.\n",
                          net_info->users[user_idx].port.c_str(ctx), net_info->users[user_idx].cell->name.c_str(ctx));

            if (ctx->debug) {
                log("    Destination wire: %s\n", ctx->getWireName(dst_wire).c_str(ctx));
                log("    Path delay estimate: %.2f\n", float(ctx->estimateDelay(src_wire, dst_wire)));
            }

            route(src_wires, dst_wire);

            if (visited.count(dst_wire) == 0) {
                if (ctx->debug)
                    log("Failed to route %s -> %s.\n", ctx->getWireName(src_wire).c_str(ctx),
                        ctx->getWireName(dst_wire).c_str(ctx));
                else if (ripup)
                    log_info("Failed to route %s -> %s.\n", ctx->getWireName(src_wire).c_str(ctx),
                             ctx->getWireName(dst_wire).c_str(ctx));
                ripup_net(ctx, net_name);
                failedDest = dst_wire;
                return;
            }

            if (ctx->debug)
                log("    Final path delay: %.3f\n", ctx->getDelayNS(visited[dst_wire].delay));
            maxDelay = fmaxf(maxDelay, visited[dst_wire].delay);

            if (ctx->debug)
                log("    Route (from destination to source):\n");

            WireId cursor = dst_wire;

            while (1) {
                if (ctx->debug)
                    log("    %8.3f %s\n", ctx->getDelayNS(visited[cursor].delay), ctx->getWireName(cursor).c_str(ctx));

                if (src_wires.count(cursor))
                    break;

                IdString conflicting_wire_net = ctx->getConflictingWireNet(cursor);

                if (conflicting_wire_net != IdString()) {
                    NPNR_ASSERT(ripup);
                    NPNR_ASSERT(conflicting_wire_net != net_name);

                    ctx->unbindWire(cursor);
                    if (!ctx->checkWireAvail(cursor))
                        ripup_net(ctx, conflicting_wire_net);

                    rippedNets.insert(conflicting_wire_net);
                    scores.wireScores[cursor]++;
                    scores.netWireScores[std::make_pair(net_name, cursor)]++;
                    scores.netWireScores[std::make_pair(conflicting_wire_net, cursor)]++;
                }

                PipId pip = visited[cursor].pip;
                IdString conflicting_pip_net = ctx->getConflictingPipNet(pip);

                if (conflicting_pip_net != IdString()) {
                    NPNR_ASSERT(ripup);
                    NPNR_ASSERT(conflicting_pip_net != net_name);

                    ctx->unbindPip(pip);
                    if (!ctx->checkPipAvail(pip))
                        ripup_net(ctx, conflicting_pip_net);

                    rippedNets.insert(conflicting_pip_net);
                    scores.pipScores[visited[cursor].pip]++;
                    scores.netPipScores[std::make_pair(net_name, visited[cursor].pip)]++;
                    scores.netPipScores[std::make_pair(conflicting_pip_net, visited[cursor].pip)]++;
                }

                ctx->bindPip(visited[cursor].pip, net_name, STRENGTH_WEAK);
                src_wires[cursor] = visited[cursor].delay;
                cursor = ctx->getPipSrcWire(visited[cursor].pip);
            }
        }

        routedOkay = true;
    }
};

struct RouteJob
{
    IdString net;
    int user_idx = -1;
    delay_t slack = 0;
    int randtag = 0;

    struct Greater
    {
        bool operator()(const RouteJob &lhs, const RouteJob &rhs) const noexcept
        {
            return lhs.slack == rhs.slack ? lhs.randtag > rhs.randtag : lhs.slack > rhs.slack;
        }
    };
};

void addFullNetRouteJob(Context *ctx, IdString net_name, std::unordered_map<IdString, std::vector<bool>> &cache,
                        std::priority_queue<RouteJob, std::vector<RouteJob>, RouteJob::Greater> &queue)
{
    NetInfo *net_info = ctx->nets.at(net_name).get();

    if (net_info->driver.cell == nullptr)
        return;

    auto src_wire = ctx->getNetinfoSourceWire(net_info);

    if (src_wire == WireId())
        log_error("No wire found for port %s on source cell %s.\n", net_info->driver.port.c_str(ctx),
                  net_info->driver.cell->name.c_str(ctx));

    auto &net_cache = cache[net_name];

    if (net_cache.empty())
        net_cache.resize(net_info->users.size());

    RouteJob job;
    job.net = net_name;
    job.user_idx = -1;
    job.slack = 0;
    job.randtag = ctx->rng();

    bool got_slack = false;

    for (int user_idx = 0; user_idx < int(net_info->users.size()); user_idx++) {
        if (net_cache[user_idx])
            continue;

        auto dst_wire = ctx->getNetinfoSinkWire(net_info, user_idx);

        if (dst_wire == WireId())
            log_error("No wire found for port %s on destination cell %s.\n", net_info->users[user_idx].port.c_str(ctx),
                      net_info->users[user_idx].cell->name.c_str(ctx));

        if (user_idx == 0)
            job.slack = net_info->users[user_idx].budget - ctx->estimateDelay(src_wire, dst_wire);
        else
            job.slack = std::min(job.slack, net_info->users[user_idx].budget - ctx->estimateDelay(src_wire, dst_wire));

        WireId cursor = dst_wire;
        while (src_wire != cursor) {
            auto it = net_info->wires.find(cursor);
            if (it == net_info->wires.end()) {
                if (!got_slack)
                    job.slack = net_info->users[user_idx].budget - ctx->estimateDelay(src_wire, dst_wire);
                else
                    job.slack = std::min(job.slack,
                                         net_info->users[user_idx].budget - ctx->estimateDelay(src_wire, dst_wire));
                got_slack = true;
                break;
            }
            NPNR_ASSERT(it->second.pip != PipId());
            cursor = ctx->getPipSrcWire(it->second.pip);
        }
    }

    queue.push(job);

    for (int user_idx = 0; user_idx < int(net_info->users.size()); user_idx++)
        net_cache[user_idx] = true;
}

void addNetRouteJobs(Context *ctx, IdString net_name, std::unordered_map<IdString, std::vector<bool>> &cache,
                     std::priority_queue<RouteJob, std::vector<RouteJob>, RouteJob::Greater> &queue)
{
    NetInfo *net_info = ctx->nets.at(net_name).get();

    if (net_info->driver.cell == nullptr)
        return;

    auto src_wire = ctx->getNetinfoSourceWire(net_info);

    if (src_wire == WireId())
        log_error("No wire found for port %s on source cell %s.\n", net_info->driver.port.c_str(ctx),
                  net_info->driver.cell->name.c_str(ctx));

    auto &net_cache = cache[net_name];

    if (net_cache.empty())
        net_cache.resize(net_info->users.size());

    for (int user_idx = 0; user_idx < int(net_info->users.size()); user_idx++) {
        if (net_cache[user_idx])
            continue;

        auto dst_wire = ctx->getNetinfoSinkWire(net_info, user_idx);

        if (dst_wire == WireId())
            log_error("No wire found for port %s on destination cell %s.\n", net_info->users[user_idx].port.c_str(ctx),
                      net_info->users[user_idx].cell->name.c_str(ctx));

        WireId cursor = dst_wire;
        while (src_wire != cursor) {
            auto it = net_info->wires.find(cursor);
            if (it == net_info->wires.end()) {
                RouteJob job;
                job.net = net_name;
                job.user_idx = user_idx;
                job.slack = net_info->users[user_idx].budget - ctx->estimateDelay(src_wire, dst_wire);
                job.randtag = ctx->rng();
                queue.push(job);
                net_cache[user_idx] = true;
                break;
            }
            NPNR_ASSERT(it->second.pip != PipId());
            cursor = ctx->getPipSrcWire(it->second.pip);
        }
    }
}

} // namespace

NEXTPNR_NAMESPACE_BEGIN

bool router1(Context *ctx)
{
    try {
        int totalVisitCnt = 0, totalRevisitCnt = 0, totalOvertimeRevisitCnt = 0;
        delay_t ripup_penalty = ctx->getRipupDelayPenalty();
        RipupScoreboard scores;

        log_break();
        log_info("Routing..\n");
        ctx->lock();

        std::unordered_map<IdString, std::vector<bool>> jobCache;
        std::priority_queue<RouteJob, std::vector<RouteJob>, RouteJob::Greater> jobQueue;

        for (auto &net_it : ctx->nets)
            addNetRouteJobs(ctx, net_it.first, jobCache, jobQueue);

        if (jobQueue.empty()) {
            log_info("found no unrouted source-sink pairs. no routing necessary.\n");
            return true;
        }

        log_info("found %d unrouted source-sink pairs. starting routing procedure.\n", int(jobQueue.size()));

        int iterCnt = 0;

        while (!jobQueue.empty()) {
            if (iterCnt == 200) {
                log_warning("giving up after %d iterations.\n", iterCnt);
                log_info("Checksum: 0x%08x\n", ctx->checksum());
#ifndef NDEBUG
                ctx->check();
#endif
                return false;
            }

            iterCnt++;
            if (ctx->verbose)
                log_info("-- %d --\n", iterCnt);

            int visitCnt = 0, revisitCnt = 0, overtimeRevisitCnt = 0, jobCnt = 0, failedCnt = 0;

            std::unordered_set<IdString> normalRouteNets, ripupQueue;

            if (ctx->verbose || iterCnt == 1)
                log_info("routing queue contains %d jobs.\n", int(jobQueue.size()));
            else if (iterCnt % ctx->slack_redist_iter == 0)
                assign_budget(ctx, true /* quiet */);

            bool printNets = ctx->verbose && (jobQueue.size() < 10);

            while (!jobQueue.empty()) {
                if (ctx->debug)
                    log("Next job slack: %f\n", double(jobQueue.top().slack));

                auto net_name = jobQueue.top().net;
                auto user_idx = jobQueue.top().user_idx;
                jobQueue.pop();

                if (printNets) {
                    if (user_idx < 0)
                        log_info("  routing all %d users of net %s\n", int(ctx->nets.at(net_name)->users.size()),
                                 net_name.c_str(ctx));
                    else
                        log_info("  routing user %d of net %s\n", user_idx, net_name.c_str(ctx));
                }

                Router router(ctx, scores, net_name, user_idx, false, false);

                jobCnt++;
                visitCnt += router.visitCnt;
                revisitCnt += router.revisitCnt;
                overtimeRevisitCnt += router.overtimeRevisitCnt;

                if (!router.routedOkay) {
                    if (printNets)
                        log_info("    failed to route to %s.\n", ctx->getWireName(router.failedDest).c_str(ctx));
                    ripupQueue.insert(net_name);
                    failedCnt++;
                } else {
                    normalRouteNets.insert(net_name);
                }

                if ((ctx->verbose || iterCnt == 1) && !printNets && (jobCnt % 100 == 0)) {
                    log_info("  processed %d jobs. (%d routed, %d failed)\n", jobCnt, jobCnt - failedCnt, failedCnt);
                    ctx->yield();
                }
            }

            NPNR_ASSERT(jobQueue.empty());
            jobCache.clear();

            if ((ctx->verbose || iterCnt == 1) && (jobCnt % 100 != 0)) {
                log_info("  processed %d jobs. (%d routed, %d failed)\n", jobCnt, jobCnt - failedCnt, failedCnt);
                ctx->yield();
            }

            if (ctx->verbose)
                log_info("  visited %d PIPs (%.2f%% revisits, %.2f%% overtime "
                         "revisits).\n",
                         visitCnt, (100.0 * revisitCnt) / visitCnt, (100.0 * overtimeRevisitCnt) / visitCnt);

            if (!ripupQueue.empty()) {
                if (ctx->verbose || iterCnt == 1)
                    log_info("failed to route %d nets. re-routing in ripup "
                             "mode.\n",
                             int(ripupQueue.size()));

                printNets = ctx->verbose && (ripupQueue.size() < 10);

                visitCnt = 0;
                revisitCnt = 0;
                overtimeRevisitCnt = 0;
                int netCnt = 0;
                int ripCnt = 0;

                std::vector<IdString> ripupArray(ripupQueue.begin(), ripupQueue.end());
                ctx->sorted_shuffle(ripupArray);

                for (auto net_name : ripupArray) {
                    if (printNets)
                        log_info("  routing net %s. (%d users)\n", net_name.c_str(ctx),
                                 int(ctx->nets.at(net_name)->users.size()));

                    Router router(ctx, scores, net_name, -1, false, true, ripup_penalty);

                    netCnt++;
                    visitCnt += router.visitCnt;
                    revisitCnt += router.revisitCnt;
                    overtimeRevisitCnt += router.overtimeRevisitCnt;

                    if (!router.routedOkay)
                        log_error("Net %s is impossible to route.\n", net_name.c_str(ctx));

                    for (auto it : router.rippedNets)
                        addFullNetRouteJob(ctx, it, jobCache, jobQueue);

                    if (printNets) {
                        if (router.rippedNets.size() < 10) {
                            log_info("    ripped up %d other nets:\n", int(router.rippedNets.size()));
                            for (auto n : router.rippedNets)
                                log_info("      %s (%d users)\n", n.c_str(ctx), int(ctx->nets.at(n)->users.size()));
                        } else {
                            log_info("    ripped up %d other nets.\n", int(router.rippedNets.size()));
                        }
                    }

                    ripCnt += router.rippedNets.size();

                    if ((ctx->verbose || iterCnt == 1) && !printNets && (netCnt % 100 == 0)) {
                        log_info("  routed %d nets, ripped %d nets.\n", netCnt, ripCnt);
                        ctx->yield();
                    }
                }

                if ((ctx->verbose || iterCnt == 1) && (netCnt % 100 != 0))
                    log_info("  routed %d nets, ripped %d nets.\n", netCnt, ripCnt);

                if (ctx->verbose)
                    log_info("  visited %d PIPs (%.2f%% revisits, %.2f%% "
                             "overtime revisits).\n",
                             visitCnt, (100.0 * revisitCnt) / visitCnt, (100.0 * overtimeRevisitCnt) / visitCnt);

                if (ctx->verbose && !jobQueue.empty())
                    log_info("  ripped up %d previously routed nets. continue "
                             "routing.\n",
                             int(jobQueue.size()));
            }

            if (!ctx->verbose)
                log_info("iteration %d: routed %d nets without ripup, routed %d nets with ripup.\n", iterCnt,
                         int(normalRouteNets.size()), int(ripupQueue.size()));

            totalVisitCnt += visitCnt;
            totalRevisitCnt += revisitCnt;
            totalOvertimeRevisitCnt += overtimeRevisitCnt;

            if (iterCnt == 8 || iterCnt == 16 || iterCnt == 32 || iterCnt == 64 || iterCnt == 128)
                ripup_penalty += ctx->getRipupDelayPenalty();

            ctx->yield();
        }

        log_info("routing complete after %d iterations.\n", iterCnt);

        log_info("visited %d PIPs (%.2f%% revisits, %.2f%% "
                 "overtime revisits).\n",
                 totalVisitCnt, (100.0 * totalRevisitCnt) / totalVisitCnt,
                 (100.0 * totalOvertimeRevisitCnt) / totalVisitCnt);

        {
            float tns = 0;
            int tns_net_count = 0;
            int tns_arc_count = 0;
            for (auto &net_it : ctx->nets) {
                bool got_negative_slack = false;
                NetInfo *net_info = ctx->nets.at(net_it.first).get();
                for (int user_idx = 0; user_idx < int(net_info->users.size()); user_idx++) {
                    delay_t arc_delay = ctx->getNetinfoRouteDelay(net_info, user_idx);
                    delay_t arc_budget = net_info->users[user_idx].budget;
                    delay_t arc_slack = arc_budget - arc_delay;
                    if (arc_slack < 0) {
                        if (!got_negative_slack) {
                            if (ctx->verbose)
                                log_info("net %s has negative slack arcs:\n", net_info->name.c_str(ctx));
                            tns_net_count++;
                        }
                        if (ctx->verbose)
                            log_info("  arc %s -> %s has %f ns slack (delay %f, budget %f)\n",
                                     ctx->getWireName(ctx->getNetinfoSourceWire(net_info)).c_str(ctx),
                                     ctx->getWireName(ctx->getNetinfoSinkWire(net_info, user_idx)).c_str(ctx),
                                     ctx->getDelayNS(arc_slack), ctx->getDelayNS(arc_delay),
                                     ctx->getDelayNS(arc_budget));
                        tns += ctx->getDelayNS(arc_slack);
                        tns_arc_count++;
                    }
                }
            }
            log_info("final tns with respect to arc budgets: %f ns (%d nets, %d arcs)\n", tns, tns_net_count,
                     tns_arc_count);
        }

        NPNR_ASSERT(jobQueue.empty());
        jobCache.clear();

        for (auto &net_it : ctx->nets)
            addNetRouteJobs(ctx, net_it.first, jobCache, jobQueue);

#ifndef NDEBUG
        if (!jobQueue.empty()) {
            log_info("Design strangely still contains unrouted source-sink pairs:\n");
            while (!jobQueue.empty()) {
                log_info("  user %d on net %s.\n", jobQueue.top().user_idx, jobQueue.top().net.c_str(ctx));
                jobQueue.pop();
            }
            log_info("Checksum: 0x%08x\n", ctx->checksum());
            ctx->check();
            return false;
        }
#endif

        log_info("Checksum: 0x%08x\n", ctx->checksum());
#ifndef NDEBUG
        ctx->check();
#endif
        timing_analysis(ctx, true /* print_fmax */, true /* print_path */);
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

bool Context::getActualRouteDelay(WireId src_wire, WireId dst_wire, delay_t &delay)
{
    RipupScoreboard scores;
    Router router(this, scores, src_wire, dst_wire);
    if (router.routedOkay)
        delay = router.visited.at(dst_wire).delay;
    return router.routedOkay;
}

NEXTPNR_NAMESPACE_END
