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
    WireId drv_wire = ctx->getBelPinWire(driver_cell->bel, ctx->portPinFromId(net->driver.port));
    if (driver_gb)
        return 0;
    float worst_slack = 1000;
    int xmin = driver_loc.x, xmax = driver_loc.x, ymin = driver_loc.y, ymax = driver_loc.y;
    for (auto load : net->users) {
        if (load.cell == nullptr)
            continue;
        CellInfo *load_cell = load.cell;
        if (load_cell->bel == BelId())
            continue;
        if (ctx->timing_driven && type == MetricType::COST) {
            WireId user_wire = ctx->getBelPinWire(load_cell->bel, ctx->portPinFromId(load.port));
            delay_t raw_wl = ctx->estimateDelay(drv_wire, user_wire);
            float slack = ctx->getDelayNS(load.budget) - ctx->getDelayNS(raw_wl);
            if (slack < 0)
                tns += slack;
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
        wirelength = wirelen_t((((ymax - ymin) + (xmax - xmin)) * std::min(5.0, (1.0 + std::exp(-worst_slack / 5)))));
    } else {
        wirelength = wirelen_t((ymax - ymin) + (xmax - xmin));
    }

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

NEXTPNR_NAMESPACE_END
