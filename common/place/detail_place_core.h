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

/*
This provides core data structures for a thread-safe detail placer that needs to swap cells and evaluate the cost
changes of swaps.

It works on a partition-based threading approach; although threading can be avoided by only instantiating one
per-thread structure and calling its methods from the main thread.

Each thread's data includes its own local net indexing for nets inside the partition (which can overlap thread
boundaries); and its own local cell-to-bel mapping for any cells on those nets, so there are no races with moves
made by other threads.

A move is an atomic transaction of updated cell to bel mappings inside a thread. The first step is to reset the
per-move structures; then to add all of the moved cells to the move with add_to_move.

Evaluation of wirelength and timing changes of a move is done with compute_changes_for_cell and compute_total_change.

bind_move will probationally bind the move using the arch API functions, acquiring a lock during this time to prevent
races on non-thread-safe arch implementations, returning true if the bind succeeded or false if something went wrong
and it should be aborted. check_validity must then be called to use the arch API validity check functions on the move.

Finally if the move meets criteria and is accepted then commit_move marks it as committed, otherwise revert_move
aborts the entire move transaction.
*/

#ifndef DETAIL_PLACE_CORE_H
#define DETAIL_PLACE_CORE_H

#include "nextpnr.h"

#include "detail_place_cfg.h"
#include "fast_bels.h"
#include "timing.h"

#include <queue>

#if !defined(NPNR_DISABLE_THREADS)
#include <shared_mutex>
#endif

NEXTPNR_NAMESPACE_BEGIN

struct PlacePartition
{
    int x0, y0, x1, y1;
    std::vector<CellInfo *> cells;
    PlacePartition() = default;
    explicit PlacePartition(Context *ctx);
    void split(Context *ctx, bool yaxis, float pivot, PlacePartition &l, PlacePartition &r);
};

typedef int64_t wirelen_t;

struct NetBB
{
    // Actual bounding box
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    // Number of cells at each extremity
    int nx0 = 0, nx1 = 0, ny0 = 0, ny1 = 0;
    inline wirelen_t hpwl(const DetailPlaceCfg &cfg) const
    {
        return wirelen_t(cfg.hpwl_scale_x * (x1 - x0) + cfg.hpwl_scale_y * (y1 - y0));
    }
    static NetBB compute(const Context *ctx, const NetInfo *net, const dict<IdString, BelId> *cell2bel = nullptr);
};

struct DetailPlacerState
{
    explicit DetailPlacerState(Context *ctx, DetailPlaceCfg &cfg)
            : ctx(ctx), base_cfg(cfg), bels(ctx, false, 64), tmg(ctx){};
    Context *ctx;
    DetailPlaceCfg &base_cfg;
    FastBels bels;
    std::vector<NetInfo *> flat_nets; // flat array of all nets in the design for fast referencing by index
    std::vector<NetBB> last_bounds;
    std::vector<std::vector<double>> last_tmg_costs;
    dict<IdString, NetBB> region_bounds;
    TimingAnalyser tmg;

    wirelen_t total_wirelen = 0;
    double total_timing_cost = 0;

#if !defined(NPNR_DISABLE_THREADS)
    std::shared_timed_mutex archapi_mutex;
#endif

    inline double get_timing_cost(const NetInfo *net, store_index<PortRef> user,
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
        return delay * std::pow(crit, base_cfg.crit_exp);
    }

    inline bool skip_net(const NetInfo *net) const
    {
        if (!net->driver.cell)
            return true;
        if (ctx->getBelGlobalBuf(net->driver.cell->bel))
            return true;
        return false;
    }

    inline bool timing_skip_net(const NetInfo *net) const
    {
        if (!net->driver.cell)
            return true;
        int cc;
        auto cls = ctx->getPortTimingClass(net->driver.cell, net->driver.port, cc);
        if (cls == TMG_IGNORE || cls == TMG_GEN_CLOCK)
            return true;
        return false;
    }

    void update_global_costs();
};

struct DetailPlacerThreadState
{
    Context *ctx;         // Nextpnr context pointer
    DetailPlacerState &g; // Placer engine state
    int idx;              // Index of the thread
    DeterministicRNG rng; // Local RNG
    // The cell partition that the thread works on
    PlacePartition p;
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
    // Wirelen related are handled on a per-axis basis to reduce
    enum BoundChange
    {
        NO_CHANGE,
        CELL_MOVED_INWARDS,
        CELL_MOVED_OUTWARDS,
        FULL_RECOMPUTE
    };
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

    DetailPlacerThreadState(Context *ctx, DetailPlacerState &g, int idx) : ctx(ctx), g(g), idx(idx){};
    void set_partition(const PlacePartition &part);
    void setup_initial_state();
    bool bounds_check(BelId bel);

    // Reset the inflight move state
    void reset_move_state();
    // Add a cell change to the move
    bool add_to_move(CellInfo *cell, BelId old_bel, BelId new_bel);
    // For an inflight move; attempt to actually apply the changes to the arch API
    bool bind_move();
    // Checks if the arch API bel validity for a move is accepted
    bool check_validity();
    // Undo any changes relating to an inflight move
    void revert_move();
    // Mark the inflight move as complete and update cost structures
    void commit_move();
    // Update the inflight cost change structures for a given cell moe
    void compute_changes_for_cell(CellInfo *cell, BelId old_bel, BelId new_bel);
    // Update the total cost change for an inflight move
    void compute_total_change();
};

NEXTPNR_NAMESPACE_END

#endif
