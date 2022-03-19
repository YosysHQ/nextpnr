/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021-22  gatecat <gatecat@ds0.me>
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

#include "parallel_refine.h"
#include "log.h"

#if !defined(__wasm)

#include "fast_bels.h"
#include "scope_lock.h"
#include "timing.h"

#include <chrono>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <thread>

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct Partition
{
    int x0, y0, x1, y1;
    std::vector<CellInfo *> cells;
    Partition() = default;
    explicit Partition(Context *ctx)
    {
        x0 = ctx->getGridDimX();
        y0 = ctx->getGridDimY();
        x1 = 0;
        y1 = 0;
        for (auto &cell : ctx->cells) {
            Loc l = ctx->getBelLocation(cell.second->bel);
            x0 = std::min(x0, l.x);
            x1 = std::max(x1, l.x);
            y0 = std::min(y0, l.y);
            y1 = std::max(y1, l.y);
            cells.push_back(cell.second.get());
        }
    }
    void split(Context *ctx, bool yaxis, float pivot, Partition &l, Partition &r)
    {
        std::sort(cells.begin(), cells.end(), [&](CellInfo *a, CellInfo *b) {
            Loc l0 = ctx->getBelLocation(a->bel), l1 = ctx->getBelLocation(b->bel);
            return yaxis ? (l0.y < l1.y) : (l0.x < l1.x);
        });
        size_t pivot_point = size_t(cells.size() * pivot);
        l.cells.clear();
        r.cells.clear();
        l.cells.reserve(pivot_point);
        r.cells.reserve(cells.size() - pivot_point);
        int pivot_coord = (pivot_point == 0) ? (yaxis ? y1 : x1)
                                             : (yaxis ? ctx->getBelLocation(cells.at(pivot_point - 1)->bel).y
                                                      : ctx->getBelLocation(cells.at(pivot_point - 1)->bel).x);
        for (size_t i = 0; i < cells.size(); i++) {
            Loc loc = ctx->getBelLocation(cells.at(i)->bel);
            ((yaxis ? loc.y : loc.x) <= pivot_coord ? l.cells : r.cells).push_back(cells.at(i));
        }
        if (yaxis) {
            l.x0 = r.x0 = x0;
            l.x1 = r.x1 = x1;
            l.y0 = y0;
            l.y1 = pivot_coord;
            r.y0 = (pivot_coord == y1) ? y1 : (pivot_coord + 1);
            r.y1 = y1;
        } else {
            l.y0 = r.y0 = y0;
            l.y1 = r.y1 = y1;
            l.x0 = x0;
            l.x1 = pivot_coord;
            r.x0 = (pivot_coord == x1) ? x1 : (pivot_coord + 1);
            r.x1 = x1;
        }
    }
};

typedef int64_t wirelen_t;

struct NetBB
{
    // Actual bounding box
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    // Number of cells at each extremity
    int nx0 = 0, nx1 = 0, ny0 = 0, ny1 = 0;
    wirelen_t hpwl(const ParallelRefineCfg &cfg) const
    {
        return wirelen_t(cfg.hpwl_scale_x * (x1 - x0) + cfg.hpwl_scale_y * (y1 - y0));
    }
    static NetBB compute(const Context *ctx, const NetInfo *net, const dict<IdString, BelId> *cell2bel = nullptr)
    {
        NetBB result{};
        if (!net->driver.cell)
            return result;
        auto bel_loc = [&](const CellInfo *cell) {
            BelId bel = cell2bel ? cell2bel->at(cell->name) : cell->bel;
            return ctx->getBelLocation(bel);
        };
        result.nx0 = result.nx1 = result.ny0 = result.ny1 = 1;
        Loc drv_loc = bel_loc(net->driver.cell);
        result.x0 = result.x1 = drv_loc.x;
        result.y0 = result.y1 = drv_loc.y;
        for (auto &usr : net->users) {
            Loc l = bel_loc(usr.cell);
            if (l.x == result.x0)
                ++result.nx0; // on the edge
            else if (l.x < result.x0) {
                result.x0 = l.x; // extends the edge
                result.nx0 = 1;
            }
            if (l.x == result.x1)
                ++result.nx1; // on the edge
            else if (l.x > result.x1) {
                result.x1 = l.x; // extends the edge
                result.nx1 = 1;
            }
            if (l.y == result.y0)
                ++result.ny0; // on the edge
            else if (l.y < result.y0) {
                result.y0 = l.y; // extends the edge
                result.ny0 = 1;
            }
            if (l.y == result.y1)
                ++result.ny1; // on the edge
            else if (l.y > result.y1) {
                result.y1 = l.y; // extends the edge
                result.ny1 = 1;
            }
        }
        return result;
    }
};

