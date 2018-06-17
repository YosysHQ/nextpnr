/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
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

static const NetInfo *get_net_or_nullptr(const CellInfo *cell,
                                         const IdString port)
{
    auto found = cell->ports.find(port);
    if (found != cell->ports.end())
        return found->second.net;
    else
        return nullptr;
};

static bool logicCellsCompatible(const std::vector<const CellInfo *> &cells)
{
    bool dffs_exist = false, dffs_neg = false;
    const NetInfo *cen = nullptr, *clk = nullptr, *sr = nullptr;
    std::unordered_set<const NetInfo *> locals;

    for (auto cell : cells) {
        if (bool_or_default(cell->params, "DFF_ENABLE")) {
            if (!dffs_exist) {
                dffs_exist = true;
                cen = get_net_or_nullptr(cell, "CEN");
                clk = get_net_or_nullptr(cell, "CLK");
                sr = get_net_or_nullptr(cell, "SR");

                if (!is_global_net(cen))
                    locals.insert(cen);
                if (!is_global_net(clk))
                    locals.insert(clk);
                if (!is_global_net(sr))
                    locals.insert(sr);

                if (bool_or_default(cell->params, "NEG_CLK")) {
                    dffs_neg = true;
                }
            } else {
                if (cen != get_net_or_nullptr(cell, "CEN"))
                    return false;
                if (clk != get_net_or_nullptr(cell, "CLK"))
                    return false;
                if (sr != get_net_or_nullptr(cell, "SR"))
                    return false;
                if (dffs_neg != bool_or_default(cell->params, "NEG_CLK"))
                    return false;
            }
        }

        locals.insert(get_net_or_nullptr(cell, "I0"));
        locals.insert(get_net_or_nullptr(cell, "I1"));
        locals.insert(get_net_or_nullptr(cell, "I2"));
        locals.insert(get_net_or_nullptr(cell, "I3"));
    }

    locals.erase(nullptr); // disconnected signals don't use local tracks

    return locals.size() <= 32;
}

bool isBelLocationValid(Design *design, BelId bel) {
    const Chip &chip = design->chip;
    if (chip.getBelType(bel) == TYPE_ICESTORM_LC) {
        std::vector<const CellInfo *> cells;
        for (auto bel_other : chip.getBelsAtSameTile(bel)) {
            IdString cell_other = chip.getBelCell(bel_other, false);
            if (cell_other != IdString()) {
                const CellInfo *ci_other = design->cells[cell_other];
                cells.push_back(ci_other);
            }
        }
        return logicCellsCompatible(cells);
    } else {
        IdString cellId = chip.getBelCell(bel, false);
        if (cellId == IdString())
            return true;
        else
            return isValidBelForCell(design, design->cells.at(cellId), bel);
    }
}

bool isValidBelForCell(Design *design, CellInfo *cell, BelId bel)
{
    const Chip &chip = design->chip;
    if (cell->type == "ICESTORM_LC") {
        assert(chip.getBelType(bel) == TYPE_ICESTORM_LC);

        std::vector<const CellInfo *> cells;

        for (auto bel_other : chip.getBelsAtSameTile(bel)) {
            IdString cell_other = chip.getBelCell(bel_other, false);
            if (cell_other != IdString()) {
                const CellInfo *ci_other = design->cells[cell_other];
                cells.push_back(ci_other);
            }
        }

        cells.push_back(cell);
        return logicCellsCompatible(cells);
    } else if (cell->type == "SB_IO") {
        return design->chip.getBelPackagePin(bel) != "";
    } else if (cell->type == "SB_GB") {
        bool is_reset = false, is_cen = false;
        assert(cell->ports.at("GLOBAL_BUFFER_OUTPUT").net != nullptr);
        for (auto user : cell->ports.at("GLOBAL_BUFFER_OUTPUT").net->users) {
            if (is_reset_port(user))
                is_reset = true;
            if (is_enable_port(user))
                is_cen = true;
        }
        IdString glb_net = chip.getWireName(
                chip.getWireBelPin(bel, PIN_GLOBAL_BUFFER_OUTPUT));
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
