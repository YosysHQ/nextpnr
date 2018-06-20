/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
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

#include "place_sa.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <ostream>
#include <queue>
#include <random>
#include <set>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "arch_place.h"
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

class SAPlacer
{
  public:
    SAPlacer(Context *ctx) : ctx(ctx)
    {
        checker = new PlaceValidityChecker(ctx);
        int num_bel_types = 0;
        for (auto bel : ctx->getBels()) {
            int x, y;
            bool gb;
            ctx->estimatePosition(bel, x, y, gb);
            BelType type = ctx->getBelType(bel);
            int type_idx;
            if (bel_types.find(type) == bel_types.end()) {
                type_idx = num_bel_types++;
                bel_types[type] = type_idx;
            } else {
                type_idx = bel_types.at(type);
            }
            if (int(fast_bels.size()) < type_idx + 1)
                fast_bels.resize(type_idx + 1);
            if (int(fast_bels.at(type_idx).size()) < (x + 1))
                fast_bels.at(type_idx).resize(x + 1);
            if (int(fast_bels.at(type_idx).at(x).size()) < (y + 1))
                fast_bels.at(type_idx).at(x).resize(y + 1);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
            fast_bels.at(type_idx).at(x).at(y).push_back(bel);
        }
        diameter = std::max(max_x, max_y) + 1;
    }

