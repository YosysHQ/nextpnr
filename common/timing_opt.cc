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

/*
 * Timing-optimised detailed placement algorithm
 * Based on "An Effective Timing-Driven Detailed Placement Algorithm for FPGAs"
 * https://www.cerc.utexas.edu/utda/publications/C205.pdf
 */

#include "timing.h"
#include "timing_opt.h"
#include "nextpnr.h"
#include "util.h"
NEXTPNR_NAMESPACE_BEGIN

class TimingOptimiser
{
  public:
    TimingOptimiser(Context *ctx) : ctx(ctx){};
    bool optimise() {}

  private:
    // Ratio of available to already-candidates to begin borrowing
    const float borrow_thresh = 0.2;

    void setup_delay_limits() {
        for (auto net : sorted(ctx->nets)) {
            NetInfo *ni = net.second;
            max_net_delay[ni].clear();
            max_net_delay[ni].resize(ni->users.size(), std::numeric_limits<delay_t>::max());
            if (!net_crit.count(net.first))
                continue;
            auto &nc = net_crit.at(net.first);
            if (nc.slack.empty())
                continue;
            for (size_t i = 0; i < ni->users.size(); i++) {
                delay_t net_delay = ctx->getNetinfoRouteDelay(ni, ni->users.at(i));
                max_net_delay[ni].at(i) = net_delay + ((nc.slack.at(i) - nc.cd_worst_slack) / nc.max_path_length);
            }
        }
    }

    bool check_cell_delay_limits(CellInfo *cell) {
        for (const auto &port : cell->ports) {
            int nc;
            if (ctx->getPortTimingClass(cell, port.first, nc) == TMG_IGNORE)
                continue;
            NetInfo *net = port.second.net;
            if (net == nullptr)
                continue;
            if (port.second.type == PORT_IN) {
                if (net->driver.cell == nullptr || net->driver.cell->bel == BelId())
                    continue;
                BelId srcBel = net->driver.cell->bel;
                if (ctx->estimateDelay(ctx->getBelPinWire(srcBel, net->driver.port),
                        ctx->getBelPinWire(cell->bel, port.first)) > max_net_delay.at(std::make_pair(cell->name, port.first)))
                    return false;
            } else if (port.second.type == PORT_OUT) {
                for (auto user : net->users) {
                    // This could get expensive for high-fanout nets??
                    BelId dstBel = user.cell->bel;
                    if (dstBel == BelId())
                        continue;
                    if (ctx->estimateDelay(ctx->getBelPinWire(cell->bel, port.first),
                                           ctx->getBelPinWire(dstBel, user.port)) > max_net_delay.at(std::make_pair(user.cell->name, user.port)))
                        return false;
                }
            }

        }
        return true;
    }

    bool acceptable_bel_candidate(CellInfo *cell, BelId newBel) {
        bool result = true;
        // At the moment we have to actually do the swap to get an accurate legality result
        // Switching to macro swaps might help with this
        BelId oldBel = cell->bel;
        CellInfo *other_cell = ctx->getBoundBelCell(newBel);
        if (other_cell != nullptr && other_cell->belStrength > STRENGTH_WEAK) {
            return false;
        }

        ctx->bindBel(newBel, cell, STRENGTH_WEAK);
        if (other_cell != nullptr) {
            ctx->bindBel(oldBel, other_cell, STRENGTH_WEAK);
        }
        if (!ctx->isBelLocationValid(newBel) || ((other_cell != nullptr && !ctx->isBelLocationValid(oldBel)))) {
            result = false;
            goto unbind;
        }

        if (!check_cell_delay_limits(cell) || (other_cell != nullptr && !check_cell_delay_limits(other_cell))) {
            result = false;
            goto unbind;
        }

unbind:
        ctx->unbindBel(newBel);
        if (other_cell != nullptr)
            ctx->unbindBel(oldBel);
        // Undo the swap
        ctx->bindBel(oldBel, cell, STRENGTH_WEAK);
        if (other_cell != nullptr) {
            ctx->bindBel(newBel, other_cell, STRENGTH_WEAK);
        }
        return result;
    }

