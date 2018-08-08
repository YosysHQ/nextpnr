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

NEXTPNR_NAMESPACE_BEGIN

bool Arch::logicCellsCompatible(const std::vector<const CellInfo *> &cells) const
{
    bool dffs_exist = false, dffs_neg = false;
    const NetInfo *cen = nullptr, *clk = nullptr, *sr = nullptr;
    int locals_count = 0;

    for (auto cell : cells) {
        NPNR_ASSERT(cell->belType == TYPE_ICESTORM_LC);
        if (cell->lcInfo.dffEnable) {
            if (!dffs_exist) {
                dffs_exist = true;
                cen = cell->lcInfo.cen;
                clk = cell->lcInfo.clk;
                sr = cell->lcInfo.sr;

                if (cen != nullptr && !cen->is_global)
                    locals_count++;
                if (clk != nullptr && !clk->is_global)
                    locals_count++;
                if (sr != nullptr && !sr->is_global)
                    locals_count++;

                if (cell->lcInfo.negClk) {
                    dffs_neg = true;
                }
            } else {
                if (cen != cell->lcInfo.cen)
                    return false;
                if (clk != cell->lcInfo.clk)
                    return false;
                if (sr != cell->lcInfo.sr)
                    return false;
                if (dffs_neg != cell->lcInfo.negClk)
                    return false;
            }
        }

        locals_count += cell->lcInfo.inputCount;
    }

    return locals_count <= 32;
}

bool Arch::isBelLocationValid(BelId bel) const
{
    if (getBelType(bel) == TYPE_ICESTORM_LC) {
        std::vector<const CellInfo *> bel_cells;
        Loc bel_loc = getBelLocation(bel);
        for (auto bel_other : getBelsByTile(bel_loc.x, bel_loc.y)) {
            CellInfo *ci_other = getBoundBelCell(bel_other);
            if (ci_other != nullptr) {
                bel_cells.push_back(ci_other);
            }
        }
        return logicCellsCompatible(bel_cells);
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
    if (cell->type == id_icestorm_lc) {
        NPNR_ASSERT(getBelType(bel) == TYPE_ICESTORM_LC);

        std::vector<const CellInfo *> bel_cells;
        Loc bel_loc = getBelLocation(bel);
        for (auto bel_other : getBelsByTile(bel_loc.x, bel_loc.y)) {
            CellInfo *ci_other = getBoundBelCell(bel_other);
            if (ci_other != nullptr && bel_other != bel) {
                bel_cells.push_back(ci_other);
            }
        }

        bel_cells.push_back(cell);
        return logicCellsCompatible(bel_cells);
    } else if (cell->type == id_sb_io) {
        // Do not allow placement of input SB_IOs on blocks where there a PLL is outputting to.

        // Find shared PLL by looking for driving bel siblings from D_IN_0
        // that are a PLL clock output.
        auto wire = getBelPinWire(bel, PIN_D_IN_0);
        PortPin pll_bel_pin;
        BelId pll_bel;
        for (auto pin : getWireBelPins(wire)) {
            if (pin.pin == PIN_PLLOUT_A || pin.pin == PIN_PLLOUT_B) {
                pll_bel = pin.bel;
                pll_bel_pin = pin.pin;
                break;
            }
        }
        // Is there a PLL that shares this IO buffer?
        if (pll_bel.index != -1) {
            auto pll_cell = getBoundBelCell(pll_bel);
            // Is a PLL placed in this PLL bel?
            if (pll_cell != nullptr) {
                // Is the shared port driving a net?
                auto pi = pll_cell->ports[portPinToId(pll_bel_pin)];
                if (pi.net != nullptr) {
                    // Are we perhaps a PAD INPUT Bel that can be placed here?
                    if (pll_cell->attrs[id("BEL_PAD_INPUT")] == getBelName(bel).str(this)) {
                        return true;
                    }
                    return false;
                }
            }
        }
        return getBelPackagePin(bel) != "";
    } else if (cell->type == id_sb_gb) {
        NPNR_ASSERT(cell->ports.at(id_glb_buf_out).net != nullptr);
        const NetInfo *net = cell->ports.at(id_glb_buf_out).net;
        IdString glb_net = getWireName(getBelPinWire(bel, PIN_GLOBAL_BUFFER_OUTPUT));
        int glb_id = std::stoi(std::string("") + glb_net.str(this).back());
        if (net->is_reset && net->is_enable)
            return false;
        else if (net->is_reset)
            return (glb_id % 2) == 0;
        else if (net->is_enable)
            return (glb_id % 2) == 1;
        else
            return true;
    } else {
        // TODO: IO cell clock checks
        return true;
    }
}

NEXTPNR_NAMESPACE_END
