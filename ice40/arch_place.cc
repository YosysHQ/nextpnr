/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2018  Serge Bazanski <q3k@q3k.org>
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

bool Arch::logic_cells_compatible(const CellInfo **it, const size_t size) const
{
    bool dffs_exist = false, dffs_neg = false;
    const NetInfo *cen = nullptr, *clk = nullptr, *sr = nullptr;
    int locals_count = 0;

    for (auto cell : boost::make_iterator_range(it, it + size)) {
        NPNR_ASSERT(cell->type == id_ICESTORM_LC);
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

static inline bool _io_pintype_need_clk_in(unsigned pin_type) { return (pin_type & 0x01) == 0x00; }

static inline bool _io_pintype_need_clk_out(unsigned pin_type)
{
    return ((pin_type & 0x30) == 0x30) || ((pin_type & 0x3c) && ((pin_type & 0x0c) != 0x08));
}

static inline bool _io_pintype_need_clk_en(unsigned pin_type)
{
    return _io_pintype_need_clk_in(pin_type) || _io_pintype_need_clk_out(pin_type);
}

bool Arch::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    if (getBelType(bel) == id_ICESTORM_LC) {
        std::array<const CellInfo *, 8> bel_cells;
        size_t num_cells = 0;
        Loc bel_loc = getBelLocation(bel);
        for (auto bel_other : getBelsByTile(bel_loc.x, bel_loc.y)) {
            CellInfo *ci_other = getBoundBelCell(bel_other);
            if (ci_other != nullptr)
                bel_cells[num_cells++] = ci_other;
        }
        return logic_cells_compatible(bel_cells.data(), num_cells);
    } else {
        const CellInfo *cell = getBoundBelCell(bel);
        if (cell == nullptr)
            return true;
        else if (cell->type == id_SB_IO) {
            // Do not allow placement of input SB_IOs on blocks where there a PLL is outputting to.

            // Find shared PLL by looking for driving bel siblings from D_IN_0
            // that are a PLL clock output.
            auto wire = getBelPinWire(bel, id_D_IN_0);
            for (auto pin : getWireBelPins(wire)) {
                if (pin.pin.in(id_PLLOUT_A, id_PLLOUT_B)) {
                    // Is there a PLL there ?
                    const CellInfo *pll_cell = getBoundBelCell(pin.bel);
                    if (pll_cell == nullptr)
                        break;

                    // Is that port actually used ?
                    if ((pin.pin == id_PLLOUT_B) && !is_sb_pll40_dual(this, pll_cell))
                        break;

                    // Is that SB_IO used at an input ?
                    if ((cell->getPort(id_D_IN_0) == nullptr) && (cell->getPort(id_D_IN_1) == nullptr))
                        break;

                    // Are we perhaps a PAD INPUT Bel that can be placed here?
                    if (str_or_default(pll_cell->attrs, id_BEL_PAD_INPUT, "") == getBelName(bel).str(getCtx()))
                        return true;

                    // Conflict
                    if (explain_invalid)
                        log_nonfatal_error("Cell '%s' conflicts with PLL cell '%s'\n", nameOf(cell), nameOf(pll_cell));
                    return false;
                }
            }

            Loc ioLoc = getBelLocation(bel);
            Loc compLoc = ioLoc;
            compLoc.z = 1 - compLoc.z;

            // Check LVDS pairing
            if (cell->ioInfo.lvds) {
                // Check correct z and complement location is free
                if (ioLoc.z != 0) {
                    if (explain_invalid)
                        log_nonfatal_error("Bel '%s' can't be used for LVDS\n", getCtx()->nameOfBel(bel));
                    return false;
                }
                BelId compBel = getBelByLocation(compLoc);
                CellInfo *compCell = getBoundBelCell(compBel);
                if (compCell) {
                    if (explain_invalid)
                        log_nonfatal_error("Cell '%s' LVDS complement occupied by cell '%s'\n", nameOf(cell),
                                           nameOf(compCell));
                    return false;
                }
            } else {
                // Check LVDS IO is not placed at complement location
                BelId compBel = getBelByLocation(compLoc);
                const CellInfo *compCell = getBoundBelCell(compBel);
                if (compCell && compCell->ioInfo.lvds) {
                    if (explain_invalid)
                        log_nonfatal_error("Cell '%s' can't occupy LVDS complement of cell '%s'\n", nameOf(cell),
                                           nameOf(compCell));
                    return false;
                }

                // Check for conflicts on shared nets
                // - CLOCK_ENABLE
                // - OUTPUT_CLK
                // - INPUT_CLK
                if (compCell) {
                    bool use[6] = {
                            _io_pintype_need_clk_in(cell->ioInfo.pintype),
                            _io_pintype_need_clk_in(compCell->ioInfo.pintype),
                            _io_pintype_need_clk_out(cell->ioInfo.pintype),
                            _io_pintype_need_clk_out(compCell->ioInfo.pintype),
                            _io_pintype_need_clk_en(cell->ioInfo.pintype),
                            _io_pintype_need_clk_en(compCell->ioInfo.pintype),
                    };
                    const NetInfo *nets[] = {
                            cell->getPort(id_INPUT_CLK),    compCell->getPort(id_INPUT_CLK),
                            cell->getPort(id_OUTPUT_CLK),   compCell->getPort(id_OUTPUT_CLK),
                            cell->getPort(id_CLOCK_ENABLE), compCell->getPort(id_CLOCK_ENABLE),
                    };

                    for (int i = 0; i < 6; i++)
                        if (use[i] && (nets[i] != nets[i ^ 1]) && (use[i ^ 1] || (nets[i ^ 1] != nullptr))) {
                            if (explain_invalid)
                                log_nonfatal_error("Net '%s' for cell '%s' conflicts with net '%s' for '%s'\n",
                                                   nameOf(nets[i]), nameOf(cell), nameOf(nets[i ^ 1]),
                                                   nameOf(compCell));
                            return false;
                        }
                }
            }

            return get_bel_package_pin(bel) != "";
        } else if (cell->type == id_SB_GB) {
            if (cell->gbInfo.forPadIn)
                return true;
            NPNR_ASSERT(cell->ports.at(id_GLOBAL_BUFFER_OUTPUT).net != nullptr);
            const NetInfo *net = cell->ports.at(id_GLOBAL_BUFFER_OUTPUT).net;
            int glb_id = get_driven_glb_netwk(bel);
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
}

NEXTPNR_NAMESPACE_END
