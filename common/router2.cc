/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019  gatecat <gatecat@ds0.me>
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

#include "router2.h"

#include <algorithm>
#include <boost/container/flat_map.hpp>
#include <chrono>
#include <deque>
#include <fstream>
#include <queue>

#include "log.h"
#include "nextpnr.h"
#include "router1.h"
#include "scope_lock.h"
#include "timing.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct Router2
{

    struct PerArcData
    {
        WireId sink_wire;
        ArcBounds bb;
        bool routed = false;
        float arc_crit = 0;
    };

    // As we allow overlap at first; the nextpnr bind functions can't be used
    // as the primary relation between arcs and wires/pips
    struct PerNetData
    {
        WireId src_wire;
        std::vector<std::vector<PerArcData>> arcs;
        ArcBounds bb;
        // Coordinates of the center of the net, used for the weight-to-average
        int cx, cy, hpwl;
        int total_route_us = 0;
        float max_crit = 0;
        int fail_count = 0;
    };

    struct WireScore
    {
        float cost;
        float togo_cost;
        delay_t delay;
        float total() const { return cost + togo_cost; }
    };

    struct PerWireData
    {
        // nextpnr
        WireId w;
        // net --> number of arcs; driving pip
        boost::container::flat_map<int, std::pair<int, PipId>> bound_nets;
        // Historical congestion cost
        float hist_cong_cost = 1.0;
        // Wire is unavailable as locked to another arc
        bool unavailable = false;
        // This wire has to be used for this net
        int reserved_net = -1;
        // The notional location of the wire, to guarantee thread safety
        int16_t x = 0, y = 0;
        // Visit data
        struct
        {
            bool dirty = false, visited = false;
            PipId pip;
            WireScore score;
        } visit;
    };

    float present_wire_cost(const PerWireData &w, int net_uid)
    {
        int other_sources = int(w.bound_nets.size());
        if (w.bound_nets.count(net_uid))
            other_sources -= 1;
        if (other_sources == 0)
            return 1.0f;
        else
            return 1 + other_sources * curr_cong_weight;
    }

    Context *ctx;
    Router2Cfg cfg;

    Router2(Context *ctx, const Router2Cfg &cfg) : ctx(ctx), cfg(cfg), tmg(ctx) { tmg.setup(); }

    // Use 'udata' for fast net lookups and indexing
    std::vector<NetInfo *> nets_by_udata;
    std::vector<PerNetData> nets;

    bool timing_driven;
    TimingAnalyser tmg;

    void setup_nets()
    {
        // Populate per-net and per-arc structures at start of routing
        nets.resize(ctx->nets.size());
        nets_by_udata.resize(ctx->nets.size());
        size_t i = 0;
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
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
                WireId src_wire = ctx->getNetinfoSourceWire(ni);
                for (auto &dst_wire : ctx->getNetinfoSinkWires(ni, usr)) {
                    nets.at(i).src_wire = src_wire;
                    if (ni->driver.cell == nullptr)
                        src_wire = dst_wire;
                    if (ni->driver.cell == nullptr && dst_wire == WireId())
                        continue;
                    if (src_wire == WireId())
                        log_error("No wire found for port %s on source cell %s.\n", ctx->nameOf(ni->driver.port),
                                  ctx->nameOf(ni->driver.cell));
                    if (dst_wire == WireId())
                        log_error("No wire found for port %s on destination cell %s.\n", ctx->nameOf(usr.port),
                                  ctx->nameOf(usr.cell));
                    nets.at(i).arcs.at(j).emplace_back();
                    auto &ad = nets.at(i).arcs.at(j).back();
                    ad.sink_wire = dst_wire;
                    // Set bounding box for this arc
                    ad.bb = ctx->getRouteBoundingBox(src_wire, dst_wire);
                    // Expand net bounding box to include this arc
                    nets.at(i).bb.x0 = std::min(nets.at(i).bb.x0, ad.bb.x0);
                    nets.at(i).bb.x1 = std::max(nets.at(i).bb.x1, ad.bb.x1);
                    nets.at(i).bb.y0 = std::min(nets.at(i).bb.y0, ad.bb.y0);
                    nets.at(i).bb.y1 = std::max(nets.at(i).bb.y1, ad.bb.y1);
                }
                // Add location to centroid sum
                Loc usr_loc = ctx->getBelLocation(usr.cell->bel);
                nets.at(i).cx += usr_loc.x;
                nets.at(i).cy += usr_loc.y;
            }
            nets.at(i).hpwl = std::max(
                    std::abs(nets.at(i).bb.y1 - nets.at(i).bb.y0) + std::abs(nets.at(i).bb.x1 - nets.at(i).bb.x0), 1);
            nets.at(i).cx /= int(ni->users.size() + 1);
            nets.at(i).cy /= int(ni->users.size() + 1);
            if (ctx->debug)
                log_info("%s: bb=(%d, %d)->(%d, %d) c=(%d, %d) hpwl=%d\n", ctx->nameOf(ni), nets.at(i).bb.x0,
                         nets.at(i).bb.y0, nets.at(i).bb.x1, nets.at(i).bb.y1, nets.at(i).cx, nets.at(i).cy,
                         nets.at(i).hpwl);
            nets.at(i).bb.x0 = std::max(nets.at(i).bb.x0 - cfg.bb_margin_x, 0);
            nets.at(i).bb.y0 = std::max(nets.at(i).bb.y0 - cfg.bb_margin_y, 0);
            nets.at(i).bb.x1 = std::min(nets.at(i).bb.x1 + cfg.bb_margin_x, ctx->getGridDimX());
            nets.at(i).bb.y1 = std::min(nets.at(i).bb.y1 + cfg.bb_margin_y, ctx->getGridDimY());
            i++;
        }
    }

    dict<WireId, int> wire_to_idx;
    std::vector<PerWireData> flat_wires;

    PerWireData &wire_data(WireId w) { return flat_wires[wire_to_idx.at(w)]; }

    void setup_wires()
    {
        // Set up per-wire structures, so that MT parts don't have to do any memory allocation
        // This is possibly quite wasteful and not cache-optimal; further consideration necessary
        for (auto wire : ctx->getWires()) {
            PerWireData pwd;
            pwd.w = wire;
            NetInfo *bound = ctx->getBoundWireNet(wire);
            if (bound != nullptr) {
                auto iter = bound->wires.find(wire);
                if (iter != bound->wires.end()) {
                    pwd.bound_nets[bound->udata] = std::make_pair(0, bound->wires.at(wire).pip);
                    if (bound->wires.at(wire).strength == STRENGTH_PLACER) {
                        pwd.reserved_net = bound->udata;
                    } else if (bound->wires.at(wire).strength > STRENGTH_PLACER) {
                        pwd.unavailable = true;
                    }
                }
            }

            ArcBounds wire_loc = ctx->getRouteBoundingBox(wire, wire);
            pwd.x = (wire_loc.x0 + wire_loc.x1) / 2;
            pwd.y = (wire_loc.y0 + wire_loc.y1) / 2;

            wire_to_idx[wire] = int(flat_wires.size());
            flat_wires.push_back(pwd);
        }

        for (auto &net_pair : ctx->nets) {
            auto *net = net_pair.second.get();
            auto &nd = nets.at(net->udata);
            for (size_t usr = 0; usr < net->users.size(); usr++) {
                auto &ad = nd.arcs.at(usr);
                for (size_t phys_pin = 0; phys_pin < ad.size(); phys_pin++) {
                    if (check_arc_routing(net, usr, phys_pin)) {
                        record_prerouted_net(net, usr, phys_pin);
                    }
                }
            }
        }
    }

    struct QueuedWire
    {

        explicit QueuedWire(int wire = -1, PipId pip = PipId(), Loc loc = Loc(), WireScore score = WireScore{},
                            int randtag = 0)
                : wire(wire), pip(pip), loc(loc), score(score), randtag(randtag){};

        int wire;
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

    bool hit_test_pip(ArcBounds &bb, Loc l) { return l.x >= bb.x0 && l.x <= bb.x1 && l.y >= bb.y0 && l.y <= bb.y1; }

    double curr_cong_weight, hist_cong_weight, estimate_weight;

    struct ThreadContext
    {
        // Nets to route
        std::vector<NetInfo *> route_nets;
        // Nets that failed routing
        std::vector<NetInfo *> failed_nets;

        std::vector<std::pair<size_t, size_t>> route_arcs;

        std::priority_queue<QueuedWire, std::vector<QueuedWire>, QueuedWire::Greater> queue;
        // Special case where one net has multiple logical arcs to the same physical sink
        pool<WireId> processed_sinks;

        // Backwards routing
        std::queue<int> backwards_queue;

        std::vector<int> dirty_wires;

        // Thread bounding box
        ArcBounds bb;

        DeterministicRNG rng;
    };

    bool thread_test_wire(ThreadContext &t, PerWireData &w)
    {
        return w.x >= t.bb.x0 && w.x <= t.bb.x1 && w.y >= t.bb.y0 && w.y <= t.bb.y1;
    }

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

    void bind_pip_internal(NetInfo *net, size_t user, int wire, PipId pip)
    {
        auto &b = flat_wires.at(wire).bound_nets[net->udata];
        ++b.first;
        if (b.first == 1) {
            b.second = pip;
        } else {
            NPNR_ASSERT(b.second == pip);
        }
    }

    void unbind_pip_internal(NetInfo *net, size_t user, WireId wire)
    {
        auto &b = wire_data(wire).bound_nets.at(net->udata);
        --b.first;
        NPNR_ASSERT(b.first >= 0);
        if (b.first == 0) {
            wire_data(wire).bound_nets.erase(net->udata);
        }
    }

    void ripup_arc(NetInfo *net, size_t user, size_t phys_pin)
    {
        auto &ad = nets.at(net->udata).arcs.at(user).at(phys_pin);
        if (!ad.routed)
            return;
        WireId src = nets.at(net->udata).src_wire;
        WireId cursor = ad.sink_wire;
        while (cursor != src) {
            auto &wd = wire_data(cursor);
            PipId pip = wd.bound_nets.at(net->udata).second;
            unbind_pip_internal(net, user, cursor);
            cursor = ctx->getPipSrcWire(pip);
        }
        ad.routed = false;
    }

    float score_wire_for_arc(NetInfo *net, size_t user, size_t phys_pin, WireId wire, PipId pip)
    {
        auto &wd = wire_data(wire);
        auto &nd = nets.at(net->udata);
        float base_cost = ctx->getDelayNS(ctx->getPipDelay(pip).maxDelay() + ctx->getWireDelay(wire).maxDelay() +
                                          ctx->getDelayEpsilon());
        float present_cost = present_wire_cost(wd, net->udata);
        float hist_cost = wd.hist_cong_cost;
        float bias_cost = 0;
        int source_uses = 0;
        if (wd.bound_nets.count(net->udata))
            source_uses = wd.bound_nets.at(net->udata).first;
        if (timing_driven) {
            float max_bound_crit = 0;
            for (auto &bound : wd.bound_nets)
                if (bound.first != net->udata)
                    max_bound_crit = std::max(max_bound_crit, nets.at(bound.first).max_crit);
            if (max_bound_crit >= 0.8 && nd.arcs.at(user).at(phys_pin).arc_crit < (max_bound_crit + 0.01)) {
                present_cost *= 1.5;
            }
        }
        if (pip != PipId()) {
            Loc pl = ctx->getPipLocation(pip);
            bias_cost = cfg.bias_cost_factor * (base_cost / int(net->users.size())) *
                        ((std::abs(pl.x - nd.cx) + std::abs(pl.y - nd.cy)) / float(nd.hpwl));
        }
        return base_cost * hist_cost * present_cost / (1 + source_uses) + bias_cost;
    }

    float get_togo_cost(NetInfo *net, size_t user, int wire, WireId sink, delay_t *delay)
    {
        auto &wd = flat_wires[wire];
        int source_uses = 0;
        if (wd.bound_nets.count(net->udata))
            source_uses = wd.bound_nets.at(net->udata).first;
        // FIXME: timing/wirelength balance?
        *delay = ctx->estimateDelay(wd.w, sink);
        return (ctx->getDelayNS(*delay) / (1 + source_uses)) + cfg.ipin_cost_adder;
    }

    bool check_arc_routing(NetInfo *net, size_t usr, size_t phys_pin)
    {
        auto &ad = nets.at(net->udata).arcs.at(usr).at(phys_pin);
        WireId src_wire = nets.at(net->udata).src_wire;
        WireId cursor = ad.sink_wire;
        while (wire_data(cursor).bound_nets.count(net->udata)) {
            auto &wd = wire_data(cursor);
            if (wd.bound_nets.size() != 1)
                return false;
            auto &uh = wd.bound_nets.at(net->udata).second;
            if (uh == PipId())
                break;
            cursor = ctx->getPipSrcWire(uh);
        }
        return (cursor == src_wire);
    }

    void record_prerouted_net(NetInfo *net, size_t usr, size_t phys_pin)
    {
        auto &ad = nets.at(net->udata).arcs.at(usr).at(phys_pin);
        ad.routed = true;

        WireId src = nets.at(net->udata).src_wire;
        WireId cursor = ad.sink_wire;
        while (cursor != src) {
            size_t wire_idx = wire_to_idx.at(cursor);
            auto &wd = flat_wires.at(wire_idx);
            PipId pip = wd.bound_nets.at(net->udata).second;
            bind_pip_internal(net, usr, wire_idx, pip);
            cursor = ctx->getPipSrcWire(pip);
        }
    }

    // Returns true if a wire contains no source ports or driving pips
    bool is_wire_undriveable(WireId wire, const NetInfo *net, int iter_count = 0)
    {
        // This is specifically designed to handle a particularly icky case that the current router struggles with in
        // the nexus device,
        // C -> C lut input only
        // C; D; or F from another lut -> D lut input
        // D or M -> M ff input
        // without careful reservation of C for C lut input and D for D lut input, there is fighting for D between FF
        // and LUT
        if (iter_count > 7)
            return false; // heuristic to assume we've hit general routing
        if (wire_data(wire).reserved_net != -1 && wire_data(wire).reserved_net != net->udata)
            return true; // reserved for another net
        for (auto bp : ctx->getWireBelPins(wire))
            if ((net->driver.cell == nullptr || bp.bel == net->driver.cell->bel) &&
                ctx->getBelPinType(bp.bel, bp.pin) != PORT_IN)
                return false;
        for (auto p : ctx->getPipsUphill(wire))
            if (ctx->checkPipAvail(p)) {
                if (!is_wire_undriveable(ctx->getPipSrcWire(p), net, iter_count + 1))
                    return false;
            }
        return true;
    }

    // Find all the wires that must be used to route a given arc
    bool reserve_wires_for_arc(NetInfo *net, size_t i)
    {
        bool did_something = false;
        WireId src = ctx->getNetinfoSourceWire(net);
        for (auto sink : ctx->getNetinfoSinkWires(net, net->users.at(i))) {
            pool<WireId> rsv;
            WireId cursor = sink;
            bool done = false;
            if (ctx->debug)
                log("reserving wires for arc %d of net %s\n", int(i), ctx->nameOf(net));
            while (!done) {
                auto &wd = wire_data(cursor);
                if (ctx->debug)
                    log("      %s\n", ctx->nameOfWire(cursor));
                did_something |= (wd.reserved_net != net->udata);
                wd.reserved_net = net->udata;
                if (cursor == src)
                    break;
                WireId next_cursor;
                for (auto uh : ctx->getPipsUphill(cursor)) {
                    WireId w = ctx->getPipSrcWire(uh);
                    if (is_wire_undriveable(w, net))
                        continue;
                    if (next_cursor != WireId()) {
                        done = true;
                        break;
                    }
                    next_cursor = w;
                }
                if (next_cursor == WireId())
                    break;
                cursor = next_cursor;
            }
        }
        return did_something;
    }

    void find_all_reserved_wires()
    {
        // Run iteratively, as reserving wires for one net might limit choices for another
        bool did_something = false;
        do {
            did_something = false;
            for (auto net : nets_by_udata) {
                WireId src = ctx->getNetinfoSourceWire(net);
                if (src == WireId())
                    continue;
                for (size_t i = 0; i < net->users.size(); i++)
                    did_something |= reserve_wires_for_arc(net, i);
            }
        } while (did_something);
    }

    void reset_wires(ThreadContext &t)
    {
        for (auto w : t.dirty_wires) {
            flat_wires[w].visit.visited = false;
            flat_wires[w].visit.dirty = false;
            flat_wires[w].visit.pip = PipId();
            flat_wires[w].visit.score = WireScore();
        }
        t.dirty_wires.clear();
    }

    void set_visited(ThreadContext &t, int wire, PipId pip, WireScore score)
    {
        auto &v = flat_wires.at(wire).visit;
        if (!v.dirty)
            t.dirty_wires.push_back(wire);
        v.dirty = true;
        v.visited = true;
        v.pip = pip;
        v.score = score;
    }
    bool was_visited(int wire) { return flat_wires.at(wire).visit.visited; }

    ArcRouteResult route_arc(ThreadContext &t, NetInfo *net, size_t i, size_t phys_pin, bool is_mt, bool is_bb = true)
    {
        auto arc_start = std::chrono::high_resolution_clock::now();
        auto &nd = nets[net->udata];
        auto &ad = nd.arcs.at(i).at(phys_pin);
        auto &usr = net->users.at(i);
        ROUTE_LOG_DBG("Routing arc %d of net '%s' (%d, %d) -> (%d, %d)\n", int(i), ctx->nameOf(net), ad.bb.x0, ad.bb.y0,
                      ad.bb.x1, ad.bb.y1);
        WireId src_wire = ctx->getNetinfoSourceWire(net), dst_wire = ctx->getNetinfoSinkWire(net, usr, phys_pin);
        if (src_wire == WireId())
            ARC_LOG_ERR("No wire found for port %s on source cell %s.\n", ctx->nameOf(net->driver.port),
                        ctx->nameOf(net->driver.cell));
        if (dst_wire == WireId())
            ARC_LOG_ERR("No wire found for port %s on destination cell %s.\n", ctx->nameOf(usr.port),
                        ctx->nameOf(usr.cell));
        int src_wire_idx = wire_to_idx.at(src_wire);
        int dst_wire_idx = wire_to_idx.at(dst_wire);
        // Check if arc was already done _in this iteration_
        if (t.processed_sinks.count(dst_wire))
            return ARC_SUCCESS;

        if (!t.queue.empty()) {
            std::priority_queue<QueuedWire, std::vector<QueuedWire>, QueuedWire::Greater> new_queue;
            t.queue.swap(new_queue);
        }
        if (!t.backwards_queue.empty()) {
            std::queue<int> new_queue;
            t.backwards_queue.swap(new_queue);
        }
        // First try strongly iteration-limited routing backwards BFS
        // this will deal with certain nets faster than forward A*
        // and comes at a minimal performance cost for the others
        // This could also be used to speed up forwards routing by a hybrid
        // bidirectional approach
        int backwards_iter = 0;
        int backwards_limit =
                ctx->getBelGlobalBuf(net->driver.cell->bel) ? cfg.global_backwards_max_iter : cfg.backwards_max_iter;
        t.backwards_queue.push(wire_to_idx.at(dst_wire));
        while (!t.backwards_queue.empty() && backwards_iter < backwards_limit) {
            int cursor = t.backwards_queue.front();
            t.backwards_queue.pop();
            auto &cwd = flat_wires[cursor];
            PipId cpip;
            if (cwd.bound_nets.count(net->udata)) {
                // If we can tack onto existing routing; try that
                // Only do this if the existing routing is uncontented; however
                int cursor2 = cursor;
                bool bwd_merge_fail = false;
                while (flat_wires.at(cursor2).bound_nets.count(net->udata)) {
                    if (flat_wires.at(cursor2).bound_nets.size() > 1) {
                        bwd_merge_fail = true;
                        break;
                    }
                    PipId p = flat_wires.at(cursor2).bound_nets.at(net->udata).second;
                    if (p == PipId())
                        break;
                    cursor2 = wire_to_idx.at(ctx->getPipSrcWire(p));
                }
                if (!bwd_merge_fail && cursor2 == src_wire_idx) {
                    // Found a path to merge to existing routing; backwards
                    cursor2 = cursor;
                    while (flat_wires.at(cursor2).bound_nets.count(net->udata)) {
                        PipId p = flat_wires.at(cursor2).bound_nets.at(net->udata).second;
                        if (p == PipId())
                            break;
                        cursor2 = wire_to_idx.at(ctx->getPipSrcWire(p));
                        set_visited(t, cursor2, p, WireScore());
                    }
                    break;
                }
                cpip = cwd.bound_nets.at(net->udata).second;
            }
            bool did_something = false;
            for (auto uh : ctx->getPipsUphill(flat_wires[cursor].w)) {
                did_something = true;
                if (!ctx->checkPipAvailForNet(uh, net))
                    continue;
                if (cpip != PipId() && cpip != uh)
                    continue; // don't allow multiple pips driving a wire with a net
                int next = wire_to_idx.at(ctx->getPipSrcWire(uh));
                if (was_visited(next))
                    continue; // skip wires that have already been visited
                auto &wd = flat_wires[next];
                if (wd.unavailable)
                    continue;
                if (wd.reserved_net != -1 && wd.reserved_net != net->udata)
                    continue;
                if (wd.bound_nets.size() > 1 || (wd.bound_nets.size() == 1 && !wd.bound_nets.count(net->udata)))
                    continue; // never allow congestion in backwards routing
                if (!thread_test_wire(t, wd))
                    continue; // thread safety issue
                t.backwards_queue.push(next);
                set_visited(t, next, uh, WireScore());
            }
            if (did_something)
                ++backwards_iter;
        }
        // Check if backwards routing succeeded in reaching source
        if (was_visited(src_wire_idx)) {
            ROUTE_LOG_DBG("   Routed (backwards): ");
            int cursor_fwd = src_wire_idx;
            bind_pip_internal(net, i, src_wire_idx, PipId());
            while (was_visited(cursor_fwd)) {
                auto &v = flat_wires.at(cursor_fwd).visit;
                cursor_fwd = wire_to_idx.at(ctx->getPipDstWire(v.pip));
                bind_pip_internal(net, i, cursor_fwd, v.pip);
                if (ctx->debug) {
                    auto &wd = flat_wires.at(cursor_fwd);
                    ROUTE_LOG_DBG("      wire: %s (curr %d hist %f)\n", ctx->nameOfWire(wd.w),
                                  int(wd.bound_nets.size()) - 1, wd.hist_cong_cost);
                }
            }
            NPNR_ASSERT(cursor_fwd == dst_wire_idx);
            ad.routed = true;
            t.processed_sinks.insert(dst_wire);
            reset_wires(t);
            return ARC_SUCCESS;
        }

        // Normal forwards A* routing
        reset_wires(t);
        WireScore base_score;
        base_score.cost = 0;
        base_score.delay = ctx->getWireDelay(src_wire).maxDelay();
        delay_t forward;
        base_score.togo_cost = get_togo_cost(net, i, src_wire_idx, dst_wire, &forward);

        ROUTE_LOG_DBG("src_wire = %s -> dst_wire = %s (backward: %s, forward: %s, sum: %s)\n",
                      ctx->nameOfWire(src_wire), ctx->nameOfWire(dst_wire), std::to_string(base_score.delay).c_str(),
                      std::to_string(forward).c_str(), std::to_string(base_score.delay + forward).c_str());

        // Add source wire to queue
        t.queue.push(QueuedWire(src_wire_idx, PipId(), Loc(), base_score));
        set_visited(t, src_wire_idx, PipId(), base_score);

        int toexplore = 25000 * std::max(1, (ad.bb.x1 - ad.bb.x0) + (ad.bb.y1 - ad.bb.y0));
        int iter = 0;
        int explored = 1;
        bool debug_arc = /*usr.cell->type.str(ctx).find("RAMB") != std::string::npos && (usr.port ==
                            ctx->id("ADDRATIEHIGH0") || usr.port == ctx->id("ADDRARDADDRL0"))*/
                false;

        // When running without a bounding box, the toexplore limit should be
        // suspended until a solution is reached.  Once a solution is found,
        // the toexplore limit should be used again to prevent requiring the
        // router to drain the routing queue.
        //
        // Note that is it important that the must_drain_queue be set to true
        // when running without a bb to ensure that a routing failure is
        // because there is not route, rather than just because the toexplore
        // heuristic is incorrect.
        bool must_drain_queue = !is_bb;
        while (!t.queue.empty() && (must_drain_queue || iter < toexplore)) {
            auto curr = t.queue.top();
            auto &d = flat_wires.at(curr.wire);
            t.queue.pop();
            ++iter;
#if 0
            ROUTE_LOG_DBG("current wire %s\n", ctx->nameOfWire(d.w));
#endif
            // Explore all pips downhill of cursor
            for (auto dh : ctx->getPipsDownhill(d.w)) {
                // Skip pips outside of box in bounding-box mode
#if 0
                ROUTE_LOG_DBG("trying pip %s\n", ctx->nameOfPip(dh));
#endif
#if 0
                int wire_intent = ctx->wireIntent(curr.wire);
                if (is_bb && !hit_test_pip(ad.bb, ctx->getPipLocation(dh)) && wire_intent != ID_PSEUDO_GND && wire_intent != ID_PSEUDO_VCC)
                    continue;
#else
                if (is_bb && !hit_test_pip(nd.bb, ctx->getPipLocation(dh)))
                    continue;
                if (!ctx->checkPipAvailForNet(dh, net)) {
#if 0
                    ROUTE_LOG_DBG("Skipping pip %s because it is bound to net '%s' not net '%s'\n", ctx->nameOfPip(dh),
                                  ctx->getBoundPipNet(dh) != nullptr ? ctx->getBoundPipNet(dh)->name.c_str(ctx)
                                                                     : "<not a net>",
                                  net->name.c_str(ctx));
#endif
                    continue;
                }
#endif
                // Evaluate score of next wire
                WireId next = ctx->getPipDstWire(dh);
                int next_idx = wire_to_idx.at(next);
                if (was_visited(next_idx)) {
                    // Don't expand the same node twice.
                    continue;
                }
#if 1
                if (debug_arc)
                    ROUTE_LOG_DBG("   src wire %s\n", ctx->nameOfWire(next));
#endif
                auto &nwd = flat_wires.at(next_idx);
                if (nwd.unavailable)
                    continue;
                if (nwd.reserved_net != -1 && nwd.reserved_net != net->udata)
                    continue;
                if (nwd.bound_nets.count(net->udata) && nwd.bound_nets.at(net->udata).second != dh)
                    continue;
                if (!thread_test_wire(t, nwd))
                    continue; // thread safety issue
                WireScore next_score;
                next_score.cost = curr.score.cost + score_wire_for_arc(net, i, phys_pin, next, dh);
                next_score.delay =
                        curr.score.delay + ctx->getPipDelay(dh).maxDelay() + ctx->getWireDelay(next).maxDelay();
                next_score.togo_cost = cfg.estimate_weight * get_togo_cost(net, i, next_idx, dst_wire, &forward);
#if 0
                ROUTE_LOG_DBG(
                        "src_wire = %s -> next %s -> dst_wire = %s (backward: %s, forward: %s, sum: %s, cost = %f, "
                        "togo_cost = %f, total = %f), dt = %02fs\n",
                        ctx->nameOfWire(src_wire), ctx->nameOfWire(next), ctx->nameOfWire(dst_wire),
                        std::to_string(next_score.delay).c_str(), std::to_string(forward).c_str(),
                        std::to_string(next_score.delay + forward).c_str(), next_score.cost, next_score.togo_cost,
                        next_score.cost + next_score.togo_cost,
                        std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - arc_start).count());
#endif
                const auto &v = nwd.visit;
                if (!v.visited || (v.score.total() > next_score.total())) {
                    ++explored;
#if 0
                    ROUTE_LOG_DBG("exploring wire %s cost %f togo %f\n", ctx->nameOfWire(next), next_score.cost,
                                  next_score.togo_cost);
#endif
                    // Add wire to queue if it meets criteria
                    t.queue.push(QueuedWire(next_idx, dh, ctx->getPipLocation(dh), next_score, t.rng.rng()));
                    set_visited(t, next_idx, dh, next_score);
                    if (next == dst_wire) {
                        toexplore = std::min(toexplore, iter + 5);
                        must_drain_queue = false;
                    }
                }
            }
        }
        if (was_visited(dst_wire_idx)) {
            ROUTE_LOG_DBG("   Routed (explored %d wires): ", explored);
            int cursor_bwd = dst_wire_idx;
            while (was_visited(cursor_bwd)) {
                auto &v = flat_wires.at(cursor_bwd).visit;
                bind_pip_internal(net, i, cursor_bwd, v.pip);
                if (ctx->debug) {
                    auto &wd = flat_wires.at(cursor_bwd);
                    ROUTE_LOG_DBG("      wire: %s (curr %d hist %f share %d)\n", ctx->nameOfWire(wd.w),
                                  int(wd.bound_nets.size()) - 1, wd.hist_cong_cost,
                                  wd.bound_nets.count(net->udata) ? wd.bound_nets.at(net->udata).first : 0);
                }
                if (v.pip == PipId()) {
                    NPNR_ASSERT(cursor_bwd == src_wire_idx);
                    break;
                }
                ROUTE_LOG_DBG("         pip: %s (%d, %d)\n", ctx->nameOfPip(v.pip), ctx->getPipLocation(v.pip).x,
                              ctx->getPipLocation(v.pip).y);
                cursor_bwd = wire_to_idx.at(ctx->getPipSrcWire(v.pip));
            }
            t.processed_sinks.insert(dst_wire);
            ad.routed = true;
            reset_wires(t);

            auto arc_end = std::chrono::high_resolution_clock::now();
            ROUTE_LOG_DBG("Routing arc %d of net '%s' (is_bb = %d) took %02fs\n", int(i), ctx->nameOf(net), is_bb,
                          std::chrono::duration<float>(arc_end - arc_start).count());
            return ARC_SUCCESS;
        } else {
            auto arc_end = std::chrono::high_resolution_clock::now();
            ROUTE_LOG_DBG("Failed routing arc %d of net '%s' (is_bb = %d) took %02fs\n", int(i), ctx->nameOf(net),
                          is_bb, std::chrono::duration<float>(arc_end - arc_start).count());
            reset_wires(t);
            return ARC_RETRY_WITHOUT_BB;
        }
    }
