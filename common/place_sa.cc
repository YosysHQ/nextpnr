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

struct rnd_state
{
    uint32_t state;
};

/* The state word must be initialized to non-zero */
static uint32_t xorshift32(rnd_state &rnd)
{
    /* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
    uint32_t x = rnd.state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rnd.state = x;
    return x;
}

static float random_float_upto(rnd_state &rnd, float limit)
{
    return xorshift32(rnd) / (4294967296 / limit);
}

static int random_int_between(rnd_state &rnd, int a, int b)
{
    return a + int(random_float_upto(rnd, b - a) - 0.00001);
}

// Initial random placement
static void place_initial(Context *ctx, CellInfo *cell, rnd_state &rnd)
{
    bool all_placed = false;
    int iters = 25;
    while (!all_placed) {
        BelId best_bel = BelId();
        float best_score = std::numeric_limits<float>::infinity(),
              best_ripup_score = std::numeric_limits<float>::infinity();
        CellInfo *ripup_target = nullptr;
        BelId ripup_bel = BelId();
        if (cell->bel != BelId()) {
            ctx->unbindBel(cell->bel);
            cell->bel = BelId();
        }
        BelType targetType = ctx->belTypeFromId(cell->type);
        for (auto bel : ctx->getBels()) {
            if (ctx->getBelType(bel) == targetType &&
                isValidBelForCell(ctx, cell, bel)) {
                if (ctx->checkBelAvail(bel)) {
                    float score = random_float_upto(rnd, 1.0);
                    if (score <= best_score) {
                        best_score = score;
                        best_bel = bel;
                    }
                } else {
                    float score = random_float_upto(rnd, 1.0);
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

// Stores the state of the SA placer
struct SAState
{
    std::unordered_map<NetInfo *, float> wirelengths;
    float curr_wirelength = std::numeric_limits<float>::infinity();
    float temp = 1000;
    bool improved = false;
    int n_move, n_accept;
    int diameter = 35;
    std::unordered_map<BelType, int> bel_types;
    std::vector<std::vector<std::vector<std::vector<BelId>>>> fast_bels;
    std::unordered_set<BelId> locked_bels;
};

// Get the total estimated wirelength for a net
static float get_wirelength(Context *ctx, NetInfo *net)
{
    float wirelength = 0;
    int driver_x = 0, driver_y = 0;
    bool consider_driver = false;
    CellInfo *driver_cell = net->driver.cell;
    if (!driver_cell)
        return 0;
    if (driver_cell->bel == BelId())
        return 0;
    consider_driver =
            ctx->estimatePosition(driver_cell->bel, driver_x, driver_y);
    WireId drv_wire = ctx->getWireBelPin(driver_cell->bel,
                                         ctx->portPinFromId(net->driver.port));
    if (!consider_driver)
        return 0;
    for (auto load : net->users) {
        if (load.cell == nullptr)
            continue;
        CellInfo *load_cell = load.cell;
        if (load_cell->bel == BelId())
            continue;
        // ctx->estimatePosition(load_cell->bel, load_x, load_y);
        WireId user_wire = ctx->getWireBelPin(load_cell->bel,
                                              ctx->portPinFromId(load.port));
        // wirelength += std::abs(load_x - driver_x) + std::abs(load_y -
        // driver_y);
        wirelength += ctx->estimateDelay(drv_wire, user_wire);
    }
    return wirelength;
}

// Attempt a SA position swap, return true on success or false on failure
static bool try_swap_position(Context *ctx, CellInfo *cell, BelId newBel,
                              rnd_state &rnd, SAState &state)
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

    if (!isBelLocationValid(ctx, newBel) ||
        ((other != IdString() && !isBelLocationValid(ctx, oldBel)))) {
        ctx->unbindBel(newBel);
        if (other != IdString())
            ctx->unbindBel(oldBel);
        goto swap_fail;
    }

    cell->bel = newBel;
    if (other != IdString())
        other_cell->bel = oldBel;

    new_wirelength = state.curr_wirelength;

    // Recalculate wirelengths for all nets touched by the peturbation
    for (auto net : update) {
        new_wirelength -= state.wirelengths.at(net);
        float net_new_wl = get_wirelength(ctx, net);
        new_wirelength += net_new_wl;
        new_lengths.push_back(std::make_pair(net, net_new_wl));
    }
    delta = new_wirelength - state.curr_wirelength;
    state.n_move++;
    // SA acceptance criterea
    if (delta < 0 ||
        (state.temp > 1e-6 &&
         random_float_upto(rnd, 1.0) <= std::exp(-delta / state.temp))) {
        state.n_accept++;
        if (delta < 0)
            state.improved = true;
    } else {
        if (other != IdString())
            ctx->unbindBel(oldBel);
        ctx->unbindBel(newBel);
        goto swap_fail;
    }
    state.curr_wirelength = new_wirelength;
    for (auto new_wl : new_lengths)
        state.wirelengths.at(new_wl.first) = new_wl.second;

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
BelId random_bel_for_cell(Context *ctx, CellInfo *cell, SAState &state,
                          rnd_state &rnd)
{
    BelType targetType = ctx->belTypeFromId(cell->type);
    int x = 0, y = 0;
    ctx->estimatePosition(cell->bel, x, y);
    while (true) {
        int nx = random_int_between(rnd, std::max(int(x) - state.diameter, 0),
                                    int(x) + state.diameter + 1);
        int ny = random_int_between(rnd, std::max(int(y) - state.diameter, 0),
                                    int(y) + state.diameter + 1);
        int beltype_idx = state.bel_types.at(targetType);
        if (nx >= int(state.fast_bels.at(beltype_idx).size()))
            continue;
        if (ny >= int(state.fast_bels.at(beltype_idx).at(nx).size()))
            continue;
        const auto &fb = state.fast_bels.at(beltype_idx).at(nx).at(ny);
        if (fb.size() == 0)
            continue;
        BelId bel = fb.at(random_int_between(rnd, 0, fb.size()));
        if (state.locked_bels.find(bel) != state.locked_bels.end())
            continue;
        return bel;
    }
}

void place_design_sa(Context *ctx, int seed)
{
    SAState state;

    size_t placed_cells = 0;
    std::queue<CellInfo *> visit_cells;
    // Initial constraints placer
    for (auto cell_entry : ctx->cells) {
        CellInfo *cell = cell_entry.second;
        auto loc = cell->attrs.find(ctx->id("BEL"));
        if (loc != cell->attrs.end()) {
            std::string loc_name = loc->second;
            BelId bel = ctx->getBelByName(ctx->id(loc_name));
            if (bel == BelId()) {
                log_error("No Bel named \'%s\' located for "
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
            state.locked_bels.insert(bel);
            placed_cells++;
            visit_cells.push(cell);
        }
    }
    log_info("place_constraints placed %d\n", int(placed_cells));
    rnd_state rnd;
    rnd.state = seed;
    std::vector<CellInfo *> autoplaced;
    // Sort to-place cells for deterministic initial placement
    for (auto cell : ctx->cells) {
        CellInfo *ci = cell.second;
        if (ci->bel == BelId()) {
            autoplaced.push_back(cell.second);
        }
    }
    std::sort(autoplaced.begin(), autoplaced.end(),
              [](CellInfo *a, CellInfo *b) { return a->name < b->name; });
    // Place cells randomly initially
    for (auto cell : autoplaced) {
        place_initial(ctx, cell, rnd);
        placed_cells++;
    }
    // Build up a fast position/type to Bel lookup table
    int max_x = 0, max_y = 0;
    int bel_types = 0;
    for (auto bel : ctx->getBels()) {
        int x, y;
        ctx->estimatePosition(bel, x, y);
        BelType type = ctx->getBelType(bel);
        int type_idx;
        if (state.bel_types.find(type) == state.bel_types.end()) {
            type_idx = bel_types++;
            state.bel_types[type] = type_idx;
        } else {
            type_idx = state.bel_types.at(type);
        }
        if (int(state.fast_bels.size()) < type_idx + 1)
            state.fast_bels.resize(type_idx + 1);
        if (int(state.fast_bels.at(type_idx).size()) < (x + 1))
            state.fast_bels.at(type_idx).resize(x + 1);
        if (int(state.fast_bels.at(type_idx).at(x).size()) < (y + 1))
            state.fast_bels.at(type_idx).at(x).resize(y + 1);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
        state.fast_bels.at(type_idx).at(x).at(y).push_back(bel);
    }
    state.diameter = std::max(max_x, max_y) + 1;
    // Calculate wirelength after initial placement
    state.curr_wirelength = 0;
    for (auto net : ctx->nets) {
        float wl = get_wirelength(ctx, net.second);
        state.wirelengths[net.second] = wl;
        state.curr_wirelength += wl;
    }

    int n_no_progress = 0;
    double avg_wirelength = state.curr_wirelength;
    state.temp = 10000;

    // Main simulated annealing loop
    for (int iter = 1;; iter++) {
        state.n_move = state.n_accept = 0;
        state.improved = false;

        if (iter % 5 == 0)
            log("  at iteration #%d: temp = %f, wire length = %f\n", iter,
                state.temp, state.curr_wirelength);

        for (int m = 0; m < 15; ++m) {
            // Loop through all automatically placed cells
            for (auto cell : autoplaced) {
                // Find another random Bel for this cell
                BelId try_bel = random_bel_for_cell(ctx, cell, state, rnd);
                // If valid, try and swap to a new position and see if
                // the new position is valid/worthwhile
                if (try_bel != BelId() && try_bel != cell->bel)
                    try_swap_position(ctx, cell, try_bel, rnd, state);
            }
        }
        // Heuristic to improve placement on the 8k
        if (state.improved) {
            n_no_progress = 0;
            // std::cout << "improved\n";
        } else
            ++n_no_progress;

        if (state.temp <= 1e-3 && n_no_progress >= 5)
            break;

        double Raccept = (double)state.n_accept / (double)state.n_move;

        int M = std::max(max_x, max_y) + 1;

        double upper = 0.6, lower = 0.4;

        if (state.curr_wirelength < 0.95 * avg_wirelength)
            avg_wirelength = 0.8 * avg_wirelength + 0.2 * state.curr_wirelength;
        else {
            if (Raccept >= 0.8) {
                state.temp *= 0.7;
            } else if (Raccept > upper) {
                if (state.diameter < M)
                    ++state.diameter;
                else
                    state.temp *= 0.9;
            } else if (Raccept > lower) {
                state.temp *= 0.95;
            } else {
                // Raccept < 0.3
                if (state.diameter > 1)
                    --state.diameter;
                else
                    state.temp *= 0.8;
            }
        }
    }
    for (auto bel : ctx->getBels()) {
        if (!isBelLocationValid(ctx, bel)) {
            std::string cell_text = "no cell";
            IdString cell = ctx->getBelCell(bel, false);
            if (cell != IdString())
                cell_text = std::string("cell '") + cell.str(ctx) + "'";
            log_error("post-placement validity check failed for Bel '%s' (%s)",
                      ctx->getBelName(bel).c_str(ctx), cell_text.c_str());
        }
    }
}

NEXTPNR_NAMESPACE_END
