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
 */

#include <algorithm>
#include <deque>
#include <queue>
#include "log.h"
#include "nextpnr.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct Router2
{
    struct arc_key
    {
        NetInfo *net_info;
        int user_idx;

        bool operator==(const arc_key &other) const
        {
            return (net_info == other.net_info) && (user_idx == other.user_idx);
        }
        bool operator<(const arc_key &other) const
        {
            return net_info == other.net_info ? user_idx < other.user_idx : net_info->name < other.net_info->name;
        }

        struct Hash
        {
            std::size_t operator()(const arc_key &arg) const noexcept
            {
                std::size_t seed = std::hash<NetInfo *>()(arg.net_info);
                seed ^= std::hash<int>()(arg.user_idx) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                return seed;
            }
        };
    };

    struct PerArcData
    {
        std::vector<std::pair<WireId, PipId>> wires;
        ArcBounds bb;
    };

    // As we allow overlap at first; the nextpnr bind functions can't be used
    // as the primary relation between arcs and wires/pips
    struct PerNetData
    {
        std::vector<PerArcData> arcs;
        ArcBounds bb;
    };

    struct PerWireData
    {
        // net --> driving pip
        std::unordered_map<int, PipId> bound_nets;
        // Which net is bound in the Arch API
        int arch_bound_net = -1;
        // Historical congestion cost
        float hist_cong_cost = 0;
    };

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

            for (size_t j = 0; j < ni->users.size(); j++) {
                auto &usr = ni->users.at(j);
                WireId src_wire = ctx->getNetinfoSourceWire(ni), dst_wire = ctx->getNetinfoSinkWire(ni, usr);
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
            }
            i++;
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
               l.y <= (bb.y1 - bb_margin_y);
    }

    double curr_cong_weight, hist_cong_weight, estimate_weight;
    // Soft-route a net (don't touch Arch data structures which might not be thread safe)
    // If is_mt is true, then strict bounding box rules are applied and log_* won't be called
    struct ThreadContext
    {
        std::priority_queue<QueuedWire, std::vector<QueuedWire>, QueuedWire::Greater> queue;
        std::unordered_map<WireId, QueuedWire> visited;
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

    ArcRouteResult route_arc(ThreadContext &t, NetInfo *net, size_t i, bool is_mt, bool is_bb = true)
    {
        auto &usr = net->users.at(i);
        ROUTE_LOG_DBG("Routing arc %d of net '%s'", int(i), ctx->nameOf(net));
        WireId src_wire = ctx->getNetinfoSourceWire(net), dst_wire = ctx->getNetinfoSinkWire(net, usr);

        if (src_wire == WireId())
            ARC_LOG_ERR("No wire found for port %s on source cell %s.\n", ctx->nameOf(net->driver.port),
                        ctx->nameOf(net->driver.cell));
        if (dst_wire == WireId())
            ARC_LOG_ERR("No wire found for port %s on destination cell %s.\n", ctx->nameOf(usr.port),
                        ctx->nameOf(usr.cell));

        return ARC_SUCCESS;
    }
#undef ARC_ERR
    bool route_net(ThreadContext &t, NetInfo *net, bool is_mt)
    {
        ROUTE_LOG_DBG("Routing net '%s'...\n", ctx->nameOf(net));
        if (!t.queue.empty()) {
            std::priority_queue<QueuedWire, std::vector<QueuedWire>, QueuedWire::Greater> new_queue;
            t.queue.swap(new_queue);
        }
        t.visited.clear();
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
#undef ARC_LOG_DBG

    void bind_and_check_legality(NetInfo *net) {}
};
} // namespace

NEXTPNR_NAMESPACE_END