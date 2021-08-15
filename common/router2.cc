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
    };

    // As we allow overlap at first; the nextpnr bind functions can't be used
    // as the primary relation between arcs and wires/pips
    struct PerNetData
    {
        WireId src_wire;
        dict<WireId, std::pair<PipId, int>> wires;
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
        float total() const { return cost + togo_cost; }
    };

    struct PerWireData
    {
        // nextpnr
        WireId w;
        // Historical congestion cost
        int curr_cong = 0;
        float hist_cong_cost = 1.0;
        // Wire is unavailable as locked to another arc
        bool unavailable = false;
        // This wire has to be used for this net
        int reserved_net = -1;
        // The notional location of the wire, to guarantee thread safety
        int16_t x = 0, y = 0;
        // Visit data
        PipId pip_fwd, pip_bwd;
        bool visited_fwd, visited_bwd;
    };

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
                    auto &nd = nets.at(bound->udata);
                    nd.wires[wire] = std::make_pair(bound->wires.at(wire).pip, 0);
                    pwd.curr_cong = 1;
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

        explicit QueuedWire(int wire = -1, WireScore score = WireScore{}, int randtag = 0)
                : wire(wire), score(score), randtag(randtag){};

        int wire;
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

        std::priority_queue<QueuedWire, std::vector<QueuedWire>, QueuedWire::Greater> fwd_queue, bwd_queue;
        // Special case where one net has multiple logical arcs to the same physical sink
        pool<WireId> processed_sinks;

        std::vector<int> dirty_wires;

        // Thread bounding box
        ArcBounds bb;

        DeterministicRNG rng;

        // Used to add existing routing to the heap
        pool<WireId> in_wire_by_loc;
        dict<std::pair<int, int>, pool<WireId>> wire_by_loc;
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

    void bind_pip_internal(PerNetData &net, size_t user, int wire, PipId pip)
    {
        auto &wd = flat_wires.at(wire);
        auto found = net.wires.find(wd.w);
        if (found == net.wires.end()) {
            // Not yet used for any arcs of this net, add to list
            net.wires.emplace(wd.w, std::make_pair(pip, 1));
            // Increase bound count of wire by 1
            ++wd.curr_cong;
        } else {
            // Already used for at least one other arc of this net
            // Don't allow two uphill PIPs for the same net and wire
            NPNR_ASSERT(found->second.first == pip);
            // Increase the count of bound arcs
            ++found->second.second;
        }
    }

    void unbind_pip_internal(PerNetData &net, size_t user, WireId wire)
    {
        auto &wd = wire_data(wire);
        auto &b = net.wires.at(wd.w);
        --b.second;
        if (b.second == 0) {
            // No remaining arcs of this net bound to this wire
            --wd.curr_cong;
            net.wires.erase(wd.w);
        }
    }

    void ripup_arc(NetInfo *net, size_t user, size_t phys_pin)
    {
        auto &nd = nets.at(net->udata);
        auto &ad = nd.arcs.at(user).at(phys_pin);
        if (!ad.routed)
            return;
        WireId src = nets.at(net->udata).src_wire;
        WireId cursor = ad.sink_wire;
        while (cursor != src) {
            PipId pip = nd.wires.at(cursor).first;
            unbind_pip_internal(nd, user, cursor);
            cursor = ctx->getPipSrcWire(pip);
        }
        ad.routed = false;
    }

    float score_wire_for_arc(NetInfo *net, size_t user, size_t phys_pin, WireId wire, PipId pip, float crit_weight)
    {
        auto &wd = wire_data(wire);
        auto &nd = nets.at(net->udata);
        float base_cost = ctx->getDelayNS(ctx->getPipDelay(pip).maxDelay() + ctx->getWireDelay(wire).maxDelay() +
                                          ctx->getDelayEpsilon());
        int overuse = wd.curr_cong;
        float hist_cost = wd.hist_cong_cost;
        float bias_cost = 0;
        int source_uses = 0;
        if (nd.wires.count(wire)) {
            overuse -= 1;
            source_uses = nd.wires.at(wire).second;
        }
        float present_cost = 1.0f + overuse * curr_cong_weight * crit_weight;
        if (pip != PipId()) {
            Loc pl = ctx->getPipLocation(pip);
            bias_cost = cfg.bias_cost_factor * (base_cost / int(net->users.size())) *
                        ((std::abs(pl.x - nd.cx) + std::abs(pl.y - nd.cy)) / float(nd.hpwl));
        }
        return base_cost * hist_cost * present_cost / (1 + (source_uses * crit_weight)) + bias_cost;
    }

    float get_togo_cost(NetInfo *net, size_t user, int wire, WireId src_sink, float crit_weight, bool bwd = false)
    {
        auto &nd = nets.at(net->udata);
        auto &wd = flat_wires[wire];
        int source_uses = 0;
        if (nd.wires.count(wd.w)) {
            source_uses = nd.wires.at(wd.w).second;
        }
        // FIXME: timing/wirelength balance?
        delay_t est_delay = ctx->estimateDelay(bwd ? src_sink : wd.w, bwd ? wd.w : src_sink);
        return (ctx->getDelayNS(est_delay) / (1 + source_uses * crit_weight)) + cfg.ipin_cost_adder;
    }

    bool check_arc_routing(NetInfo *net, size_t usr, size_t phys_pin)
    {
        auto &nd = nets.at(net->udata);
        auto &ad = nd.arcs.at(usr).at(phys_pin);
        WireId src_wire = nets.at(net->udata).src_wire;
        WireId cursor = ad.sink_wire;
        while (nd.wires.count(cursor)) {
            auto &wd = wire_data(cursor);
            if (wd.curr_cong != 1)
                return false;
            auto &uh = nd.wires.at(cursor).first;
            if (uh == PipId())
                break;
            cursor = ctx->getPipSrcWire(uh);
        }
        return (cursor == src_wire);
    }

    void record_prerouted_net(NetInfo *net, size_t usr, size_t phys_pin)
    {
        auto &nd = nets.at(net->udata);
        auto &ad = nd.arcs.at(usr).at(phys_pin);
        ad.routed = true;

        WireId src = nets.at(net->udata).src_wire;
        WireId cursor = ad.sink_wire;
        while (cursor != src) {
            size_t wire_idx = wire_to_idx.at(cursor);
            PipId pip = nd.wires.at(cursor).first;
            bind_pip_internal(nd, usr, wire_idx, pip);
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
        if (wire_data(wire).unavailable)
            return true;
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
            flat_wires[w].pip_fwd = PipId();
            flat_wires[w].pip_bwd = PipId();
            flat_wires[w].visited_fwd = false;
            flat_wires[w].visited_bwd = false;
        }
        t.dirty_wires.clear();
    }

    // These nets have very-high-fanout pips and special rules must be followed (only working backwards) to avoid
    // crippling perf
    bool is_pseudo_const_net(const NetInfo *net)
    {
#ifdef ARCH_NEXUS
        if (net->driver.cell != nullptr && net->driver.cell->type == id_VCC_DRV)
            return true;
#endif
        return false;
    }

    void update_wire_by_loc(ThreadContext &t, NetInfo *net, size_t i, size_t phys_pin, bool is_mt)
    {
        if (is_pseudo_const_net(net))
            return;
        auto &nd = nets.at(net->udata);
        auto &ad = nd.arcs.at(i).at(phys_pin);
        WireId cursor = ad.sink_wire;
        if (!nd.wires.count(cursor))
            return;
        while (cursor != nd.src_wire) {
            if (!t.in_wire_by_loc.count(cursor)) {
                t.in_wire_by_loc.insert(cursor);
                for (auto dh : ctx->getPipsDownhill(cursor)) {
                    Loc dh_loc = ctx->getPipLocation(dh);
                    t.wire_by_loc[std::make_pair(dh_loc.x, dh_loc.y)].insert(cursor);
                }
            }
            cursor = ctx->getPipSrcWire(nd.wires.at(cursor).first);
        }
    }

    // Functions for marking wires as visited, and checking if they have already been visited
    void set_visited_fwd(ThreadContext &t, int wire, PipId pip)
    {
        auto &wd = flat_wires.at(wire);
        if (!wd.visited_fwd && !wd.visited_bwd)
            t.dirty_wires.push_back(wire);
        wd.pip_fwd = pip;
        wd.visited_fwd = true;
    }
    void set_visited_bwd(ThreadContext &t, int wire, PipId pip)
    {
        auto &wd = flat_wires.at(wire);
        if (!wd.visited_fwd && !wd.visited_bwd)
            t.dirty_wires.push_back(wire);
        wd.pip_bwd = pip;
        wd.visited_bwd = true;
    }

    bool was_visited_fwd(int wire) { return flat_wires.at(wire).visited_fwd; }
    bool was_visited_bwd(int wire) { return flat_wires.at(wire).visited_bwd; }

    float get_arc_crit(NetInfo *net, size_t i)
    {
        if (!timing_driven)
            return 0;
        return tmg.get_criticality(CellPortKey(net->users.at(i)));
    }

    ArcRouteResult route_arc(ThreadContext &t, NetInfo *net, size_t i, size_t phys_pin, bool is_mt, bool is_bb = true)
    {
        // Do some initial lookups and checks
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
        // Calculate a timing weight based on criticality
        float crit = get_arc_crit(net, i);
        float crit_weight = (1.0f - std::pow(crit, 2));
        // Check if arc was already done _in this iteration_
        if (t.processed_sinks.count(dst_wire))
            return ARC_SUCCESS;

        // We have two modes:
        //     0. starting within a small range of existing routing
        //     1. expanding from all routing
        int mode = 0;
        if (net->users.size() < 4 || nd.wires.empty())
            mode = 1;

        // This records the point where forwards and backwards routing met
        int midpoint_wire = -1;
        int explored = 1;

        for (; mode < 2; mode++) {
            // Clear out the queues
            if (!t.fwd_queue.empty()) {
                std::priority_queue<QueuedWire, std::vector<QueuedWire>, QueuedWire::Greater> new_queue;
                t.fwd_queue.swap(new_queue);
            }
            if (!t.bwd_queue.empty()) {
                std::priority_queue<QueuedWire, std::vector<QueuedWire>, QueuedWire::Greater> new_queue;
                t.bwd_queue.swap(new_queue);
            }
            // Unvisit any previously visited wires
            reset_wires(t);

            ROUTE_LOG_DBG("src_wire = %s -> dst_wire = %s\n", ctx->nameOfWire(src_wire), ctx->nameOfWire(dst_wire));

            // Add 'forward' direction startpoints to queue
            auto seed_queue_fwd = [&](WireId wire, float wire_cost = 0) {
                WireScore base_score;
                base_score.cost = wire_cost;
                int wire_idx = wire_to_idx.at(wire);
                base_score.togo_cost = get_togo_cost(net, i, wire_idx, dst_wire, false, crit_weight);
                t.fwd_queue.push(QueuedWire(wire_idx, base_score));
                set_visited_fwd(t, wire_idx, PipId());
            };
            auto &dst_data = flat_wires.at(dst_wire_idx);
            // Look for nearby existing routing
            for (int dy = -cfg.bb_margin_y; dy <= cfg.bb_margin_y; dy++)
                for (int dx = -cfg.bb_margin_x; dx <= cfg.bb_margin_x; dx++) {
                    auto fnd = t.wire_by_loc.find(std::make_pair(dst_data.x + dx, dst_data.y + dy));
                    if (fnd == t.wire_by_loc.end())
                        continue;
                    for (WireId wire : fnd->second) {
                        ROUTE_LOG_DBG("   seeding with %s\n", ctx->nameOfWire(wire));
                        seed_queue_fwd(wire);
                    }
                }

            if (mode == 0 && t.fwd_queue.size() < 4)
                continue;

            if (mode == 1 && !is_pseudo_const_net(net)) {
                // Seed forwards with the source wire, if less than 8 existing wires added
                seed_queue_fwd(src_wire);
            } else {
                set_visited_fwd(t, src_wire_idx, PipId());
            }
            auto seed_queue_bwd = [&](WireId wire) {
                WireScore base_score;
                base_score.cost = 0;
                int wire_idx = wire_to_idx.at(wire);
                base_score.togo_cost = get_togo_cost(net, i, wire_idx, src_wire, true, crit_weight);
                t.bwd_queue.push(QueuedWire(wire_idx, base_score));
                set_visited_bwd(t, wire_idx, PipId());
            };

            // Seed backwards with the dest wire
            seed_queue_bwd(dst_wire);

            int toexplore = 25000 * std::max(1, (ad.bb.x1 - ad.bb.x0) + (ad.bb.y1 - ad.bb.y0));
            int iter = 0;

            // Mode 0 required both queues to be live
            while (((mode == 0) ? (!t.fwd_queue.empty() && !t.bwd_queue.empty())
                                : (!t.fwd_queue.empty() || !t.bwd_queue.empty())) &&
                   (!is_bb || iter < toexplore)) {
                ++iter;
                if (!t.fwd_queue.empty()) {
                    // Explore forwards
                    auto curr = t.fwd_queue.top();
                    t.fwd_queue.pop();
                    if (was_visited_bwd(curr.wire)) {
                        // Meet in the middle; done
                        midpoint_wire = curr.wire;
                        break;
                    }
                    auto &curr_data = flat_wires.at(curr.wire);
                    for (PipId dh : ctx->getPipsDownhill(curr_data.w)) {
                        // Skip pips outside of box in bounding-box mode
                        if (is_bb && !hit_test_pip(nd.bb, ctx->getPipLocation(dh)))
                            continue;
                        if (!ctx->checkPipAvailForNet(dh, net))
                            continue;
                        WireId next = ctx->getPipDstWire(dh);
                        int next_idx = wire_to_idx.at(next);
                        if (was_visited_fwd(next_idx)) {
                            // Don't expand the same node twice.
                            continue;
                        }
                        auto &nwd = flat_wires.at(next_idx);
                        if (nwd.unavailable)
                            continue;
                        // Reserved for another net
                        if (nwd.reserved_net != -1 && nwd.reserved_net != net->udata)
                            continue;
                        // Don't allow the same wire to be bound to the same net with a different driving pip
                        auto fnd_wire = nd.wires.find(next);
                        if (fnd_wire != nd.wires.end() && fnd_wire->second.first != dh)
                            continue;
                        if (!thread_test_wire(t, nwd))
                            continue; // thread safety issue
                        WireScore next_score;
                        next_score.cost = curr.score.cost + score_wire_for_arc(net, i, phys_pin, next, dh, crit_weight);
                        next_score.togo_cost =
                                cfg.estimate_weight * get_togo_cost(net, i, next_idx, dst_wire, false, crit_weight);
                        set_visited_fwd(t, next_idx, dh);
                        t.fwd_queue.push(QueuedWire(next_idx, next_score, t.rng.rng()));
                    }
                }
                if (!t.bwd_queue.empty()) {
                    // Explore backwards
                    auto curr = t.bwd_queue.top();
                    t.bwd_queue.pop();
                    if (was_visited_fwd(curr.wire)) {
                        // Meet in the middle; done
                        midpoint_wire = curr.wire;
                        break;
                    }
                    auto &curr_data = flat_wires.at(curr.wire);
                    // Don't allow the same wire to be bound to the same net with a different driving pip
                    PipId bound_pip;
                    auto fnd_wire = nd.wires.find(curr_data.w);
                    if (fnd_wire != nd.wires.end())
                        bound_pip = fnd_wire->second.first;

                    for (PipId uh : ctx->getPipsUphill(curr_data.w)) {
                        if (bound_pip != PipId() && bound_pip != uh)
                            continue;
                        if (is_bb && !hit_test_pip(nd.bb, ctx->getPipLocation(uh)))
                            continue;
                        if (!ctx->checkPipAvailForNet(uh, net))
                            continue;
                        WireId next = ctx->getPipSrcWire(uh);
                        int next_idx = wire_to_idx.at(next);
                        if (was_visited_bwd(next_idx)) {
                            // Don't expand the same node twice.
                            continue;
                        }
                        auto &nwd = flat_wires.at(next_idx);
                        if (nwd.unavailable)
                            continue;
                        // Reserved for another net
                        if (nwd.reserved_net != -1 && nwd.reserved_net != net->udata)
                            continue;
                        if (!thread_test_wire(t, nwd))
                            continue; // thread safety issue
                        WireScore next_score;
                        next_score.cost = curr.score.cost + score_wire_for_arc(net, i, phys_pin, next, uh, crit_weight);
                        next_score.togo_cost =
                                cfg.estimate_weight * get_togo_cost(net, i, next_idx, src_wire, true, crit_weight);
                        set_visited_bwd(t, next_idx, uh);
                        t.bwd_queue.push(QueuedWire(next_idx, next_score, t.rng.rng()));
                    }
                }
            }
            if (midpoint_wire != -1)
                break;
        }
        ArcRouteResult result = ARC_SUCCESS;
        if (midpoint_wire != -1) {
            ROUTE_LOG_DBG("   Routed (explored %d wires): ", explored);
            int cursor_bwd = midpoint_wire;
            while (was_visited_fwd(cursor_bwd)) {
                PipId pip = flat_wires.at(cursor_bwd).pip_fwd;
                if (pip == PipId() && cursor_bwd != src_wire_idx)
                    break;
                bind_pip_internal(nd, i, cursor_bwd, pip);
                if (ctx->debug && !is_mt) {
                    auto &wd = flat_wires.at(cursor_bwd);
                    ROUTE_LOG_DBG("      fwd wire: %s (curr %d hist %f share %d)\n", ctx->nameOfWire(wd.w),
                                  wd.curr_cong - 1, wd.hist_cong_cost, nd.wires.at(wd.w).second);
                }
                if (pip == PipId()) {
                    break;
                }
                ROUTE_LOG_DBG("         fwd pip: %s (%d, %d)\n", ctx->nameOfPip(pip), ctx->getPipLocation(pip).x,
                              ctx->getPipLocation(pip).y);
                cursor_bwd = wire_to_idx.at(ctx->getPipSrcWire(pip));
            }

            while (cursor_bwd != src_wire_idx) {
                // Tack onto existing routing
                WireId bwd_w = flat_wires.at(cursor_bwd).w;
                if (!nd.wires.count(bwd_w))
                    break;
                auto &bound = nd.wires.at(bwd_w);
                PipId pip = bound.first;
                if (ctx->debug && !is_mt) {
                    auto &wd = flat_wires.at(cursor_bwd);
                    ROUTE_LOG_DBG("      ext wire: %s (curr %d hist %f share %d)\n", ctx->nameOfWire(wd.w),
                                  wd.curr_cong - 1, wd.hist_cong_cost, bound.second);
                }
                bind_pip_internal(nd, i, cursor_bwd, pip);
                if (pip == PipId())
                    break;
                cursor_bwd = wire_to_idx.at(ctx->getPipSrcWire(pip));
            }

            NPNR_ASSERT(cursor_bwd == src_wire_idx);

            int cursor_fwd = midpoint_wire;
            while (was_visited_bwd(cursor_fwd)) {
                PipId pip = flat_wires.at(cursor_fwd).pip_bwd;
                if (pip == PipId()) {
                    break;
                }
                ROUTE_LOG_DBG("         bwd pip: %s (%d, %d)\n", ctx->nameOfPip(pip), ctx->getPipLocation(pip).x,
                              ctx->getPipLocation(pip).y);
                cursor_fwd = wire_to_idx.at(ctx->getPipDstWire(pip));
                bind_pip_internal(nd, i, cursor_fwd, pip);
                if (ctx->debug && !is_mt) {
                    auto &wd = flat_wires.at(cursor_fwd);
                    ROUTE_LOG_DBG("      bwd wire: %s (curr %d hist %f share %d)\n", ctx->nameOfWire(wd.w),
                                  wd.curr_cong - 1, wd.hist_cong_cost, nd.wires.at(wd.w).second);
                }
            }
            NPNR_ASSERT(cursor_fwd == dst_wire_idx);

            update_wire_by_loc(t, net, i, phys_pin, is_mt);
            t.processed_sinks.insert(dst_wire);
            ad.routed = true;
            auto arc_end = std::chrono::high_resolution_clock::now();
            ROUTE_LOG_DBG("Routing arc %d of net '%s' (is_bb = %d) took %02fs\n", int(i), ctx->nameOf(net), is_bb,
                          std::chrono::duration<float>(arc_end - arc_start).count());
        } else {
            auto arc_end = std::chrono::high_resolution_clock::now();
            ROUTE_LOG_DBG("Failed routing arc %d of net '%s' (is_bb = %d) took %02fs\n", int(i), ctx->nameOf(net),
                          is_bb, std::chrono::duration<float>(arc_end - arc_start).count());
            result = ARC_RETRY_WITHOUT_BB;
        }
        reset_wires(t);
        return result;
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
        t.wire_by_loc.clear();
        t.in_wire_by_loc.clear();
        auto &nd = nets.at(net->udata);
        for (size_t i = 0; i < net->users.size(); i++) {
            auto &ad = nd.arcs.at(i);
            for (size_t j = 0; j < ad.size(); j++) {
                // Ripup failed arcs to start with
                // Check if arc is already legally routed
                if (check_arc_routing(net, i, j)) {
                    update_wire_by_loc(t, net, i, j, true);
                    continue;
                }

                // Ripup arc to start with
                ripup_arc(net, i, j);
                t.route_arcs.emplace_back(i, j);
            }
        }
        // Route most critical arc first
        std::stable_sort(t.route_arcs.begin(), t.route_arcs.end(),
                         [&](std::pair<size_t, size_t> a, std::pair<size_t, size_t> b) {
                             return get_arc_crit(net, a.first) > get_arc_crit(net, b.first);
                         });
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
        pool<WireId> already_updated;
        for (size_t i = 0; i < nets.size(); i++) {
            auto &nd = nets.at(i);
            for (const auto &w : nd.wires) {
                ++total_wire_use;
                auto &wd = wire_data(w.first);
                if (wd.curr_cong > 1) {
                    if (already_updated.count(w.first)) {
                        ++total_overuse;
                    } else {
                        if (curr_cong_weight > 0)
                            wd.hist_cong_cost =
                                    std::min(1e9, wd.hist_cong_cost + (wd.curr_cong - 1) * hist_cong_weight);
                        already_updated.insert(w.first);
                        ++overused_wires;
                    }
                    failed_nets.insert(i);
                }
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
            if (!nd.wires.count(cursor)) {
                log("Failure details:\n");
                log("    Cursor: %s\n", ctx->nameOfWire(cursor));
                log_error("Internal error; incomplete route tree for arc %d of net %s.\n", usr_idx, ctx->nameOf(net));
            }
            PipId p = nd.wires.at(cursor).first;
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

    void write_wiretype_heatmap(std::ostream &out)
    {
        dict<IdString, std::vector<int>> cong_by_type;
        size_t max_cong = 0;
        // Build histogram
        for (auto &wd : flat_wires) {
            size_t val = wd.curr_cong;
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

    delay_t get_route_delay(int net, int usr_idx, int phys_idx)
    {
        auto &nd = nets.at(net);
        auto &ad = nd.arcs.at(usr_idx).at(phys_idx);
        WireId cursor = ad.sink_wire;
        if (cursor == WireId() || nd.src_wire == WireId())
            return 0;
        delay_t delay = 0;
        while (true) {
            delay += ctx->getWireDelay(cursor).maxDelay();
            if (!nd.wires.count(cursor))
                break;
            auto &bound = nd.wires.at(cursor);
            if (bound.first == PipId())
                break;
            delay += ctx->getPipDelay(bound.first).maxDelay();
            cursor = ctx->getPipSrcWire(bound.first);
        }
        NPNR_ASSERT(cursor == nd.src_wire);
        return delay;
    }

    void update_route_delays()
    {
        for (int net : route_queue) {
            NetInfo *ni = nets_by_udata.at(net);
#ifdef ARCH_ECP5
            if (ni->is_global)
                continue;
#endif
            auto &nd = nets.at(net);
            for (int i = 0; i < int(nd.arcs.size()); i++) {
                delay_t arc_delay = 0;
                for (int j = 0; j < int(nd.arcs.at(i).size()); j++)
                    arc_delay = std::max(arc_delay, get_route_delay(net, i, j));
                tmg.set_route_delay(CellPortKey(ni->users.at(i)), DelayPair(arc_delay));
            }
        }
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

            if (timing_driven && (int(route_queue.size()) > (int(nets_by_udata.size()) / 500))) {
                // Heuristic: reduce runtime by skipping STA in the case of a "long tail" of a few
                // congested nodes
                tmg.run(iter == 1);
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

            do_route();
            update_route_delays();
            route_queue.clear();
            update_congestion();

            if (!cfg.heatmap.empty()) {
                std::string filename(cfg.heatmap + "_" + std::to_string(iter) + ".csv");
                std::ofstream cong_map(filename);
                if (!cong_map)
                    log_error("Failed to open wiretype heatmap %s for writing.\n", filename.c_str());
                write_wiretype_heatmap(cong_map);
                log_info("        wrote wiretype heatmap to %s.\n", filename.c_str());
            }

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
                curr_cong_weight += cfg.curr_cong_mult;
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
    estimate_weight = ctx->setting<float>("router2/estimateWeight", 1.25f);
    perf_profile = ctx->setting<bool>("router2/perfProfile", false);
    if (ctx->settings.count(ctx->id("router2/heatmap")))
        heatmap = ctx->settings.at(ctx->id("router2/heatmap")).as_string();
    else
        heatmap = "";
}

NEXTPNR_NAMESPACE_END
