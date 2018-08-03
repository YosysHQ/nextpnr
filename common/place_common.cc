/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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

#include "place_common.h"
#include <cmath>
#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

// Get the total estimated wirelength for a net
wirelen_t get_net_metric(const Context *ctx, const NetInfo *net, MetricType type, float &tns)
{
    wirelen_t wirelength = 0;
    Loc driver_loc;
    bool driver_gb;
    CellInfo *driver_cell = net->driver.cell;
    if (!driver_cell)
        return 0;
    if (driver_cell->bel == BelId())
        return 0;
    driver_gb = ctx->getBelGlobalBuf(driver_cell->bel);
    driver_loc = ctx->getBelLocation(driver_cell->bel);
    if (driver_gb)
        return 0;
    delay_t negative_slack = 0;
    delay_t worst_slack = std::numeric_limits<delay_t>::max();
    int xmin = driver_loc.x, xmax = driver_loc.x, ymin = driver_loc.y, ymax = driver_loc.y;
    for (auto load : net->users) {
        if (load.cell == nullptr)
            continue;
        CellInfo *load_cell = load.cell;
        if (load_cell->bel == BelId())
            continue;
        if (ctx->timing_driven && type == MetricType::COST) {
            delay_t net_delay = ctx->predictDelay(net, load);
            auto slack = load.budget - net_delay;
            if (slack < 0)
                negative_slack += slack;
            worst_slack = std::min(slack, worst_slack);
        }

        if (ctx->getBelGlobalBuf(load_cell->bel))
            continue;
        Loc load_loc = ctx->getBelLocation(load_cell->bel);

        xmin = std::min(xmin, load_loc.x);
        ymin = std::min(ymin, load_loc.y);
        xmax = std::max(xmax, load_loc.x);
        ymax = std::max(ymax, load_loc.y);
    }
    if (ctx->timing_driven && type == MetricType::COST) {
        wirelength = wirelen_t(
                (((ymax - ymin) + (xmax - xmin)) * std::min(5.0, (1.0 + std::exp(-ctx->getDelayNS(worst_slack) / 5)))));
    } else {
        wirelength = wirelen_t((ymax - ymin) + (xmax - xmin));
    }

    tns += ctx->getDelayNS(negative_slack);
    return wirelength;
}

// Get the total wirelength for a cell
wirelen_t get_cell_metric(const Context *ctx, const CellInfo *cell, MetricType type)
{
    std::set<IdString> nets;
    for (auto p : cell->ports) {
        if (p.second.net)
            nets.insert(p.second.net->name);
    }
    wirelen_t wirelength = 0;
    float tns = 0;
    for (auto n : nets) {
        wirelength += get_net_metric(ctx, ctx->nets.at(n).get(), type, tns);
    }
    return wirelength;
}

wirelen_t get_cell_metric_at_bel(const Context *ctx, CellInfo *cell, BelId bel, MetricType type)
{
    BelId oldBel = cell->bel;
    cell->bel = bel;
    wirelen_t wirelen = get_cell_metric(ctx, cell, type);
    cell->bel = oldBel;
    return wirelen;
}

// Placing a single cell
bool place_single_cell(Context *ctx, CellInfo *cell, bool require_legality)
{
    bool all_placed = false;
    int iters = 25;
    while (!all_placed) {
        BelId best_bel = BelId();
        wirelen_t best_wirelen = std::numeric_limits<wirelen_t>::max(),
                  best_ripup_wirelen = std::numeric_limits<wirelen_t>::max();
        CellInfo *ripup_target = nullptr;
        BelId ripup_bel = BelId();
        if (cell->bel != BelId()) {
            ctx->unbindBel(cell->bel);
        }
        BelType targetType = ctx->belTypeFromId(cell->type);
        for (auto bel : ctx->getBels()) {
            if (ctx->getBelType(bel) == targetType && (!require_legality || ctx->isValidBelForCell(cell, bel))) {
                if (ctx->checkBelAvail(bel)) {
                    wirelen_t wirelen = get_cell_metric_at_bel(ctx, cell, bel, MetricType::COST);
                    if (iters >= 4)
                        wirelen += ctx->rng(25);
                    if (wirelen <= best_wirelen) {
                        best_wirelen = wirelen;
                        best_bel = bel;
                    }
                } else {
                    wirelen_t wirelen = get_cell_metric_at_bel(ctx, cell, bel, MetricType::COST);
                    if (iters >= 4)
                        wirelen += ctx->rng(25);
                    if (wirelen <= best_ripup_wirelen) {
                        CellInfo *curr_cell = ctx->cells.at(ctx->getBoundBelCell(bel)).get();
                        if (curr_cell->belStrength < STRENGTH_STRONG) {
                            best_ripup_wirelen = wirelen;
                            ripup_bel = bel;
                            ripup_target = curr_cell;
                        }
                    }
                }
            }
        }
        if (best_bel == BelId()) {
            if (iters == 0) {
                log_error("failed to place cell '%s' of type '%s' (ripup iteration limit exceeded)\n",
                          cell->name.c_str(ctx), cell->type.c_str(ctx));
            }
            if (ripup_bel == BelId()) {
                log_error("failed to place cell '%s' of type '%s'\n", cell->name.c_str(ctx), cell->type.c_str(ctx));
            }
            --iters;
            ctx->unbindBel(ripup_target->bel);
            best_bel = ripup_bel;
        } else {
            all_placed = true;
        }
        ctx->bindBel(best_bel, cell->name, STRENGTH_WEAK);

        cell = ripup_target;
    }
    return true;
}

