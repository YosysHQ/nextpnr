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

#ifndef PLACE_COMMON_H
#define PLACE_COMMON_H

#include "nextpnr.h"

#include <set>

NEXTPNR_NAMESPACE_BEGIN

typedef int64_t wirelen_t;

// Get the total estimated wirelength for a net
template <typename T> wirelen_t get_net_wirelength(const T &proxy, const Context *ctx, const NetInfo *net, float &tns)
{
    wirelen_t wirelength = 0;
    int driver_x, driver_y;
    bool driver_gb;
    CellInfo *driver_cell = net->driver.cell;
    if (!driver_cell)
        return 0;
    if (driver_cell->bel == BelId())
        return 0;
    ctx->estimatePosition(driver_cell->bel, driver_x, driver_y, driver_gb);
    WireId drv_wire = proxy.getWireBelPin(driver_cell->bel, ctx->portPinFromId(net->driver.port));
    if (driver_gb)
        return 0;
    float worst_slack = 1000;
    int xmin = driver_x, xmax = driver_x, ymin = driver_y, ymax = driver_y;
    for (auto load : net->users) {
        if (load.cell == nullptr)
            continue;
        CellInfo *load_cell = load.cell;
        if (load_cell->bel == BelId())
            continue;
        if (ctx->timing_driven) {
            WireId user_wire = proxy.getWireBelPin(load_cell->bel, ctx->portPinFromId(load.port));
            delay_t raw_wl = ctx->estimateDelay(drv_wire, user_wire);
            float slack = ctx->getDelayNS(load.budget) - ctx->getDelayNS(raw_wl);
            if (slack < 0)
                tns += slack;
            worst_slack = std::min(slack, worst_slack);
        }

        int load_x, load_y;
        bool load_gb;
        ctx->estimatePosition(load_cell->bel, load_x, load_y, load_gb);
        if (load_gb)
            continue;
        xmin = std::min(xmin, load_x);
        ymin = std::min(ymin, load_y);
        xmax = std::max(xmax, load_x);
        ymax = std::max(ymax, load_y);
    }
    if (ctx->timing_driven) {
        wirelength = wirelen_t((((ymax - ymin) + (xmax - xmin)) * std::min(5.0, (1.0 + std::exp(-worst_slack / 5)))));
    } else {
        wirelength = wirelen_t((ymax - ymin) + (xmax - xmin));
    }

    return wirelength;
}

// Return the wirelength of all nets connected to a cell
template <typename T> wirelen_t get_cell_wirelength(const T &proxy, const Context *ctx, const CellInfo *cell)
{
    std::set<IdString> nets;
    for (auto p : cell->ports) {
        if (p.second.net)
            nets.insert(p.second.net->name);
    }
    wirelen_t wirelength = 0;
    float tns = 0;
    for (auto n : nets) {
        wirelength += get_net_wirelength(proxy, ctx, ctx->nets.at(n).get(), tns);
    }
    return wirelength;
}

// Return the wirelength of all nets connected to a cell, when the cell is at a given bel
template <typename T>
wirelen_t get_cell_wirelength_at_bel(const T &proxy, const Context *ctx, CellInfo *cell, BelId bel)
{
    BelId oldBel = cell->bel;
    cell->bel = bel;
    wirelen_t wirelen = get_cell_wirelength(proxy, ctx, cell);
    cell->bel = oldBel;
    return wirelen;
}

// Place a single cell in the lowest wirelength Bel available, optionally requiring validity check
bool place_single_cell(MutateContext &proxy, Context *ctx, CellInfo *cell, bool require_legality);

NEXTPNR_NAMESPACE_END

#endif
