/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
 *
 *  Simulated annealing implementation based on arachne-pnr
 *  Copyright (C) 2015-2018 Cotton Seed
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

#include "placer1.h"
#include <algorithm>
#include <boost/lexical_cast.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <ostream>
#include <queue>
#include <set>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "log.h"
#include "place_common.h"
#include "timing.h"
#include "util.h"


namespace std {
template <> struct hash<std::pair<NEXTPNR_NAMESPACE_PREFIX IdString, std::size_t>>
{
    std::size_t operator()(const std::pair<NEXTPNR_NAMESPACE_PREFIX IdString, std::size_t> &idp) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, hash<NEXTPNR_NAMESPACE_PREFIX IdString>()(idp.first));
        boost::hash_combine(seed, hash<std::size_t>()(idp.second));
        return seed;
    }
};
} // namespace std

NEXTPNR_NAMESPACE_BEGIN

class SAPlacer
{
  private:
    struct BoundingBox
    {
        int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        bool is_inside_inc(int x, int y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
        bool touches_bounds(int x, int y) const { return x == x0 || x == x1 || y == y0 || y == y1; }
        wirelen_t hpwl() const { return wirelen_t((x1 - x0) + (y1 - y0)); }
    };

  public:
    SAPlacer(Context *ctx, Placer1Cfg cfg) : ctx(ctx), cfg(cfg)
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
            Loc loc = ctx->getBelLocation(bel);
            IdString type = ctx->getBelType(bel);
            int type_idx = std::get<0>(bel_types.at(type));
            int type_cnt = std::get<1>(bel_types.at(type));
            if (type_cnt < cfg.minBelsForGridPick)
                loc.x = loc.y = 0;
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
        diameter = std::max(max_x, max_y) + 1;

        build_port_index();
    }

    ~SAPlacer() {}

