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

#include "cells.h"
#include "log.h"
#include "nextpnr.h"
#include "util.h"
NEXTPNR_NAMESPACE_BEGIN

inline NetInfo *port_or_nullptr(const CellInfo *cell, IdString name)
{
    auto found = cell->ports.find(name);
    if (found == cell->ports.end())
        return nullptr;
    return found->second.net;
}

bool Arch::slicesCompatible(const std::vector<const CellInfo *> &cells) const
{
    // TODO: allow different LSR/CLK and MUX/SRMODE settings once
    // routing details are worked out
    NetInfo *clk_sig = nullptr, *lsr_sig = nullptr;
    std::string CLKMUX, LSRMUX, SRMODE;
    bool first = true;
    for (auto cell : cells) {
        if (first) {
            clk_sig = port_or_nullptr(cell, id_clk);
            lsr_sig = port_or_nullptr(cell, id_lsr);
            CLKMUX = str_or_default(cell->params, id_clkmux, "CLK");
            LSRMUX = str_or_default(cell->params, id_lsrmux, "LSR");
            SRMODE = str_or_default(cell->params, id_srmode, "CE_OVER_LSR");
        } else {
            if (port_or_nullptr(cell, id_clk) != clk_sig)
                return false;
            if (port_or_nullptr(cell, id_lsr) != lsr_sig)
                return false;
            if (str_or_default(cell->params, id_clkmux, "CLK") != CLKMUX)
                return false;
            if (str_or_default(cell->params, id_lsrmux, "LSR") != LSRMUX)
                return false;
            if (str_or_default(cell->params, id_srmode, "CE_OVER_LSR") != SRMODE)
                return false;
        }
        first = false;
    }
    return true;
}

bool Arch::isBelLocationValid(BelId bel) const
{
    if (getBelType(bel) == TYPE_TRELLIS_SLICE) {
        std::vector<const CellInfo *> bel_cells;
        Loc bel_loc = getBelLocation(bel);
        for (auto bel_other : getBelsByTile(bel_loc.x, bel_loc.y)) {
            IdString cell_other = getBoundBelCell(bel_other);
            if (cell_other != IdString()) {
                const CellInfo *ci_other = cells.at(cell_other).get();
                bel_cells.push_back(ci_other);
            }
        }
        return slicesCompatible(bel_cells);
    } else {
        IdString cellId = getBoundBelCell(bel);
        if (cellId == IdString())
            return true;
        else
            return isValidBelForCell(cells.at(cellId).get(), bel);
    }
}

bool Arch::isValidBelForCell(CellInfo *cell, BelId bel) const
{
    if (cell->type == id_trellis_slice) {
        NPNR_ASSERT(getBelType(bel) == TYPE_TRELLIS_SLICE);

        std::vector<const CellInfo *> bel_cells;
        Loc bel_loc = getBelLocation(bel);
        for (auto bel_other : getBelsByTile(bel_loc.x, bel_loc.y)) {
            IdString cell_other = getBoundBelCell(bel_other);
            if (cell_other != IdString() && bel_other != bel) {
                const CellInfo *ci_other = cells.at(cell_other).get();
                bel_cells.push_back(ci_other);
            }
        }

        bel_cells.push_back(cell);
        return slicesCompatible(bel_cells);
    } else {
        // other checks
        return true;
    }
}

NEXTPNR_NAMESPACE_END
