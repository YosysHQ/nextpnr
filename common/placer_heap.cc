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
 */

#include <deque>
#include <numeric>
#include <queue>
#include <unordered_map>
#include "log.h"
#include "nextpnr.h"
#include "place_common.h"
#include "placer_math.h"
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

    void solve(std::vector<double> &x)
    {
        NPNR_ASSERT(x.size() == A.size());

        int nnz = std::accumulate(A.begin(), A.end(), 0,
                                  [](int a, const std::vector<std::pair<int, T>> &vec) { return a + int(vec.size()); });
        taucif_system *sys = taucif_create_system(int(rhs.size()), int(A.size()), nnz);
        for (int col = 0; col < int(A.size()); col++) {
            auto &Ac = A[col];
            for (auto &el : Ac) {
                if (col <= el.first) {
                    // log_info("%d %d %f\n", el.first, col, el.second);
                    taucif_add_matrix_value(sys, el.first, col, el.second);
                }

                // FIXME: in debug mode, assert really is symmetric
            }
        }
        taucif_finalise_matrix(sys);
        int result = taucif_solve_system(sys, x.data(), rhs.data());
        NPNR_ASSERT(result == 0);
        taucif_free_system(sys);

        // for (int i = 0; i < int(x.size()); i++)
        //    log_info("x[%d] = %f\n", i, x.at(i));
    }
};

} // namespace

class HeAPPlacer
{
  public:
    HeAPPlacer(Context *ctx) : ctx(ctx) {}
    bool place()
    {
        ctx->lock();
        taucif_init_solver();
        place_constraints();
        build_fast_bels();
        seed_placement();
        update_all_chains();
        wirelen_t hpwl = total_hpwl();
        log_info("Initial placer starting hpwl = %d\n", int(hpwl));
        for (int i = 0; i < 20; i++) {
            setup_solve_cells();

            EquationSystem<double> esx(solve_cells.size(), solve_cells.size());
            build_equations(esx, false);
            // log_info("x-axis\n");
            solve_equations(esx, false);

            EquationSystem<double> esy(solve_cells.size(), solve_cells.size());
            build_equations(esy, true);
            // log_info("y-axis\n");
            solve_equations(esy, true);

            update_all_chains();

            hpwl = total_hpwl();
            log_info("Initial placer iter %d, hpwl = %d\n", i, int(hpwl));
        }

        // legalise_with_cuts(true);
        CutLegaliser(this, ctx->id("ICESTORM_LC")).run();
        NPNR_ASSERT(false);

        bool valid = false;
        wirelen_t solved_hpwl = 0, legal_hpwl = 1, best_hpwl = std::numeric_limits<wirelen_t>::max();
        int iter = 0, stalled = 0;
        while (!valid || (stalled < 5 && (solved_hpwl < legal_hpwl * 0.8))) {
            if ((solved_hpwl < legal_hpwl * 0.8) || (stalled > 5)) {
                stalled = 0;
                best_hpwl = std::numeric_limits<wirelen_t>::max();
                valid = true;
            }
            setup_solve_cells();

            EquationSystem<double> esx(solve_cells.size(), solve_cells.size());
            build_equations(esx, false, iter);
            // log_info("x-axis\n");
            solve_equations(esx, false);

            EquationSystem<double> esy(solve_cells.size(), solve_cells.size());
            build_equations(esy, true, iter);
            // log_info("y-axis\n");
            solve_equations(esy, true);
            solved_hpwl = total_hpwl();
            log_info("Solved HPWL = %d\n", int(solved_hpwl));

            update_all_chains();
            legalise_placement_simple(valid);
            update_all_chains();

            legal_hpwl = total_hpwl();
            log_info("Legalised HPWL = %d\n", int(legal_hpwl));
            if (legal_hpwl < best_hpwl) {
                best_hpwl = legal_hpwl;
                stalled = 0;
            } else {
                ++stalled;
            }
            ctx->yield();
            ++iter;
        }
        ctx->unlock();
        return true;
    }

  private:
    Context *ctx;

    int max_x = 0, max_y = 0;
    std::vector<std::vector<std::vector<std::vector<BelId>>>> fast_bels;
    std::unordered_map<IdString, std::tuple<int, int>> bel_types;