struct GlobalState
{
    explicit GlobalState(Context *ctx, ParallelRefineCfg cfg) : ctx(ctx), cfg(cfg), bels(ctx, false, 64), tmg(ctx){};
    Context *ctx;
    ParallelRefineCfg cfg;
    FastBels bels;
    std::vector<NetInfo *> flat_nets; // flat array of all nets in the design for fast referencing by index
    std::vector<NetBB> last_bounds;
    std::vector<std::vector<double>> last_tmg_costs;
    dict<IdString, NetBB> region_bounds;
    TimingAnalyser tmg;

    std::shared_timed_mutex archapi_mutex;

    double temperature = 1e-7;
    int radius = 3;

    wirelen_t total_wirelen = 0;
    double total_timing_cost = 0;

    double get_timing_cost(const NetInfo *net, store_index<PortRef> user,
                           const dict<IdString, BelId> *cell2bel = nullptr)
    {
        if (!net->driver.cell)
            return 0;
        const auto &sink = net->users.at(user);
        IdString driver_pin, sink_pin;
        // Pick the first pin for a prediction; assume all will be similar enouhg
        for (auto pin : ctx->getBelPinsForCellPin(net->driver.cell, net->driver.port)) {
            driver_pin = pin;
            break;
        }
        for (auto pin : ctx->getBelPinsForCellPin(sink.cell, sink.port)) {
            sink_pin = pin;
            break;
        }
        float crit = tmg.get_criticality(CellPortKey(sink));
        BelId src_bel = cell2bel ? cell2bel->at(net->driver.cell->name) : net->driver.cell->bel;
        BelId dst_bel = cell2bel ? cell2bel->at(sink.cell->name) : sink.cell->bel;
        double delay = ctx->getDelayNS(ctx->predictDelay(src_bel, driver_pin, dst_bel, sink_pin));
        return delay * std::pow(crit, cfg.crit_exp);
    }

    bool skip_net(const NetInfo *net) const
    {
        if (!net->driver.cell)
            return true;
        if (ctx->getBelGlobalBuf(net->driver.cell->bel))
            return true;
        return false;
    }
    bool timing_skip_net(const NetInfo *net) const
    {
        if (!net->driver.cell)
            return true;
        int cc;
        auto cls = ctx->getPortTimingClass(net->driver.cell, net->driver.port, cc);
        if (cls == TMG_IGNORE || cls == TMG_GEN_CLOCK)
            return true;
        return false;
    }
    // ....
};

struct ThreadState
{
    Context *ctx;         // Nextpnr context pointer
    GlobalState &g;       // Refinement engine state
    int idx;              // Index of the thread
    DeterministicRNG rng; // Local RNG
    // The cell partition that the thread works on
    Partition p;
    // Mapping from design-wide net index to thread-wide net index -- not all nets are in all partitions, so we can
    // optimise
    std::vector<int> thread_net_idx;
    // List of nets inside the partition; and their committed bounding boxes & timing costs from the thread's
    // perspective
    std::vector<NetInfo *> thread_nets;
    std::vector<NetBB> net_bounds;
    std::vector<std::vector<double>> arc_tmg_cost;
    std::vector<bool> ignored_nets, tmg_ignored_nets;
    bool arch_state_dirty = false;
    // Our local cell-bel map; that won't be affected by out-of-partition moves
    dict<IdString, BelId> local_cell2bel;

    // Data on an inflight move
    dict<IdString, std::pair<BelId, BelId>> moved_cells; // cell -> (old; new)
    // For cluster moves only
    std::vector<std::pair<CellInfo *, Loc>> cell_rel;
    // For incremental wirelength and delay updates
    wirelen_t wirelen_delta = 0;
    double timing_delta = 0;

    // Total made and accepted moved
    int n_move = 0, n_accept = 0;

    enum BoundChange
    {
        NO_CHANGE,
        CELL_MOVED_INWARDS,
        CELL_MOVED_OUTWARDS,
        FULL_RECOMPUTE
    };
    // Wirelen related are handled on a per-axis basis to reduce
    struct AxisChanges
    {
        std::vector<int> bounds_changed_nets;
        std::vector<BoundChange> already_bounds_changed;
    };
    std::array<AxisChanges, 2> axes;
    std::vector<NetBB> new_net_bounds;