#undef ARC_ERR

    bool route_net(ThreadContext &t, NetInfo *net, bool is_mt)
    {

#ifdef ARCH_ECP5
        if (net->is_global)
            return true;
#endif

        ROUTE_LOG_DBG("Routing net '%s'...\n", ctx->nameOf(net));

        auto rstart = std::chrono::high_resolution_clock::now();

        // Nothing to do if net is undriven
        if (net->driver.cell == nullptr)
            return true;

        bool have_failures = false;
        t.processed_sinks.clear();
        t.route_arcs.clear();
        auto &nd = nets.at(net->udata);
        for (size_t i = 0; i < net->users.size(); i++) {
            auto &ad = nd.arcs.at(i);
            for (size_t j = 0; j < ad.size(); j++) {
                // Ripup failed arcs to start with
                // Check if arc is already legally routed
                if (check_arc_routing(net, i, j)) {
                    continue;
                }

                // Ripup arc to start with
                ripup_arc(net, i, j);
                t.route_arcs.emplace_back(i, j);
            }
        }
        for (auto a : t.route_arcs) {
            auto res1 = route_arc(t, net, a.first, a.second, is_mt, true);
            if (res1 == ARC_FATAL)
                return false; // Arc failed irrecoverably
            else if (res1 == ARC_RETRY_WITHOUT_BB) {
                if (is_mt) {
                    // Can't break out of bounding box in multi-threaded mode, so mark this arc as a failure
                    have_failures = true;
                } else {
                    // Attempt a re-route without the bounding box constraint
                    ROUTE_LOG_DBG("Rerouting arc %d.%d of net '%s' without bounding box, possible tricky routing...\n",
                                  int(a.first), int(a.second), ctx->nameOf(net));
                    auto res2 = route_arc(t, net, a.first, a.second, is_mt, false);
                    // If this also fails, no choice but to give up
                    if (res2 != ARC_SUCCESS) {
                        if (ctx->debug) {
                            log_info("Pre-bound routing: \n");
                            for (auto &wire_pair : net->wires) {
                                log("        %s", ctx->nameOfWire(wire_pair.first));
                                if (wire_pair.second.pip != PipId())
                                    log(" %s", ctx->nameOfPip(wire_pair.second.pip));
                                log("\n");
                            }
                        }
                        log_error("Failed to route arc %d.%d of net '%s', from %s to %s.\n", int(a.first),
                                  int(a.second), ctx->nameOf(net), ctx->nameOfWire(ctx->getNetinfoSourceWire(net)),
                                  ctx->nameOfWire(ctx->getNetinfoSinkWire(net, net->users.at(a.first), a.second)));
                    }
                }
            }
        }
        if (cfg.perf_profile) {
            auto rend = std::chrono::high_resolution_clock::now();
            nets.at(net->udata).total_route_us +=
                    (std::chrono::duration_cast<std::chrono::microseconds>(rend - rstart).count());
        }
        return !have_failures;
    }
