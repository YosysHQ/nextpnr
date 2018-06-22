/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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

#include "arch_place.h"
#include "cells.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

PlaceValidityChecker::PlaceValidityChecker(Context *ctx)
        : ctx(ctx), id_icestorm_lc(ctx, "ICESTORM_LC"), id_sb_io(ctx, "SB_IO"),
          id_sb_gb(ctx, "SB_GB"), id_cen(ctx, "CEN"), id_clk(ctx, "CLK"),
          id_sr(ctx, "SR"), id_i0(ctx, "I0"), id_i1(ctx, "I1"),
          id_i2(ctx, "I2"), id_i3(ctx, "I3"), id_dff_en(ctx, "DFF_ENABLE"),
          id_neg_clk(ctx, "NEG_CLK")
{
}

static const NetInfo *get_net_or_empty(const CellInfo *cell,
                                       const IdString port)
{
    auto found = cell->ports.find(port);
    if (found != cell->ports.end())
        return found->second.net;
    else
        return nullptr;
};

bool PlaceValidityChecker::logicCellsCompatible(
        const Context *ctx, const std::vector<const CellInfo *> &cells)
{
    bool dffs_exist = false, dffs_neg = false;
    const NetInfo *cen = nullptr, *clk = nullptr, *sr = nullptr;
    int locals_count = 0;

    for (auto cell : cells) {
        if (bool_or_default(cell->params, id_dff_en)) {
            if (!dffs_exist) {
                dffs_exist = true;
                cen = get_net_or_empty(cell, id_cen);
                clk = get_net_or_empty(cell, id_clk);
                sr = get_net_or_empty(cell, id_sr);

                if (!is_global_net(ctx, cen) && cen != nullptr)
                    locals_count++;
                if (!is_global_net(ctx, clk) && clk != nullptr)
                    locals_count++;
                if (!is_global_net(ctx, sr) && sr != nullptr)
                    locals_count++;

                if (bool_or_default(cell->params, id_neg_clk)) {
                    dffs_neg = true;
                }
            } else {
                if (cen != get_net_or_empty(cell, id_cen))
                    return false;
                if (clk != get_net_or_empty(cell, id_clk))
                    return false;
                if (sr != get_net_or_empty(cell, id_sr))
                    return false;
                if (dffs_neg != bool_or_default(cell->params, id_neg_clk))
                    return false;
            }
        }

        const NetInfo *i0 = get_net_or_empty(cell, id_i0),
                      *i1 = get_net_or_empty(cell, id_i1),
                      *i2 = get_net_or_empty(cell, id_i2),
                      *i3 = get_net_or_empty(cell, id_i3);
        if (i0 != nullptr)
            locals_count++;
        if (i1 != nullptr)
            locals_count++;
        if (i2 != nullptr)
            locals_count++;
        if (i3 != nullptr)
            locals_count++;
    }

    return locals_count <= 32;
}

bool PlaceValidityChecker::isBelLocationValid(BelId bel)
{
    if (ctx->getBelType(bel) == TYPE_ICESTORM_LC) {
        std::vector<const CellInfo *> cells;
        for (auto bel_other : ctx->getBelsAtSameTile(bel)) {
            IdString cell_other = ctx->getBelCell(bel_other, false);
            if (cell_other != IdString()) {
                const CellInfo *ci_other = ctx->cells[cell_other];
                cells.push_back(ci_other);
            }
        }
        return logicCellsCompatible(ctx, cells);
    } else {
        IdString cellId = ctx->getBelCell(bel, false);
        if (cellId == IdString())
            return true;
        else
            return isValidBelForCell(ctx->cells.at(cellId), bel);
    }
}

bool PlaceValidityChecker::isValidBelForCell(CellInfo *cell, BelId bel)
{
    if (cell->type == id_icestorm_lc) {
        assert(ctx->getBelType(bel) == TYPE_ICESTORM_LC);

        std::vector<const CellInfo *> cells;

        for (auto bel_other : ctx->getBelsAtSameTile(bel)) {
            IdString cell_other = ctx->getBelCell(bel_other, false);
            if (cell_other != IdString()) {
                const CellInfo *ci_other = ctx->cells[cell_other];
                cells.push_back(ci_other);
            }
        }

        cells.push_back(cell);
        return logicCellsCompatible(ctx, cells);
    } else if (cell->type == id_sb_io) {
        return ctx->getBelPackagePin(bel) != "";
    } else if (cell->type == id_sb_gb) {
        bool is_reset = false, is_cen = false;
        assert(cell->ports.at("GLOBAL_BUFFER_OUTPUT").net != nullptr);
        for (auto user : cell->ports.at("GLOBAL_BUFFER_OUTPUT").net->users) {
            if (is_reset_port(ctx, user))
                is_reset = true;
            if (is_enable_port(ctx, user))
                is_cen = true;
        }
        IdString glb_net = ctx->getWireName(
                ctx->getWireBelPin(bel, PIN_GLOBAL_BUFFER_OUTPUT));
        int glb_id = std::stoi(std::string("") + glb_net.str().back());
        if (is_reset && is_cen)
            return false;
        else if (is_reset)
            return (glb_id % 2) == 0;
        else if (is_cen)
            return (glb_id % 2) == 1;
        else
            return true;
    } else {
        // TODO: IO cell clock checks
        return true;
    }
}

NEXTPNR_NAMESPACE_END