    std::vector<std::vector<bool>> already_timing_changed;
    std::vector<std::pair<int, store_index<PortRef>>> timing_changed_arcs;
    std::vector<double> new_timing_costs;

    ThreadState(Context *ctx, GlobalState &g, int idx) : ctx(ctx), g(g), idx(idx){};
    void set_partition(const Partition &part)
    {
        p = part;
        thread_nets.clear();
        thread_net_idx.resize(g.flat_nets.size());
        std::fill(thread_net_idx.begin(), thread_net_idx.end(), -1);
        // Determine the set of nets that are within the thread; and therefore we care about
        for (auto thread_cell : part.cells) {
            for (auto &port : thread_cell->ports) {
                if (!port.second.net)
                    continue;
                int global_idx = port.second.net->udata;
                auto &thread_idx = thread_net_idx.at(global_idx);
                // Already added to the set
                if (thread_idx != -1)
                    continue;
                thread_idx = thread_nets.size();
                thread_nets.push_back(port.second.net);
            }
        }
        tmg_ignored_nets.clear();
        ignored_nets.clear();
        for (auto tn : thread_nets) {
            ignored_nets.push_back(g.skip_net(tn));
            tmg_ignored_nets.push_back(g.timing_skip_net(tn));
        }
        // Set up the original cell-bel map for all nets inside the thread
        local_cell2bel.clear();
        for (NetInfo *net : thread_nets) {
            if (net->driver.cell)
                local_cell2bel[net->driver.cell->name] = net->driver.cell->bel;
            for (auto &usr : net->users)
                local_cell2bel[usr.cell->name] = usr.cell->bel;
        }
    }
    void setup_initial_state()
    {
        // Setup initial net bounding boxes and timing costs
        net_bounds.clear();
        arc_tmg_cost.clear();
        for (auto tn : thread_nets) {
            net_bounds.push_back(g.last_bounds.at(tn->udata));
            arc_tmg_cost.push_back(g.last_tmg_costs.at(tn->udata));
        }
        new_net_bounds = net_bounds;
        for (int j = 0; j < 2; j++) {
            auto &a = axes.at(j);
            a.already_bounds_changed.resize(net_bounds.size());
        }
        already_timing_changed.clear();
        already_timing_changed.resize(net_bounds.size());
        for (size_t i = 0; i < thread_nets.size(); i++)
            already_timing_changed.at(i) = std::vector<bool>(thread_nets.at(i)->users.capacity());
    }
    bool bounds_check(BelId bel)
    {
        Loc l = ctx->getBelLocation(bel);
        if (l.x < p.x0 || l.x > p.x1 || l.y < p.y0 || l.y > p.y1)
            return false;
        return true;
    }
    bool bind_move()
    {
        std::unique_lock<std::shared_timed_mutex> l(g.archapi_mutex);
        for (auto &entry : moved_cells) {
            ctx->unbindBel(entry.second.first);
        }
        bool success = true;
        for (auto &entry : moved_cells) {
            // Make sure targets are available before we bind them
            if (!ctx->checkBelAvail(entry.second.second)) {
                success = false;
                break;
            }
            ctx->bindBel(entry.second.second, ctx->cells.at(entry.first).get(), STRENGTH_WEAK);
        }
        arch_state_dirty = true;
        return success;
    }
    bool check_validity()
    {
        std::shared_lock<std::shared_timed_mutex> l(g.archapi_mutex);
        bool result = true;
        for (auto e : moved_cells) {
            if (!ctx->isBelLocationValid(e.second.first)) {
                // Have to check old; too; as unbinding a bel could make a placement illegal by virtue of no longer
                // enabling dedicated routes to be used
                result = false;
                break;
            }
            if (!ctx->isBelLocationValid(e.second.second)) {
                result = false;
                break;
            }
        }
        return result;
    }
    void revert_move()
    {
        if (arch_state_dirty) {
            // If changes to the arch state were made, revert them by restoring original cell bindings
            std::unique_lock<std::shared_timed_mutex> l(g.archapi_mutex);
            for (auto &entry : moved_cells) {
                BelId curr_bound = ctx->cells.at(entry.first)->bel;
                if (curr_bound != BelId())
                    ctx->unbindBel(curr_bound);
            }
            for (auto &entry : moved_cells) {
                ctx->bindBel(entry.second.first, ctx->cells.at(entry.first).get(), STRENGTH_WEAK);
            }
            arch_state_dirty = false;
        }
        for (auto &entry : moved_cells)
            local_cell2bel[entry.first] = entry.second.first;
    }
    void commit_move()
    {
        arch_state_dirty = false;
        for (auto &axis : axes) {
            for (auto bc : axis.bounds_changed_nets) {
                // Commit updated net bounds
                net_bounds.at(bc) = new_net_bounds.at(bc);
            }
        }
        if (g.cfg.timing_driven) {
            NPNR_ASSERT(timing_changed_arcs.size() == new_timing_costs.size());
            for (size_t i = 0; i < timing_changed_arcs.size(); i++) {
                auto arc = timing_changed_arcs.at(i);
                arc_tmg_cost.at(arc.first).at(arc.second.idx()) = new_timing_costs.at(i);
            }
        }
    }
    void compute_changes_for_cell(CellInfo *cell, BelId old_bel, BelId new_bel)
    {
        Loc new_loc = ctx->getBelLocation(new_bel);
        Loc old_loc = ctx->getBelLocation(old_bel);
        for (const auto &port : cell->ports) {
            NetInfo *pn = port.second.net;
            if (!pn)
                continue;
            int idx = thread_net_idx.at(pn->udata);
            if (ignored_nets.at(idx))
                continue;
            NetBB &new_bounds = new_net_bounds.at(idx);
            // For the x-axis (i=0) and y-axis (i=1)
            for (int i = 0; i < 2; i++) {
                auto &axis = axes.at(i);
                // New and old on this axis
                int new_pos = i ? new_loc.y : new_loc.x, old_pos = i ? old_loc.y : old_loc.x;
                // References to updated bounding box entries
                auto &b0 = i ? new_bounds.y0 : new_bounds.x0;
                auto &n0 = i ? new_bounds.ny0 : new_bounds.nx0;
                auto &b1 = i ? new_bounds.y1 : new_bounds.x1;
                auto &n1 = i ? new_bounds.ny1 : new_bounds.nx1;
                auto &change = axis.already_bounds_changed.at(idx);
                // Lower bound
                if (new_pos < b0) {
                    // Further out than current lower bound
                    b0 = new_pos;
                    n0 = 1;
                    if (change == NO_CHANGE) {
                        change = CELL_MOVED_OUTWARDS;
                        axis.bounds_changed_nets.push_back(idx);
                    }
                } else if (new_pos == b0 && old_pos > b0) {
                    // Moved from inside into current bound
                    ++n0;
                    if (change == NO_CHANGE) {
                        change = CELL_MOVED_OUTWARDS;
                        axis.bounds_changed_nets.push_back(idx);
                    }
                } else if (old_pos == b0 && new_pos > b0) {
                    // Moved from current bound to inside
                    if (change == NO_CHANGE)
                        axis.bounds_changed_nets.push_back(idx);
                    if (n0 == 1) {
                        // Was the last cell on the bound; have to do a full recompute
                        change = FULL_RECOMPUTE;
                    } else {
                        --n0;
                        if (change == NO_CHANGE)
                            change = CELL_MOVED_INWARDS;
                    }
                }
                // Upper bound
                if (new_pos > b1) {
                    // Further out than current upper bound
                    b1 = new_pos;
                    n1 = new_pos;
                    if (change == NO_CHANGE) {
                        change = CELL_MOVED_OUTWARDS;
                        axis.bounds_changed_nets.push_back(idx);
                    }
                } else if (new_pos == b1 && old_pos < b1) {
                    // Moved onto current bound
                    ++n1;
                    if (change == NO_CHANGE) {
                        change = CELL_MOVED_OUTWARDS;
                        axis.bounds_changed_nets.push_back(idx);
                    }
                } else if (old_pos == b1 && new_pos < b1) {
                    // Moved from current bound to inside
                    if (change == NO_CHANGE)
                        axis.bounds_changed_nets.push_back(idx);
                    if (n1 == 1) {
                        // Was the last cell on the bound; have to do a full recompute
                        change = FULL_RECOMPUTE;
                    } else {
                        --n1;
                        if (change == NO_CHANGE)
                            change = CELL_MOVED_INWARDS;
                    }
                }
            }
            // Timing updates if timing driven
            if (g.cfg.timing_driven && !tmg_ignored_nets.at(idx)) {
                if (port.second.type == PORT_OUT) {
                    int cc;
                    TimingPortClass cls = ctx->getPortTimingClass(cell, port.first, cc);
                    if (cls != TMG_IGNORE) {
                        for (auto usr : pn->users.enumerate())
                            if (!already_timing_changed.at(idx).at(usr.index.idx())) {
                                timing_changed_arcs.emplace_back(std::make_pair(idx, usr.index));
                                already_timing_changed.at(idx).at(usr.index.idx()) = true;
                            }
                    }
                } else {
                    auto usr = port.second.user_idx;
                    if (!already_timing_changed.at(idx).at(usr.idx())) {
                        timing_changed_arcs.emplace_back(std::make_pair(idx, usr));
                        already_timing_changed.at(idx).at(usr.idx()) = true;
                    }
                }
            }
        }
    }
    void compute_total_change()
    {
        auto &xa = axes.at(0), &ya = axes.at(1);
        for (auto &bc : xa.bounds_changed_nets)
            if (xa.already_bounds_changed.at(bc) == FULL_RECOMPUTE)
                new_net_bounds.at(bc) = NetBB::compute(ctx, thread_nets.at(bc), &local_cell2bel);
        for (auto &bc : ya.bounds_changed_nets)
            if (xa.already_bounds_changed.at(bc) != FULL_RECOMPUTE &&
                ya.already_bounds_changed.at(bc) == FULL_RECOMPUTE)
                new_net_bounds.at(bc) = NetBB::compute(ctx, thread_nets.at(bc), &local_cell2bel);
        for (auto &bc : xa.bounds_changed_nets)
            wirelen_delta += (new_net_bounds.at(bc).hpwl(g.cfg) - net_bounds.at(bc).hpwl(g.cfg));
        for (auto &bc : ya.bounds_changed_nets)
            if (xa.already_bounds_changed.at(bc) == NO_CHANGE)
                wirelen_delta += (new_net_bounds.at(bc).hpwl(g.cfg) - net_bounds.at(bc).hpwl(g.cfg));
        if (g.cfg.timing_driven) {
            NPNR_ASSERT(new_timing_costs.empty());
            for (auto arc : timing_changed_arcs) {
                double new_cost = g.get_timing_cost(thread_nets.at(arc.first), arc.second, &local_cell2bel);
                timing_delta += (new_cost - arc_tmg_cost.at(arc.first).at(arc.second.idx()));
                new_timing_costs.push_back(new_cost);
            }
        }
    }
    void reset_move_state()
    {
        moved_cells.clear();
        cell_rel.clear();
        for (auto &axis : axes) {
            for (auto bc : axis.bounds_changed_nets) {
                new_net_bounds.at(bc) = net_bounds.at(bc);
                axis.already_bounds_changed[bc] = NO_CHANGE;
            }
            axis.bounds_changed_nets.clear();
        }
        for (auto &arc : timing_changed_arcs) {
            already_timing_changed.at(arc.first).at(arc.second.idx()) = false;
        }
        timing_changed_arcs.clear();
        new_timing_costs.clear();
        wirelen_delta = 0;
        timing_delta = 0;
    }