    bool place()
    {
        size_t placed_cells = 0;
        // Initial constraints placer
        for (auto cell_entry : ctx->cells) {
            CellInfo *cell = cell_entry.second;
            auto loc = cell->attrs.find(ctx->id("BEL"));
            if (loc != cell->attrs.end()) {
                std::string loc_name = loc->second;
                BelId bel = ctx->getBelByName(ctx->id(loc_name));
                if (bel == BelId()) {
                    log_error(
                            "No Bel named \'%s\' located for "
                            "this chip (processing BEL attribute on \'%s\')\n",
                            loc_name.c_str(), cell->name.c_str(ctx));
                }

                BelType bel_type = ctx->getBelType(bel);
                if (bel_type != ctx->belTypeFromId(cell->type)) {
                    log_error("Bel \'%s\' of type \'%s\' does not match cell "
                              "\'%s\' of type \'%s\'",
                              loc_name.c_str(),
                              ctx->belTypeToId(bel_type).c_str(ctx),
                              cell->name.c_str(ctx), cell->type.c_str(ctx));
                }

                cell->bel = bel;
                ctx->bindBel(bel, cell->name);
                locked_bels.insert(bel);
                placed_cells++;
            }
        }
        int constr_placed_cells = placed_cells;
        log_info("Placed %d cells based on constraints.\n", int(placed_cells));

        // Sort to-place cells for deterministic initial placement
        std::vector<CellInfo *> autoplaced;
        for (auto cell : ctx->cells) {
            CellInfo *ci = cell.second;
            if (ci->bel == BelId()) {
                autoplaced.push_back(cell.second);
            }
        }
        std::sort(autoplaced.begin(), autoplaced.end(),
                  [](CellInfo *a, CellInfo *b) { return a->name < b->name; });
        ctx->shuffle(autoplaced);

        // Place cells randomly initially
        log_info("Creating initial placement for remaining %d cells.\n",
                 int(autoplaced.size()));

        for (auto cell : autoplaced) {
            place_initial(cell);
            placed_cells++;
            if ((placed_cells - constr_placed_cells) % 500 == 0)
                log_info("  initial placement placed %d/%d cells\n",
                         int(placed_cells - constr_placed_cells),
                         int(autoplaced.size()));
        }
        if ((placed_cells - constr_placed_cells) % 500 != 0)
            log_info("  initial placement placed %d/%d cells\n",
                     int(placed_cells - constr_placed_cells),
                     int(autoplaced.size()));

        log_info("Running simulated annealing placer.\n");

        // Calculate wirelength after initial placement
        curr_wirelength = 0;
        for (auto net : ctx->nets) {
            float wl = get_wirelength(net.second);
            wirelengths[net.second] = wl;
            curr_wirelength += wl;
        }

        int n_no_progress = 0;
        double avg_wirelength = curr_wirelength;
        temp = 10000;

        // Main simulated annealing loop
        for (int iter = 1;; iter++) {
            n_move = n_accept = 0;
            improved = false;

            if (iter % 5 == 0 || iter == 1)
                log_info("  at iteration #%d: temp = %f, wire length = %f\n",
                         iter, temp, curr_wirelength);

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
            }
            // Heuristic to improve placement on the 8k
            if (improved)
                n_no_progress = 0;
            else
                n_no_progress++;

            if (temp <= 1e-3 && n_no_progress >= 5) {
                if (iter % 5 != 0)
                    log_info(
                            "  at iteration #%d: temp = %f, wire length = %f\n",
                            iter, temp, curr_wirelength);
                break;
            }

            double Raccept = double(n_accept) / double(n_move);

            int M = std::max(max_x, max_y) + 1;

            double upper = 0.6, lower = 0.4;

            if (curr_wirelength < 0.95 * avg_wirelength) {
                avg_wirelength = 0.8 * avg_wirelength + 0.2 * curr_wirelength;
            } else {
                if (Raccept >= 0.8) {
                    temp *= 0.7;
                } else if (Raccept > upper) {
                    if (diameter < M)
                        diameter++;
                    else
                        temp *= 0.9;
                } else if (Raccept > lower) {
                    temp *= 0.95;
                } else {
                    // Raccept < 0.3
                    if (diameter > 1)
                        diameter--;
                    else
                        temp *= 0.8;
                }
            }
        }
        // Final post-pacement validitiy check
        for (auto bel : ctx->getBels()) {
            if (!checker->isBelLocationValid(bel)) {
                std::string cell_text = "no cell";
                IdString cell = ctx->getBelCell(bel, false);
                if (cell != IdString())
                    cell_text = std::string("cell '") + cell.str(ctx) + "'";
                log_error("post-placement validity check failed for Bel '%s' "
                          "(%s)",
                          ctx->getBelName(bel).c_str(ctx), cell_text.c_str());
            }
        }
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
                cell->bel = BelId();
            }
            BelType targetType = ctx->belTypeFromId(cell->type);
            for (auto bel : ctx->getBels()) {
                if (ctx->getBelType(bel) == targetType &&
                    checker->isValidBelForCell(cell, bel)) {
                    if (ctx->checkBelAvail(bel)) {
                        uint64_t score = ctx->rng64();
                        if (score <= best_score) {
                            best_score = score;
                            best_bel = bel;
                        }
                    } else {
                        uint64_t score = ctx->rng64();
                        if (score <= best_ripup_score) {
                            best_ripup_score = score;
                            ripup_target =
                                    ctx->cells.at(ctx->getBelCell(bel, true));
                            ripup_bel = bel;
                        }
                    }
                }
            }
            if (best_bel == BelId()) {
                if (iters == 0 || ripup_bel == BelId())
                    log_error("failed to place cell '%s' of type '%s'\n",
                              cell->name.c_str(ctx), cell->type.c_str(ctx));
                --iters;
                ctx->unbindBel(ripup_target->bel);
                ripup_target->bel = BelId();
                best_bel = ripup_bel;
            } else {
                all_placed = true;
            }
            cell->bel = best_bel;
            ctx->bindBel(cell->bel, cell->name);

            // Back annotate location
            cell->attrs[ctx->id("BEL")] = ctx->getBelName(cell->bel).str(ctx);
            cell = ripup_target;
        }
    }

    // Get the total estimated wirelength for a net
    float get_wirelength(NetInfo *net)
    {
        float wirelength = 0;
        int driver_x, driver_y;
        bool driver_gb;
        CellInfo *driver_cell = net->driver.cell;
        if (!driver_cell)
            return 0;
        if (driver_cell->bel == BelId())
            return 0;
        ctx->estimatePosition(driver_cell->bel, driver_x, driver_y, driver_gb);
        WireId drv_wire = ctx->getWireBelPin(
                driver_cell->bel, ctx->portPinFromId(net->driver.port));
        if (driver_gb)
            return 0;
        for (auto load : net->users) {
            if (load.cell == nullptr)
                continue;
            CellInfo *load_cell = load.cell;
            if (load_cell->bel == BelId())
                continue;
            // ctx->estimatePosition(load_cell->bel, load_x, load_y, load_gb);
            WireId user_wire = ctx->getWireBelPin(
                    load_cell->bel, ctx->portPinFromId(load.port));
            // wirelength += std::abs(load_x - driver_x) + std::abs(load_y -
            // driver_y);
            wirelength += ctx->estimateDelay(drv_wire, user_wire);
        }
        return wirelength;
    }

    // Attempt a SA position swap, return true on success or false on failure
    bool try_swap_position(CellInfo *cell, BelId newBel)
    {
        static std::unordered_set<NetInfo *> update;
        static std::vector<std::pair<NetInfo *, float>> new_lengths;
        new_lengths.clear();
        update.clear();
        BelId oldBel = cell->bel;
        IdString other = ctx->getBelCell(newBel, true);
        CellInfo *other_cell = nullptr;
        float new_wirelength = 0, delta;
        ctx->unbindBel(oldBel);
        if (other != IdString()) {
            other_cell = ctx->cells[other];
            ctx->unbindBel(newBel);
        }

        for (const auto &port : cell->ports)
            if (port.second.net != nullptr)
                update.insert(port.second.net);

        if (other != IdString()) {
            for (const auto &port : other_cell->ports)
                if (port.second.net != nullptr)
                    update.insert(port.second.net);
        }

        ctx->bindBel(newBel, cell->name);

        if (other != IdString()) {
            ctx->bindBel(oldBel, other_cell->name);
        }

        if (!checker->isBelLocationValid(newBel) ||
            ((other != IdString() && !checker->isBelLocationValid(oldBel)))) {
            ctx->unbindBel(newBel);
            if (other != IdString())
                ctx->unbindBel(oldBel);
            goto swap_fail;
        }

        cell->bel = newBel;
        if (other != IdString())
            other_cell->bel = oldBel;

        new_wirelength = curr_wirelength;

        // Recalculate wirelengths for all nets touched by the peturbation
        for (auto net : update) {
            new_wirelength -= wirelengths.at(net);
            float net_new_wl = get_wirelength(net);
            new_wirelength += net_new_wl;
            new_lengths.push_back(std::make_pair(net, net_new_wl));
        }
        delta = new_wirelength - curr_wirelength;
        n_move++;
        // SA acceptance criterea
        if (delta < 0 ||
            (temp > 1e-6 &&
             (ctx->rng() / float(0x3fffffff)) <= std::exp(-delta / temp))) {
            n_accept++;
            if (delta < 0)
                improved = true;
        } else {
            if (other != IdString())
                ctx->unbindBel(oldBel);
            ctx->unbindBel(newBel);
            goto swap_fail;
        }
        curr_wirelength = new_wirelength;
        for (auto new_wl : new_lengths)
            wirelengths.at(new_wl.first) = new_wl.second;

        return true;
    swap_fail:
        ctx->bindBel(oldBel, cell->name);
        cell->bel = oldBel;
        if (other != IdString()) {
            ctx->bindBel(newBel, other);
            other_cell->bel = newBel;
        }
        return false;
    }

    // Find a random Bel of the correct type for a cell, within the specified
    // diameter
    BelId random_bel_for_cell(CellInfo *cell)
    {
        BelType targetType = ctx->belTypeFromId(cell->type);
        int x, y;
        bool gb;
        ctx->estimatePosition(cell->bel, x, y, gb);
        while (true) {
            int nx = ctx->rng(2 * diameter + 1) + std::max(x - diameter, 0);
            int ny = ctx->rng(2 * diameter + 1) + std::max(y - diameter, 0);
            int beltype_idx = bel_types.at(targetType);
            if (nx >= int(fast_bels.at(beltype_idx).size()))
                continue;
            if (ny >= int(fast_bels.at(beltype_idx).at(nx).size()))
                continue;
            const auto &fb = fast_bels.at(beltype_idx).at(nx).at(ny);
            if (fb.size() == 0)
                continue;
            BelId bel = fb.at(ctx->rng(int(fb.size())));
            if (locked_bels.find(bel) != locked_bels.end())
                continue;
            return bel;
        }
    }

    Context *ctx;
    std::unordered_map<NetInfo *, float> wirelengths;
    float curr_wirelength = std::numeric_limits<float>::infinity();
    float temp = 1000;
    bool improved = false;
    int n_move, n_accept;
    int diameter = 35, max_x = 1, max_y = 1;
    std::unordered_map<BelType, int> bel_types;
    std::vector<std::vector<std::vector<std::vector<BelId>>>> fast_bels;
    std::unordered_set<BelId> locked_bels;
    PlaceValidityChecker *checker;
};

bool place_design_sa(Context *ctx)
{
    SAPlacer placer(ctx);
    placer.place();
    return true;
}

NEXTPNR_NAMESPACE_END
