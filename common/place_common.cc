/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
 *  Copyright (C) 2018  Serge Bazanski  <q3k@symbioticeda.com>
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

// Placing a single cell
bool place_single_cell(ArchRWProxy &proxy, Context *ctx, CellInfo *cell, bool require_legality)
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
            proxy.unbindBel(cell->bel);
        }
        BelType targetType = ctx->belTypeFromId(cell->type);
        for (auto bel : ctx->getBels()) {
            if (ctx->getBelType(bel) == targetType && (!require_legality || proxy.isValidBelForCell(cell, bel))) {
                if (proxy.checkBelAvail(bel)) {
                    wirelen_t wirelen = get_cell_wirelength_at_bel(proxy, ctx, cell, bel);
                    if (iters >= 4)
                        wirelen += ctx->rng(25);
                    if (wirelen <= best_wirelen) {
                        best_wirelen = wirelen;
                        best_bel = bel;
                    }
                } else {
                    wirelen_t wirelen = get_cell_wirelength_at_bel(proxy, ctx, cell, bel);
                    if (iters >= 4)
                        wirelen += ctx->rng(25);
                    if (wirelen <= best_ripup_wirelen) {
                        ripup_target = proxy.getCell(proxy.getBoundBelCell(bel));
                        if (ripup_target->belStrength < STRENGTH_STRONG) {
                            best_ripup_wirelen = wirelen;
                            ripup_bel = bel;
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
            proxy.unbindBel(ripup_target->bel);
            best_bel = ripup_bel;
        } else {
            all_placed = true;
        }
        proxy.bindBel(best_bel, cell->name, STRENGTH_WEAK);

        cell = ripup_target;
    }
    return true;
}

NEXTPNR_NAMESPACE_END
