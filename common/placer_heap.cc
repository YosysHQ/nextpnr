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

            EquationSystem<double> esx(place_cells.size(), place_cells.size());
            build_equations(esx, false);
            // log_info("x-axis\n");
            solve_equations(esx, false);

            EquationSystem<double> esy(place_cells.size(), place_cells.size());
            build_equations(esy, true);
            // log_info("y-axis\n");
            solve_equations(esy, true);

            update_all_chains();

            hpwl = total_hpwl();
            log_info("Initial placer iter %d, hpwl = %d\n", i, int(hpwl));
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
    std::unordered_map<IdString, CellInfo*> chain_root;

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
                if (nc.at(x) == -1 || std::abs(loc.x - nc.at(x)) <= (x - loc.x))
                    break;
                nc.at(x) = loc.x;
            }
            for (int x = loc.x - 1; x >= 0; x--) {
                if (nc.at(x) == -1 || std::abs(loc.x - nc.at(x)) <= (loc.x - x))
                    break;
                nc.at(x) = loc.x;
            }
            for (int y = loc.y; y <= max_y; y++) {
                if (nr.at(y) == -1 || std::abs(loc.y - nr.at(y)) <= (y - loc.y))
                    break;
                nr.at(y) = loc.y;
            }
            for (int y = loc.y - 1; y >= 0; y--) {
                if (nr.at(y) == -1 || std::abs(loc.y - nr.at(y)) <= (loc.y - y))
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
    int setup_solve_cells(std::unordered_set<IdString> *celltypes = nullptr) {
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
    void build_equations(EquationSystem<double> &es, bool yaxis)
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
                    es.add_rhs(row, -(yaxis ? cell_offsets.at(var.cell->name).second : cell_offsets.at(var.cell->name).first) * weight);
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

/*
                    if (port.cell->udata != -1) {
                        es.add_coeff(port.cell->udata, port.cell->udata, weight);
                        if (!cell_locs.at(other->cell->name).locked)
                            es.add_coeff(other->cell->udata, port.cell->udata, -weight);
                    } else {
                        // Add our fixed position to the other end's RHS
                        if (!cell_locs.at(other->cell->name).locked)
                            es.add_rhs(other->cell->udata, this_pos * weight);
                    }
                    // Opposite for the other end of the connection
                    if (!cell_locs.at(other->cell->name).locked) {
                        es.add_coeff(other->cell->udata, other->cell->udata, weight);
                        if (!cell_locs.at(port.cell->name).locked)
                            es.add_coeff(port.cell->udata, other->cell->udata, -weight);
                    } else {
                        // Add our fixed position to the other end's RHS
                        if (!cell_locs.at(port.cell->name).locked)
                            es.add_rhs(port.cell->udata, this_pos * weight);
                    }
*/
                };
                process_arc(lbport);
                process_arc(ubport);
            });
        }
    }

    // Build the system of equations for either X or Y
    void solve_equations(EquationSystem<double> &es, bool yaxis)
    {
        // Return the x or y position of a cell, depending on ydir
        auto cell_pos = [&](CellInfo *cell) { return yaxis ? cell_locs.at(cell->name).y : cell_locs.at(cell->name).x; };
        build_equations(es, yaxis);
        std::vector<double> vals;
        std::transform(place_cells.begin(), place_cells.end(), std::back_inserter(vals), cell_pos);
        es.solve(vals);
        for (size_t i = 0; i < vals.size(); i++)
            if (yaxis)
                cell_locs.at(place_cells.at(i)->name).y = int(vals.at(i) + 0.5);
            else
                cell_locs.at(place_cells.at(i)->name).x = int(vals.at(i) + 0.5);
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

    typedef decltype(CellInfo::udata) cell_udata_t;
    cell_udata_t dont_solve = std::numeric_limits<cell_udata_t>::max();
};

bool placer_heap(Context *ctx) { return HeAPPlacer(ctx).place(); }

NEXTPNR_NAMESPACE_END