    bool accept_move()
    {
        static constexpr double epsilon = 1e-20;
        double delta = g.cfg.lambda * (timing_delta / std::max<double>(epsilon, g.total_timing_cost)) +
                       (1.0 - g.cfg.lambda) * (double(wirelen_delta) / std::max<double>(epsilon, g.total_wirelen));
        return delta < 0 ||
               (g.temperature > 1e-8 && (rng.rng() / float(0x3fffffff)) <= std::exp(-delta / g.temperature));
    }

    bool add_to_move(CellInfo *cell, BelId old_bel, BelId new_bel)
    {
        if (!bounds_check(old_bel) || !bounds_check(new_bel))
            return false;
        if (!ctx->isValidBelForCellType(cell->type, new_bel))
            return false;
        NPNR_ASSERT(!moved_cells.count(cell->name));
        moved_cells[cell->name] = std::make_pair(old_bel, new_bel);
        local_cell2bel[cell->name] = new_bel;
        compute_changes_for_cell(cell, old_bel, new_bel);
        return true;
    }

    bool single_cell_swap(CellInfo *cell, BelId new_bel)
    {
        NPNR_ASSERT(moved_cells.empty());
        BelId old_bel = cell->bel;
        CellInfo *bound = ctx->getBoundBelCell(new_bel);
        if (bound && (bound->belStrength > STRENGTH_STRONG || bound->cluster != ClusterId()))
            return false;
        if (!add_to_move(cell, old_bel, new_bel))
            goto fail;
        if (bound && !add_to_move(bound, new_bel, old_bel))
            goto fail;
        compute_total_change();
        // SA acceptance criteria

        if (!accept_move()) {
            // SA fail
            goto fail;
        }
        // Check validity rules
        if (!bind_move())
            goto fail;
        if (!check_validity())
            goto fail;
        // Accepted!
        commit_move();
        reset_move_state();
        return true;
    fail:
        revert_move();
        reset_move_state();
        return false;
    }