#undef ROUTE_LOG_DBG

    int total_wire_use = 0;
    int overused_wires = 0;
    int total_overuse = 0;
    std::vector<int> route_queue;
    std::set<int> failed_nets;

    void update_congestion()
    {
        total_overuse = 0;
        overused_wires = 0;
        total_wire_use = 0;
        failed_nets.clear();
        for (auto &wire : flat_wires) {
            total_wire_use += int(wire.bound_nets.size());
            int overuse = int(wire.bound_nets.size()) - 1;
            if (overuse > 0) {
                wire.hist_cong_cost = std::min(1e9, wire.hist_cong_cost + overuse * hist_cong_weight);
                total_overuse += overuse;
                overused_wires += 1;
                for (auto &bound : wire.bound_nets)
                    failed_nets.insert(bound.first);
            }
        }
        for (int n : failed_nets) {
            auto &net_data = nets.at(n);
            ++net_data.fail_count;
            if ((net_data.fail_count % 3) == 0) {
                // Every three times a net fails to route, expand the bounding box to increase the search space
#ifndef ARCH_MISTRAL
                // This patch seems to make thing worse for CycloneV, as it slows down the resolution of TD congestion,
                // disable it
                net_data.bb.x0 = std::max(net_data.bb.x0 - 1, 0);
                net_data.bb.y0 = std::max(net_data.bb.y0 - 1, 0);
                net_data.bb.x1 = std::min(net_data.bb.x1 + 1, ctx->getGridDimX());
                net_data.bb.y1 = std::min(net_data.bb.y1 + 1, ctx->getGridDimY());
#endif
            }
        }
    }

    bool bind_and_check(NetInfo *net, int usr_idx, int phys_pin)
    {
#ifdef ARCH_ECP5
        if (net->is_global)
            return true;
#endif
        bool success = true;
        auto &nd = nets.at(net->udata);
        auto &ad = nd.arcs.at(usr_idx).at(phys_pin);
        auto &usr = net->users.at(usr_idx);
        WireId src = ctx->getNetinfoSourceWire(net);
        // Skip routes with no source
        if (src == WireId())
            return true;
        WireId dst = ctx->getNetinfoSinkWire(net, usr, phys_pin);
        if (dst == WireId())
            return true;

        // Skip routes where there is no routing (special cases)
        if (!ad.routed) {
            if ((src == dst) && ctx->getBoundWireNet(dst) != net)
                ctx->bindWire(src, net, STRENGTH_WEAK);
            if (ctx->debug) {
                log("Net %s not routed, not binding\n", ctx->nameOf(net));
            }
            return true;
        }

        WireId cursor = dst;

        std::vector<PipId> to_bind;

        while (cursor != src) {
            if (!ctx->checkWireAvail(cursor)) {
                NetInfo *bound_net = ctx->getBoundWireNet(cursor);
                if (bound_net != net) {
                    if (ctx->verbose) {
                        if (bound_net != nullptr) {
                            log_info("Failed to bind wire %s to net %s, bound to net %s\n", ctx->nameOfWire(cursor),
                                     net->name.c_str(ctx), bound_net->name.c_str(ctx));
                        } else {
                            log_info("Failed to bind wire %s to net %s, bound net nullptr\n", ctx->nameOfWire(cursor),
                                     net->name.c_str(ctx));
                        }
                    }
                    success = false;
                    break;
                }
            }
            auto &wd = wire_data(cursor);
            if (!wd.bound_nets.count(net->udata)) {
                log("Failure details:\n");
                log("    Cursor: %s\n", ctx->nameOfWire(cursor));
                log_error("Internal error; incomplete route tree for arc %d of net %s.\n", usr_idx, ctx->nameOf(net));
            }
            auto &p = wd.bound_nets.at(net->udata).second;
            if (ctx->checkPipAvailForNet(p, net)) {
                NetInfo *bound_net = ctx->getBoundPipNet(p);
                if (bound_net == nullptr) {
                    to_bind.push_back(p);
                }
            } else {
                if (ctx->verbose) {
                    log_info("Failed to bind pip %s to net %s\n", ctx->nameOfPip(p), net->name.c_str(ctx));
                }
                success = false;
                break;
            }
            cursor = ctx->getPipSrcWire(p);
        }

        if (success) {
            if (ctx->getBoundWireNet(src) == nullptr)
                ctx->bindWire(src, net, STRENGTH_WEAK);
            for (auto tb : to_bind)
                ctx->bindPip(tb, net, STRENGTH_WEAK);
        } else {
            ripup_arc(net, usr_idx, phys_pin);
            failed_nets.insert(net->udata);
        }
        return success;
    }

    int arch_fail = 0;
    bool bind_and_check_all()
    {
        // Make sure arch is internally consistent before we mess with it.
        ctx->check();

        bool success = true;
        std::vector<WireId> net_wires;
        for (auto net : nets_by_udata) {
#ifdef ARCH_ECP5
            if (net->is_global)
                continue;
#endif
            // Ripup wires and pips used by the net in nextpnr's structures
            net_wires.clear();
            for (auto &w : net->wires) {
                if (w.second.strength <= STRENGTH_STRONG) {
                    net_wires.push_back(w.first);
                } else if (ctx->debug) {
                    log("Net %s didn't rip up wire %s because strength was %d\n", ctx->nameOf(net),
                        ctx->nameOfWire(w.first), w.second.strength);
                }
            }
            for (auto w : net_wires)
                ctx->unbindWire(w);

            if (ctx->debug) {
                log("Ripped up %zu wires on net %s\n", net_wires.size(), ctx->nameOf(net));
            }

            // Bind the arcs using the routes we have discovered
            for (size_t i = 0; i < net->users.size(); i++) {
                for (size_t phys_pin = 0; phys_pin < nets.at(net->udata).arcs.at(i).size(); phys_pin++) {
                    if (!bind_and_check(net, i, phys_pin)) {
                        ++arch_fail;
                        success = false;
                    }
                }
            }
        }

        // Check that the arch is still internally consistent!
        ctx->check();

        return success;
    }

    void write_xy_heatmap(std::ostream &out, bool congestion = false)
    {
        std::vector<std::vector<int>> hm_xy;
        int max_x = 0, max_y = 0;
        for (auto &wd : flat_wires) {
            int val = int(wd.bound_nets.size()) - (congestion ? 1 : 0);
            if (wd.bound_nets.empty())
                continue;
            // Estimate wire location by driving pip location
            PipId drv;
            for (auto &bn : wd.bound_nets)
                if (bn.second.second != PipId()) {
                    drv = bn.second.second;
                    break;
                }
            if (drv == PipId())
                continue;
            Loc l = ctx->getPipLocation(drv);
            max_x = std::max(max_x, l.x);
            max_y = std::max(max_y, l.y);
            if (l.y >= int(hm_xy.size()))
                hm_xy.resize(l.y + 1);
            if (l.x >= int(hm_xy.at(l.y).size()))
                hm_xy.at(l.y).resize(l.x + 1);
            if (val > 0)
                hm_xy.at(l.y).at(l.x) += val;
        }
        for (int y = 0; y <= max_y; y++) {
            for (int x = 0; x <= max_x; x++) {
                if (y >= int(hm_xy.size()) || x >= int(hm_xy.at(y).size()))
                    out << "0,";
                else
                    out << hm_xy.at(y).at(x) << ",";
            }
            out << std::endl;
        }
    }

    void write_wiretype_heatmap(std::ostream &out)
    {
        dict<IdString, std::vector<int>> cong_by_type;
        size_t max_cong = 0;
        // Build histogram
        for (auto &wd : flat_wires) {
            size_t val = wd.bound_nets.size();
            IdString type = ctx->getWireType(wd.w);
            max_cong = std::max(max_cong, val);
            if (cong_by_type[type].size() <= max_cong)
                cong_by_type[type].resize(max_cong + 1);
            cong_by_type[type].at(val) += 1;
        }
        // Write csv
        out << "type,";
        for (size_t i = 0; i <= max_cong; i++)
            out << "bound=" << i << ",";
        out << std::endl;
        for (auto &ty : cong_by_type) {
            out << ctx->nameOf(ty.first) << ",";
            for (int count : ty.second)
                out << count << ",";
            out << std::endl;
        }
    }

    int mid_x = 0, mid_y = 0;

    void partition_nets()
    {
        // Create a histogram of positions in X and Y positions
        std::map<int, int> cxs, cys;
        for (auto &n : nets) {
            if (n.cx != -1)
                ++cxs[n.cx];
            if (n.cy != -1)
                ++cys[n.cy];
        }
        // 4-way split for now
        int accum_x = 0, accum_y = 0;
        int halfway = int(nets.size()) / 2;
        for (auto &p : cxs) {
            if (accum_x < halfway && (accum_x + p.second) >= halfway)
                mid_x = p.first;
            accum_x += p.second;
        }
        for (auto &p : cys) {
            if (accum_y < halfway && (accum_y + p.second) >= halfway)
                mid_y = p.first;
            accum_y += p.second;
        }
        if (ctx->verbose) {
            log_info("    x splitpoint: %d\n", mid_x);
            log_info("    y splitpoint: %d\n", mid_y);
        }
        std::vector<int> bins(5, 0);
        for (auto &n : nets) {
            if (n.bb.x0 < mid_x && n.bb.x1 < mid_x && n.bb.y0 < mid_y && n.bb.y1 < mid_y)
                ++bins[0]; // TL
            else if (n.bb.x0 >= mid_x && n.bb.x1 >= mid_x && n.bb.y0 < mid_y && n.bb.y1 < mid_y)
                ++bins[1]; // TR
            else if (n.bb.x0 < mid_x && n.bb.x1 < mid_x && n.bb.y0 >= mid_y && n.bb.y1 >= mid_y)
                ++bins[2]; // BL
            else if (n.bb.x0 >= mid_x && n.bb.x1 >= mid_x && n.bb.y0 >= mid_y && n.bb.y1 >= mid_y)
                ++bins[3]; // BR
            else
                ++bins[4]; // cross-boundary
        }
        if (ctx->verbose)
            for (int i = 0; i < 5; i++)
                log_info("        bin %d N=%d\n", i, bins[i]);
    }

    void router_thread(ThreadContext &t, bool is_mt)
    {
        for (auto n : t.route_nets) {
            bool result = route_net(t, n, is_mt);
            if (!result)
                t.failed_nets.push_back(n);
        }
    }

    void do_route()
    {
        // Don't multithread if fewer than 200 nets (heuristic)
        if (route_queue.size() < 200) {
            ThreadContext st;
            st.rng.rngseed(ctx->rng64());
            st.bb = ArcBounds(0, 0, std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
            for (size_t j = 0; j < route_queue.size(); j++) {
                route_net(st, nets_by_udata[route_queue[j]], false);
            }
            return;
        }
        const int Nq = 4, Nv = 2, Nh = 2;
        const int N = Nq + Nv + Nh;
        std::vector<ThreadContext> tcs(N + 1);
        for (auto &th : tcs) {
            th.rng.rngseed(ctx->rng64());
        }
        int le_x = mid_x;
        int rs_x = mid_x;
        int le_y = mid_y;
        int rs_y = mid_y;
        // Set up thread bounding boxes
        tcs.at(0).bb = ArcBounds(0, 0, mid_x, mid_y);
        tcs.at(1).bb = ArcBounds(mid_x + 1, 0, std::numeric_limits<int>::max(), le_y);
        tcs.at(2).bb = ArcBounds(0, mid_y + 1, mid_x, std::numeric_limits<int>::max());
        tcs.at(3).bb =
                ArcBounds(mid_x + 1, mid_y + 1, std::numeric_limits<int>::max(), std::numeric_limits<int>::max());

        tcs.at(4).bb = ArcBounds(0, 0, std::numeric_limits<int>::max(), mid_y);
        tcs.at(5).bb = ArcBounds(0, mid_y + 1, std::numeric_limits<int>::max(), std::numeric_limits<int>::max());

        tcs.at(6).bb = ArcBounds(0, 0, mid_x, std::numeric_limits<int>::max());
        tcs.at(7).bb = ArcBounds(mid_x + 1, 0, std::numeric_limits<int>::max(), std::numeric_limits<int>::max());

        tcs.at(8).bb = ArcBounds(0, 0, std::numeric_limits<int>::max(), std::numeric_limits<int>::max());

        for (auto n : route_queue) {
            auto &nd = nets.at(n);
            auto ni = nets_by_udata.at(n);
            int bin = N;
            // Quadrants
            if (nd.bb.x0 < le_x && nd.bb.x1 < le_x && nd.bb.y0 < le_y && nd.bb.y1 < le_y)
                bin = 0;
            else if (nd.bb.x0 >= rs_x && nd.bb.x1 >= rs_x && nd.bb.y0 < le_y && nd.bb.y1 < le_y)
                bin = 1;
            else if (nd.bb.x0 < le_x && nd.bb.x1 < le_x && nd.bb.y0 >= rs_y && nd.bb.y1 >= rs_y)
                bin = 2;
            else if (nd.bb.x0 >= rs_x && nd.bb.x1 >= rs_x && nd.bb.y0 >= rs_y && nd.bb.y1 >= rs_y)
                bin = 3;
            // Vertical split
            else if (nd.bb.y0 < le_y && nd.bb.y1 < le_y)
                bin = Nq + 0;
            else if (nd.bb.y0 >= rs_y && nd.bb.y1 >= rs_y)
                bin = Nq + 1;
            // Horizontal split
            else if (nd.bb.x0 < le_x && nd.bb.x1 < le_x)
                bin = Nq + Nv + 0;
            else if (nd.bb.x0 >= rs_x && nd.bb.x1 >= rs_x)
                bin = Nq + Nv + 1;
            tcs.at(bin).route_nets.push_back(ni);
        }
        if (ctx->verbose)
            log_info("%d/%d nets not multi-threadable\n", int(tcs.at(N).route_nets.size()), int(route_queue.size()));
#ifdef NPNR_DISABLE_THREADS
        // Singlethreaded routing - quadrants
        for (int i = 0; i < Nq; i++) {
            router_thread(tcs.at(i), /*is_mt=*/false);
        }
        // Vertical splits
        for (int i = Nq; i < Nq + Nv; i++) {
            router_thread(tcs.at(i), /*is_mt=*/false);
        }
        // Horizontal splits
        for (int i = Nq + Nv; i < Nq + Nv + Nh; i++) {
            router_thread(tcs.at(i), /*is_mt=*/false);
        }
#else
        // Multithreaded part of routing - quadrants
        std::vector<boost::thread> threads;
        for (int i = 0; i < Nq; i++) {
            threads.emplace_back([this, &tcs, i]() { router_thread(tcs.at(i), /*is_mt=*/true); });
        }
        for (auto &t : threads)
            t.join();
        threads.clear();
        // Vertical splits
        for (int i = Nq; i < Nq + Nv; i++) {
            threads.emplace_back([this, &tcs, i]() { router_thread(tcs.at(i), /*is_mt=*/true); });
        }
        for (auto &t : threads)
            t.join();
        threads.clear();
        // Horizontal splits
        for (int i = Nq + Nv; i < Nq + Nv + Nh; i++) {
            threads.emplace_back([this, &tcs, i]() { router_thread(tcs.at(i), /*is_mt=*/true); });
        }
        for (auto &t : threads)
            t.join();
        threads.clear();
#endif
        // Singlethreaded part of routing - nets that cross partitions
        // or don't fit within bounding box
        for (auto st_net : tcs.at(N).route_nets)
            route_net(tcs.at(N), st_net, false);
        // Failed nets
        for (int i = 0; i < N; i++)
            for (auto fail : tcs.at(i).failed_nets)
                route_net(tcs.at(N), fail, false);
    }

    //#define ROUTER2_STATISTICS

    void dump_statistics()
    {
#ifdef ROUTER2_STATISTICS
        int total_wires = int(flat_wires.size());
        int have_hist_cong = 0;
        int have_any_bound = 0, have_1_bound = 0, have_2_bound = 0, have_gte3_bound = 0;
        for (auto &wire : flat_wires) {
            int bound = wire.bound_nets.size();
            if (bound != 0)
                ++have_any_bound;
            if (bound == 1)
                ++have_1_bound;
            else if (bound == 2)
                ++have_2_bound;
            else if (bound >= 3)
                ++have_gte3_bound;
            if (wire.hist_cong_cost > 1.0)
                ++have_hist_cong;
        }
        log_info("Out of %d wires:\n", total_wires);
        log_info("     %d (%.02f%%) have any bound nets\n", have_any_bound, (100.0 * have_any_bound) / total_wires);
        log_info("     %d (%.02f%%) have 1 bound net\n", have_1_bound, (100.0 * have_1_bound) / total_wires);
        log_info("     %d (%.02f%%) have 2 bound nets\n", have_2_bound, (100.0 * have_2_bound) / total_wires);
        log_info("     %d (%.02f%%) have >2 bound nets\n", have_gte3_bound, (100.0 * have_gte3_bound) / total_wires);
        log_info("     %d (%.02f%%) have historical congestion\n", have_hist_cong,
                 (100.0 * have_hist_cong) / total_wires);
#endif
    }

    void operator()()
    {
        log_info("Running router2...\n");
        log_info("Setting up routing resources...\n");
        auto rstart = std::chrono::high_resolution_clock::now();
        setup_nets();
        setup_wires();
        find_all_reserved_wires();
        partition_nets();
        curr_cong_weight = cfg.init_curr_cong_weight;
        hist_cong_weight = cfg.hist_cong_weight;
        ThreadContext st;
        int iter = 1;

        ScopeLock<Context> lock(ctx);

        for (size_t i = 0; i < nets_by_udata.size(); i++)
            route_queue.push_back(i);

        timing_driven = ctx->setting<bool>("timing_driven");
        log_info("Running main router loop...\n");
        do {
            ctx->sorted_shuffle(route_queue);

            if (timing_driven && (int(route_queue.size()) > (int(nets_by_udata.size()) / 50))) {
                // Heuristic: reduce runtime by skipping STA in the case of a "long tail" of a few
                // congested nodes
                tmg.run();
                for (auto n : route_queue) {
                    NetInfo *ni = nets_by_udata.at(n);
                    auto &net = nets.at(n);
                    net.max_crit = 0;
                    for (auto &usr : ni->users) {
                        float c = tmg.get_criticality(CellPortKey(usr));
                        net.max_crit = std::max(net.max_crit, c);
                    }
                }
                std::stable_sort(route_queue.begin(), route_queue.end(),
                                 [&](int na, int nb) { return nets.at(na).max_crit > nets.at(nb).max_crit; });
            }

#if 0
            for (size_t j = 0; j < route_queue.size(); j++) {
                route_net(st, nets_by_udata[route_queue[j]], false);
                if ((j % 1000) == 0 || j == (route_queue.size() - 1))
                    log("    routed %d/%d\n", int(j), int(route_queue.size()));
            }
#endif
            do_route();
            route_queue.clear();
            update_congestion();
#if 0
            if (iter == 1 && ctx->debug) {
                std::ofstream cong_map("cong_map_0.csv");
                write_xy_heatmap(cong_map, true);
            }
#endif
            if (!cfg.heatmap.empty()) {
                std::string filename(cfg.heatmap + "_" + std::to_string(iter) + ".csv");
                std::ofstream cong_map(filename);
                if (!cong_map)
                    log_error("Failed to open wiretype heatmap %s for writing.\n", filename.c_str());
                write_wiretype_heatmap(cong_map);
                log_info("        wrote wiretype heatmap to %s.\n", filename.c_str());
            }
            dump_statistics();

            if (overused_wires == 0) {
                // Try and actually bind nextpnr Arch API wires
                bind_and_check_all();
            }
            for (auto cn : failed_nets)
                route_queue.push_back(cn);
            log_info("    iter=%d wires=%d overused=%d overuse=%d archfail=%s\n", iter, total_wire_use, overused_wires,
                     total_overuse, overused_wires > 0 ? "NA" : std::to_string(arch_fail).c_str());
            ++iter;
            if (curr_cong_weight < 1e9)
                curr_cong_weight *= cfg.curr_cong_mult;
        } while (!failed_nets.empty());
        if (cfg.perf_profile) {
            std::vector<std::pair<int, IdString>> nets_by_runtime;
            for (auto &n : nets_by_udata) {
                nets_by_runtime.emplace_back(nets.at(n->udata).total_route_us, n->name);
            }
            std::sort(nets_by_runtime.begin(), nets_by_runtime.end(), std::greater<std::pair<int, IdString>>());
            log_info("1000 slowest nets by runtime:\n");
            for (int i = 0; i < std::min(int(nets_by_runtime.size()), 1000); i++) {
                log("        %80s %6d %.1fms\n", nets_by_runtime.at(i).second.c_str(ctx),
                    int(ctx->nets.at(nets_by_runtime.at(i).second)->users.size()),
                    nets_by_runtime.at(i).first / 1000.0);
            }
        }
        auto rend = std::chrono::high_resolution_clock::now();
        log_info("Router2 time %.02fs\n", std::chrono::duration<float>(rend - rstart).count());

        log_info("Running router1 to check that route is legal...\n");

        lock.unlock_early();

        router1(ctx, Router1Cfg(ctx));
    }
};
} // namespace

