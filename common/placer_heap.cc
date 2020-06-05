/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019  David Shah <david@symbioticeda.com>
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
 *  [[cite]] HeAP
 *  Analytical Placement for Heterogeneous FPGAs, Marcel Gort and Jason H. Anderson
 *  https://janders.eecg.utoronto.ca/pdfs/marcelfpl12.pdf
 *
 *  [[cite]] SimPL
 *  SimPL: An Effective Placement Algorithm, Myung-Chul Kim, Dong-Jin Lee and Igor L. Markov
 *  http://www.ece.umich.edu/cse/awards/pdfs/iccad10-simpl.pdf
 *
 *  Notable changes from the original algorithm
 *   - Following the other nextpnr placer, Bels are placed rather than CLBs. This means a strict legalisation pass is
 *     added in addition to coarse legalisation (referred to as "spreading" to avoid confusion with strict legalisation)
 *     as described in HeAP to ensure validity. This searches random bels in the vicinity of the position chosen by
 *     spreading, with diameter increasing over iterations, with a heuristic to prefer lower wirelength choices.
 *   - To make the placer timing-driven, the bound2bound weights are multiplied by (1 + 10 * crit^2)
 */

#ifdef WITH_HEAP

#include "placer_heap.h"
#include <Eigen/Core>
#include <Eigen/IterativeLinearSolvers>
#include <boost/optional.hpp>
#include <chrono>
#include <deque>
#include <fstream>
#include <numeric>
#include <queue>
#include <tuple>
#include <unordered_map>
#include "log.h"
#include "nextpnr.h"
#include "place_common.h"
#include "placer1.h"
#include "timing.h"
#include "util.h"
NEXTPNR_NAMESPACE_BEGIN

namespace {
// A simple internal representation for a sparse system of equations Ax = rhs
// This is designed to decouple the functions that build the matrix to the engine that
// solves it, and the representation that requires
template <typename T> struct EquationSystem
{

    EquationSystem(size_t rows, size_t cols)
    {
        A.resize(cols);
        rhs.resize(rows);
    }

    // Simple sparse format, easy to convert to CCS for solver
    std::vector<std::vector<std::pair<int, T>>> A; // col -> (row, x[row, col]) sorted by row
    std::vector<T> rhs;                            // RHS vector
    void reset()
    {
        for (auto &col : A)
            col.clear();
        std::fill(rhs.begin(), rhs.end(), T());
    }

    void add_coeff(int row, int col, T val)
    {
        auto &Ac = A.at(col);
        // Binary search
        int b = 0, e = int(Ac.size()) - 1;
        while (b <= e) {
            int i = (b + e) / 2;
            if (Ac.at(i).first == row) {
                Ac.at(i).second += val;
                return;
            }
            if (Ac.at(i).first > row)
                e = i - 1;
            else
                b = i + 1;
        }
        Ac.insert(Ac.begin() + b, std::make_pair(row, val));
    }

    void add_rhs(int row, T val) { rhs[row] += val; }

    void solve(std::vector<T> &x, float tolerance)
    {
        using namespace Eigen;
        if (x.empty())
            return;
        NPNR_ASSERT(x.size() == A.size());

        VectorXd vx(x.size()), vb(rhs.size());
        SparseMatrix<T> mat(A.size(), A.size());

        std::vector<int> colnnz;
        for (auto &Ac : A)
            colnnz.push_back(int(Ac.size()));
        mat.reserve(colnnz);
        for (int col = 0; col < int(A.size()); col++) {
            auto &Ac = A.at(col);
            for (auto &el : Ac)
                mat.insert(el.first, col) = el.second;
        }

        for (int i = 0; i < int(x.size()); i++)
            vx[i] = x.at(i);
        for (int i = 0; i < int(rhs.size()); i++)
            vb[i] = rhs.at(i);

        ConjugateGradient<SparseMatrix<T>, Lower | Upper> solver;
        solver.setTolerance(tolerance);
        VectorXd xr = solver.compute(mat).solveWithGuess(vb, vx);
        for (int i = 0; i < int(x.size()); i++)
            x.at(i) = xr[i];
        // for (int i = 0; i < int(x.size()); i++)
        //    log_info("x[%d] = %f\n", i, x.at(i));
    }
};

} // namespace

class HeAPPlacer
{
  public:
    HeAPPlacer(Context *ctx, PlacerHeapCfg cfg) : ctx(ctx), cfg(cfg) { Eigen::initParallel(); }

    bool place()
    {
        auto startt = std::chrono::high_resolution_clock::now();

        ctx->lock();
        place_constraints();
        build_fast_bels();
        seed_placement();
        update_all_chains();
        wirelen_t hpwl = total_hpwl();
        log_info("Creating initial analytic placement for %d cells, random placement wirelen = %d.\n",
                 int(place_cells.size()), int(hpwl));
        for (int i = 0; i < 4; i++) {
            setup_solve_cells();
            auto solve_startt = std::chrono::high_resolution_clock::now();
#ifdef NPNR_DISABLE_THREADS
            build_solve_direction(false, -1);
            build_solve_direction(true, -1);
#else
            boost::thread xaxis([&]() { build_solve_direction(false, -1); });
            build_solve_direction(true, -1);
            xaxis.join();
#endif
            auto solve_endt = std::chrono::high_resolution_clock::now();
            solve_time += std::chrono::duration<double>(solve_endt - solve_startt).count();

            update_all_chains();

            hpwl = total_hpwl();
            log_info("    at initial placer iter %d, wirelen = %d\n", i, int(hpwl));
        }

        wirelen_t solved_hpwl = 0, spread_hpwl = 0, legal_hpwl = 0, best_hpwl = std::numeric_limits<wirelen_t>::max();
        int iter = 0, stalled = 0;

        std::vector<std::tuple<CellInfo *, BelId, PlaceStrength>> solution;

        std::vector<std::unordered_set<IdString>> heap_runs;
        std::unordered_set<IdString> all_celltypes;
        std::unordered_map<IdString, int> ct_count;

        for (auto cell : place_cells) {
            if (!all_celltypes.count(cell->type)) {
                heap_runs.push_back(std::unordered_set<IdString>{cell->type});
                all_celltypes.insert(cell->type);
            }
            ct_count[cell->type]++;
        }
        // If more than 98% of cells are one cell type, always solve all at once
        // Otherwise, follow full HeAP strategy of rotate&all
        for (auto &c : ct_count)
            if (c.second >= 0.98 * int(place_cells.size())) {
                heap_runs.clear();
                break;
            }

        if (cfg.placeAllAtOnce) {
            // Never want to deal with LUTs, FFs, MUXFxs seperately,
            // for now disable all single-cell-type runs and only have heteregenous
            // runs
            heap_runs.clear();
        }

        heap_runs.push_back(all_celltypes);
        // The main HeAP placer loop
        log_info("Running main analytical placer.\n");
        while (stalled < 5 && (solved_hpwl <= legal_hpwl * 0.8)) {
            // Alternate between particular Bel types and all bels
            for (auto &run : heap_runs) {
                auto run_startt = std::chrono::high_resolution_clock::now();

                setup_solve_cells(&run);
                if (solve_cells.empty())
                    continue;
                // Heuristic: don't bother with threading below a certain size
                auto solve_startt = std::chrono::high_resolution_clock::now();

#ifndef NPNR_DISABLE_THREADS
                if (solve_cells.size() >= 500) {
                    boost::thread xaxis([&]() { build_solve_direction(false, (iter == 0) ? -1 : iter); });
                    build_solve_direction(true, (iter == 0) ? -1 : iter);
                    xaxis.join();
                } else
#endif
                {
                    build_solve_direction(false, (iter == 0) ? -1 : iter);
                    build_solve_direction(true, (iter == 0) ? -1 : iter);
                }
                auto solve_endt = std::chrono::high_resolution_clock::now();
                solve_time += std::chrono::duration<double>(solve_endt - solve_startt).count();
                update_all_chains();
                solved_hpwl = total_hpwl();

                update_all_chains();

                for (const auto &group : cfg.cellGroups)
                    CutSpreader(this, group).run();

                for (auto type : sorted(run))
                    if (std::all_of(cfg.cellGroups.begin(), cfg.cellGroups.end(),
                                    [type](const std::unordered_set<IdString> &grp) { return !grp.count(type); }))
                        CutSpreader(this, {type}).run();

                update_all_chains();
                spread_hpwl = total_hpwl();
                legalise_placement_strict(true);
                update_all_chains();

                legal_hpwl = total_hpwl();
                auto run_stopt = std::chrono::high_resolution_clock::now();
                log_info("    at iteration #%d, type %s: wirelen solved = %d, spread = %d, legal = %d; time = %.02fs\n",
                         iter + 1, (run.size() > 1 ? "ALL" : run.begin()->c_str(ctx)), int(solved_hpwl),
                         int(spread_hpwl), int(legal_hpwl),
                         std::chrono::duration<double>(run_stopt - run_startt).count());
            }

            if (cfg.timing_driven)
                get_criticalities(ctx, &net_crit);

            if (legal_hpwl < best_hpwl) {
                best_hpwl = legal_hpwl;
                stalled = 0;
                // Save solution
                solution.clear();
                for (auto cell : sorted(ctx->cells)) {
                    solution.emplace_back(cell.second, cell.second->bel, cell.second->belStrength);
                }
            } else {
                ++stalled;
            }
            for (auto &cl : cell_locs) {
                cl.second.legal_x = cl.second.x;
                cl.second.legal_y = cl.second.y;
            }
            ctx->yield();
            ++iter;
        }

        // Apply saved solution
        for (auto &sc : solution) {
            CellInfo *cell = std::get<0>(sc);
            if (cell->bel != BelId())
                ctx->unbindBel(cell->bel);
        }
        for (auto &sc : solution) {
            CellInfo *cell;
            BelId bel;
            PlaceStrength strength;
            std::tie(cell, bel, strength) = sc;
            ctx->bindBel(bel, cell, strength);
        }

        for (auto cell : sorted(ctx->cells)) {
            if (cell.second->bel == BelId())
                log_error("Found unbound cell %s\n", cell.first.c_str(ctx));
            if (ctx->getBoundBelCell(cell.second->bel) != cell.second)
                log_error("Found cell %s with mismatched binding\n", cell.first.c_str(ctx));
            if (ctx->debug)
                log_info("AP soln: %s -> %s\n", cell.first.c_str(ctx), ctx->getBelName(cell.second->bel).c_str(ctx));
        }

        ctx->unlock();
        auto endtt = std::chrono::high_resolution_clock::now();
        log_info("HeAP Placer Time: %.02fs\n", std::chrono::duration<double>(endtt - startt).count());
        log_info("  of which solving equations: %.02fs\n", solve_time);
        log_info("  of which spreading cells: %.02fs\n", cl_time);
        log_info("  of which strict legalisation: %.02fs\n", sl_time);

        ctx->check();

        placer1_refine(ctx, Placer1Cfg(ctx));

        return true;
    }