    int find_neighbours(CellInfo *cell, IdString prev_cell, int d, bool allow_swap) {
        BelId curr = cell->bel;
        Loc curr_loc = ctx->getBelLocation(curr);
        int found_count = 0;
        for (int dy = -d; dy <= d; dy++) {
            for (int dx = -d; dx <= d; dx++) {
                if (dx == 0 && dy == 0)
                    continue;
                // Go through all the Bels at this location
                // First, find all bels of the correct type that are either unbound or bound normally
                // Strongly bound bels are ignored
                // FIXME: This means that we cannot touch carry chains or similar relatively constrained macros
                std::vector<BelId> free_bels_at_loc;
                std::vector<BelId> bound_bels_at_loc;
                for (auto bel : ctx->getBelsByTile(curr_loc.x + dx, curr_loc.y + dy)) {
                    if (ctx->getBelType(bel) != cell->type)
                        continue;
                    CellInfo *bound = ctx->getBoundBelCell(bel);
                    if (bound == nullptr) {
                        free_bels_at_loc.push_back(bel);
                    } else if (bound->belStrength <= STRENGTH_WEAK) {
                        bound_bels_at_loc.push_back(bel);
                    }
                }
                BelId candidate;

                while (!free_bels_at_loc.empty() && !bound_bels_at_loc.empty()) {
                    BelId try_bel;
                    if (!free_bels_at_loc.empty()) {
                        int try_idx = ctx->rng(int(free_bels_at_loc.size()));
                        try_bel = free_bels_at_loc.at(try_idx);
                        free_bels_at_loc.erase(free_bels_at_loc.begin() + try_idx);
                    } else {
                        int try_idx = ctx->rng(int(bound_bels_at_loc.size()));
                        try_bel = bound_bels_at_loc.at(try_idx);
                        bound_bels_at_loc.erase(bound_bels_at_loc.begin() + try_idx);
                    }
                    if (bel_candidate_cells.count(try_bel) && !allow_swap) {
                        // Overlap is only allowed if it is with the previous cell (this is handled by removing those
                        // edges in the graph), or if allow_swap is true to deal with cases where overlap means few neighbours
                        // are identified
                        if (bel_candidate_cells.at(try_bel).size() > 1 || (bel_candidate_cells.at(try_bel).size() == 0 ||
                        *(bel_candidate_cells.at(try_bel).begin()) != prev_cell))
                            continue;
                    }
                    if (acceptable_bel_candidate(cell, try_bel)) {
                        candidate = try_bel;
                        break;
                    }
                }

                if (candidate != BelId()) {
                    cell_neighbour_bels[cell->name].insert(candidate);
                    bel_candidate_cells[candidate].insert(cell->name);
                    // Work out if we need to delete any overlap
                    std::vector<IdString> overlap;
                    for (auto other : bel_candidate_cells[candidate])
                        if (other != cell->name && other != prev_cell)
                            overlap.push_back(other);
                    if (overlap.size() > 0)
                        NPNR_ASSERT(allow_swap);
                    for (auto ov : overlap) {
                        bel_candidate_cells[candidate].erase(ov);
                        cell_neighbour_bels[ov].erase(candidate);
                    }
                }
            }
        }
        return found_count;
    }

    // Current candidate Bels for cells (linked in both direction>
    std::vector<IdString> path_cells;
    std::unordered_map<IdString, std::unordered_set<BelId>> cell_neighbour_bels;
    std::unordered_map<BelId, std::unordered_set<IdString>> bel_candidate_cells;
    // Map cell ports to net delay limit
    std::unordered_map<std::pair<IdString, IdString>, delay_t> max_net_delay;
    // Criticality data from timing analysis
    NetCriticalityMap net_crit;

    Context *ctx;
};

bool timing_opt(Context *ctx, TimingOptCfg cfg) { return TimingOptimiser(ctx).optimise(); }

NEXTPNR_NAMESPACE_END