    bool place()
    {
        log_break();
        ctx->lock();

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
                locked_bels.insert(bel);
                placed_cells++;
            }
        }
        int constr_placed_cells = placed_cells;
        log_info("Placed %d cells based on constraints.\n", int(placed_cells));
        ctx->yield();

        // Sort to-place cells for deterministic initial placement
        std::vector<CellInfo *> autoplaced;
        std::vector<CellInfo *> chain_basis;

        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->bel == BelId()) {
                autoplaced.push_back(cell.second.get());
            }
        }
        std::sort(autoplaced.begin(), autoplaced.end(), [](CellInfo *a, CellInfo *b) { return a->name < b->name; });
        ctx->shuffle(autoplaced);

        // Place cells randomly initially
        log_info("Creating initial placement for remaining %d cells.\n", int(autoplaced.size()));

        for (auto cell : autoplaced) {
            place_initial(cell);
            placed_cells++;
            if ((placed_cells - constr_placed_cells) % 500 == 0)
                log_info("  initial placement placed %d/%d cells\n", int(placed_cells - constr_placed_cells),
                         int(autoplaced.size()));
        }
        if ((placed_cells - constr_placed_cells) % 500 != 0)
            log_info("  initial placement placed %d/%d cells\n", int(placed_cells - constr_placed_cells),
                     int(autoplaced.size()));
        if (cfg.budgetBased && ctx->slack_redist_iter > 0)
            assign_budget(ctx);
        ctx->yield();

        log_info("Running simulated annealing placer.\n");

        // Invoke timing analysis to obtain criticalities
        if (!cfg.budgetBased)
            get_criticalities(ctx, &net_crit);

        // Calculate costs after initial placement
        setup_costs();
        curr_wirelen_cost = total_wirelen_cost();
        curr_timing_cost = total_timing_cost();
        last_wirelen_cost = curr_wirelen_cost;
        last_timing_cost = curr_timing_cost;

        wirelen_t avg_wirelen = curr_wirelen_cost;
        wirelen_t min_wirelen = curr_wirelen_cost;

        int n_no_progress = 0;
        temp = cfg.startTemp;

        // Main simulated annealing loop
        for (int iter = 1;; iter++) {
            n_move = n_accept = 0;
            improved = false;

            if (iter % 5 == 0 || iter == 1)
                log_info("  at iteration #%d: temp = %f, timing cost = "
                         "%.0f, wirelen = %.0f\n",
                         iter, temp, double(curr_timing_cost), double(curr_wirelen_cost));

            for (int m = 0; m < 15; ++m) {
                // Loop through all automatically placed cells
                for (auto cell : autoplaced) {
                    // Find another random Bel for this cell
                    BelId try_bel = random_bel_for_cell(cell);
                    // If valid, try and swap to a new position and see if
                    // the new position is valid/worthwhile
                    if (try_bel != BelId() && try_bel != cell->bel)
                        try_swap_position(cell, try_bel);
                }
                // Also try swapping chains, if applicable
                for (auto cb : chain_basis) {
                    Loc chain_base_loc = ctx->getBelLocation(cb->bel);
                    BelId try_base = random_bel_for_cell(cb, chain_base_loc.z);
                    if (try_base != BelId() && try_base != cb->bel)
                        try_swap_chain(cb, try_base);
                }
            }

            if (curr_wirelen_cost < min_wirelen) {
                min_wirelen = curr_wirelen_cost;
                improved = true;
            }

            // Heuristic to improve placement on the 8k
            if (improved)
                n_no_progress = 0;
            else
                n_no_progress++;

            if (temp <= 1e-7 && n_no_progress >= 5) {
                log_info("  at iteration #%d: temp = %f, timing cost = "
                         "%.0f, wirelen = %.0f \n",
                         iter, temp, double(curr_timing_cost), double(curr_wirelen_cost));
                break;
            }

            double Raccept = double(n_accept) / double(n_move);

            int M = std::max(max_x, max_y) + 1;

            if (ctx->verbose)
                log("iter #%d: temp = %f, timing cost = "
                         "%.0f, wirelen = %.0f, dia = %d, Ra = %.02f \n",
                         iter, temp, double(curr_timing_cost), double(curr_wirelen_cost), diameter, Raccept);

            if (curr_wirelen_cost < 0.95 * avg_wirelen) {
                avg_wirelen = 0.8 * avg_wirelen + 0.2 * curr_wirelen_cost;
            } else {
                double diam_next = diameter * (1.0 - 0.44 + Raccept);
                diameter = std::max<int>(1, std::min<int>(M, int(diam_next + 0.5)));
                if (Raccept > 0.96) {
                    temp *= 0.5;
                } else if (Raccept > 0.8) {
                    temp *= 0.9;
                } else if (Raccept > 0.15 && diameter > 1) {
                    temp *= 0.95;
                } else {
                    temp *= 0.8;
                }
            }
            // Once cooled below legalise threshold, run legalisation and start requiring
            // legal moves only
            if (temp < legalise_temp && require_legal) {
                if (legalise_relative_constraints(ctx)) {
                    // Only increase temperature if something was moved
                    autoplaced.clear();
                    chain_basis.clear();
                    for (auto cell : sorted(ctx->cells)) {
                        if (cell.second->belStrength <= STRENGTH_STRONG && cell.second->constr_parent == nullptr && !cell.second->constr_children.empty())
                            chain_basis.push_back(cell.second);
                        else if (cell.second->belStrength < STRENGTH_STRONG)
                            autoplaced.push_back(cell.second);
                    }
                    temp = post_legalise_temp;
                    diameter = std::min<int>(M, diameter * post_legalise_dia_scale);
                    ctx->shuffle(autoplaced);

                    // Legalisation is a big change so force a slack redistribution here
                    if (ctx->slack_redist_iter > 0 && cfg.budgetBased)
                        assign_budget(ctx, true /* quiet */);
                }
                require_legal = false;
            } else if (cfg.budgetBased && ctx->slack_redist_iter > 0 && iter % ctx->slack_redist_iter == 0) {
                assign_budget(ctx, true /* quiet */);
            }

            // Invoke timing analysis to obtain criticalities
            if (!cfg.budgetBased)
                get_criticalities(ctx, &net_crit);
            // Need to rebuild costs after criticalities change
            setup_costs();
            // Recalculate total metric entirely to avoid rounding errors
            // accumulating over time
            curr_wirelen_cost = total_wirelen_cost();
            curr_timing_cost = total_timing_cost();
            last_wirelen_cost = curr_wirelen_cost;
            last_timing_cost = curr_timing_cost;
            // Let the UI show visualization updates.
            ctx->yield();
        }
        // Final post-pacement validitiy check
        ctx->yield();
        for (auto bel : ctx->getBels()) {
            CellInfo *cell = ctx->getBoundBelCell(bel);
            if (!ctx->isBelLocationValid(bel)) {
                std::string cell_text = "no cell";
                if (cell != nullptr)
                    cell_text = std::string("cell '") + ctx->nameOf(cell) + "'";
                if (ctx->force) {
                    log_warning("post-placement validity check failed for Bel '%s' "
                                "(%s)\n",
                                ctx->getBelName(bel).c_str(ctx), cell_text.c_str());
                } else {
                    log_error("post-placement validity check failed for Bel '%s' "
                              "(%s)\n",
                              ctx->getBelName(bel).c_str(ctx), cell_text.c_str());
                }
            }
        }
        for (auto cell : sorted(ctx->cells))
            if (get_constraints_distance(ctx, cell.second) != 0)
                log_error("constraint satisfaction check failed for cell '%s' at Bel '%s'\n", cell.first.c_str(ctx),
                          ctx->getBelName(cell.second->bel).c_str(ctx));
        timing_analysis(ctx);
        ctx->unlock();
        return true;
    }

  private:
    // Initial random placement
    void place_initial(CellInfo *cell)
    {
        bool all_placed = false;
        int iters = 25;
        while (!all_placed) {
            BelId best_bel = BelId();
            uint64_t best_score = std::numeric_limits<uint64_t>::max(),
                     best_ripup_score = std::numeric_limits<uint64_t>::max();
            CellInfo *ripup_target = nullptr;
            BelId ripup_bel = BelId();
            if (cell->bel != BelId()) {
                ctx->unbindBel(cell->bel);
            }
            IdString targetType = cell->type;
            for (auto bel : ctx->getBels()) {
                if (ctx->getBelType(bel) == targetType && ctx->isValidBelForCell(cell, bel)) {
                    if (ctx->checkBelAvail(bel)) {
                        uint64_t score = ctx->rng64();
                        if (score <= best_score) {
                            best_score = score;
                            best_bel = bel;
                        }
                    } else {
                        uint64_t score = ctx->rng64();
                        CellInfo *bound_cell = ctx->getBoundBelCell(bel);
                        if (score <= best_ripup_score && bound_cell->belStrength < STRENGTH_STRONG) {
                            best_ripup_score = score;
                            ripup_target = bound_cell;
                            ripup_bel = bel;
                        }
                    }
                }
            }
            if (best_bel == BelId()) {
                if (iters == 0 || ripup_bel == BelId())
                    log_error("failed to place cell '%s' of type '%s'\n", cell->name.c_str(ctx), cell->type.c_str(ctx));
                --iters;
                ctx->unbindBel(ripup_target->bel);
                best_bel = ripup_bel;
            } else {
                all_placed = true;
            }
            ctx->bindBel(best_bel, cell, STRENGTH_WEAK);

            // Back annotate location
            cell->attrs[ctx->id("BEL")] = ctx->getBelName(cell->bel).str(ctx);
            cell = ripup_target;
        }
    }

    // Attempt a SA position swap, return true on success or false on failure
    bool try_swap_position(CellInfo *cell, BelId newBel)
    {
        moveChange.reset();
        BelId oldBel = cell->bel;
        CellInfo *other_cell = ctx->getBoundBelCell(newBel);
        if (other_cell != nullptr && other_cell->belStrength > STRENGTH_WEAK) {
            return false;
        }
        int old_dist = get_constraints_distance(ctx, cell);
        int new_dist;
        if (other_cell != nullptr)
            old_dist += get_constraints_distance(ctx, other_cell);
        double delta = 0;
        ctx->unbindBel(oldBel);
        if (other_cell != nullptr) {
            ctx->unbindBel(newBel);
        }

        ctx->bindBel(newBel, cell, STRENGTH_WEAK);

        if (other_cell != nullptr) {
            ctx->bindBel(oldBel, other_cell, STRENGTH_WEAK);
        }

        add_move_cell(moveChange, cell, oldBel);

        if (other_cell != nullptr) {
            add_move_cell(moveChange, other_cell, newBel);
        }

        if (!ctx->isBelLocationValid(newBel) || ((other_cell != nullptr && !ctx->isBelLocationValid(oldBel)))) {
            ctx->unbindBel(newBel);
            if (other_cell != nullptr)
                ctx->unbindBel(oldBel);
            goto swap_fail;
        }

        // Recalculate metrics for all nets touched by the peturbation
        compute_cost_changes(moveChange);

        new_dist = get_constraints_distance(ctx, cell);
        if (other_cell != nullptr)
            new_dist += get_constraints_distance(ctx, other_cell);
        delta = lambda * (moveChange.timing_delta / last_timing_cost) +
                (1 - lambda) * (double(moveChange.wirelen_delta) / last_wirelen_cost);
        delta += (cfg.constraintWeight / temp) * (new_dist - old_dist) / last_wirelen_cost;
        n_move++;
        // SA acceptance criterea
        if (delta < 0 || (temp > 1e-8 && (ctx->rng() / float(0x3fffffff)) <= std::exp(-delta / temp))) {
            n_accept++;
        } else {
            if (other_cell != nullptr)
                ctx->unbindBel(oldBel);
            ctx->unbindBel(newBel);
            goto swap_fail;
        }
        commit_cost_changes(moveChange);
        return true;
    swap_fail:
        ctx->bindBel(oldBel, cell, STRENGTH_WEAK);
        if (other_cell != nullptr) {
            ctx->bindBel(newBel, other_cell, STRENGTH_WEAK);
        }
        return false;
    }

    inline bool is_constrained(CellInfo *cell) {
        return cell->constr_parent != nullptr || !cell->constr_children.empty();
    }

    // Swap the Bel of a cell with another, return the original location
    BelId swap_cell_bels(CellInfo *cell, BelId newBel) {
        BelId oldBel = cell->bel;
        CellInfo *bound = ctx->getBoundBelCell(newBel);
        if (bound != nullptr)
            ctx->unbindBel(newBel);
        ctx->unbindBel(oldBel);
        ctx->bindBel(newBel, cell, is_constrained(cell) ? STRENGTH_STRONG : STRENGTH_WEAK);
        if (bound != nullptr)
            ctx->bindBel(oldBel, bound, is_constrained(bound) ? STRENGTH_STRONG : STRENGTH_WEAK);
        return oldBel;
    }

    // Discover the relative positions of all cells in a chain
    void discover_chain(Loc baseLoc, CellInfo *cell, std::vector<std::pair<CellInfo*, Loc>> &cell_rel) {
        Loc cellLoc = ctx->getBelLocation(cell->bel);
        Loc rel{cellLoc.x - baseLoc.x, cellLoc.y - baseLoc.y, cellLoc.z};
        cell_rel.emplace_back(std::make_pair(cell, rel));
        for (auto child : cell->constr_children)
            discover_chain(baseLoc, child, cell_rel);
    }

    // Attempt to swap a chain with a non-chain
    bool try_swap_chain(CellInfo *cell, BelId newBase) {
        std::vector<std::pair<CellInfo *, Loc>> cell_rel;
        std::unordered_set<IdString> cells;
        std::vector<std::pair<CellInfo*, BelId>> moves_made;
        std::vector<std::pair<CellInfo *, BelId>> dest_bels;
        double delta = 0;
        moveChange.reset();
        if (ctx->debug)
            log_info("finding cells for chain swap %s\n", cell->name.c_str(ctx));

        Loc baseLoc = ctx->getBelLocation(cell->bel);
        discover_chain(baseLoc, cell, cell_rel);
        Loc newBaseLoc = ctx->getBelLocation(newBase);
        NPNR_ASSERT(newBaseLoc.z == baseLoc.z);
        for (const auto &cr : cell_rel)
            cells.insert(cr.first->name);

        for (const auto &cr : cell_rel) {
            Loc targetLoc = {newBaseLoc.x + cr.second.x, newBaseLoc.y + cr.second.y, cr.second.z};
            BelId targetBel = ctx->getBelByLocation(targetLoc);
            if (targetBel == BelId())
                return false;
            if (ctx->getBelType(targetBel) != cell->type)
                return false;
            CellInfo *bound = ctx->getBoundBelCell(targetBel);
            // We don't consider swapping chains with other chains, at least for the time being - unless it is
            // part of this chain
            if (bound != nullptr && !cells.count(bound->name) && (bound->belStrength >= STRENGTH_STRONG || is_constrained(bound)))
                return false;
            dest_bels.emplace_back(std::make_pair(cr.first, targetBel));
        }
        if (ctx->debug)
            log_info("trying chain swap %s\n", cell->name.c_str(ctx));
        // <cell, oldBel>
        for (const auto &db : dest_bels) {
            BelId oldBel = swap_cell_bels(db.first, db.second);
            moves_made.emplace_back(std::make_pair(db.first, oldBel));
        }
        for (const auto &mm : moves_made) {
            if (!ctx->isBelLocationValid(mm.first->bel))
                goto swap_fail;
            if (!ctx->isBelLocationValid(mm.second))
                goto swap_fail;
            add_move_cell(moveChange, mm.first, mm.second);
            CellInfo *bound = ctx->getBoundBelCell(mm.second);
            if (bound != nullptr)
                add_move_cell(moveChange, bound, mm.first->bel);
        }
        compute_cost_changes(moveChange);
        delta = lambda * (moveChange.timing_delta / last_timing_cost) +
                (1 - lambda) * (double(moveChange.wirelen_delta) / last_wirelen_cost);
        n_move++;
        // SA acceptance criterea
        if (delta < 0 || (temp > 1e-9 && (ctx->rng() / float(0x3fffffff)) <= std::exp(-delta / temp))) {
            n_accept++;
            if (ctx->debug)
                log_info("accepted chain swap %s\n", cell->name.c_str(ctx));
        } else {
            goto swap_fail;
        }
        commit_cost_changes(moveChange);
        return true;
swap_fail:
        for (const auto &entry : boost::adaptors::reverse(moves_made))
            swap_cell_bels(entry.first, entry.second);
        return false;
    }

    // Find a random Bel of the correct type for a cell, within the specified
    // diameter
    BelId random_bel_for_cell(CellInfo *cell, int force_z = -1)
    {
        IdString targetType = cell->type;
        Loc curr_loc = ctx->getBelLocation(cell->bel);
        while (true) {
            int nx = ctx->rng(2 * diameter + 1) + std::max(curr_loc.x - diameter, 0);
            int ny = ctx->rng(2 * diameter + 1) + std::max(curr_loc.y - diameter, 0);
            int beltype_idx, beltype_cnt;
            std::tie(beltype_idx, beltype_cnt) = bel_types.at(targetType);
            if (beltype_cnt < cfg.minBelsForGridPick)
                nx = ny = 0;
            if (nx >= int(fast_bels.at(beltype_idx).size()))
                continue;
            if (ny >= int(fast_bels.at(beltype_idx).at(nx).size()))
                continue;
            const auto &fb = fast_bels.at(beltype_idx).at(nx).at(ny);
            if (fb.size() == 0)
                continue;
            BelId bel = fb.at(ctx->rng(int(fb.size())));
            if (force_z != -1) {
                Loc loc = ctx->getBelLocation(bel);
                if (loc.z != force_z)
                    continue;
            }
            if (locked_bels.find(bel) != locked_bels.end())
                continue;
            return bel;
        }
    }

    // Return true if a net is to be entirely ignored
    inline bool ignore_net(NetInfo *net)
    {
        return net->driver.cell == nullptr || net->driver.cell->bel == BelId() ||
               ctx->getBelGlobalBuf(net->driver.cell->bel);
    }

    // Get the bounding box for a net
    inline BoundingBox get_net_bounds(NetInfo *net)
    {
        BoundingBox bb;
        NPNR_ASSERT(net->driver.cell != nullptr);
        Loc dloc = ctx->getBelLocation(net->driver.cell->bel);
        bb.x0 = dloc.x;
        bb.x1 = dloc.x;
        bb.y0 = dloc.y;
        bb.y1 = dloc.y;

        for (auto user : net->users) {
            if (user.cell->bel == BelId())
                continue;
            Loc uloc = ctx->getBelLocation(user.cell->bel);
            bb.x0 = std::min(bb.x0, uloc.x);
            bb.x1 = std::max(bb.x1, uloc.x);
            bb.y0 = std::min(bb.y0, uloc.y);
            bb.y1 = std::max(bb.y1, uloc.y);
        }

        return bb;
    }

    // Get the timing cost for an arc of a net
    inline double get_timing_cost(NetInfo *net, size_t user)
    {
        int cc;
        if (net->driver.cell == nullptr)
            return 0;
        if (ctx->getPortTimingClass(net->driver.cell, net->driver.port, cc) == TMG_IGNORE)
            return 0;
        if (cfg.budgetBased) {
            double delay = ctx->getDelayNS(ctx->predictDelay(net, net->users.at(user)));
            return std::min(10.0, std::exp(delay - ctx->getDelayNS(net->users.at(user).budget)));
        } else {
            auto crit = net_crit.find(net->name);
            if (crit == net_crit.end() || crit->second.criticality.empty())
                return 0;
            double delay = ctx->getDelayNS(ctx->predictDelay(net, net->users.at(user)));
            return delay * std::pow(crit->second.criticality.at(user), crit_exp);
        }
    }

    // Set up the cost maps
    void setup_costs()
    {
        for (auto net : sorted(ctx->nets)) {
            NetInfo *ni = net.second;
            if (ignore_net(ni))
                continue;
            net_bounds[ni->name] = get_net_bounds(ni);
            net_arc_tcost[ni->name].resize(ni->users.size());
            for (size_t i = 0; i < ni->users.size(); i++)
                net_arc_tcost[ni->name][i] = get_timing_cost(ni, i);
        }
    }

    // Get the total wiring cost for the design
    wirelen_t total_wirelen_cost()
    {
        wirelen_t cost = 0;
        for (const auto &net : net_bounds)
            cost += net.second.hpwl();
        return cost;
    }

    // Get the total timing cost for the design
    double total_timing_cost()
    {
        double cost = 0;
        for (const auto &net : net_arc_tcost) {
            for (auto arc_cost : net.second) {
                cost += arc_cost;
            }
        }
        return cost;
    }

    // Cost-change-related data for a move
    struct MoveChangeData
    {
        std::unordered_set<IdString> bounds_changed_nets;
        std::unordered_set<std::pair<IdString, size_t>> changed_arcs;

        std::unordered_map<IdString, BoundingBox> new_net_bounds;
        std::unordered_map<std::pair<IdString, size_t>, double> new_arc_costs;

        wirelen_t wirelen_delta = 0;
        double timing_delta = 0;

        void reset()
        {
            bounds_changed_nets.clear();
            changed_arcs.clear();
            new_net_bounds.clear();
            new_arc_costs.clear();
            wirelen_delta = 0;
            timing_delta = 0;
        }

    } moveChange;

    void add_move_cell(MoveChangeData &mc, CellInfo *cell, BelId old_bel)
    {
        Loc curr_loc = ctx->getBelLocation(cell->bel);
        Loc old_loc = ctx->getBelLocation(old_bel);
        // Check net bounds
        for (const auto &port : cell->ports) {
            NetInfo *pn = port.second.net;
            if (pn == nullptr)
                continue;
            if (ignore_net(pn))
                continue;
            const BoundingBox &curr_bounds = net_bounds[pn->name];
            // If the old location was at the edge of the bounds, or the new location exceeds the bounds,
            // an update is needed
            if (curr_bounds.touches_bounds(old_loc.x, old_loc.y) || !curr_bounds.is_inside_inc(curr_loc.x, curr_loc.y))
                mc.bounds_changed_nets.insert(pn->name);
            // Output ports - all arcs change timing
            if (port.second.type == PORT_OUT) {
                int cc;
                TimingPortClass cls = ctx->getPortTimingClass(cell, port.first, cc);
                if (cls != TMG_IGNORE)
                    for (size_t i = 0; i < pn->users.size(); i++)
                        mc.changed_arcs.insert(std::make_pair(pn->name, i));
            } else if (port.second.type == PORT_IN) {
                mc.changed_arcs.insert(std::make_pair(pn->name, fast_port_to_user.at(&port.second)));
            }
        }
    }

    void compute_cost_changes(MoveChangeData &md)
    {
        for (const auto &bc : md.bounds_changed_nets) {
            wirelen_t old_hpwl = net_bounds.at(bc).hpwl();
            auto bounds = get_net_bounds(ctx->nets.at(bc).get());
            md.new_net_bounds[bc] = bounds;
            md.wirelen_delta += (bounds.hpwl() - old_hpwl);
        }

        for (const auto &tc : md.changed_arcs) {
            double old_cost = net_arc_tcost.at(tc.first).at(tc.second);
            double new_cost = get_timing_cost(ctx->nets.at(tc.first).get(), tc.second);
            md.new_arc_costs[tc] = new_cost;
            md.timing_delta += (new_cost - old_cost);
        }
    }

    void commit_cost_changes(MoveChangeData &md)
    {
        for (const auto &bc : md.new_net_bounds)
            net_bounds[bc.first] = bc.second;
        for (const auto &tc : md.new_arc_costs)
            net_arc_tcost[tc.first.first].at(tc.first.second) = tc.second;
        curr_wirelen_cost += md.wirelen_delta;
        curr_timing_cost += md.timing_delta;
    }
    // Build the cell port -> user index
    void build_port_index()
    {
        for (auto net : sorted(ctx->nets)) {
            NetInfo *ni = net.second;
            for (size_t i = 0; i < ni->users.size(); i++) {
                auto &usr = ni->users.at(i);
                fast_port_to_user[&(usr.cell->ports.at(usr.port))] = i;
            }
        }
    }

    // Get the combined wirelen/timing metric
    inline double curr_metric() { return lambda * curr_timing_cost + (1 - lambda) * curr_wirelen_cost; }

    // Map nets to their bounding box (so we can skip recompute for moves that do not exceed the bounds
    std::unordered_map<IdString, BoundingBox> net_bounds;
    // Map net arcs to their timing cost (criticality * delay ns)
    std::unordered_map<IdString, std::vector<double>> net_arc_tcost;

    // Fast lookup for cell port to net user index
    std::unordered_map<const PortInfo *, size_t> fast_port_to_user;

    // Wirelength and timing cost at last and current iteration
    wirelen_t last_wirelen_cost, curr_wirelen_cost;
    double last_timing_cost, curr_timing_cost;

    // Criticality data from timing analysis
    NetCriticalityMap net_crit;

    Context *ctx;
    float temp = 10;
    float crit_exp = 8;
    float lambda = 0.5;
    bool improved = false;
    int n_move, n_accept;
    int diameter = 35, max_x = 1, max_y = 1;
    std::unordered_map<IdString, std::tuple<int, int>> bel_types;
    std::vector<std::vector<std::vector<std::vector<BelId>>>> fast_bels;
    std::unordered_set<BelId> locked_bels;
    bool require_legal = true;
    const float legalise_temp = 0.001;
    const float post_legalise_temp = 0.002;
    const float post_legalise_dia_scale = 1.5;
    Placer1Cfg cfg;
};

Placer1Cfg::Placer1Cfg(Context *ctx) : Settings(ctx)
{
    constraintWeight = get<float>("placer1/constraintWeight", 10);
    minBelsForGridPick = get<int>("placer1/minBelsForGridPick", 64);
    budgetBased = get<bool>("placer1/budgetBased", false);
    startTemp = get<float> ("placer1/startTemp", 1);
}

bool placer1(Context *ctx, Placer1Cfg cfg)
{
    try {
        SAPlacer placer(ctx, cfg);
        placer.place();
        log_info("Checksum: 0x%08x\n", ctx->checksum());
#ifndef NDEBUG
        ctx->lock();
        ctx->check();
        ctx->unlock();
#endif
        return true;
    } catch (log_execution_error_exception) {
#ifndef NDEBUG
        ctx->check();
#endif
        return false;
    }
}

NEXTPNR_NAMESPACE_END
