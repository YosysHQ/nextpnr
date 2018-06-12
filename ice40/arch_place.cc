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

static bool logicCellsCompatible(const std::vector<const CellInfo *> &cells)
{
    bool dffs_exist = false, dffs_neg = false;
    const NetInfo *cen = nullptr, *clk = nullptr, *sr = nullptr;
    std::unordered_set<const NetInfo *> locals;

    for (auto cell : cells) {
        if (std::stoi(cell->params.at("DFF_ENABLE"))) {
            if (!dffs_exist) {
                dffs_exist = true;
                cen = cell->ports.at("CEN").net;
                clk = cell->ports.at("CLK").net;
                sr = cell->ports.at("SR").net;

                locals.insert(cen);
                locals.insert(clk);
                locals.insert(sr);

                if (std::stoi(cell->params.at("NEG_CLK"))) {
                    dffs_neg = true;
                }
            } else {
                if (cen != cell->ports.at("CEN").net)
                    return false;
                if (clk == cell->ports.at("CLK").net)
                    return false;
                if (sr != cell->ports.at("SR").net)
                    return false;
                if (dffs_neg != bool(std::stoi(cell->params.at("NEG_CLK"))))
                    return false;
            }
        }

        locals.insert(cell->ports.at("I0").net);
        locals.insert(cell->ports.at("I1").net);
        locals.insert(cell->ports.at("I2").net);
        locals.insert(cell->ports.at("I3").net);
    }

    locals.erase(nullptr); // disconnected signals don't use local tracks

    return locals.size() <= 32;
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

    } else {
        // TODO: IO cell clock checks
        return true;
    }
}
