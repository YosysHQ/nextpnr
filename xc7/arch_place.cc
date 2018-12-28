/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@symbioticeda.com>
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
#include "nextpnr.h"
#include "util.h"

#include <boost/range/iterator_range.hpp>

NEXTPNR_NAMESPACE_BEGIN

bool Arch::logicCellsCompatible(const CellInfo **it, const size_t size) const
{
    // TODO: Check clock, clock-enable, and set-reset compatiility
    return true;
}

bool Arch::isBelLocationValid(BelId bel) const
{
    if (getBelType(bel) == id("XC7_LC")) {
        std::array<const CellInfo *, 4> bel_cells;
        size_t num_cells = 0;
        Loc bel_loc = getBelLocation(bel);
        for (auto bel_other : getBelsByTile(bel_loc.x, bel_loc.y)) {
            CellInfo *ci_other = getBoundBelCell(bel_other);
            if (ci_other != nullptr)
                bel_cells[num_cells++] = ci_other;
        }
        return logicCellsCompatible(bel_cells.data(), num_cells);
    } else {
        CellInfo *ci = getBoundBelCell(bel);
        if (ci == nullptr)
            return true;
        else
            return isValidBelForCell(ci, bel);
    }
}

bool Arch::isValidBelForCell(CellInfo *cell, BelId bel) const
{
    if (cell->type == id("XC7_LC")) {
        std::array<const CellInfo *, 4> bel_cells;
        size_t num_cells = 0;
    
        Loc bel_loc = getBelLocation(bel);
        for (auto bel_other : getBelsByTile(bel_loc.x, bel_loc.y)) {
            CellInfo *ci_other = getBoundBelCell(bel_other);
            if (ci_other != nullptr && bel_other != bel)
                bel_cells[num_cells++] = ci_other;
        }
    
        bel_cells[num_cells++] = cell;
        return logicCellsCompatible(bel_cells.data(), num_cells);
    }
    else {
        return true;
    }
}

NEXTPNR_NAMESPACE_END