  private:
    Context *ctx;
    PlacerHeapCfg cfg;

    int max_x = 0, max_y = 0;
    std::vector<std::vector<std::vector<std::vector<BelId>>>> fast_bels;
    std::unordered_map<IdString, std::tuple<int, int>> bel_types;

    // For fast handling of heterogeneosity during initial placement without full legalisation,
    // for each Bel type this goes from x or y to the nearest x or y where a Bel of a given type exists
    // This is particularly important for the iCE40 architecture, where multipliers and BRAM only exist at the
    // edges and corners respectively
    std::vector<std::vector<int>> nearest_row_with_bel;
    std::vector<std::vector<int>> nearest_col_with_bel;

    struct BoundingBox
    {
        // Actual bounding box
        int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    };

    std::unordered_map<IdString, BoundingBox> constraint_region_bounds;

    // In some cases, we can't use bindBel because we allow overlap in the earlier stages. So we use this custom
    // structure instead
    struct CellLocation
    {
        int x, y;
        int legal_x, legal_y;
        double rawx, rawy;
        bool locked, global;
    };
    std::unordered_map<IdString, CellLocation> cell_locs;
    // The set of cells that we will actually place. This excludes locked cells and children cells of macros/chains
    // (only the root of each macro is placed.)
    std::vector<CellInfo *> place_cells;

    // The cells in the current equation being solved (a subset of place_cells in some cases, where we only place
    // cells of a certain type)
    std::vector<CellInfo *> solve_cells;

    // For cells in a chain, this is the ultimate root cell of the chain (sometimes this is not constr_parent
    // where chains are within chains
    std::unordered_map<IdString, CellInfo *> chain_root;
    std::unordered_map<IdString, int> chain_size;

    // The offset from chain_root to a cell in the chain
    std::unordered_map<IdString, std::pair<int, int>> cell_offsets;

    // Performance counting
    double solve_time = 0, cl_time = 0, sl_time = 0;

    NetCriticalityMap net_crit;

    // Place cells with the BEL attribute set to constrain them
    void place_constraints()
    {
        size_t placed_cells = 0;
        // Initial constraints placer
        for (auto &cell_entry : ctx->cells) {
            CellInfo *cell = cell_entry.second.get();
            auto loc = cell->attrs.find(ctx->id("BEL"));
            if (loc != cell->attrs.end()) {
                std::string loc_name = loc->second.as_string();
                BelId bel = ctx->getBelByName(ctx->id(loc_name));
                if (bel == BelId()) {
                    log_error("No Bel named \'%s\' located for "
                              "this chip (processing BEL attribute on \'%s\')\n",
                              loc_name.c_str(), cell->name.c_str(ctx));
                }

                IdString bel_type = ctx->getBelType(bel);
                if (bel_type != cell->type) {
                    log_error("Bel \'%s\' of type \'%s\' does not match cell "
                              "\'%s\' of type \'%s\'\n",
                              loc_name.c_str(), bel_type.c_str(ctx), cell->name.c_str(ctx), cell->type.c_str(ctx));
                }
                if (!ctx->isValidBelForCell(cell, bel)) {
                    log_error("Bel \'%s\' of type \'%s\' is not valid for cell "
                              "\'%s\' of type \'%s\'\n",
                              loc_name.c_str(), bel_type.c_str(ctx), cell->name.c_str(ctx), cell->type.c_str(ctx));
                }

                auto bound_cell = ctx->getBoundBelCell(bel);
                if (bound_cell) {
                    log_error("Cell \'%s\' cannot be bound to bel \'%s\' since it is already bound to cell \'%s\'\n",
                              cell->name.c_str(ctx), loc_name.c_str(), bound_cell->name.c_str(ctx));
                }

                ctx->bindBel(bel, cell, STRENGTH_USER);
                placed_cells++;
            }
        }
        log_info("Placed %d cells based on constraints.\n", int(placed_cells));
        ctx->yield();
    }