    // For fast handling of heterogeneosity during initial placement without full legalisation,
    // for each Bel type this goes from x or y to the nearest x or y where a Bel of a given type exists
    // This is particularly important for the iCE40 architecture, where multipliers and BRAM only exist at the
    // edges and corners respectively
    std::vector<std::vector<int>> nearest_row_with_bel;
    std::vector<std::vector<int>> nearest_col_with_bel;

    // In some cases, we can't use bindBel because we allow overlap in the earlier stages. So we use this custom
    // structure instead
    struct CellLocation
    {
        int x, y;
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

    // Place cells with the BEL attribute set to constrain them
    void place_constraints()
    {
        size_t placed_cells = 0;
        // Initial constraints placer
        for (auto &cell_entry : ctx->cells) {
            CellInfo *cell = cell_entry.second.get();
            auto loc = cell->attrs.find(ctx->id("BEL"));
            if (loc != cell->attrs.end()) {
                std::string loc_name = loc->second;
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
        int constr_placed_cells = placed_cells;
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
        std::unordered_map<IdString, std::vector<BelId>> available_bels;
        for (auto bel : ctx->getBels()) {
            if (!ctx->checkBelAvail(bel))
                continue;
            available_bels[ctx->getBelType(bel)].push_back(bel);
        }
        for (auto &ab : available_bels)
            ctx->shuffle(ab.second);
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->bel != BelId()) {
                Loc loc = ctx->getBelLocation(ci->bel);
                cell_locs[cell.first].x = loc.x;
                cell_locs[cell.first].y = loc.y;
                cell_locs[cell.first].locked = true;
                cell_locs[cell.first].global = ctx->getBelGlobalBuf(ci->bel);
            } else if (ci->constr_parent == nullptr) {
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
                if (has_connectivity(cell.second) && cell.second->type != ctx->id("SB_IO")) {
                    place_cells.push_back(ci);
                } else {
                    ctx->bindBel(bel, ci, STRENGTH_STRONG);
                    cell_locs[cell.first].locked = true;
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
            chain_size[root->name]++;
            if (child->constr_x != child->UNCONSTR)
                cell_locs[child->name].x = base.x + child->constr_x;
            else
                cell_locs[child->name].x = base.x; // better handling of UNCONSTR?
            if (child->constr_y != child->UNCONSTR)
                cell_locs[child->name].y = base.y + child->constr_y;
            else
                cell_locs[child->name].y = base.y; // better handling of UNCONSTR?
            chain_root[cell->name] = root;
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
            func(net->driver);
        for (auto &user : net->users)
            func(user);
    }

    // Build the system of equations for either X or Y
    void build_equations(EquationSystem<double> &es, bool yaxis, int iter = -1)
    {
        // Return the x or y position of a cell, depending on ydir
        auto cell_pos = [&](CellInfo *cell) { return yaxis ? cell_locs.at(cell->name).y : cell_locs.at(cell->name).x; };

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
            foreach_port(ni, [&](PortRef &port) {
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
            foreach_port(ni, [&](PortRef &port) {
                int this_pos = cell_pos(port.cell);
                auto process_arc = [&](PortRef *other) {
                    if (other == &port)
                        return;
                    int o_pos = cell_pos(other->cell);
                    // if (o_pos == this_pos)
                    //    return; // FIXME: or clamp to 1?
                    double weight = 1. / (ni->users.size() * std::max(1, std::abs(o_pos - this_pos)));
                    // FIXME: add criticality to weighting

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
            const float alpha = 0.3;
            float weight = alpha * iter;
            for (size_t row = 0; row < solve_cells.size(); row++) {
                // Add an arc from legalised to current position
                es.add_coeff(row, row, weight);
                es.add_rhs(row, weight * cell_pos(solve_cells.at(row)));
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
        es.solve(vals);
        for (size_t i = 0; i < vals.size(); i++)
            if (yaxis)
                cell_locs.at(solve_cells.at(i)->name).y = std::min(max_y, std::max(0, int(vals.at(i) + 0.5)));
            else
                cell_locs.at(solve_cells.at(i)->name).x = std::min(max_x, std::max(0, int(vals.at(i) + 0.5)));
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
            hpwl += (xmax - xmin) + (ymax - ymin);
        }
        return hpwl;
    }

    // Swap the Bel of a cell with another, return the original location
    BelId swap_cell_bels(CellInfo *cell, BelId newBel)
    {
        BelId oldBel = cell->bel;
        CellInfo *bound = ctx->getBoundBelCell(newBel);
        if (bound != nullptr)
            ctx->unbindBel(newBel);
        ctx->unbindBel(oldBel);
        ctx->bindBel(newBel, cell, STRENGTH_WEAK);
        if (bound != nullptr)
            ctx->bindBel(oldBel, bound, STRENGTH_WEAK);
        return oldBel;
    }

    // Placement legalisation
    // Note that there are *two meanings* of legalisation in nextpnr placement
    // The first kind, as in HeAP, simply ensures that there is no overlap (each Bel maps only to one cell)
    // The second kind also ensures that validity rules (isValidBelForCell) are met, because there is no guarantee
    // in nextpnr that Bels are freely swappable (indeed many a architectures Bel is a logic cell with complex
    // validity rules for control sets, etc, rather than a CLB/tile as in a more conventional pack&place flow)
    void legalise_placement_simple(bool require_validity = false)
    {
        // Unbind all cells placed in this solution
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->udata != dont_solve && ci->bel != BelId())
                ctx->unbindBel(ci->bel);
        }

        // At the moment we don't follow the full HeAP algorithm using cuts for legalisation, instead using
        // the simple greedy largest-macro-first approach.
        std::priority_queue<std::pair<int, IdString>> remaining;
        for (auto cell : solve_cells) {
            remaining.emplace(chain_size[cell->name], cell->name);
        }

        while (!remaining.empty()) {
            auto top = remaining.top();
            remaining.pop();

            CellInfo *ci = ctx->cells.at(top.second).get();
            // Was now placed, ignore
            if (ci->bel != BelId())
                continue;
            // log_info("   Legalising %s\n", top.second.c_str(ctx));
            int bt = std::get<0>(bel_types.at(ci->type));
            auto &fb = fast_bels.at(bt);
            int radius = 0;
            int iter = 0;
            bool placed = false;
            while (!placed) {

                int nx = ctx->rng(2 * radius + 1) + std::max(cell_locs.at(ci->name).x - radius, 0);
                int ny = ctx->rng(2 * radius + 1) + std::max(cell_locs.at(ci->name).x - radius, 0);

                iter++;
                if ((iter % (20 * (radius + 1))) == 0)
                    radius = std::min(std::max(max_x, max_y), radius + 1);

                if (nx < 0 || nx > max_x)
                    continue;
                if (ny < 0 || ny > max_x)
                    continue;

                // ny = nearest_row_with_bel.at(bt).at(ny);
                // nx = nearest_col_with_bel.at(bt).at(nx);

                if (nx >= int(fb.size()))
                    continue;
                if (ny >= int(fb.at(nx).size()))
                    continue;
                if (fb.at(nx).at(ny).empty())
                    continue;

                if (ci->constr_children.empty()) {
                    for (auto sz : fb.at(nx).at(ny)) {
                        if (ctx->checkBelAvail(sz) || radius > (max_x / 4)) {
                            CellInfo *bound = ctx->getBoundBelCell(sz);
                            if (bound != nullptr) {
                                if (bound->constr_parent != nullptr || !bound->constr_children.empty())
                                    continue;
                                ctx->unbindBel(bound->bel);
                                remaining.emplace(chain_size[bound->name], bound->name);
                            }
                            ctx->bindBel(sz, ci, STRENGTH_WEAK);
                            if (require_validity && !ctx->isBelLocationValid(sz)) {
                                ctx->unbindBel(sz);
                                if (bound != nullptr)
                                    ctx->bindBel(sz, bound, STRENGTH_WEAK);
                            } else {
                                Loc loc = ctx->getBelLocation(sz);
                                cell_locs[ci->name].x = loc.x;
                                cell_locs[ci->name].y = loc.y;
                                placed = true;
                                break;
                            }
                        }
                    }
                } else {
                    // FIXME
                    NPNR_ASSERT(false);
                }
            }
        }
    }

    static constexpr float beta = 0.9;

    struct ChainExtent
    {
        int x0, y0, x1, y1;
    };

    struct LegaliserRegion
    {
        int id;
        int x0, y0, x1, y1;
        int cells, bels;
        std::unordered_set<IdString> included_chains;
        bool overused() const
        {
            if (bels < 4)
                return cells > bels;
            else
                return cells > beta * bels;
        }
    };

    class CutLegaliser
    {
      public:
        CutLegaliser(HeAPPlacer *p, IdString beltype)
                : p(p), ctx(p->ctx), beltype(beltype), fb(p->fast_bels.at(std::get<0>(p->bel_types.at(beltype))))
        {
        }

        void run()
        {
            init();
            find_overused_regions();
            expand_regions();
            for (auto &r : regions) {
                if (!merged_regions.count(r.id))
                    log_info("%s (%d, %d) |_> (%d, %d) %d/%d\n", beltype.c_str(ctx), r.x0, r.y0, r.x1, r.y1, r.cells,
                             r.bels);
            }
        }

      private:
        HeAPPlacer *p;
        Context *ctx;
        IdString beltype;
        std::vector<std::vector<int>> occupancy;
        std::vector<std::vector<int>> groups;
        std::vector<std::vector<ChainExtent>> chaines;
        std::vector<std::vector<std::vector<BelId>>> &fb;

        std::vector<LegaliserRegion> regions;
        std::unordered_set<int> merged_regions;

        int occ_at(int x, int y) { return occupancy.at(x).at(y); }

        int bels_at(int x, int y)
        {
            if (x >= int(fb.size()) || y >= int(fb.at(x).size()))
                return 0;
            return int(fb.at(x).at(y).size());
        }

        void init()
        {
            occupancy.resize(p->max_x + 1, std::vector<int>(p->max_y + 1, 0));
            groups.resize(p->max_x + 1, std::vector<int>(p->max_y + 1, -1));
            chaines.resize(p->max_x + 1, std::vector<ChainExtent>(p->max_y + 1));

            for (int x = 0; x <= p->max_x; x++)
                for (int y = 0; y <= p->max_y; y++) {
                    occupancy.at(x).at(y) = 0;
                    groups.at(x).at(y) = -1;
                    chaines.at(x).at(y) = {x, y, x, y};
                }

            std::map<IdString, ChainExtent> cr_extents;

            auto set_chain_ext = [&](IdString cell, int x, int y) {
                if (!cr_extents.count(cell))
                    cr_extents[cell] = {x, y, x, y};
                else {
                    cr_extents[cell].x0 = std::min(cr_extents[cell].x0, x);
                    cr_extents[cell].y0 = std::min(cr_extents[cell].y0, y);
                    cr_extents[cell].x1 = std::max(cr_extents[cell].x1, x);
                    cr_extents[cell].y1 = std::max(cr_extents[cell].y1, y);
                }
            };

            for (auto &cell : p->cell_locs) {
                if (ctx->cells.at(cell.first)->type == beltype)
                    occupancy.at(cell.second.x).at(cell.second.y)++;
                // Compute ultimate extent of each chain root
                if (p->chain_root.count(cell.first)) {
                    set_chain_ext(p->chain_root.at(cell.first)->name, cell.second.x, cell.second.y);
                } else if (!ctx->cells.at(cell.first)->constr_children.empty()) {
                    set_chain_ext(cell.first, cell.second.x, cell.second.y);
                }
            }
            for (auto &cell : p->cell_locs) {
                // Transfer chain extents to the actual chaines structure
                ChainExtent *ce = nullptr;
                if (p->chain_root.count(cell.first))
                    ce = &(cr_extents.at(p->chain_root.at(cell.first)->name));
                else if (!ctx->cells.at(cell.first)->constr_children.empty())
                    ce = &(cr_extents.at(cell.first));
                if (ce) {
                    auto &lce = chaines.at(cell.second.x).at(cell.second.y);
                    lce.x0 = std::min(lce.x0, ce->x0);
                    lce.y0 = std::min(lce.y0, ce->y0);
                    lce.x1 = std::max(lce.x1, ce->x1);
                    lce.y1 = std::max(lce.y1, ce->y1);
                }
            }
        }
        void merge_regions(LegaliserRegion &merged, LegaliserRegion &mergee)
        {
            // Prevent grow_region from recursing while doing this
            for (int x = mergee.x0; x <= mergee.x1; x++)
                for (int y = mergee.y0; y <= mergee.y1; y++) {
                    // log_info("%d %d\n", groups.at(x).at(y), mergee.id);
                    NPNR_ASSERT(groups.at(x).at(y) == mergee.id);
                    groups.at(x).at(y) = merged.id;
                    merged.cells += occ_at(x, y);
                    merged.bels += bels_at(x, y);
                }
            merged_regions.insert(mergee.id);
            grow_region(merged, mergee.x0, mergee.y0, mergee.x1, mergee.y1);
        }

        void grow_region(LegaliserRegion &r, int x0, int y0, int x1, int y1, bool init = false)
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
                if (groups.at(x).at(y) != r.id) {
                    r.bels += bels_at(x, y);
                    r.cells += occ_at(x, y);
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
                    if (groups.at(x).at(y) != -1 || (occ_at(x, y) <= bels_at(x, y)))
                        continue;
                    // log_info("%d %d %d\n", x, y, occ_at(x, y));
                    int id = int(regions.size());
                    groups.at(x).at(y) = id;
                    LegaliserRegion reg;
                    reg.id = id;
                    reg.x0 = reg.x1 = x;
                    reg.y0 = reg.y1 = y;
                    reg.bels = bels_at(x, y);
                    reg.cells = occ_at(x, y);
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
                                if (occ_at(reg.x1 + 1, y1) > bels_at(reg.x1 + 1, y1)) {
                                    // log_info("(%d, %d) occ %d bels %d\n", reg.x1+ 1, y1, occ_at(reg.x1 + 1, y1),
                                    // bels_at(reg.x1 + 1, y1));
                                    over_occ_x = true;
                                    break;
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
                                if (occ_at(x1, reg.y1 + 1) > bels_at(x1, reg.y1 + 1)) {
                                    // log_info("(%d, %d) occ %d bels %d\n", x1, reg.y1 + 1, occ_at(x1, reg.y1 + 1),
                                    // bels_at(x1, reg.y1 + 1));
                                    over_occ_y = true;
                                    break;
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
            for (auto &r : regions) {
                if (!merged_regions.count(r.id) && r.overused())
                    overu_regions.push(r.id);
            }
            while (!overu_regions.empty()) {
                int rid = overu_regions.front();
                overu_regions.pop();
                if (merged_regions.count(rid))
                    continue;
                auto &reg = regions.at(rid);
                while (reg.overused()) {
                    bool changed = false;
                    if (reg.x0 > 0) {
                        grow_region(reg, reg.x0 - 1, reg.y0, reg.x1, reg.y1);
                        changed = true;
                        if (!reg.overused())
                            break;
                    }
                    if (reg.x1 < p->max_x) {
                        grow_region(reg, reg.x0, reg.y0, reg.x1 + 1, reg.y1);
                        changed = true;
                        if (!reg.overused())
                            break;
                    }
                    if (reg.y0 > 0) {
                        grow_region(reg, reg.x0, reg.y0 - 1, reg.x1, reg.y1);
                        changed = true;
                        if (!reg.overused())
                            break;
                    }
                    if (reg.y1 < p->max_y) {
                        grow_region(reg, reg.x0, reg.y0, reg.x1, reg.y1 + 1);
                        changed = true;
                        if (!reg.overused())
                            break;
                    }
                    if (!changed)
                        log_error("Failed to expand region (%d, %d) |_> (%d, %d) of %d %ss\n", reg.x0, reg.y0, reg.x1,
                                  reg.y1, reg.cells, beltype.c_str(ctx));
                }
            }
        }
    };

    typedef decltype(CellInfo::udata) cell_udata_t;
    cell_udata_t dont_solve = std::numeric_limits<cell_udata_t>::max();
};

bool placer_heap(Context *ctx) { return HeAPPlacer(ctx).place(); }

NEXTPNR_NAMESPACE_END