class ConstraintLegaliseWorker
{
  private:
    Context *ctx;
    std::vector<CellInfo *> rippedCells;

    class IncreasingDiameterSearch
    {
      public:
        IncreasingDiameterSearch() : start(0), min(0), max(-1){};
        IncreasingDiameterSearch(int x) : start(x), min(x), max(x){};
        IncreasingDiameterSearch(int start, int min, int max) : start(start), min(min), max(max){};
        bool done() { return (diameter > (max - min)); };
        int next()
        {
            int val = start + sign * diameter;
            val = std::max(val, min);
            val = std::min(val, max);

            if (sign == 0) {
                sign = 1;
                diameter = 1;
            } else if (sign == -1) {
                sign = 1;
                ++diameter;
            } else {
                sign = -1;
            }

            return val;
        }

      private:
        int start, min, max;
        int diameter = 0;
        int sign = 0;
    };

    typedef std::unordered_map<IdString, Loc> CellLocations;

    // Check if a location would be suitable for a cell and all its constrained children
    // This also makes a crude attempt to "solve" unconstrained constraints, that is slow and horrible
    // and will need to be reworked if mixed constrained/unconstrained chains become common
    bool valid_loc_for(const CellInfo *cell, Loc loc, CellLocations &solution)
    {
        BelId locBel = ctx->getBelByLocation(loc);
        if (locBel == BelId())
            return false;
        if (ctx->getBelType(locBel) != ctx->belTypeFromId(cell->type))
            return false;
        for (auto child : cell->constr_children) {
            IncreasingDiameterSearch xSearch, ySearch, zSearch;
            if (child->constr_x == child->UNCONSTR) {
                xSearch = IncreasingDiameterSearch(loc.x, 0, ctx->getGridDimX());
            } else {
                xSearch = IncreasingDiameterSearch(loc.x + child->constr_x);
            }
            if (child->constr_y == child->UNCONSTR) {
                ySearch = IncreasingDiameterSearch(loc.y, 0, ctx->getGridDimY());
            } else {
                ySearch = IncreasingDiameterSearch(loc.y + child->constr_y);
            }
            if (child->constr_z == child->UNCONSTR) {
                zSearch = IncreasingDiameterSearch(loc.z, 0, ctx->getTileDimZ(loc.x, loc.y));
            } else {
                if (child->constr_abs_z) {
                    zSearch = IncreasingDiameterSearch(child->constr_z);
                } else {
                    zSearch = IncreasingDiameterSearch(loc.z + child->constr_z);
                }
            }
            while (!(xSearch.done() && ySearch.done() && zSearch.done())) {
                Loc cloc;
                cloc.x = xSearch.next();
                cloc.y = ySearch.next();
                cloc.z = zSearch.next();
                if (valid_loc_for(child, cloc, solution))
                    return true;
            }
            return false;
        }

        solution[cell->name] = loc;
        return true;
    }

    // Check if constraints are currently satisfied on a cell and its children
    bool constraints_satisfied(const CellInfo *cell) { return get_constraints_distance(ctx, cell) == 0; }
};

// Get the total distance from satisfied constraints for a cell
int get_constraints_distance(const Context *ctx, const CellInfo *cell)
{
    int dist = 0;
    NPNR_ASSERT(cell->bel != BelId());
    Loc loc = ctx->getBelLocation(cell->bel);
    if (cell->constr_parent == nullptr) {
        if (cell->constr_x != cell->UNCONSTR)
            dist += std::abs(cell->constr_x - loc.x);
        if (cell->constr_y != cell->UNCONSTR)
            dist += std::abs(cell->constr_y - loc.y);
        if (cell->constr_z != cell->UNCONSTR)
            dist += std::abs(cell->constr_z - loc.z);
    } else {
        Loc parent_loc = ctx->getBelLocation(cell->constr_parent->bel);
        if (cell->constr_x != cell->UNCONSTR)
            dist += std::abs(cell->constr_x - (loc.x - parent_loc.x));
        if (cell->constr_y != cell->UNCONSTR)
            dist += std::abs(cell->constr_y - (loc.y - parent_loc.y));
        if (cell->constr_z != cell->UNCONSTR) {
            if (cell->constr_abs_z)
                dist += std::abs(cell->constr_z - loc.z);
            else
                dist += std::abs(cell->constr_z - (loc.z - parent_loc.z));
        }
    }
    for (auto child : cell->constr_children)
        dist += get_constraints_distance(ctx, child);
    return dist;
}

NEXTPNR_NAMESPACE_END