    bool chain_swap(CellInfo *root_cell, BelId new_root_bel)
    {
        NPNR_ASSERT(moved_cells.empty());
        std::queue<std::pair<ClusterId, BelId>> displaced_clusters;
        pool<BelId> used_bels;
        displaced_clusters.emplace(root_cell->cluster, new_root_bel);
        while (!displaced_clusters.empty()) {
            std::vector<std::pair<CellInfo *, BelId>> dest_bels;
            auto cursor = displaced_clusters.front();
            displaced_clusters.pop();
            if (!ctx->getClusterPlacement(cursor.first, cursor.second, dest_bels))
                goto fail;
            for (const auto &db : dest_bels) {
                BelId old_bel = db.first->bel;
                if (moved_cells.count(db.first->name))
                    goto fail;
                if (!add_to_move(db.first, old_bel, db.second))
                    goto fail;
                if (used_bels.count(db.second))
                    goto fail;
                used_bels.insert(db.second);

                CellInfo *bound = ctx->getBoundBelCell(db.second);
                if (bound) {
                    if (moved_cells.count(bound->name)) {
                        // Don't move a cell multiple times in the same go
                        goto fail;
                    } else if (bound->belStrength > STRENGTH_STRONG) {
                        goto fail;
                    } else if (bound->cluster != ClusterId()) {
                        // Displace the entire cluster
                        Loc old_loc = ctx->getBelLocation(old_bel);
                        Loc bound_loc = ctx->getBelLocation(bound->bel);
                        Loc root_loc = ctx->getBelLocation(ctx->getClusterRootCell(bound->cluster)->bel);
                        BelId new_root = ctx->getBelByLocation(Loc(old_loc.x + (root_loc.x - bound_loc.x),
                                                                   old_loc.y + (root_loc.y - bound_loc.y),
                                                                   old_loc.z + (root_loc.z - bound_loc.z)));
                        if (new_root == BelId())
                            goto fail;
                        displaced_clusters.emplace(bound->cluster, new_root);
                    } else {
                        // Single cell swap
                        if (used_bels.count(old_bel))
                            goto fail;
                        used_bels.insert(old_bel);
                        if (!add_to_move(bound, bound->bel, old_bel))
                            goto fail;
                    }
                } else if (!ctx->checkBelAvail(db.second)) {
                    goto fail;
                }
            }
        }
        compute_total_change();
        // SA acceptance criteria

        if (!accept_move()) {
            // SA fail
            goto fail;
        }
        // Check validity rules
        if (!bind_move())
            goto fail;
        if (!check_validity())
            goto fail;
        // Accepted!
        commit_move();
        reset_move_state();
        return true;
    fail:
        revert_move();
        reset_move_state();
        return false;
    }