void router2(Context *ctx, const Router2Cfg &cfg)
{
    Router2 rt(ctx, cfg);
    rt.ctx = ctx;
    rt();
}

Router2Cfg::Router2Cfg(Context *ctx)
{
    backwards_max_iter = ctx->setting<int>("router2/bwdMaxIter", 20);
    global_backwards_max_iter = ctx->setting<int>("router2/glbBwdMaxIter", 200);
    bb_margin_x = ctx->setting<int>("router2/bbMargin/x", 3);
    bb_margin_y = ctx->setting<int>("router2/bbMargin/y", 3);
    ipin_cost_adder = ctx->setting<float>("router2/ipinCostAdder", 0.0f);
    bias_cost_factor = ctx->setting<float>("router2/biasCostFactor", 0.25f);
    init_curr_cong_weight = ctx->setting<float>("router2/initCurrCongWeight", 0.5f);
    hist_cong_weight = ctx->setting<float>("router2/histCongWeight", 1.0f);
    curr_cong_mult = ctx->setting<float>("router2/currCongWeightMult", 2.0f);
    estimate_weight = ctx->setting<float>("router2/estimateWeight", 1.75f);
    perf_profile = ctx->setting<bool>("router2/perfProfile", false);
    if (ctx->settings.count(ctx->id("router2/heatmap")))
        heatmap = ctx->settings.at(ctx->id("router2/heatmap")).as_string();
    else
        heatmap = "";
}

NEXTPNR_NAMESPACE_END