    // Construct the fast_bels, nearest_row_with_bel and nearest_col_with_bel
    void build_fast_bels()
    {

        int num_bel_types = 0;
        for (auto bel : ctx->getBels()) {
            IdString type = ctx->getBelType(bel);
            if (bel_types.find(type) == bel_types.end()) {
                bel_types[type] = std::tuple<int, int>(num_bel_types++, 1);
            } else {
                std::get<1>(bel_types.at(type))++;
            }
        }
        for (auto bel : ctx->getBels()) {
            if (!ctx->checkBelAvail(bel))
                continue;
            Loc loc = ctx->getBelLocation(bel);
            IdString type = ctx->getBelType(bel);
            int type_idx = std::get<0>(bel_types.at(type));
            if (int(fast_bels.size()) < type_idx + 1)
                fast_bels.resize(type_idx + 1);
            if (int(fast_bels.at(type_idx).size()) < (loc.x + 1))
                fast_bels.at(type_idx).resize(loc.x + 1);
            if (int(fast_bels.at(type_idx).at(loc.x).size()) < (loc.y + 1))
                fast_bels.at(type_idx).at(loc.x).resize(loc.y + 1);
            max_x = std::max(max_x, loc.x);
            max_y = std::max(max_y, loc.y);
            fast_bels.at(type_idx).at(loc.x).at(loc.y).push_back(bel);
        }

        nearest_row_with_bel.resize(num_bel_types, std::vector<int>(max_y + 1, -1));
        nearest_col_with_bel.resize(num_bel_types, std::vector<int>(max_x + 1, -1));
        for (auto bel : ctx->getBels()) {
            if (!ctx->checkBelAvail(bel))
                continue;
            Loc loc = ctx->getBelLocation(bel);
            int type_idx = std::get<0>(bel_types.at(ctx->getBelType(bel)));
            auto &nr = nearest_row_with_bel.at(type_idx), &nc = nearest_col_with_bel.at(type_idx);
            // Traverse outwards through nearest_row_with_bel and nearest_col_with_bel, stopping once
            // another row/col is already recorded as being nearer
            for (int x = loc.x; x <= max_x; x++) {
                if (nc.at(x) != -1 && std::abs(loc.x - nc.at(x)) <= (x - loc.x))
                    break;
                nc.at(x) = loc.x;
            }
            for (int x = loc.x - 1; x >= 0; x--) {
                if (nc.at(x) != -1 && std::abs(loc.x - nc.at(x)) <= (loc.x - x))
                    break;
                nc.at(x) = loc.x;
            }
            for (int y = loc.y; y <= max_y; y++) {
                if (nr.at(y) != -1 && std::abs(loc.y - nr.at(y)) <= (y - loc.y))
                    break;
                nr.at(y) = loc.y;
            }
            for (int y = loc.y - 1; y >= 0; y--) {
                if (nr.at(y) != -1 && std::abs(loc.y - nr.at(y)) <= (loc.y - y))
                    break;
                nr.at(y) = loc.y;
            }
        }

        // Determine bounding boxes of region constraints
        for (auto &region : sorted(ctx->region)) {
            Region *r = region.second;
            BoundingBox bb;
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
                bb.x1 = max_x;
                bb.y1 = max_y;
            }
            constraint_region_bounds[r->name] = bb;
        }
    }

    // Build and solve in one direction
    void build_solve_direction(bool yaxis, int iter)
    {
        for (int i = 0; i < 5; i++) {
            EquationSystem<double> esx(solve_cells.size(), solve_cells.size());
            build_equations(esx, yaxis, iter);
            solve_equations(esx, yaxis);
        }
    }

    // Check if a cell has any meaningful connectivity
    bool has_connectivity(CellInfo *cell)
    {
        for (auto port : cell->ports) {
            if (port.second.net != nullptr && port.second.net->driver.cell != nullptr &&
                !port.second.net->users.empty())
                return true;
        }
        return false;
    }

    // Build up a random initial placement, without regard to legality
    // FIXME: Are there better approaches to the initial placement (e.g. greedy?)
    void seed_placement()
    {
        std::unordered_map<IdString, std::deque<BelId>> available_bels;
        for (auto bel : ctx->getBels()) {
            if (!ctx->checkBelAvail(bel))
                continue;
            available_bels[ctx->getBelType(bel)].push_back(bel);
        }
        for (auto &t : available_bels) {
            std::random_shuffle(t.second.begin(), t.second.end(), [&](size_t n) { return ctx->rng(int(n)); });
        }
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->bel != BelId()) {
                Loc loc = ctx->getBelLocation(ci->bel);
                cell_locs[cell.first].x = loc.x;
                cell_locs[cell.first].y = loc.y;
                cell_locs[cell.first].locked = true;
                cell_locs[cell.first].global = ctx->getBelGlobalBuf(ci->bel);
            } else if (ci->constr_parent == nullptr) {
                bool placed = false;
                while (!placed) {
                    if (!available_bels.count(ci->type) || available_bels.at(ci->type).empty())
                        log_error("Unable to place cell '%s', no Bels remaining of type '%s'\n", ci->name.c_str(ctx),
                                  ci->type.c_str(ctx));
                    BelId bel = available_bels.at(ci->type).back();
                    available_bels.at(ci->type).pop_back();
                    Loc loc = ctx->getBelLocation(bel);
                    cell_locs[cell.first].x = loc.x;
                    cell_locs[cell.first].y = loc.y;
                    cell_locs[cell.first].locked = false;
                    cell_locs[cell.first].global = ctx->getBelGlobalBuf(bel);
                    // FIXME
                    if (has_connectivity(cell.second) && !cfg.ioBufTypes.count(ci->type)) {
                        place_cells.push_back(ci);
                        placed = true;
                    } else {
                        if (ctx->isValidBelForCell(ci, bel)) {
                            ctx->bindBel(bel, ci, STRENGTH_STRONG);
                            cell_locs[cell.first].locked = true;
                            placed = true;
                        } else {
                            available_bels.at(ci->type).push_front(bel);
                        }
                    }
                }
            }
        }
    }

    // Setup the cells to be solved, returns the number of rows
    int setup_solve_cells(std::unordered_set<IdString> *celltypes = nullptr)
    {
        int row = 0;
        solve_cells.clear();
        // First clear the udata of all cells
        for (auto cell : sorted(ctx->cells))
            cell.second->udata = dont_solve;
        // Then update cells to be placed, which excludes cell children
        for (auto cell : place_cells) {
            if (celltypes && !celltypes->count(cell->type))
                continue;
            cell->udata = row++;
            solve_cells.push_back(cell);
        }
        // Finally, update the udata of children
        for (auto chained : chain_root)
            ctx->cells.at(chained.first)->udata = chained.second->udata;
        return row;
    }

    // Update the location of all children of a chain
    void update_chain(CellInfo *cell, CellInfo *root)
    {
        const auto &base = cell_locs[cell->name];
        for (auto child : cell->constr_children) {
            // FIXME: Improve handling of heterogeneous chains
            if (child->type == root->type)
                chain_size[root->name]++;
            if (child->constr_x != child->UNCONSTR)
                cell_locs[child->name].x = std::max(0, std::min(max_x, base.x + child->constr_x));
            else
                cell_locs[child->name].x = base.x; // better handling of UNCONSTR?
            if (child->constr_y != child->UNCONSTR)
                cell_locs[child->name].y = std::max(0, std::min(max_y, base.y + child->constr_y));
            else
                cell_locs[child->name].y = base.y; // better handling of UNCONSTR?
            chain_root[child->name] = root;
            if (!child->constr_children.empty())
                update_chain(child, root);
        }
    }

    // Update all chains
    void update_all_chains()
    {
        for (auto cell : place_cells) {
            chain_size[cell->name] = 1;
            if (!cell->constr_children.empty())
                update_chain(cell, cell);
        }
    }

    // Run a function on all ports of a net - including the driver and all users
    template <typename Tf> void foreach_port(NetInfo *net, Tf func)
    {
        if (net->driver.cell != nullptr)
            func(net->driver, -1);
        for (size_t i = 0; i < net->users.size(); i++)
            func(net->users.at(i), i);
    }

    // Build the system of equations for either X or Y
    void build_equations(EquationSystem<double> &es, bool yaxis, int iter = -1)
    {
        // Return the x or y position of a cell, depending on ydir
        auto cell_pos = [&](CellInfo *cell) { return yaxis ? cell_locs.at(cell->name).y : cell_locs.at(cell->name).x; };
        auto legal_pos = [&](CellInfo *cell) {
            return yaxis ? cell_locs.at(cell->name).legal_y : cell_locs.at(cell->name).legal_x;
        };

        es.reset();

        for (auto net : sorted(ctx->nets)) {
            NetInfo *ni = net.second;
            if (ni->driver.cell == nullptr)
                continue;
            if (ni->users.empty())
                continue;
            if (cell_locs.at(ni->driver.cell->name).global)
                continue;
            // Find the bounds of the net in this axis, and the ports that correspond to these bounds
            PortRef *lbport = nullptr, *ubport = nullptr;
            int lbpos = std::numeric_limits<int>::max(), ubpos = std::numeric_limits<int>::min();
            foreach_port(ni, [&](PortRef &port, int user_idx) {
                int pos = cell_pos(port.cell);
                if (pos < lbpos) {
                    lbpos = pos;
                    lbport = &port;
                }
                if (pos > ubpos) {
                    ubpos = pos;
                    ubport = &port;
                }
            });
            NPNR_ASSERT(lbport != nullptr);
            NPNR_ASSERT(ubport != nullptr);

            auto stamp_equation = [&](PortRef &var, PortRef &eqn, double weight) {
                if (eqn.cell->udata == dont_solve)
                    return;
                int row = eqn.cell->udata;
                int v_pos = cell_pos(var.cell);
                if (var.cell->udata != dont_solve) {
                    es.add_coeff(row, var.cell->udata, weight);
                } else {
                    es.add_rhs(row, -v_pos * weight);
                }
                if (cell_offsets.count(var.cell->name)) {
                    es.add_rhs(row, -(yaxis ? cell_offsets.at(var.cell->name).second
                                            : cell_offsets.at(var.cell->name).first) *
                                            weight);
                }
            };

            // Add all relevant connections to the matrix
            foreach_port(ni, [&](PortRef &port, int user_idx) {
                int this_pos = cell_pos(port.cell);
                auto process_arc = [&](PortRef *other) {
                    if (other == &port)
                        return;
                    int o_pos = cell_pos(other->cell);
                    double weight = 1.0 / (ni->users.size() *
                                           std::max<double>(1, (yaxis ? cfg.hpwl_scale_y : cfg.hpwl_scale_x) *
                                                                       std::abs(o_pos - this_pos)));

                    if (user_idx != -1 && net_crit.count(ni->name)) {
                        auto &nc = net_crit.at(ni->name);
                        if (user_idx < int(nc.criticality.size()))
                            weight *= (1.0 + cfg.timingWeight *
                                                     std::pow(nc.criticality.at(user_idx), cfg.criticalityExponent));
                    }

                    // If cell 0 is not fixed, it will stamp +w on its equation and -w on the other end's equation,
                    // if the other end isn't fixed
                    stamp_equation(port, port, weight);
                    stamp_equation(port, *other, -weight);
                    stamp_equation(*other, *other, weight);
                    stamp_equation(*other, port, -weight);
                };
                process_arc(lbport);
                process_arc(ubport);
            });
        }
        if (iter != -1) {
            float alpha = cfg.alpha;
            for (size_t row = 0; row < solve_cells.size(); row++) {
                int l_pos = legal_pos(solve_cells.at(row));
                int c_pos = cell_pos(solve_cells.at(row));

                double weight =
                        alpha * iter /
                        std::max<double>(1, (yaxis ? cfg.hpwl_scale_y : cfg.hpwl_scale_x) * std::abs(l_pos - c_pos));
                // Add an arc from legalised to current position
                es.add_coeff(row, row, weight);
                es.add_rhs(row, weight * l_pos);
            }
        }
    }

    // Build the system of equations for either X or Y
    void solve_equations(EquationSystem<double> &es, bool yaxis)
    {
        // Return the x or y position of a cell, depending on ydir
        auto cell_pos = [&](CellInfo *cell) { return yaxis ? cell_locs.at(cell->name).y : cell_locs.at(cell->name).x; };
        std::vector<double> vals;
        std::transform(solve_cells.begin(), solve_cells.end(), std::back_inserter(vals), cell_pos);
        es.solve(vals, cfg.solverTolerance);
        for (size_t i = 0; i < vals.size(); i++)
            if (yaxis) {
                cell_locs.at(solve_cells.at(i)->name).rawy = vals.at(i);
                cell_locs.at(solve_cells.at(i)->name).y = std::min(max_y, std::max(0, int(vals.at(i))));
                if (solve_cells.at(i)->region != nullptr)
                    cell_locs.at(solve_cells.at(i)->name).y =
                            limit_to_reg(solve_cells.at(i)->region, cell_locs.at(solve_cells.at(i)->name).y, true);
            } else {
                cell_locs.at(solve_cells.at(i)->name).rawx = vals.at(i);
                cell_locs.at(solve_cells.at(i)->name).x = std::min(max_x, std::max(0, int(vals.at(i))));
                if (solve_cells.at(i)->region != nullptr)
                    cell_locs.at(solve_cells.at(i)->name).x =
                            limit_to_reg(solve_cells.at(i)->region, cell_locs.at(solve_cells.at(i)->name).x, false);
            }
    }

    // Compute HPWL
    wirelen_t total_hpwl()
    {
        wirelen_t hpwl = 0;
        for (auto net : sorted(ctx->nets)) {
            NetInfo *ni = net.second;
            if (ni->driver.cell == nullptr)
                continue;
            CellLocation &drvloc = cell_locs.at(ni->driver.cell->name);
            if (drvloc.global)
                continue;
            int xmin = drvloc.x, xmax = drvloc.x, ymin = drvloc.y, ymax = drvloc.y;
            for (auto &user : ni->users) {
                CellLocation &usrloc = cell_locs.at(user.cell->name);
                xmin = std::min(xmin, usrloc.x);
                xmax = std::max(xmax, usrloc.x);
                ymin = std::min(ymin, usrloc.y);
                ymax = std::max(ymax, usrloc.y);
            }
            hpwl += cfg.hpwl_scale_x * (xmax - xmin) + cfg.hpwl_scale_y * (ymax - ymin);
        }
        return hpwl;
    }

    // Strict placement legalisation, performed after the initial HeAP spreading
    void legalise_placement_strict(bool require_validity = false)
    {
        auto startt = std::chrono::high_resolution_clock::now();

        // Unbind all cells placed in this solution
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->bel != BelId() && (ci->udata != dont_solve ||
                                       (chain_root.count(ci->name) && chain_root.at(ci->name)->udata != dont_solve)))
                ctx->unbindBel(ci->bel);
        }

        // At the moment we don't follow the full HeAP algorithm using cuts for legalisation, instead using
        // the simple greedy largest-macro-first approach.
        std::priority_queue<std::pair<int, IdString>> remaining;
        for (auto cell : solve_cells) {
            remaining.emplace(chain_size[cell->name], cell->name);
        }
        int ripup_radius = 2;
        int total_iters = 0;
        int total_iters_noreset = 0;
        while (!remaining.empty()) {
            auto top = remaining.top();
            remaining.pop();

            CellInfo *ci = ctx->cells.at(top.second).get();
            // Was now placed, ignore
            if (ci->bel != BelId())
                continue;
            // log_info("   Legalising %s (%s)\n", top.second.c_str(ctx), ci->type.c_str(ctx));
            int bt = std::get<0>(bel_types.at(ci->type));
            auto &fb = fast_bels.at(bt);
            int radius = 0;
            int iter = 0;
            int iter_at_radius = 0;
            bool placed = false;
            BelId bestBel;
            int best_inp_len = std::numeric_limits<int>::max();

            total_iters++;
            total_iters_noreset++;
            if (total_iters > int(solve_cells.size())) {
                total_iters = 0;
                ripup_radius = std::max(std::max(max_x, max_y), ripup_radius * 2);
            }

            if (total_iters_noreset > std::max(5000, 8 * int(ctx->cells.size()))) {
                log_error("Unable to find legal placement for all cells, design is probably at utilisation limit.\n");
            }

            while (!placed) {

                // Set a conservative timeout
                if (iter > std::max(10000, 3 * int(ctx->cells.size())))
                    log_error("Unable to find legal placement for cell '%s', check constraints and utilisation.\n",
                              ctx->nameOf(ci));

                int rx = radius, ry = radius;

                if (ci->region != nullptr) {
                    rx = std::min(radius, (constraint_region_bounds[ci->region->name].x1 -
                                           constraint_region_bounds[ci->region->name].x0) /
                                                          2 +
                                                  1);
                    ry = std::min(radius, (constraint_region_bounds[ci->region->name].y1 -
                                           constraint_region_bounds[ci->region->name].y0) /
                                                          2 +
                                                  1);
                }

                int nx = ctx->rng(2 * rx + 1) + std::max(cell_locs.at(ci->name).x - rx, 0);
                int ny = ctx->rng(2 * ry + 1) + std::max(cell_locs.at(ci->name).y - ry, 0);

                iter++;
                iter_at_radius++;
                if (iter >= (10 * (radius + 1))) {
                    radius = std::min(std::max(max_x, max_y), radius + 1);
                    while (radius < std::max(max_x, max_y)) {
                        for (int x = std::max(0, cell_locs.at(ci->name).x - radius);
                             x <= std::min(max_x, cell_locs.at(ci->name).x + radius); x++) {
                            if (x >= int(fb.size()))
                                break;
                            for (int y = std::max(0, cell_locs.at(ci->name).y - radius);
                                 y <= std::min(max_y, cell_locs.at(ci->name).y + radius); y++) {
                                if (y >= int(fb.at(x).size()))
                                    break;
                                if (fb.at(x).at(y).size() > 0)
                                    goto notempty;
                            }
                        }
                        radius = std::min(std::max(max_x, max_y), radius + 1);
                    }
                notempty:
                    iter_at_radius = 0;
                    iter = 0;
                }
                if (nx < 0 || nx > max_x)
                    continue;
                if (ny < 0 || ny > max_y)
                    continue;

                // ny = nearest_row_with_bel.at(bt).at(ny);
                // nx = nearest_col_with_bel.at(bt).at(nx);

                if (nx >= int(fb.size()))
                    continue;
                if (ny >= int(fb.at(nx).size()))
                    continue;
                if (fb.at(nx).at(ny).empty())
                    continue;

                int need_to_explore = 2 * radius;

                if (iter_at_radius >= need_to_explore && bestBel != BelId()) {
                    CellInfo *bound = ctx->getBoundBelCell(bestBel);
                    if (bound != nullptr) {
                        ctx->unbindBel(bound->bel);
                        remaining.emplace(chain_size[bound->name], bound->name);
                    }
                    ctx->bindBel(bestBel, ci, STRENGTH_WEAK);
                    placed = true;
                    Loc loc = ctx->getBelLocation(bestBel);
                    cell_locs[ci->name].x = loc.x;
                    cell_locs[ci->name].y = loc.y;
                    break;
                }

                if (ci->constr_children.empty() && !ci->constr_abs_z) {
                    for (auto sz : fb.at(nx).at(ny)) {
                        if (ci->region != nullptr && ci->region->constr_bels && !ci->region->bels.count(sz))
                            continue;
                        if (ctx->checkBelAvail(sz) || (radius > ripup_radius || ctx->rng(20000) < 10)) {
                            CellInfo *bound = ctx->getBoundBelCell(sz);
                            if (bound != nullptr) {
                                if (bound->constr_parent != nullptr || !bound->constr_children.empty() ||
                                    bound->constr_abs_z)
                                    continue;
                                ctx->unbindBel(bound->bel);
                            }
                            ctx->bindBel(sz, ci, STRENGTH_WEAK);
                            if (require_validity && !ctx->isBelLocationValid(sz)) {
                                ctx->unbindBel(sz);
                                if (bound != nullptr)
                                    ctx->bindBel(sz, bound, STRENGTH_WEAK);
                            } else if (iter_at_radius < need_to_explore) {
                                ctx->unbindBel(sz);
                                if (bound != nullptr)
                                    ctx->bindBel(sz, bound, STRENGTH_WEAK);
                                int input_len = 0;
                                for (auto &port : ci->ports) {
                                    auto &p = port.second;
                                    if (p.type != PORT_IN || p.net == nullptr || p.net->driver.cell == nullptr)
                                        continue;
                                    CellInfo *drv = p.net->driver.cell;
                                    auto drv_loc = cell_locs.find(drv->name);
                                    if (drv_loc == cell_locs.end())
                                        continue;
                                    if (drv_loc->second.global)
                                        continue;
                                    input_len += std::abs(drv_loc->second.x - nx) + std::abs(drv_loc->second.y - ny);
                                }
                                if (input_len < best_inp_len) {
                                    best_inp_len = input_len;
                                    bestBel = sz;
                                }
                                break;
                            } else {
                                if (bound != nullptr)
                                    remaining.emplace(chain_size[bound->name], bound->name);
                                Loc loc = ctx->getBelLocation(sz);
                                cell_locs[ci->name].x = loc.x;
                                cell_locs[ci->name].y = loc.y;
                                placed = true;
                                break;
                            }
                        }
                    }
                } else {
                    for (auto sz : fb.at(nx).at(ny)) {
                        Loc loc = ctx->getBelLocation(sz);
                        if (ci->constr_abs_z && loc.z != ci->constr_z)
                            continue;
                        std::vector<std::pair<CellInfo *, BelId>> targets;
                        std::vector<std::pair<BelId, CellInfo *>> swaps_made;
                        std::queue<std::pair<CellInfo *, Loc>> visit;
                        visit.emplace(ci, loc);
                        while (!visit.empty()) {
                            CellInfo *vc = visit.front().first;
                            NPNR_ASSERT(vc->bel == BelId());
                            Loc ploc = visit.front().second;
                            visit.pop();
                            BelId target = ctx->getBelByLocation(ploc);
                            if (vc->region != nullptr && vc->region->constr_bels && !vc->region->bels.count(target))
                                goto fail;
                            CellInfo *bound;
                            if (target == BelId() || ctx->getBelType(target) != vc->type)
                                goto fail;
                            bound = ctx->getBoundBelCell(target);
                            // Chains cannot overlap
                            if (bound != nullptr)
                                if (bound->constr_z != bound->UNCONSTR || bound->constr_parent != nullptr ||
                                    !bound->constr_children.empty() || bound->belStrength > STRENGTH_WEAK)
                                    goto fail;
                            targets.emplace_back(vc, target);
                            for (auto child : vc->constr_children) {
                                Loc cloc = ploc;
                                if (child->constr_x != child->UNCONSTR)
                                    cloc.x += child->constr_x;
                                if (child->constr_y != child->UNCONSTR)
                                    cloc.y += child->constr_y;
                                if (child->constr_z != child->UNCONSTR)
                                    cloc.z = child->constr_abs_z ? child->constr_z : (ploc.z + child->constr_z);
                                visit.emplace(child, cloc);
                            }
                        }

                        for (auto &target : targets) {
                            CellInfo *bound = ctx->getBoundBelCell(target.second);
                            if (bound != nullptr)
                                ctx->unbindBel(target.second);
                            ctx->bindBel(target.second, target.first, STRENGTH_STRONG);
                            swaps_made.emplace_back(target.second, bound);
                        }

                        for (auto &sm : swaps_made) {
                            if (!ctx->isBelLocationValid(sm.first))
                                goto fail;
                        }

                        if (false) {
                        fail:
                            for (auto &swap : swaps_made) {
                                ctx->unbindBel(swap.first);
                                if (swap.second != nullptr)
                                    ctx->bindBel(swap.first, swap.second, STRENGTH_WEAK);
                            }
                            continue;
                        }
                        for (auto &target : targets) {
                            Loc loc = ctx->getBelLocation(target.second);
                            cell_locs[target.first->name].x = loc.x;
                            cell_locs[target.first->name].y = loc.y;
                            // log_info("%s %d %d %d\n", target.first->name.c_str(ctx), loc.x, loc.y, loc.z);
                        }
                        for (auto &swap : swaps_made) {
                            if (swap.second != nullptr)
                                remaining.emplace(chain_size[swap.second->name], swap.second->name);
                        }

                        placed = true;
                        break;
                    }
                }
            }
        }
        auto endt = std::chrono::high_resolution_clock::now();
        sl_time += std::chrono::duration<float>(endt - startt).count();
    }
    // Implementation of the cut-based spreading as described in the HeAP/SimPL papers

    template <typename T> T limit_to_reg(Region *reg, T val, bool dir)
    {
        if (reg == nullptr)
            return val;
        int limit_low = dir ? constraint_region_bounds[reg->name].y0 : constraint_region_bounds[reg->name].x0;
        int limit_high = dir ? constraint_region_bounds[reg->name].y1 : constraint_region_bounds[reg->name].x1;
        return std::max<T>(std::min<T>(val, limit_high), limit_low);
    }

    struct ChainExtent
    {
        int x0, y0, x1, y1;
    };

    struct SpreaderRegion
    {
        int id;
        int x0, y0, x1, y1;
        std::vector<int> cells, bels;
        bool overused(float beta) const
        {
            for (size_t t = 0; t < cells.size(); t++) {
                if (bels.at(t) < 4) {
                    if (cells.at(t) > bels.at(t))
                        return true;
                } else {
                    if (cells.at(t) > beta * bels.at(t))
                        return true;
                }
            }
            return false;
        }
    };

    class CutSpreader
    {
      public:
        CutSpreader(HeAPPlacer *p, const std::unordered_set<IdString> &beltype) : p(p), ctx(p->ctx), beltype(beltype)
        {
            int idx = 0;
            for (IdString type : sorted(beltype)) {
                type_index[type] = idx;
                fb.emplace_back(&(p->fast_bels.at(std::get<0>(p->bel_types.at(type)))));
                ++idx;
            }
        }
        static int seq;
        void run()
        {
            auto startt = std::chrono::high_resolution_clock::now();
            init();
            find_overused_regions();
            for (auto &r : regions) {
                if (merged_regions.count(r.id))
                    continue;
#if 0
                log_info("%s (%d, %d) |_> (%d, %d) %d/%d\n", beltype.c_str(ctx), r.x0, r.y0, r.x1, r.y1, r.cells,
                         r.bels);
#endif
            }
            expand_regions();
            std::queue<std::pair<int, bool>> workqueue;
#if 0
            std::vector<std::pair<double, double>> orig;
            if (ctx->debug)
                for (auto c : p->solve_cells)
                    orig.emplace_back(p->cell_locs[c->name].rawx, p->cell_locs[c->name].rawy);
#endif
            for (auto &r : regions) {
                if (merged_regions.count(r.id))
                    continue;
#if 0
                for (auto t : sorted(beltype)) {
                    log_info("%s (%d, %d) |_> (%d, %d) %d/%d\n", t.c_str(ctx), r.x0, r.y0, r.x1, r.y1,
                             r.cells.at(type_index.at(t)), r.bels.at(type_index.at(t)));
                }

#endif
                workqueue.emplace(r.id, false);
                // cut_region(r, false);
            }
            while (!workqueue.empty()) {
                auto front = workqueue.front();
                workqueue.pop();
                auto &r = regions.at(front.first);
                if (std::all_of(r.cells.begin(), r.cells.end(), [](int x) { return x == 0; }))
                    continue;
                auto res = cut_region(r, front.second);
                if (res) {
                    workqueue.emplace(res->first, !front.second);
                    workqueue.emplace(res->second, !front.second);
                } else {
                    // Try the other dir, in case stuck in one direction only
                    auto res2 = cut_region(r, !front.second);
                    if (res2) {
                        // log_info("RETRY SUCCESS\n");
                        workqueue.emplace(res2->first, front.second);
                        workqueue.emplace(res2->second, front.second);
                    }
                }
            }
#if 0
            if (ctx->debug) {
                std::ofstream sp("spread" + std::to_string(seq) + ".csv");
                for (size_t i = 0; i < p->solve_cells.size(); i++) {
                    auto &c = p->solve_cells.at(i);
                    if (c->type != beltype)
                        continue;
                    sp << orig.at(i).first << "," << orig.at(i).second << "," << p->cell_locs[c->name].rawx << "," << p->cell_locs[c->name].rawy << std::endl;
                }
                std::ofstream oc("cells" + std::to_string(seq) + ".csv");
                for (size_t y = 0; y <= p->max_y; y++) {
                    for (size_t x = 0; x <= p->max_x; x++) {
                        oc << cells_at_location.at(x).at(y).size() << ", ";
                    }
                    oc << std::endl;
                }
                ++seq;
            }
#endif
            auto endt = std::chrono::high_resolution_clock::now();
            p->cl_time += std::chrono::duration<float>(endt - startt).count();
        }

      private:
        HeAPPlacer *p;
        Context *ctx;
        std::unordered_set<IdString> beltype;
        std::unordered_map<IdString, int> type_index;
        std::vector<std::vector<std::vector<int>>> occupancy;
        std::vector<std::vector<int>> groups;
        std::vector<std::vector<ChainExtent>> chaines;
        std::map<IdString, ChainExtent> cell_extents;

        std::vector<std::vector<std::vector<std::vector<BelId>>> *> fb;

        std::vector<SpreaderRegion> regions;
        std::unordered_set<int> merged_regions;
        // Cells at a location, sorted by real (not integer) x and y
        std::vector<std::vector<std::vector<CellInfo *>>> cells_at_location;

        int occ_at(int x, int y, int type) { return occupancy.at(x).at(y).at(type); }

        int bels_at(int x, int y, int type)
        {
            if (x >= int(fb.at(type)->size()) || y >= int(fb.at(type)->at(x).size()))
                return 0;
            return int(fb.at(type)->at(x).at(y).size());
        }

        void init()
        {
            occupancy.resize(p->max_x + 1,
                             std::vector<std::vector<int>>(p->max_y + 1, std::vector<int>(beltype.size(), 0)));
            groups.resize(p->max_x + 1, std::vector<int>(p->max_y + 1, -1));
            chaines.resize(p->max_x + 1, std::vector<ChainExtent>(p->max_y + 1));
            cells_at_location.resize(p->max_x + 1, std::vector<std::vector<CellInfo *>>(p->max_y + 1));
            for (int x = 0; x <= p->max_x; x++)
                for (int y = 0; y <= p->max_y; y++) {
                    for (int t = 0; t < int(beltype.size()); t++) {
                        occupancy.at(x).at(y).at(t) = 0;
                    }
                    groups.at(x).at(y) = -1;
                    chaines.at(x).at(y) = {x, y, x, y};
                }

            auto set_chain_ext = [&](IdString cell, int x, int y) {
                if (!cell_extents.count(cell))
                    cell_extents[cell] = {x, y, x, y};
                else {
                    cell_extents[cell].x0 = std::min(cell_extents[cell].x0, x);
                    cell_extents[cell].y0 = std::min(cell_extents[cell].y0, y);
                    cell_extents[cell].x1 = std::max(cell_extents[cell].x1, x);
                    cell_extents[cell].y1 = std::max(cell_extents[cell].y1, y);
                }
            };

            for (auto &cell : p->cell_locs) {
                if (!beltype.count(ctx->cells.at(cell.first)->type))
                    continue;
                if (ctx->cells.at(cell.first)->belStrength > STRENGTH_STRONG)
                    continue;
                occupancy.at(cell.second.x).at(cell.second.y).at(type_index.at(ctx->cells.at(cell.first)->type))++;
                // Compute ultimate extent of each chain root
                if (p->chain_root.count(cell.first)) {
                    set_chain_ext(p->chain_root.at(cell.first)->name, cell.second.x, cell.second.y);
                } else if (!ctx->cells.at(cell.first)->constr_children.empty()) {
                    set_chain_ext(cell.first, cell.second.x, cell.second.y);
                }
            }
            for (auto &cell : p->cell_locs) {
                if (!beltype.count(ctx->cells.at(cell.first)->type))
                    continue;
                // Transfer chain extents to the actual chaines structure
                ChainExtent *ce = nullptr;
                if (p->chain_root.count(cell.first))
                    ce = &(cell_extents.at(p->chain_root.at(cell.first)->name));
                else if (!ctx->cells.at(cell.first)->constr_children.empty())
                    ce = &(cell_extents.at(cell.first));
                if (ce) {
                    auto &lce = chaines.at(cell.second.x).at(cell.second.y);
                    lce.x0 = std::min(lce.x0, ce->x0);
                    lce.y0 = std::min(lce.y0, ce->y0);
                    lce.x1 = std::max(lce.x1, ce->x1);
                    lce.y1 = std::max(lce.y1, ce->y1);
                }
            }
            for (auto cell : p->solve_cells) {
                if (!beltype.count(cell->type))
                    continue;
                cells_at_location.at(p->cell_locs.at(cell->name).x).at(p->cell_locs.at(cell->name).y).push_back(cell);
            }
        }
        void merge_regions(SpreaderRegion &merged, SpreaderRegion &mergee)
        {
            // Prevent grow_region from recursing while doing this
            for (int x = mergee.x0; x <= mergee.x1; x++)
                for (int y = mergee.y0; y <= mergee.y1; y++) {
                    // log_info("%d %d\n", groups.at(x).at(y), mergee.id);
                    NPNR_ASSERT(groups.at(x).at(y) == mergee.id);
                    groups.at(x).at(y) = merged.id;
                    for (size_t t = 0; t < beltype.size(); t++) {
                        merged.cells.at(t) += occ_at(x, y, t);
                        merged.bels.at(t) += bels_at(x, y, t);
                    }
                }
            merged_regions.insert(mergee.id);
            grow_region(merged, mergee.x0, mergee.y0, mergee.x1, mergee.y1);
        }

        void grow_region(SpreaderRegion &r, int x0, int y0, int x1, int y1, bool init = false)
        {
            // log_info("growing to (%d, %d) |_> (%d, %d)\n", x0, y0, x1, y1);
            if ((x0 >= r.x0 && y0 >= r.y0 && x1 <= r.x1 && y1 <= r.y1) || init)
                return;
            int old_x0 = r.x0 + (init ? 1 : 0), old_y0 = r.y0, old_x1 = r.x1, old_y1 = r.y1;
            r.x0 = std::min(r.x0, x0);
            r.y0 = std::min(r.y0, y0);
            r.x1 = std::max(r.x1, x1);
            r.y1 = std::max(r.y1, y1);

            auto process_location = [&](int x, int y) {
                // Merge with any overlapping regions
                if (groups.at(x).at(y) == -1) {
                    for (int t = 0; t < int(beltype.size()); t++) {
                        r.bels.at(t) += bels_at(x, y, t);
                        r.cells.at(t) += occ_at(x, y, t);
                    }
                }
                if (groups.at(x).at(y) != -1 && groups.at(x).at(y) != r.id)
                    merge_regions(r, regions.at(groups.at(x).at(y)));
                groups.at(x).at(y) = r.id;
                // Grow to cover any chains
                auto &chaine = chaines.at(x).at(y);
                grow_region(r, chaine.x0, chaine.y0, chaine.x1, chaine.y1);
            };
            for (int x = r.x0; x < old_x0; x++)
                for (int y = r.y0; y <= r.y1; y++)
                    process_location(x, y);
            for (int x = old_x1 + 1; x <= x1; x++)
                for (int y = r.y0; y <= r.y1; y++)
                    process_location(x, y);
            for (int y = r.y0; y < old_y0; y++)
                for (int x = r.x0; x <= r.x1; x++)
                    process_location(x, y);
            for (int y = old_y1 + 1; y <= r.y1; y++)
                for (int x = r.x0; x <= r.x1; x++)
                    process_location(x, y);
        }

        void find_overused_regions()
        {
            for (int x = 0; x <= p->max_x; x++)
                for (int y = 0; y <= p->max_y; y++) {
                    // Either already in a group, or not overutilised. Ignore
                    if (groups.at(x).at(y) != -1)
                        continue;
                    bool overutilised = false;
                    for (size_t t = 0; t < beltype.size(); t++) {
                        if (occ_at(x, y, t) > bels_at(x, y, t)) {
                            overutilised = true;
                            break;
                        }
                    }
                    if (!overutilised)
                        continue;
                    // log_info("%d %d %d\n", x, y, occ_at(x, y));
                    int id = int(regions.size());
                    groups.at(x).at(y) = id;
                    SpreaderRegion reg;
                    reg.id = id;
                    reg.x0 = reg.x1 = x;
                    reg.y0 = reg.y1 = y;
                    for (size_t t = 0; t < beltype.size(); t++) {
                        reg.bels.push_back(bels_at(x, y, t));
                        reg.cells.push_back(occ_at(x, y, t));
                    }
                    // Make sure we cover carries, etc
                    grow_region(reg, reg.x0, reg.y0, reg.x1, reg.y1, true);

                    bool expanded = true;
                    while (expanded) {
                        expanded = false;
                        // Keep trying expansion in x and y, until we find no over-occupancy cells
                        // or hit grouped cells

                        // First try expanding in x
                        if (reg.x1 < p->max_x) {
                            bool over_occ_x = false;
                            for (int y1 = reg.y0; y1 <= reg.y1; y1++) {
                                for (size_t t = 0; t < beltype.size(); t++) {
                                    if (occ_at(reg.x1 + 1, y1, t) > bels_at(reg.x1 + 1, y1, t)) {
                                        // log_info("(%d, %d) occ %d bels %d\n", reg.x1+ 1, y1, occ_at(reg.x1 + 1, y1),
                                        // bels_at(reg.x1 + 1, y1));
                                        over_occ_x = true;
                                        break;
                                    }
                                }
                            }
                            if (over_occ_x) {
                                expanded = true;
                                grow_region(reg, reg.x0, reg.y0, reg.x1 + 1, reg.y1);
                            }
                        }

                        if (reg.y1 < p->max_y) {
                            bool over_occ_y = false;
                            for (int x1 = reg.x0; x1 <= reg.x1; x1++) {
                                for (size_t t = 0; t < beltype.size(); t++) {
                                    if (occ_at(x1, reg.y1 + 1, t) > bels_at(x1, reg.y1 + 1, t)) {
                                        // log_info("(%d, %d) occ %d bels %d\n", x1, reg.y1 + 1, occ_at(x1, reg.y1 + 1),
                                        // bels_at(x1, reg.y1 + 1));
                                        over_occ_y = true;
                                        break;
                                    }
                                }
                            }
                            if (over_occ_y) {
                                expanded = true;
                                grow_region(reg, reg.x0, reg.y0, reg.x1, reg.y1 + 1);
                            }
                        }
                    }
                    regions.push_back(reg);
                }
        }

        void expand_regions()
        {
            std::queue<int> overu_regions;
            float beta = p->cfg.beta;
            for (auto &r : regions) {
                if (!merged_regions.count(r.id) && r.overused(beta))
                    overu_regions.push(r.id);
            }
            while (!overu_regions.empty()) {
                int rid = overu_regions.front();
                overu_regions.pop();
                if (merged_regions.count(rid))
                    continue;
                auto &reg = regions.at(rid);
                while (reg.overused(beta)) {
                    bool changed = false;
                    for (int j = 0; j < p->cfg.spread_scale_x; j++) {
                        if (reg.x0 > 0) {
                            grow_region(reg, reg.x0 - 1, reg.y0, reg.x1, reg.y1);
                            changed = true;
                            if (!reg.overused(beta))
                                break;
                        }
                        if (reg.x1 < p->max_x) {
                            grow_region(reg, reg.x0, reg.y0, reg.x1 + 1, reg.y1);
                            changed = true;
                            if (!reg.overused(beta))
                                break;
                        }
                    }
                    for (int j = 0; j < p->cfg.spread_scale_y; j++) {
                        if (reg.y0 > 0) {
                            grow_region(reg, reg.x0, reg.y0 - 1, reg.x1, reg.y1);
                            changed = true;
                            if (!reg.overused(beta))
                                break;
                        }
                        if (reg.y1 < p->max_y) {
                            grow_region(reg, reg.x0, reg.y0, reg.x1, reg.y1 + 1);
                            changed = true;
                            if (!reg.overused(beta))
                                break;
                        }
                    }
                    if (!changed) {
                        for (auto bt : sorted(beltype)) {
                            if (reg.cells > reg.bels)
                                log_error("Failed to expand region (%d, %d) |_> (%d, %d) of %d %ss\n", reg.x0, reg.y0,
                                          reg.x1, reg.y1, reg.cells.at(type_index.at(bt)), bt.c_str(ctx));
                        }
                        break;
                    }
                }
            }
        }

        // Implementation of the recursive cut-based spreading as described in the HeAP paper
        // Note we use "left" to mean "-x/-y" depending on dir and "right" to mean "+x/+y" depending on dir

        std::vector<CellInfo *> cut_cells;

        boost::optional<std::pair<int, int>> cut_region(SpreaderRegion &r, bool dir)
        {
            cut_cells.clear();
            auto &cal = cells_at_location;
            int total_cells = 0, total_bels = 0;
            for (int x = r.x0; x <= r.x1; x++) {
                for (int y = r.y0; y <= r.y1; y++) {
                    std::copy(cal.at(x).at(y).begin(), cal.at(x).at(y).end(), std::back_inserter(cut_cells));
                    for (size_t t = 0; t < beltype.size(); t++)
                        total_bels += bels_at(x, y, t);
                }
            }
            for (auto &cell : cut_cells) {
                total_cells += p->chain_size.count(cell->name) ? p->chain_size.at(cell->name) : 1;
            }
            std::sort(cut_cells.begin(), cut_cells.end(), [&](const CellInfo *a, const CellInfo *b) {
                return dir ? (p->cell_locs.at(a->name).rawy < p->cell_locs.at(b->name).rawy)
                           : (p->cell_locs.at(a->name).rawx < p->cell_locs.at(b->name).rawx);
            });

            if (cut_cells.size() < 2)
                return {};
            // Find the cells midpoint, counting chains in terms of their total size - making the initial source cut
            int pivot_cells = 0;
            int pivot = 0;
            for (auto &cell : cut_cells) {
                pivot_cells += p->chain_size.count(cell->name) ? p->chain_size.at(cell->name) : 1;
                if (pivot_cells >= total_cells / 2)
                    break;
                pivot++;
            }
            if (pivot >= int(cut_cells.size())) {
                pivot = int(cut_cells.size()) - 1;
            }
            // log_info("orig pivot %d/%d lc %d rc %d\n", pivot, int(cut_cells.size()), pivot_cells, total_cells -
            // pivot_cells);

            // Find the clearance required either side of the pivot
            int clearance_l = 0, clearance_r = 0;
            for (size_t i = 0; i < cut_cells.size(); i++) {
                int size;
                if (cell_extents.count(cut_cells.at(i)->name)) {
                    auto &ce = cell_extents.at(cut_cells.at(i)->name);
                    size = dir ? (ce.y1 - ce.y0 + 1) : (ce.x1 - ce.x0 + 1);
                } else {
                    size = 1;
                }
                if (int(i) < pivot)
                    clearance_l = std::max(clearance_l, size);
                else
                    clearance_r = std::max(clearance_r, size);
            }
            // Find the target cut that minimises difference in utilisation, whilst trying to ensure that all chains
            // still fit

            // First trim the boundaries of the region in the axis-of-interest, skipping any rows/cols without any
            // bels of the appropriate type
            int trimmed_l = dir ? r.y0 : r.x0, trimmed_r = dir ? r.y1 : r.x1;
            while (trimmed_l < (dir ? r.y1 : r.x1)) {
                bool have_bels = false;
                for (int i = dir ? r.x0 : r.y0; i <= (dir ? r.x1 : r.y1); i++)
                    for (size_t t = 0; t < beltype.size(); t++)
                        if (bels_at(dir ? i : trimmed_l, dir ? trimmed_l : i, t) > 0) {
                            have_bels = true;
                            break;
                        }
                if (have_bels)
                    break;
                trimmed_l++;
            }
            while (trimmed_r > (dir ? r.y0 : r.x0)) {
                bool have_bels = false;
                for (int i = dir ? r.x0 : r.y0; i <= (dir ? r.x1 : r.y1); i++)
                    for (size_t t = 0; t < beltype.size(); t++)
                        if (bels_at(dir ? i : trimmed_r, dir ? trimmed_r : i, t) > 0) {
                            have_bels = true;
                            break;
                        }
                if (have_bels)
                    break;
                trimmed_r--;
            }
            // log_info("tl %d tr %d cl %d cr %d\n", trimmed_l, trimmed_r, clearance_l, clearance_r);
            if ((trimmed_r - trimmed_l + 1) <= std::max(clearance_l, clearance_r))
                return {};
            // Now find the initial target cut that minimises utilisation imbalance, whilst
            // meeting the clearance requirements for any large macros
            std::vector<int> left_cells_v(beltype.size(), 0), right_cells_v(beltype.size(), 0);
            std::vector<int> left_bels_v(beltype.size(), 0), right_bels_v(r.bels);
            for (int i = 0; i <= pivot; i++)
                left_cells_v.at(type_index.at(cut_cells.at(i)->type)) +=
                        p->chain_size.count(cut_cells.at(i)->name) ? p->chain_size.at(cut_cells.at(i)->name) : 1;
            for (int i = pivot + 1; i < int(cut_cells.size()); i++)
                right_cells_v.at(type_index.at(cut_cells.at(i)->type)) +=
                        p->chain_size.count(cut_cells.at(i)->name) ? p->chain_size.at(cut_cells.at(i)->name) : 1;

            int best_tgt_cut = -1;
            double best_deltaU = std::numeric_limits<double>::max();
            // std::pair<int, int> target_cut_bels;
            std::vector<int> slither_bels(beltype.size(), 0);
            for (int i = trimmed_l; i <= trimmed_r; i++) {
                for (size_t t = 0; t < beltype.size(); t++)
                    slither_bels.at(t) = 0;
                for (int j = dir ? r.x0 : r.y0; j <= (dir ? r.x1 : r.y1); j++) {
                    for (size_t t = 0; t < beltype.size(); t++)
                        slither_bels.at(t) += dir ? bels_at(j, i, t) : bels_at(i, j, t);
                }
                for (size_t t = 0; t < beltype.size(); t++) {
                    left_bels_v.at(t) += slither_bels.at(t);
                    right_bels_v.at(t) -= slither_bels.at(t);
                }

                if (((i - trimmed_l) + 1) >= clearance_l && ((trimmed_r - i) + 1) >= clearance_r) {
                    // Solution is potentially valid
                    double aU = 0;
                    for (size_t t = 0; t < beltype.size(); t++)
                        aU += (left_cells_v.at(t) + right_cells_v.at(t)) *
                              std::abs(double(left_cells_v.at(t)) / double(std::max(left_bels_v.at(t), 1)) -
                                       double(right_cells_v.at(t)) / double(std::max(right_bels_v.at(t), 1)));
                    if (aU < best_deltaU) {
                        best_deltaU = aU;
                        best_tgt_cut = i;
                    }
                }
            }
            if (best_tgt_cut == -1)
                return {};
            // left_bels = target_cut_bels.first;
            // right_bels = target_cut_bels.second;
            for (size_t t = 0; t < beltype.size(); t++) {
                left_bels_v.at(t) = 0;
                right_bels_v.at(t) = 0;
            }
            for (int x = r.x0; x <= (dir ? r.x1 : best_tgt_cut); x++)
                for (int y = r.y0; y <= (dir ? best_tgt_cut : r.y1); y++) {
                    for (size_t t = 0; t < beltype.size(); t++) {
                        left_bels_v.at(t) += bels_at(x, y, t);
                    }
                }
            for (int x = dir ? r.x0 : (best_tgt_cut + 1); x <= r.x1; x++)
                for (int y = dir ? (best_tgt_cut + 1) : r.y0; y <= r.y1; y++) {
                    for (size_t t = 0; t < beltype.size(); t++) {
                        right_bels_v.at(t) += bels_at(x, y, t);
                    }
                }
            if (std::accumulate(left_bels_v.begin(), left_bels_v.end(), 0) == 0 ||
                std::accumulate(right_bels_v.begin(), right_bels_v.end(), 0) == 0)
                return {};
            // log_info("pivot %d target cut %d lc %d lb %d rc %d rb %d\n", pivot, best_tgt_cut,
            // std::accumulate(left_cells_v.begin(), left_cells_v.end(), 0), std::accumulate(left_bels_v.begin(),
            // left_bels_v.end(), 0),
            //          std::accumulate(right_cells_v.begin(), right_cells_v.end(), 0),
            //          std::accumulate(right_bels_v.begin(), right_bels_v.end(), 0));

            // Peturb the source cut to eliminate overutilisation
            auto is_part_overutil = [&](bool r) {
                double delta = 0;
                for (size_t t = 0; t < left_cells_v.size(); t++) {
                    delta += double(left_cells_v.at(t)) / double(std::max(left_bels_v.at(t), 1)) -
                             double(right_cells_v.at(t)) / double(std::max(right_bels_v.at(t), 1));
                }
                return r ? delta < 0 : delta > 0;
            };
            while (pivot > 0 && is_part_overutil(false)) {
                auto &move_cell = cut_cells.at(pivot);
                int size = p->chain_size.count(move_cell->name) ? p->chain_size.at(move_cell->name) : 1;
                left_cells_v.at(type_index.at(cut_cells.at(pivot)->type)) -= size;
                right_cells_v.at(type_index.at(cut_cells.at(pivot)->type)) += size;
                pivot--;
            }
            while (pivot < int(cut_cells.size()) - 1 && is_part_overutil(true)) {
                auto &move_cell = cut_cells.at(pivot + 1);
                int size = p->chain_size.count(move_cell->name) ? p->chain_size.at(move_cell->name) : 1;
                left_cells_v.at(type_index.at(cut_cells.at(pivot)->type)) += size;
                right_cells_v.at(type_index.at(cut_cells.at(pivot)->type)) -= size;
                pivot++;
            }
            // log_info("peturbed pivot %d lc %d lb %d rc %d rb %d\n", pivot, left_cells, left_bels, right_cells,
            // right_bels);
            // Split regions into bins, and then spread cells by linear interpolation within those bins
            auto spread_binlerp = [&](int cells_start, int cells_end, double area_l, double area_r) {
                int N = cells_end - cells_start;
                if (N <= 2) {
                    for (int i = cells_start; i < cells_end; i++) {
                        auto &pos = dir ? p->cell_locs.at(cut_cells.at(i)->name).rawy
                                        : p->cell_locs.at(cut_cells.at(i)->name).rawx;
                        pos = area_l + i * ((area_r - area_l) / N);
                    }
                    return;
                }
                // Split region into up to 10 (K) bins
                int K = std::min<int>(N, 10);
                std::vector<std::pair<int, double>> bin_bounds; // [(cell start, area start)]
                bin_bounds.emplace_back(cells_start, area_l);
                for (int i = 1; i < K; i++)
                    bin_bounds.emplace_back(cells_start + (N * i) / K, area_l + ((area_r - area_l + 0.99) * i) / K);
                bin_bounds.emplace_back(cells_end, area_r + 0.99);
                for (int i = 0; i < K; i++) {
                    auto &bl = bin_bounds.at(i), br = bin_bounds.at(i + 1);
                    double orig_left = dir ? p->cell_locs.at(cut_cells.at(bl.first)->name).rawy
                                           : p->cell_locs.at(cut_cells.at(bl.first)->name).rawx;
                    double orig_right = dir ? p->cell_locs.at(cut_cells.at(br.first - 1)->name).rawy
                                            : p->cell_locs.at(cut_cells.at(br.first - 1)->name).rawx;
                    double m = (br.second - bl.second) / std::max(0.00001, orig_right - orig_left);
                    for (int j = bl.first; j < br.first; j++) {
                        Region *cr = cut_cells.at(j)->region;
                        if (cr != nullptr) {
                            // Limit spreading bounds to constraint region; if applicable
                            double brsc = p->limit_to_reg(cr, br.second, dir);
                            double blsc = p->limit_to_reg(cr, bl.second, dir);
                            double mr = (brsc - blsc) / std::max(0.00001, orig_right - orig_left);
                            auto &pos = dir ? p->cell_locs.at(cut_cells.at(j)->name).rawy
                                            : p->cell_locs.at(cut_cells.at(j)->name).rawx;
                            NPNR_ASSERT(pos >= orig_left && pos <= orig_right);
                            pos = blsc + mr * (pos - orig_left);
                        } else {
                            auto &pos = dir ? p->cell_locs.at(cut_cells.at(j)->name).rawy
                                            : p->cell_locs.at(cut_cells.at(j)->name).rawx;
                            NPNR_ASSERT(pos >= orig_left && pos <= orig_right);
                            pos = bl.second + m * (pos - orig_left);
                        }
                        // log("[%f, %f] -> [%f, %f]: %f -> %f\n", orig_left, orig_right, bl.second, br.second,
                        // orig_pos, pos);
                    }
                }
            };
            spread_binlerp(0, pivot + 1, trimmed_l, best_tgt_cut);
            spread_binlerp(pivot + 1, int(cut_cells.size()), best_tgt_cut + 1, trimmed_r);
            // Update various data structures
            for (int x = r.x0; x <= r.x1; x++)
                for (int y = r.y0; y <= r.y1; y++) {
                    cells_at_location.at(x).at(y).clear();
                }
            for (auto cell : cut_cells) {
                auto &cl = p->cell_locs.at(cell->name);
                cl.x = std::min(r.x1, std::max(r.x0, int(cl.rawx)));
                cl.y = std::min(r.y1, std::max(r.y0, int(cl.rawy)));
                cells_at_location.at(cl.x).at(cl.y).push_back(cell);
                // log_info("spread pos %d %d\n", cl.x, cl.y);
            }
            SpreaderRegion rl, rr;
            rl.id = int(regions.size());
            rl.x0 = r.x0;
            rl.y0 = r.y0;
            rl.x1 = dir ? r.x1 : best_tgt_cut;
            rl.y1 = dir ? best_tgt_cut : r.y1;
            rl.cells = left_cells_v;
            rl.bels = left_bels_v;
            rr.id = int(regions.size()) + 1;
            rr.x0 = dir ? r.x0 : (best_tgt_cut + 1);
            rr.y0 = dir ? (best_tgt_cut + 1) : r.y0;
            rr.x1 = r.x1;
            rr.y1 = r.y1;
            rr.cells = right_cells_v;
            rr.bels = right_bels_v;
            regions.push_back(rl);
            regions.push_back(rr);
            for (int x = rl.x0; x <= rl.x1; x++)
                for (int y = rl.y0; y <= rl.y1; y++)
                    groups.at(x).at(y) = rl.id;
            for (int x = rr.x0; x <= rr.x1; x++)
                for (int y = rr.y0; y <= rr.y1; y++)
                    groups.at(x).at(y) = rr.id;
            return std::make_pair(rl.id, rr.id);
        };
    };
    typedef decltype(CellInfo::udata) cell_udata_t;
    cell_udata_t dont_solve = std::numeric_limits<cell_udata_t>::max();
};
int HeAPPlacer::CutSpreader::seq = 0;

