/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
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
#include "fast_bels.h"
#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

// Get the total estimated wirelength for a net
wirelen_t get_net_metric(const Context *ctx, const NetInfo *net, MetricType type, float &tns)
{
    wirelen_t wirelength = 0;
    CellInfo *driver_cell = net->driver.cell;
    if (!driver_cell)
        return 0;
    if (driver_cell->bel == BelId())
        return 0;
    bool driver_gb = ctx->getBelGlobalBuf(driver_cell->bel);
    if (driver_gb)
        return 0;
    int clock_count;
    bool timing_driven = ctx->setting<bool>("timing_driven") && type == MetricType::COST &&
                         ctx->getPortTimingClass(driver_cell, net->driver.port, clock_count) != TMG_IGNORE;
    delay_t negative_slack = 0;
    delay_t worst_slack = std::numeric_limits<delay_t>::max();
    Loc driver_loc = ctx->getBelLocation(driver_cell->bel);
    int xmin = driver_loc.x, xmax = driver_loc.x, ymin = driver_loc.y, ymax = driver_loc.y;
    for (auto load : net->users) {
        if (load.cell == nullptr)
            continue;
        CellInfo *load_cell = load.cell;
        if (load_cell->bel == BelId())
            continue;
        if (timing_driven) {
            delay_t net_delay = ctx->predictArcDelay(net, load);
            auto slack = -net_delay;
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
    if (timing_driven) {
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

class ConstraintLegaliseWorker
{
  private:
    Context *ctx;
    std::set<IdString> rippedCells;
    dict<IdString, Loc> oldLocations;
    dict<ClusterId, std::vector<CellInfo *>> cluster2cells;
    FastBels fast_bels;

    class IncreasingDiameterSearch
    {
      public:
        IncreasingDiameterSearch() : start(0), min(0), max(-1){};
        IncreasingDiameterSearch(int x) : start(x), min(x), max(x){};
        IncreasingDiameterSearch(int start, int min, int max) : start(start), min(min), max(max){};
        bool done() const { return (diameter > (max - min)); };
        int get() const
        {
            int val = start + sign * diameter;
            val = std::max(val, min);
            val = std::min(val, max);
            return val;
        }

        void next()
        {
            if (sign == 0) {
                sign = 1;
                diameter = 1;
            } else if (sign == -1) {
                sign = 1;
                if ((start + sign * diameter) > max)
                    sign = -1;
                ++diameter;
            } else {
                sign = -1;
                if ((start + sign * diameter) < min) {
                    sign = 1;
                    ++diameter;
                }
            }
        }

        void reset()
        {
            sign = 0;
            diameter = 0;
        }

      private:
        int start, min, max;
        int diameter = 0;
        int sign = 0;
    };

    typedef dict<IdString, Loc> CellLocations;

    // Check if a location would be suitable for a cell and all its constrained children
    bool valid_loc_for(const CellInfo *cell, Loc loc, CellLocations &solution, pool<Loc> &usedLocations)
    {
        BelId locBel = ctx->getBelByLocation(loc);
        if (locBel == BelId())
            return false;

        if (cell->cluster == ClusterId()) {
            if (!ctx->isValidBelForCellType(cell->type, locBel))
                return false;
            if (!ctx->checkBelAvail(locBel)) {
                CellInfo *confCell = ctx->getConflictingBelCell(locBel);
                if (confCell->belStrength >= STRENGTH_STRONG) {
                    return false;
                }
            }
            // Don't place at tiles where any strongly bound Bels exist, as we might need to rip them up later
            for (auto tilebel : ctx->getBelsByTile(loc.x, loc.y)) {
                CellInfo *tcell = ctx->getBoundBelCell(tilebel);
                if (tcell && tcell->belStrength >= STRENGTH_STRONG)
                    return false;
            }
            usedLocations.insert(loc);
            solution[cell->name] = loc;
        } else {
            std::vector<std::pair<CellInfo *, BelId>> placement;
            if (!ctx->getClusterPlacement(cell->cluster, locBel, placement))
                return false;
            for (auto &p : placement) {
                Loc p_loc = ctx->getBelLocation(p.second);
                if (!ctx->checkBelAvail(p.second)) {
                    CellInfo *confCell = ctx->getConflictingBelCell(p.second);
                    if (confCell->belStrength >= STRENGTH_STRONG) {
                        return false;
                    }
                }
                // Don't place at tiles where any strongly bound Bels exist, as we might need to rip them up later
                for (auto tilebel : ctx->getBelsByTile(p_loc.x, p_loc.y)) {
                    CellInfo *tcell = ctx->getBoundBelCell(tilebel);
                    if (tcell && tcell->belStrength >= STRENGTH_STRONG)
                        return false;
                }
                usedLocations.insert(p_loc);
                solution[p.first->name] = p_loc;
            }
        }

        return true;
    }

    // Set the strength to locked on all cells in chain
    void lockdown_chain(CellInfo *root)
    {
        root->belStrength = STRENGTH_STRONG;
        if (root->cluster != ClusterId())
            for (auto child : cluster2cells.at(root->cluster))
                child->belStrength = STRENGTH_STRONG;
    }

    // Legalise placement constraints on a cell
    bool legalise_cell(CellInfo *cell)
    {
        if (cell->cluster != ClusterId() && ctx->getClusterRootCell(cell->cluster) != cell)
            return true; // Only process chain roots
        if (cell->isPseudo())
            return true;
        if (constraints_satisfied(cell)) {
            if (cell->cluster != ClusterId())
                lockdown_chain(cell);
        } else {
            IncreasingDiameterSearch xRootSearch, yRootSearch, zRootSearch;
            Loc currentLoc;
            if (cell->bel != BelId())
                currentLoc = ctx->getBelLocation(cell->bel);
            else
                currentLoc = oldLocations[cell->name];
            xRootSearch = IncreasingDiameterSearch(currentLoc.x, 0, ctx->getGridDimX() - 1);
            yRootSearch = IncreasingDiameterSearch(currentLoc.y, 0, ctx->getGridDimY() - 1);
            zRootSearch = IncreasingDiameterSearch(currentLoc.z, 0, ctx->getTileBelDimZ(currentLoc.x, currentLoc.y));

            while (!xRootSearch.done()) {
                Loc rootLoc;

                rootLoc.x = xRootSearch.get();
                rootLoc.y = yRootSearch.get();
                rootLoc.z = zRootSearch.get();
                zRootSearch.next();
                if (zRootSearch.done()) {
                    zRootSearch.reset();
                    yRootSearch.next();
                    if (yRootSearch.done()) {
                        yRootSearch.reset();
                        xRootSearch.next();
                    }
                }

                CellLocations solution;
                pool<Loc> used;
                if (valid_loc_for(cell, rootLoc, solution, used)) {
                    for (auto cp : solution) {
                        // First unbind all cells
                        if (ctx->cells.at(cp.first)->bel != BelId())
                            ctx->unbindBel(ctx->cells.at(cp.first)->bel);
                    }
                    for (auto cp : solution) {
                        if (ctx->verbose)
                            log_info("     placing '%s' at (%d, %d, %d)\n", cp.first.c_str(ctx), cp.second.x,
                                     cp.second.y, cp.second.z);
                        BelId target = ctx->getBelByLocation(cp.second);
                        if (!ctx->checkBelAvail(target)) {
                            CellInfo *confl_cell = ctx->getConflictingBelCell(target);
                            if (confl_cell != nullptr) {
                                if (ctx->verbose)
                                    log_info("       '%s' already placed at '%s'\n", ctx->nameOf(confl_cell),
                                             ctx->nameOfBel(confl_cell->bel));
                                NPNR_ASSERT(confl_cell->belStrength < STRENGTH_STRONG);
                                ctx->unbindBel(target);
                                rippedCells.insert(confl_cell->name);
                            }
                        }
                        ctx->bindBel(target, ctx->cells.at(cp.first).get(), STRENGTH_STRONG);
                        rippedCells.erase(cp.first);
                    }
                    for (auto cp : solution) {
                        for (auto bel : ctx->getBelsByTile(cp.second.x, cp.second.y)) {
                            CellInfo *belCell = ctx->getBoundBelCell(bel);
                            if (belCell != nullptr && !solution.count(belCell->name)) {
                                if (!ctx->isBelLocationValid(bel)) {
                                    NPNR_ASSERT(belCell->belStrength < STRENGTH_STRONG);
                                    ctx->unbindBel(bel);
                                    rippedCells.insert(belCell->name);
                                }
                            }
                        }
                    }
                    NPNR_ASSERT(constraints_satisfied(cell));
                    return true;
                }
            }
            return false;
        }
        return true;
    }

    // Check if constraints are currently satisfied on a cell and its children
    bool constraints_satisfied(const CellInfo *cell) { return get_constraints_distance(ctx, cell) == 0; }

    // Placing a single cell
    bool place_single_cell(CellInfo *cell)
    {
        int diameter = 1;
        while (cell) {
            CellInfo *ripup_target = nullptr;
            if (cell->bel != BelId()) {
                ctx->unbindBel(cell->bel);
            }
            FastBels::FastBelsData *bel_data;
            fast_bels.getBelsForCellType(cell->type, &bel_data);

            int iter = 0;
            BelId best_bel = BelId();
            wirelen_t best_metric = std::numeric_limits<wirelen_t>::max();

            while (true) {
                ++iter;
                if (iter >= (5 * diameter)) {
                    iter = 0;
                    if (diameter < std::max(ctx->getGridDimX(), ctx->getGridDimY()))
                        ++diameter;
                    if (best_bel != BelId())
                        break;
                }
                auto old_loc = oldLocations.at(cell->name);
                int nx = old_loc.x - (diameter / 2) + ctx->rng(diameter),
                    ny = old_loc.y - (diameter / 2) + ctx->rng(diameter);
                if (nx < 0 || nx >= int(bel_data->size()))
                    continue;
                if (ny < 0 || ny >= int(bel_data->at(nx).size()))
                    continue;
                const auto &fb = bel_data->at(nx).at(ny);
                if (fb.size() == 0)
                    continue;
                BelId bel = fb.at(ctx->rng(int(fb.size())));
                if (cell->region && cell->region->constr_bels && !cell->region->bels.count(bel))
                    continue;
                if (!ctx->isValidBelForCellType(cell->type, bel))
                    continue;
                ripup_target = ctx->getBoundBelCell(bel);
                if (ripup_target != nullptr) {
                    if (ripup_target->belStrength > STRENGTH_STRONG || ripup_target->cluster != ClusterId())
                        continue;
                    ctx->unbindBel(bel);
                } else if (!ctx->checkBelAvail(bel)) {
                    continue;
                }
                ctx->bindBel(bel, cell, STRENGTH_WEAK);
                if (!ctx->isBelLocationValid(bel)) {
                    ctx->unbindBel(bel);
                    if (ripup_target)
                        ctx->bindBel(bel, ripup_target, STRENGTH_WEAK);
                    continue;
                }
                wirelen_t new_metric = get_cell_metric(ctx, cell, MetricType::COST);
                if (ripup_target)
                    new_metric *= 5;
                if (new_metric < best_metric) {
                    best_bel = bel;
                    best_metric = new_metric;
                }
                ctx->unbindBel(bel);
                if (ripup_target)
                    ctx->bindBel(bel, ripup_target, STRENGTH_WEAK);
            }

            // Back annotate location
            ripup_target = ctx->getBoundBelCell(best_bel);
            if (ripup_target)
                ctx->unbindBel(best_bel);
            ctx->bindBel(best_bel, cell, STRENGTH_WEAK);

            cell->attrs[ctx->id("BEL")] = ctx->getBelName(cell->bel).str(ctx);
            cell = ripup_target;
        }
        return true;
    }

  public:
    ConstraintLegaliseWorker(Context *ctx) : ctx(ctx), fast_bels(ctx, /*check_bel_available=*/false, 0)
    {
        for (auto &cell : ctx->cells) {
            if (cell.second->cluster != ClusterId())
                cluster2cells[cell.second->cluster].push_back(cell.second.get());
        }
    };

    unsigned print_stats(const char *point)
    {
        float distance_sum = 0;
        float max_distance = 0;
        unsigned moved_cells = 0;
        unsigned unplaced_cells = 0;
        for (auto orig : oldLocations) {
            if (ctx->cells.at(orig.first)->bel == BelId()) {
                unplaced_cells++;
                continue;
            }
            Loc newLoc = ctx->getBelLocation(ctx->cells.at(orig.first)->bel);
            if (newLoc != orig.second) {
                float distance = std::sqrt(std::pow(newLoc.x - orig.second.x, 2) + pow(newLoc.y - orig.second.y, 2));
                moved_cells++;
                distance_sum += distance;
                if (distance > max_distance)
                    max_distance = distance;
            }
        }
        log_info("    moved %d cells, %d unplaced (after %s)\n", moved_cells, unplaced_cells, point);
        if (moved_cells > 0) {
            log_info("       average distance %f\n", (distance_sum / moved_cells));
            log_info("       maximum distance %f\n", max_distance);
        }
        return moved_cells + unplaced_cells;
    }

    int legalise_constraints()
    {
        log_info("Legalising relative constraints...\n");
        for (auto &cell : ctx->cells) {
            oldLocations[cell.first] = cell.second->getLocation();
        }
        for (auto &cell : ctx->cells) {
            bool res = legalise_cell(cell.second.get());
            if (!res) {
                log_error("failed to place chain starting at cell '%s'\n", cell.first.c_str(ctx));
                return -1;
            }
        }
        if (print_stats("legalising chains") == 0)
            return 0;
        for (auto rippedCell : rippedCells) {
            bool res = place_single_cell(ctx->cells.at(rippedCell).get());
            if (!res) {
                log_error("failed to place cell '%s' after relative constraint legalisation\n", rippedCell.c_str(ctx));
                return -1;
            }
        }
        auto score = print_stats("replacing ripped up cells");
        for (auto &cell : ctx->cells)
            if (get_constraints_distance(ctx, cell.second.get()) != 0)
                log_error("constraint satisfaction check failed for cell '%s' at Bel '%s'\n", cell.first.c_str(ctx),
                          ctx->nameOfBel(cell.second->bel));
        return score;
    }
};

bool legalise_relative_constraints(Context *ctx) { return ConstraintLegaliseWorker(ctx).legalise_constraints() > 0; }

// Get the total distance from satisfied constraints for a cell
int get_constraints_distance(const Context *ctx, const CellInfo *cell)
{
    int dist = 0;
    if (cell->isPseudo())
        return 0;
    if (cell->bel == BelId())
        return 100000;
    Loc loc = ctx->getBelLocation(cell->bel);

    if (cell->cluster != ClusterId()) {
        CellInfo *root = ctx->getClusterRootCell(cell->cluster);
        if (root == cell) {
            // parent
            std::vector<std::pair<CellInfo *, BelId>> placement;
            if (!ctx->getClusterPlacement(cell->cluster, cell->bel, placement)) {
                return 100000;
            } else {
                for (const auto &p : placement) {
                    if (p.first->bel == BelId())
                        return 100000;
                    Loc c_loc = ctx->getBelLocation(p.first->bel);
                    Loc p_loc = ctx->getBelLocation(p.second);
                    dist += std::abs(c_loc.x - p_loc.x);
                    dist += std::abs(c_loc.y - p_loc.y);
                    dist += std::abs(c_loc.z - p_loc.z);
                }
            }
        } else {
            // child
            if (root->bel == BelId())
                return 100000;
            Loc root_loc = ctx->getBelLocation(root->bel);
            Loc offset = ctx->getClusterOffset(cell);
            dist += std::abs((root_loc.x + offset.x) - loc.x);
            dist += std::abs((root_loc.y + offset.y) - loc.y);
        }
    }

    return dist;
}

NEXTPNR_NAMESPACE_END