    BelId random_bel_for_cell(CellInfo *cell, int force_z = -1)
    {
        IdString targetType = cell->type;
        Loc curr_loc = ctx->getBelLocation(cell->bel);
        int count = 0;

        int dx = g.radius, dy = g.radius;
        if (cell->region != nullptr && cell->region->constr_bels) {
            dx = std::min(g.cfg.hpwl_scale_x * g.radius,
                          (g.region_bounds[cell->region->name].x1 - g.region_bounds[cell->region->name].x0) + 1);
            dy = std::min(g.cfg.hpwl_scale_y * g.radius,
                          (g.region_bounds[cell->region->name].y1 - g.region_bounds[cell->region->name].y0) + 1);
            // Clamp location to within bounds
            curr_loc.x = std::max(g.region_bounds[cell->region->name].x0, curr_loc.x);
            curr_loc.x = std::min(g.region_bounds[cell->region->name].x1, curr_loc.x);
            curr_loc.y = std::max(g.region_bounds[cell->region->name].y0, curr_loc.y);
            curr_loc.y = std::min(g.region_bounds[cell->region->name].y1, curr_loc.y);
        }

        FastBels::FastBelsData *bel_data;
        auto type_cnt = g.bels.getBelsForCellType(targetType, &bel_data);

        while (true) {
            int nx = rng.rng(2 * dx + 1) + std::max(curr_loc.x - dx, 0);
            int ny = rng.rng(2 * dy + 1) + std::max(curr_loc.y - dy, 0);
            if (type_cnt < 64)
                nx = ny = 0;
            if (nx >= int(bel_data->size()))
                continue;
            if (ny >= int(bel_data->at(nx).size()))
                continue;
            const auto &fb = bel_data->at(nx).at(ny);
            if (fb.size() == 0)
                continue;
            BelId bel = fb.at(rng.rng(int(fb.size())));
            if (!bounds_check(bel))
                continue;
            if (force_z != -1) {
                Loc loc = ctx->getBelLocation(bel);
                if (loc.z != force_z)
                    continue;
            }
            if (!cell->testRegion(bel))
                continue;
            count++;
            return bel;
        }
    }

