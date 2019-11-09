/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019  David Shah <dave@ds0.me>
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
 *  Core routing algorithm based on CRoute:
 *
 *     CRoute: A Fast High-quality Timing-driven Connection-based FPGA Router
 *     Dries Vercruyce, Elias Vansteenkiste and Dirk Stroobandt
 *     DOI 10.1109/FCCM.2019.00017 [PDF on SciHub]
 *
 *  Modified for the nextpnr Arch API and data structures; optimised for
 *  real-world FPGA architectures in particular ECP5 and Xilinx UltraScale+
 *
 */

#include <algorithm>
#include <boost/container/flat_map.hpp>
#include <deque>
#include <queue>
#include "log.h"
#include "nextpnr.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct Router2
{

    struct PerArcData
    {
        std::unordered_map<WireId, PipId> wires;
        ArcBounds bb;
    };

    // As we allow overlap at first; the nextpnr bind functions can't be used
    // as the primary relation between arcs and wires/pips
    struct PerNetData
    {
        std::vector<PerArcData> arcs;
        ArcBounds bb;
        // Coordinates of the center of the net, used for the weight-to-average
        int cx, cy, hpwl;
    };

    struct PerWireData
    {
        // net --> number of arcs; driving pip
        std::unordered_map<int, std::pair<int, PipId>> bound_nets;
        // Which net is bound in the Arch API
        int arch_bound_net = -1;
        // Historical congestion cost
        float hist_cong_cost = 1.0;
        // Wire is unavailable as locked to another arc
        bool unavailable = false;
    };

    float present_wire_cost(const PerWireData &w)
    {
        if (w.bound_nets.size() <= 1)
            return 1.0f;
        else
            return 1 + (int(w.bound_nets.size()) - 1) * curr_cong_weight;
    }

    struct WireScore
    {
        float cost;
        float togo_cost;
        delay_t delay;
        float total() const { return cost + togo_cost; }
    };

    Context *ctx;

    // Use 'udata' for fast net lookups and indexing
    std::vector<NetInfo *> nets_by_udata;
    std::vector<PerNetData> nets;
    void setup_nets()
    {
        // Populate per-net and per-arc structures at start of routing
        nets.resize(ctx->nets.size());
        nets_by_udata.resize(ctx->nets.size());
        size_t i = 0;
        for (auto net : sorted(ctx->nets)) {
            NetInfo *ni = net.second;
            ni->udata = i;
            nets_by_udata.at(i) = ni;
            nets.at(i).arcs.resize(ni->users.size());

            // Start net bounding box at overall min/max
            nets.at(i).bb.x0 = std::numeric_limits<int>::max();
            nets.at(i).bb.x1 = std::numeric_limits<int>::min();
            nets.at(i).bb.y0 = std::numeric_limits<int>::max();
            nets.at(i).bb.y1 = std::numeric_limits<int>::min();
            nets.at(i).cx = 0;
            nets.at(i).cy = 0;

            if (ni->driver.cell != nullptr) {
                Loc drv_loc = ctx->getBelLocation(ni->driver.cell->bel);
                nets.at(i).cx += drv_loc.x;
                nets.at(i).cy += drv_loc.y;
            }

            for (size_t j = 0; j < ni->users.size(); j++) {
                auto &usr = ni->users.at(j);
                WireId src_wire = ctx->getNetinfoSourceWire(ni), dst_wire = ctx->getNetinfoSinkWire(ni, usr);
                if (ni->driver.cell == nullptr)
                    src_wire = dst_wire;
                if (src_wire == WireId())
                    log_error("No wire found for port %s on source cell %s.\n", ctx->nameOf(ni->driver.port),
                              ctx->nameOf(ni->driver.cell));
                if (dst_wire == WireId())
                    log_error("No wire found for port %s on destination cell %s.\n", ctx->nameOf(usr.port),
                              ctx->nameOf(usr.cell));
                // Set bounding box for this arc
                nets.at(i).arcs.at(j).bb = ctx->getRouteBoundingBox(src_wire, dst_wire);
                // Expand net bounding box to include this arc
                nets.at(i).bb.x0 = std::min(nets.at(i).bb.x0, nets.at(i).arcs.at(j).bb.x0);
                nets.at(i).bb.x1 = std::max(nets.at(i).bb.x1, nets.at(i).arcs.at(j).bb.x1);
                nets.at(i).bb.y0 = std::min(nets.at(i).bb.y0, nets.at(i).arcs.at(j).bb.y0);
                nets.at(i).bb.y1 = std::max(nets.at(i).bb.y1, nets.at(i).arcs.at(j).bb.y1);
                // Add location to centroid sum
                Loc usr_loc = ctx->getBelLocation(usr.cell->bel);
                nets.at(i).cx += usr_loc.x;
                nets.at(i).cy += usr_loc.y;
            }
            nets.at(i).hpwl = std::min(
                    std::abs(nets.at(i).bb.y1 - nets.at(i).bb.y0) + std::abs(nets.at(i).bb.x1 - nets.at(i).bb.x0), 1);
            nets.at(i).cx /= int(ni->users.size() + 1);
            nets.at(i).cy /= int(ni->users.size() + 1);
            i++;
        }
    }

    boost::container::flat_map<WireId, PerWireData> wires;
    void setup_wires()
    {
        // Set up per-wire structures, so that MT parts don't have to do any memory allocation
        // This is possibly quite wasteful and not cache-optimal; further consideration necessary
        for (auto wire : ctx->getWires()) {
            wires[wire];
            NetInfo *bound = ctx->getBoundWireNet(wire);
            if (bound != nullptr) {
                wires[wire].bound_nets[bound->udata] = std::make_pair(1, bound->wires.at(wire).pip);
                wires[wire].arch_bound_net = bound->udata;
                if (bound->wires.at(wire).strength > STRENGTH_STRONG)
                    wires[wire].unavailable = true;
            }
        }
    }

    struct QueuedWire
    {

        explicit QueuedWire(WireId wire = WireId(), PipId pip = PipId(), Loc loc = Loc(), WireScore score = WireScore{},
                            int randtag = 0)
                : wire(wire), pip(pip), loc(loc), score(score), randtag(randtag){};

        WireId wire;
        PipId pip;
        Loc loc;
        WireScore score;
        int randtag = 0;

        struct Greater
        {
            bool operator()(const QueuedWire &lhs, const QueuedWire &rhs) const noexcept
            {
                float lhs_score = lhs.score.cost + lhs.score.togo_cost,
                      rhs_score = rhs.score.cost + rhs.score.togo_cost;
                return lhs_score == rhs_score ? lhs.randtag > rhs.randtag : lhs_score > rhs_score;
            }
        };
    };

    int bb_margin_x = 3, bb_margin_y = 3; // number of units outside the bounding box we may go
    bool hit_test_pip(ArcBounds &bb, Loc l)
    {
        return l.x >= (bb.x0 - bb_margin_x) && l.x <= (bb.x1 + bb_margin_x) && l.y >= (bb.y0 - bb_margin_y) &&
               l.y <= (bb.y1 + bb_margin_y);
    }

    double curr_cong_weight, hist_cong_weight, estimate_weight;
    // Soft-route a net (don't touch Arch data structures which might not be thread safe)
    // If is_mt is true, then strict bounding box rules are applied and log_* won't be called
    struct VisitInfo
    {
        WireScore score;
        PipId pip;
    };
    struct ThreadContext
    {
        std::priority_queue<QueuedWire, std::vector<QueuedWire>, QueuedWire::Greater> queue;
        std::unordered_map<WireId, VisitInfo> visited;
    };

    enum ArcRouteResult
    {
        ARC_SUCCESS,
        ARC_RETRY_WITHOUT_BB,
        ARC_FATAL,
    };

// Define to make sure we don't print in a multithreaded context
#define ARC_LOG_ERR(...)                                                                                               \
    do {                                                                                                               \
        if (is_mt)                                                                                                     \
            return ARC_FATAL;                                                                                          \
        else                                                                                                           \
            log_error(__VA_ARGS__);                                                                                    \
    } while (0)
#define ROUTE_LOG_DBG(...)                                                                                             \
    do {                                                                                                               \
        if (!is_mt && ctx->debug)                                                                                      \
            log(__VA_ARGS__);                                                                                          \
    } while (0)

    void bind_pip_internal(NetInfo *net, size_t user, WireId wire, PipId pip)
    {
        auto &b = wires.at(wire).bound_nets[net->udata];
        ++b.first;
        if (b.first == 1) {
            b.second = pip;
        } else {
            NPNR_ASSERT(b.second == pip);
        }
        nets.at(net->udata).arcs.at(user).wires[wire] = pip;
    }

    void unbind_pip_internal(NetInfo *net, size_t user, WireId wire, bool dont_touch_arc = false)
    {
        auto &b = wires.at(wire).bound_nets[net->udata];
        --b.first;
        if (b.first == 0) {
            wires.at(wire).bound_nets.erase(net->udata);
        }
        if (!dont_touch_arc)
            nets.at(net->udata).arcs.at(user).wires.erase(wire);
    }

    void ripup_arc(NetInfo *net, size_t user)
    {
        auto &ad = nets.at(net->udata).arcs.at(user);
        for (auto &wire : ad.wires)
            unbind_pip_internal(net, user, wire.first, true);
        ad.wires.clear();
    }

    float score_wire_for_arc(NetInfo *net, size_t user, WireId wire, PipId pip)
    {
        auto &wd = wires.at(wire);
        auto &nd = nets.at(net->udata);
        float base_cost = ctx->getDelayNS(ctx->getPipDelay(pip).maxDelay() + ctx->getWireDelay(wire).maxDelay() +
                                          ctx->getDelayEpsilon());
        float present_cost = present_wire_cost(wd);
        float hist_cost = wd.hist_cong_cost;
        float bias_cost = 0;
        int source_uses = 0;
        if (wd.bound_nets.count(net->udata))
            source_uses = wd.bound_nets.at(net->udata).first;
        if (pip != PipId()) {
            Loc pl = ctx->getPipLocation(pip);
            bias_cost = 0.5f * (base_cost / int(net->users.size())) *
                        ((std::abs(pl.x - nd.cx) + std::abs(pl.y - nd.cy)) / float(nd.hpwl));
        }
        return base_cost * hist_cost * present_cost / (1 + source_uses) + bias_cost;
    }

    float get_togo_cost(NetInfo *net, size_t user, WireId wire, WireId sink)
    {
        auto &wd = wires.at(wire);
        int source_uses = 0;
        if (wd.bound_nets.count(net->udata))
            source_uses = wd.bound_nets.at(net->udata).first;
        // FIXME: timing/wirelength balance?
        return ctx->getDelayNS(ctx->estimateDelay(wire, sink)) / (1 + source_uses);
    }

    bool check_arc_routing(NetInfo *net, size_t usr)
    {
        auto &ad = nets.at(net->udata).arcs.at(usr);
        WireId src_wire = ctx->getNetinfoSourceWire(net);
        WireId dst_wire = ctx->getNetinfoSinkWire(net, net->users.at(usr));
        WireId cursor = dst_wire;
        while (ad.wires.count(cursor)) {
            auto &wd = wires.at(cursor);
            if (wd.bound_nets.size() != 1)
                return false;
            auto &uh = ad.wires.at(cursor);
            if (uh == PipId())
                break;
            cursor = ctx->getPipSrcWire(uh);
        }
        return (cursor == src_wire);
    }

    ArcRouteResult route_arc(ThreadContext &t, NetInfo *net, size_t i, bool is_mt, bool is_bb = true)
    {

        auto &nd = nets[net->udata];
        auto &ad = nd.arcs[i];
        auto &usr = net->users.at(i);
        ROUTE_LOG_DBG("Routing arc %d of net '%s'\n", int(i), ctx->nameOf(net));
        WireId src_wire = ctx->getNetinfoSourceWire(net), dst_wire = ctx->getNetinfoSinkWire(net, usr);

        if (src_wire == WireId())
            ARC_LOG_ERR("No wire found for port %s on source cell %s.\n", ctx->nameOf(net->driver.port),
                        ctx->nameOf(net->driver.cell));
        if (dst_wire == WireId())
            ARC_LOG_ERR("No wire found for port %s on destination cell %s.\n", ctx->nameOf(usr.port),
                        ctx->nameOf(usr.cell));
        // Case of arcs that were pre-routed strongly (e.g. clocks)
        if (net->wires.count(dst_wire) && net->wires.at(dst_wire).strength > STRENGTH_STRONG)
            return ARC_SUCCESS;
        // Check if arc is already legally routed
        if (check_arc_routing(net, i))
            return ARC_SUCCESS;
        // Ripup arc to start with
        ripup_arc(net, i);

        if (!t.queue.empty()) {
            std::priority_queue<QueuedWire, std::vector<QueuedWire>, QueuedWire::Greater> new_queue;
            t.queue.swap(new_queue);
        }
        t.visited.clear();

        // Add source wire to queue
        WireScore base_score;
        base_score.cost = 0;
        base_score.delay = ctx->getWireDelay(src_wire).maxDelay();
        base_score.togo_cost = get_togo_cost(net, i, src_wire, dst_wire);
        t.queue.push(QueuedWire(src_wire, PipId(), Loc(), base_score));
        t.visited[src_wire].score = base_score;
        t.visited[src_wire].pip = PipId();

        while (!t.queue.empty()) {
            auto curr = t.queue.top();
            t.queue.pop();
#if 0
            ROUTE_LOG_DBG("current wire %s\n", ctx->nameOfWire(curr.wire));
#endif
            // Explore all pips downhill of cursor
            for (auto dh : ctx->getPipsDownhill(curr.wire)) {
                // Skip pips outside of box in bounding-box mode
#if 0
                ROUTE_LOG_DBG("trying pip %s\n", ctx->nameOfPip(dh));
#endif
                if (is_bb && !hit_test_pip(ad.bb, ctx->getPipLocation(dh)))
                    continue;
                // Evaluate score of next wire
                WireId next = ctx->getPipDstWire(dh);
#if 0
                ROUTE_LOG_DBG("   src wire %s\n", ctx->nameOfWire(next));
#endif
                auto &nwd = wires.at(next);
                if (nwd.unavailable)
                    continue;
                if (nwd.bound_nets.count(net->udata) && nwd.bound_nets.at(net->udata).second != dh)
                    continue;
                WireScore next_score;
                next_score.cost = curr.score.cost + score_wire_for_arc(net, i, next, dh);
                next_score.delay =
                        curr.score.delay + ctx->getPipDelay(dh).maxDelay() + ctx->getWireDelay(next).maxDelay();
                next_score.togo_cost = get_togo_cost(net, i, next, dst_wire);
                if (!t.visited.count(next) || (t.visited.at(next).score.total() > next_score.total())) {
#if 0
                    ROUTE_LOG_DBG("exploring wire %s cost %f togo %f\n", ctx->nameOfWire(next), next_score.cost,
                                  next_score.togo_cost);
#endif
                    // Add wire to queue if it meets criteria
                    t.queue.push(QueuedWire(next, dh, ctx->getPipLocation(dh), next_score, ctx->rng()));
                    t.visited[next].score = next_score;
                    t.visited[next].pip = dh;
                    if (next == dst_wire)
                        goto loop_done;
                }
            }
            if (false) {
            loop_done:
                break;
            }
        }
        if (t.visited.count(dst_wire)) {
            ROUTE_LOG_DBG("   Routed: ");
            WireId cursor_bwd = dst_wire;
            while (t.visited.count(cursor_bwd)) {
                ROUTE_LOG_DBG("      wire: %s\n", ctx->nameOfWire(cursor_bwd));
                auto &v = t.visited.at(cursor_bwd);
                bind_pip_internal(net, i, cursor_bwd, v.pip);
                if (v.pip == PipId()) {
                    NPNR_ASSERT(cursor_bwd == src_wire);
                    break;
                }
                ROUTE_LOG_DBG("         pip: %s\n", ctx->nameOfPip(v.pip));
                cursor_bwd = ctx->getPipSrcWire(v.pip);
            }
            return ARC_SUCCESS;
        } else {
            return ARC_RETRY_WITHOUT_BB;
        }
    }
#undef ARC_ERR

    bool route_net(ThreadContext &t, NetInfo *net, bool is_mt)
    {
        ROUTE_LOG_DBG("Routing net '%s'...\n", ctx->nameOf(net));

        // Nothing to do if net is undriven
        if (net->driver.cell == nullptr)
            return true;

        bool have_failures = false;
        for (size_t i = 0; i < net->users.size(); i++) {
            auto res1 = route_arc(t, net, i, is_mt, true);
            if (res1 == ARC_FATAL)
                return false; // Arc failed irrecoverably
            else if (res1 == ARC_RETRY_WITHOUT_BB) {
                if (is_mt) {
                    // Can't break out of bounding box in multi-threaded mode, so mark this arc as a failure
                    have_failures = true;
                } else {
                    // Attempt a re-route without the bounding box constraint
                    ROUTE_LOG_DBG("Rerouting arc %d of net '%s' without bounding box, possible tricky routing...\n",
                                  int(i), ctx->nameOf(net));
                    auto res2 = route_arc(t, net, i, is_mt, false);
                    // If this also fails, no choice but to give up
                    if (res2 != ARC_SUCCESS)
                        log_error("Failed to route arc %d of net '%s'.\n", int(i), ctx->nameOf(net));
                }
            }
        }
        return !have_failures;
    }
#undef ROUTE_LOG_DBG

    int total_wire_use = 0;
    int overused_wires = 0;
    int total_overuse = 0;

    void update_congestion()
    {
        total_overuse = 0;
        overused_wires = 0;
        total_wire_use = 0;
        for (auto &wire : wires) {
            total_wire_use += int(wire.second.bound_nets.size());
            int overuse = int(wire.second.bound_nets.size()) - 1;
            if (overuse > 0) {
                wire.second.hist_cong_cost += overuse * hist_cong_weight;
                total_overuse += overuse;
                overused_wires += 1;
            }
        }
    }

    void router_test()
    {
        setup_nets();
        setup_wires();
        curr_cong_weight = 0.5;
        hist_cong_weight = 1.0;
        ThreadContext st;
        int iter = 1;
        do {
            for (auto net : nets_by_udata)
                route_net(st, net, false);
            update_congestion();
            log_info("iter=%d wires=%d overused=%d overuse=%d\n", iter, total_overuse, overused_wires, total_overuse);
            ++iter;
            curr_cong_weight *= 2;
        } while (total_overuse > 0);
    }
};
} // namespace

void router2_test(Context *ctx)
{
    Router2 rt;
    rt.ctx = ctx;
    rt.router_test();
}

NEXTPNR_NAMESPACE_END