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

#include "cells.h"
#include "nextpnr.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

bool Arch::logicCellsCompatible(const std::vector<const CellInfo *> &cells) const
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

                if (!isGlobalNet(cen) && cen != nullptr)
                    locals_count++;
                if (!isGlobalNet(clk) && clk != nullptr)
                    locals_count++;
                if (!isGlobalNet(sr) && sr != nullptr)
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

        const NetInfo *i0 = get_net_or_empty(cell, id_i0), *i1 = get_net_or_empty(cell, id_i1),
                      *i2 = get_net_or_empty(cell, id_i2), *i3 = get_net_or_empty(cell, id_i3);
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

bool Arch::isBelLocationValid(BelId bel) const
{
    if (getBelType(bel) == TYPE_ICESTORM_LC) {
        std::vector<const CellInfo *> bel_cells;
        for (auto bel_other : getBelsAtSameTile(bel)) {
            IdString cell_other = getBoundBelCell(bel_other);
            if (cell_other != IdString()) {
                const CellInfo *ci_other = cells.at(cell_other).get();
                bel_cells.push_back(ci_other);
            }
        }
        return logicCellsCompatible(bel_cells);
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
    if (cell->type == id_icestorm_lc) {
        assert(getBelType(bel) == TYPE_ICESTORM_LC);

        std::vector<const CellInfo *> bel_cells;

        for (auto bel_other : getBelsAtSameTile(bel)) {
            IdString cell_other = getBoundBelCell(bel_other);
            if (cell_other != IdString() && bel_other != bel) {
                const CellInfo *ci_other = cells.at(cell_other).get();
                bel_cells.push_back(ci_other);
            }
        }

        bel_cells.push_back(cell);
        return logicCellsCompatible(bel_cells);
    } else if (cell->type == id_sb_io) {
        return getBelPackagePin(bel) != "";
    } else if (cell->type == id_sb_gb) {
        bool is_reset = false, is_cen = false;
        assert(cell->ports.at(id_glb_buf_out).net != nullptr);
        for (auto user : cell->ports.at(id_glb_buf_out).net->users) {
            if (is_reset_port(this, user))
                is_reset = true;
            if (is_enable_port(this, user))
                is_cen = true;
        }
        IdString glb_net = getWireName(getWireBelPin(bel, PIN_GLOBAL_BUFFER_OUTPUT));
        int glb_id = std::stoi(std::string("") + glb_net.str(this).back());
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
