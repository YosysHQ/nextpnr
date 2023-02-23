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

#if !defined(NPNR_DISABLE_THREADS)

#include "detail_place_core.h"
#include "scope_lock.h"

#include <chrono>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <thread>

NEXTPNR_NAMESPACE_BEGIN

namespace {

struct GlobalState : DetailPlacerState
{
    explicit GlobalState(Context *ctx, ParallelRefineCfg cfg) : DetailPlacerState(ctx, this->cfg), cfg(cfg){};

    dict<ClusterId, std::vector<CellInfo *>> cluster2cells;

    ParallelRefineCfg cfg;
    double temperature = 1e-7;
    int radius = 3;
    // ....
};

struct ThreadState : DetailPlacerThreadState
{
    ThreadState(Context *ctx, GlobalState &g, int idx) : DetailPlacerThreadState(ctx, g, idx), g(g){};
    // Total made and accepted moved
    GlobalState &g;
    int n_move = 0, n_accept = 0;

    dict<std::pair<int, int>, std::vector<CellInfo *>> tile2cell;

    bool accept_move()
    {
        static constexpr double epsilon = 1e-20;
        double delta = g.cfg.lambda * (timing_delta / std::max<double>(epsilon, g.total_timing_cost)) +
                       (1.0 - g.cfg.lambda) * (double(wirelen_delta) / std::max<double>(epsilon, g.total_wirelen));
        return delta < 0 ||
               (g.temperature > 1e-8 && (rng.rng() / float(0x3fffffff)) <= std::exp(-delta / g.temperature));
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

    bool cluster_inside_tile(ClusterId cluster, int x, int y)
    {
        for (auto &c : g.cluster2cells.at(cluster)) {
            Loc l = ctx->getBelLocation(c->bel);
            if (l.x != x || l.y != y)
                return false;
        }
        return true;
    }

    bool do_tile_swap(int x, int y, int xn, int yn)
    {
        if (xn < p.x0 || xn > p.x1)
            return false;
        if (yn < p.y0 || yn > p.y1)
            return false;
        if ((x == xn) && (y == yn))
            return false;

        NPNR_ASSERT(moved_cells.empty());

        auto move_tile = [&](int sx, int sy, int dx, int dy) -> bool {
            for (auto c : tile2cell[std::make_pair(sx, sy)]) {
                if (c->belStrength > STRENGTH_STRONG ||
                    ((c->cluster != ClusterId()) && !cluster_inside_tile(c->cluster, sx, sy)))
                    return false; // check clusters before we start moving stuff
            }
            for (auto c : tile2cell[std::make_pair(sx, sy)]) {
                Loc l = ctx->getBelLocation(c->bel);
                l.x = dx;
                l.y = dy;
                BelId new_bel = ctx->getBelByLocation(l);
                if (new_bel == BelId() || !ctx->isValidBelForCellType(c->type, new_bel))
                    return false;
                if (!add_to_move(c, c->bel, new_bel))
                    return false;
            }
            return true;
        };

        if (!move_tile(x, y, xn, yn))
            goto fail;
        if (!move_tile(xn, yn, x, y))
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
        std::swap(tile2cell[std::make_pair(x, y)], tile2cell[std::make_pair(xn, yn)]);
        return true;
    fail:
        revert_move();
        reset_move_state();
        return false;
    }

    void do_tile_swaps()
    {
        tile2cell.clear();
        for (auto c : p.cells) {
            auto loc = ctx->getBelLocation(c->bel);
            tile2cell[std::make_pair(loc.x, loc.y)].push_back(c);
        }
        std::vector<std::pair<int, int>> tiles;
        for (auto &t : tile2cell)
            tiles.push_back(t.first);
        rng.shuffle(tiles);
        for (auto &t : tiles) {
            int x = t.first, y = t.second;
            int lx = std::max(x - g.radius, p.x0), rx = std::min(x + g.radius, p.x1);
            int by = std::max(y - g.radius, p.y0), ty = std::min(y + g.radius, p.y1);
            int xn = lx + ctx->rng((rx - lx) + 1);
            int yn = by + ctx->rng((ty - by) + 1);
            ++n_move;
            if (do_tile_swap(x, y, xn, yn)) {
                ++n_accept;
            }
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
            if ((m % 2) == 0)
                do_tile_swaps();
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
            if (cell.second->isPseudo())
                continue;
            IdString cell_type = cell.second->type;
            cell_types_in_use.insert(cell_type);
            if (cell.second->cluster != ClusterId())
                g.cluster2cells[cell.second->cluster].push_back(cell.second.get());
        }

        for (auto cell_type : cell_types_in_use) {
            g.bels.addCellType(cell_type);
        }
    };
    std::vector<PlacePartition> parts;
    void do_partition()
    {
        parts.clear();
        parts.emplace_back(ctx);
        bool yaxis = false;
        while (parts.size() < t.size()) {
            std::vector<PlacePartition> next(parts.size() * 2);
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
        g.update_global_costs();
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
            g.update_global_costs();
            iter++;
            ctx->yield();
        }
        auto refine_end = std::chrono::high_resolution_clock::now();
        log_info("Placement refine time %.02fs\n", std::chrono::duration<float>(refine_end - refine_start).count());
    }
};
} // namespace

ParallelRefineCfg::ParallelRefineCfg(Context *ctx) : DetailPlaceCfg(ctx)
{
    threads = ctx->setting<int>("threads", 8);
    // snap to nearest power of two; and minimum thread size
    int actual_threads = 1;
    while ((actual_threads * 2) <= threads && (int(ctx->cells.size()) / (actual_threads * 2)) >= min_thread_size)
        actual_threads *= 2;
    threads = actual_threads;
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

#else /* !defined(NPNR_DISABLE_THREADS) */

NEXTPNR_NAMESPACE_BEGIN

bool parallel_refine(Context *ctx, ParallelRefineCfg cfg) { log_abort(); }

NEXTPNR_NAMESPACE_END

#endif
