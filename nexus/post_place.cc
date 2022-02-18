/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2020  gatecat <gatecat@ds0.me>
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

#include "design_utils.h"
#include "log.h"
#include "nextpnr.h"
#include "timing.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

struct NexusPostPlaceOpt
{
    Context *ctx;
    TimingAnalyser tmg;

    NexusPostPlaceOpt(Context *ctx) : ctx(ctx), tmg(ctx){};

    inline bool is_constrained(CellInfo *cell) { return cell->cluster != ClusterId(); }

    bool swap_cell_placement(CellInfo *cell, BelId new_bel)
    {
        if (is_constrained(cell))
            return false;
        BelId oldBel = cell->bel;
        CellInfo *other_cell = ctx->getBoundBelCell(new_bel);
        if (other_cell != nullptr && (is_constrained(other_cell) || other_cell->belStrength > STRENGTH_WEAK)) {
            return false;
        }

        ctx->unbindBel(oldBel);
        if (other_cell != nullptr) {
            ctx->unbindBel(new_bel);
        }

        ctx->bindBel(new_bel, cell, STRENGTH_WEAK);

        if (other_cell != nullptr) {
            ctx->bindBel(oldBel, other_cell, STRENGTH_WEAK);
        }

        if (!ctx->isBelLocationValid(new_bel) || ((other_cell != nullptr && !ctx->isBelLocationValid(oldBel)))) {
            // New placement is not legal.
            ctx->unbindBel(new_bel);
            if (other_cell != nullptr)
                ctx->unbindBel(oldBel);

            // Revert.
            ctx->bindBel(oldBel, cell, STRENGTH_WEAK);
            if (other_cell != nullptr)
                ctx->bindBel(new_bel, other_cell, STRENGTH_WEAK);
            return false;
        }

        return true;
    }

    int get_distance(BelId a, BelId b)
    {
        Loc la = ctx->getBelLocation(a);
        Loc lb = ctx->getBelLocation(b);
        return std::abs(la.x - lb.x) + std::abs(la.y - lb.y);
    }

    BelId lut_to_ff(BelId lut)
    {
        Loc ff_loc = ctx->getBelLocation(lut);
        ff_loc.z += (Arch::BEL_FF0 - Arch::BEL_LUT0);
        return ctx->getBelByLocation(ff_loc);
    }

    void opt_lutffs()
    {
        int moves_made = 0;
        for (auto &cell : ctx->cells) {
            // Search for FF cells
            CellInfo *ff = cell.second.get();
            if (ff->type != id_OXIDE_FF)
                continue;
            // Check M ('fabric') input net
            NetInfo *m = ff->getPort(id_M);
            if (m == nullptr)
                continue;

            // Ignore FFs that need both DI and M (PRLD mode)
            if (ff->getPort(id_DI) != nullptr)
                continue;

            const auto &drv = m->driver;
            // Skip if driver isn't a LUT/MUX2
            if (drv.cell == nullptr || drv.cell->type != id_OXIDE_COMB || (drv.port != id_F && drv.port != id_OFX))
                continue;
            CellInfo *lut = drv.cell;
            // Check distance to move isn't too far
            if (get_distance(ff->bel, lut->bel) > lut_ff_radius)
                continue;
            // Find the bel we plan to move into
            BelId dest_ff = lut_to_ff(lut->bel);
            NPNR_ASSERT(dest_ff != BelId());
            NPNR_ASSERT(ctx->getBelType(dest_ff) == id_OXIDE_FF);
            // Ended up in the ideal location by chance
            if (dest_ff != ff->bel) {
                // If dest_ff is already placed *and* using direct 'DI' input, don't touch it
                CellInfo *dest_ff_cell = ctx->getBoundBelCell(dest_ff);
                if (dest_ff_cell != nullptr && dest_ff_cell->getPort(id_DI) != nullptr)
                    continue;
                // Attempt the swap
                bool swap_result = swap_cell_placement(ff, dest_ff);
                if (!swap_result)
                    continue;
            }
            // Use direct interconnect
            ff->renamePort(id_M, id_DI);
            ff->params[id_SEL] = std::string("DL");
            ++moves_made;
            continue;
        }
        log_info("     created %d direct LUT-FF pairs\n", moves_made);
    }

    void operator()()
    {
        tmg.setup();
        opt_lutffs();
    }

    // Configuration
    const int lut_ff_radius = 2;
    const int lut_lut_radius = 1;
    const float lut_lut_crit = 0.85;
};

void Arch::post_place_opt()
{
    if (bool_or_default(settings, id_no_post_place_opt))
        return;
    log_info("Running post-place optimisations...\n");
    NexusPostPlaceOpt opt(getCtx());
    opt();
}

NEXTPNR_NAMESPACE_END