    void run_iter()
    {
        setup_initial_state();
        n_accept = 0;
        n_move = 0;
        for (int m = 0; m < g.cfg.inner_iters; m++) {
            for (auto cell : p.cells) {
                if (cell->belStrength > STRENGTH_STRONG)
                    continue;
                if (cell->cluster != ClusterId()) {
                    if (cell != ctx->getClusterRootCell(cell->cluster))
                        continue; // only move cluster root
                    Loc old_loc = ctx->getBelLocation(cell->bel);
                    BelId new_root = random_bel_for_cell(cell, old_loc.z);
                    if (new_root == BelId() || new_root == cell->bel)
                        continue;
                    ++n_move;
                    if (chain_swap(cell, new_root))
                        ++n_accept;
                } else {
                    BelId new_bel = random_bel_for_cell(cell);
                    if (new_bel == BelId() || new_bel == cell->bel)
                        continue;
                    ++n_move;
                    if (single_cell_swap(cell, new_bel))
                        ++n_accept;
                }
            }
        }
    }
};

struct ParallelRefine
{
    Context *ctx;
    GlobalState g;
    std::vector<ThreadState> t;
    ParallelRefine(Context *ctx, ParallelRefineCfg cfg) : ctx(ctx), g(ctx, cfg)
    {
        g.flat_nets.reserve(ctx->nets.size());
        for (auto &net : ctx->nets) {
            net.second->udata = g.flat_nets.size();
            g.flat_nets.push_back(net.second.get());
        }
        // Setup per thread context
        for (int i = 0; i < cfg.threads; i++) {
            t.emplace_back(ctx, g, i);
        }
        // Setup region bounds
        for (auto &region : ctx->region) {
            Region *r = region.second.get();
            NetBB bb;
            if (r->constr_bels) {
                bb.x0 = std::numeric_limits<int>::max();
                bb.x1 = std::numeric_limits<int>::min();
                bb.y0 = std::numeric_limits<int>::max();
                bb.y1 = std::numeric_limits<int>::min();
                for (auto bel : r->bels) {
                    Loc loc = ctx->getBelLocation(bel);
                    bb.x0 = std::min(bb.x0, loc.x);
                    bb.x1 = std::max(bb.x1, loc.x);
                    bb.y0 = std::min(bb.y0, loc.y);
                    bb.y1 = std::max(bb.y1, loc.y);
                }
            } else {
                bb.x0 = 0;
                bb.y0 = 0;
                bb.x1 = ctx->getGridDimX();
                bb.y1 = ctx->getGridDimY();
            }
            g.region_bounds[r->name] = bb;
        }
        // Setup fast bels map
        pool<IdString> cell_types_in_use;
        for (auto &cell : ctx->cells) {
            IdString cell_type = cell.second->type;
            cell_types_in_use.insert(cell_type);
        }

        for (auto cell_type : cell_types_in_use) {
            g.bels.addCellType(cell_type);
        }
    };
    std::vector<Partition> parts;
    void do_partition()
    {
        parts.clear();
        parts.emplace_back(ctx);
        bool yaxis = false;
        while (parts.size() < t.size()) {
            std::vector<Partition> next(parts.size() * 2);
            for (size_t i = 0; i < parts.size(); i++) {
                // Randomly permute pivot every iteration so we get different thread boundaries
                const float delta = 0.1;
                float pivot = (0.5 - (delta / 2)) + (delta / 2) * (ctx->rng(10000) / 10000.0f);
                parts.at(i).split(ctx, yaxis, pivot, next.at(i * 2), next.at(i * 2 + 1));
            }
            std::swap(parts, next);
            yaxis = !yaxis;
        }

        NPNR_ASSERT(parts.size() == t.size());
        // TODO: thread pool to make this worthwhile...
        std::vector<std::thread> workers;
        for (size_t i = 0; i < t.size(); i++) {
            workers.emplace_back([i, this]() { t.at(i).set_partition(parts.at(i)); });
        }
        for (auto &w : workers)
            w.join();
    }