bool placer_heap(Context *ctx, PlacerHeapCfg cfg) { return HeAPPlacer(ctx, cfg).place(); }

PlacerHeapCfg::PlacerHeapCfg(Context *ctx)
{
    alpha = ctx->setting<float>("placerHeap/alpha", 0.1);
    beta = ctx->setting<float>("placerHeap/beta", 0.9);
    criticalityExponent = ctx->setting<int>("placerHeap/criticalityExponent", 2);
    timingWeight = ctx->setting<int>("placerHeap/timingWeight", 10);
    timing_driven = ctx->setting<bool>("timing_driven");
    solverTolerance = 1e-5;
    placeAllAtOnce = false;

    hpwl_scale_x = 1;
    hpwl_scale_y = 1;
    spread_scale_x = 1;
    spread_scale_y = 1;
}

NEXTPNR_NAMESPACE_END

#else

#include "log.h"
#include "nextpnr.h"
#include "placer_heap.h"

NEXTPNR_NAMESPACE_BEGIN
bool placer_heap(Context *ctx, PlacerHeapCfg cfg)
{
    log_error("nextpnr was built without the HeAP placer\n");
    return false;
}

PlacerHeapCfg::PlacerHeapCfg(Context *ctx) {}

NEXTPNR_NAMESPACE_END

#endif
