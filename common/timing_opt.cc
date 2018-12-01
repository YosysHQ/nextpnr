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

#include "timing_opt.h"
#include "nextpnr.h"
NEXTPNR_NAMESPACE_BEGIN

class TimingOptimiser
{
  public:
    TimingOptimiser(Context *ctx) : ctx(ctx){};
    bool optimise() {}

  private:
    // Ratio of available to already-candidates to begin borrowing
    const float borrow_thresh = 0.2;

    bool check_cell_delay_limits(CellInfo *cell) {
        
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

    void find_neighbours(CellInfo *cell, int d) {
        BelId curr = cell->bel;
        Loc curr_loc = ctx->getBelLocation(curr);
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
                bool found = false;

                if (found)
                    continue;
            }
        }
    }

    // Current candidate Bels for cells (linked in both direction>
    std::vector<IdString> path_cells;
    std::unordered_map<IdString, std::unordered_set<BelId>> cell_neighbour_bels;
    std::unordered_map<BelId, std::unordered_set<IdString>> bel_candidate_cells;
    // Map net users to net delay limit
    std::unordered_map<IdString, std::vector<delay_t>> max_net_delay;
    Context *ctx;
};

bool timing_opt(Context *ctx, TimingOptCfg cfg) { return TimingOptimiser(ctx).optimise(); }

NEXTPNR_NAMESPACE_END