    void update_global_costs()
    {
        g.last_bounds.resize(g.flat_nets.size());
        g.last_tmg_costs.resize(g.flat_nets.size());
        g.total_wirelen = 0;
        g.total_timing_cost = 0;
        for (size_t i = 0; i < g.flat_nets.size(); i++) {
            NetInfo *ni = g.flat_nets.at(i);
            if (g.skip_net(ni))
                continue;
            g.last_bounds.at(i) = NetBB::compute(ctx, ni);
            g.total_wirelen += g.last_bounds.at(i).hpwl(g.cfg);
            if (!g.timing_skip_net(ni)) {
                auto &tc = g.last_tmg_costs.at(i);
                tc.resize(ni->users.capacity());
                for (auto usr : ni->users.enumerate()) {
                    tc.at(usr.index.idx()) = g.get_timing_cost(ni, usr.index);
                    g.total_timing_cost += tc.at(usr.index.idx());
                }
            }
        }
    }
    void run()
    {

        ScopeLock<Context> lock(ctx);
        auto refine_start = std::chrono::high_resolution_clock::now();

        g.tmg.setup_only = true;
        g.tmg.setup();
        do_partition();
        log_info("Running parallel refinement with %d threads.\n", int(t.size()));
        int iter = 1;
        bool done = false;
        update_global_costs();
        double avg_wirelen = g.total_wirelen;
        wirelen_t min_wirelen = g.total_wirelen;
        while (true) {
            if (iter > 1) {
                if (g.total_wirelen >= min_wirelen) {
                    done = true;
                } else if (g.total_wirelen < min_wirelen) {
                    min_wirelen = g.total_wirelen;
                }
                int n_accept = 0, n_move = 0;
                for (auto &t_data : t) {
                    n_accept += t_data.n_accept;
                    n_move += t_data.n_move;
                }
                double r_accept = n_accept / double(n_move);
                if (g.total_wirelen < (0.95 * avg_wirelen) && g.total_wirelen > 0) {
                    avg_wirelen = 0.8 * avg_wirelen + 0.2 * g.total_wirelen;
                } else {
                    if (r_accept > 0.15 && g.radius > 1) {
                        g.temperature *= 0.95;
                    } else {
                        g.temperature *= 0.8;
                    }
                }
                if ((iter % 10) == 0 && g.radius > 1)
                    --g.radius;
            }

            if ((iter == 1) || ((iter % 5) == 0) || done)
                log_info("  at iteration #%d: temp = %f, timing cost = "
                         "%.0f, wirelen = %.0f\n",
                         iter, g.temperature, double(g.total_timing_cost), double(g.total_wirelen));

            if (done)
                break;

            do_partition();

            std::vector<std::thread> workers;
            workers.reserve(t.size());
            for (int j = 0; j < int(t.size()); j++)
                workers.emplace_back([this, j]() { t.at(j).run_iter(); });
            for (auto &w : workers)
                w.join();
            g.tmg.run();
            update_global_costs();
            iter++;
            ctx->yield();
        }
        auto refine_end = std::chrono::high_resolution_clock::now();
        log_info("Placement refine time %.02fs\n", std::chrono::duration<float>(refine_end - refine_start).count());
    }
};
} // namespace

ParallelRefineCfg::ParallelRefineCfg(Context *ctx)
{
    timing_driven = ctx->setting<bool>("timing_driven");
    threads = ctx->setting<int>("threads", 8);
    // snap to nearest power of two; and minimum thread size
    int actual_threads = 1;
    while ((actual_threads * 2) <= threads && (int(ctx->cells.size()) / (actual_threads * 2)) >= min_thread_size)
        actual_threads *= 2;
    threads = actual_threads;
    hpwl_scale_x = 1;
    hpwl_scale_y = 1;
}

bool parallel_refine(Context *ctx, ParallelRefineCfg cfg)
{
    // TODO
    ParallelRefine refine(ctx, cfg);
    refine.run();
    timing_analysis(ctx);
    return true;
}

NEXTPNR_NAMESPACE_END

#else /* !defined(__wasm) */

NEXTPNR_NAMESPACE_BEGIN

bool parallel_refine(Context *ctx, ParallelRefineCfg cfg) { log_abort(); }

NEXTPNR_NAMESPACE_END

#endif
