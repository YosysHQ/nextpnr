/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
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
#include "design_utils.h"
#include "log.h"
#include "nextpnr.h"
#include "timing.h"
#include "util.h"
NEXTPNR_NAMESPACE_BEGIN

inline NetInfo *port_or_nullptr(const CellInfo *cell, IdString name)
{
    auto found = cell->ports.find(name);
    if (found == cell->ports.end())
        return nullptr;
    return found->second.net;
}

bool Arch::slices_compatible(LogicTileStatus *lts) const
{
    if (lts == nullptr)
        return true;
    for (int sl = 0; sl < 4; sl++) {
        if (!lts->slices[sl].dirty) {
            if (!lts->slices[sl].valid)
                return false;
            continue;
        }
        lts->slices[sl].dirty = false;
        lts->slices[sl].valid = false;
        bool found_ff = false;
        uint8_t last_ff_flags = 0;
        IdString last_ce_sig;
        bool ramw_used = false;
        if (sl == 2 && lts->cells[((sl * 2) << lc_idx_shift) | BEL_RAMW] != nullptr)
            ramw_used = true;
        for (int l = 0; l < 2; l++) {
            bool comb_m_used = false;
            CellInfo *comb = lts->cells[((sl * 2 + l) << lc_idx_shift) | BEL_COMB];
            if (comb != nullptr) {
                uint8_t flags = comb->combInfo.flags;
                if (ramw_used && !(flags & ArchCellInfo::COMB_RAMW_BLOCK))
                    return false;
                if (flags & ArchCellInfo::COMB_MUX5) {
                    // MUX5 uses M signal and must be in LC 0
                    comb_m_used = true;
                    if (l != 0)
                        return false;
                }
                if (flags & ArchCellInfo::COMB_MUX6) {
                    // MUX6+ uses M signal and must be in LC 1
                    comb_m_used = true;
                    if (l != 1)
                        return false;
                    if (comb->combInfo.mux_fxad != nullptr &&
                        (comb->combInfo.mux_fxad->combInfo.flags & ArchCellInfo::COMB_MUX5)) {
                        // LUT6 structure must be rooted at SLICE 0 or 2
                        if (sl != 0 && sl != 2)
                            return false;
                    }
                }
                // LUTRAM must be in bottom two SLICEs only
                if ((flags & ArchCellInfo::COMB_LUTRAM) && (sl > 1))
                    return false;
                if (l == 1) {
                    // Carry usage must be the same for LCs 0 and 1 in a SLICE
                    CellInfo *comb0 = lts->cells[((sl * 2 + 0) << lc_idx_shift) | BEL_COMB];
                    if (comb0 &&
                        ((comb0->combInfo.flags & ArchCellInfo::COMB_CARRY) != (flags & ArchCellInfo::COMB_CARRY)))
                        return false;
                }
            }

            CellInfo *ff = lts->cells[((sl * 2 + l) << lc_idx_shift) | BEL_FF];
            if (ff != nullptr) {
                uint8_t flags = ff->ffInfo.flags;
                if (comb_m_used && (flags & ArchCellInfo::FF_M_USED))
                    return false;
                if (found_ff) {
                    if ((flags & ArchCellInfo::FF_GSREN) != (last_ff_flags & ArchCellInfo::FF_GSREN))
                        return false;
                    if ((flags & ArchCellInfo::FF_CECONST) != (last_ff_flags & ArchCellInfo::FF_CECONST))
                        return false;
                    if ((flags & ArchCellInfo::FF_CEINV) != (last_ff_flags & ArchCellInfo::FF_CEINV))
                        return false;
                    if (ff->ffInfo.ce_sig != last_ce_sig)
                        return false;
                } else {
                    found_ff = true;
                    last_ff_flags = flags;
                    last_ce_sig = ff->ffInfo.ce_sig;
                }
            }
        }

        lts->slices[sl].valid = true;
    }
    if (lts->tile_dirty) {
        bool found_global_ff = false;
        bool found_global_dpram = false;
        bool global_lsrinv = false;
        bool global_clkinv = false;
        bool global_async = false;

        IdString clk_sig, lsr_sig;

        lts->tile_dirty = false;
        lts->tile_valid = false;

#define CHECK_EQUAL(x, y)                                                                                              \
    do {                                                                                                               \
        if ((x) != (y))                                                                                                \
            return false;                                                                                              \
    } while (0)
        for (int i = 0; i < 8; i++) {
            if (i < 4) {
                // DPRAM
                CellInfo *comb = lts->cells[(i << lc_idx_shift) | BEL_COMB];
                if (comb != nullptr && (comb->combInfo.flags & ArchCellInfo::COMB_LUTRAM)) {
                    if (found_global_dpram) {
                        CHECK_EQUAL(bool(comb->combInfo.flags & ArchCellInfo::COMB_RAM_WCKINV), global_clkinv);
                        CHECK_EQUAL(bool(comb->combInfo.flags & ArchCellInfo::COMB_RAM_WREINV), global_lsrinv);
                    } else {
                        global_clkinv = bool(comb->combInfo.flags & ArchCellInfo::COMB_RAM_WCKINV);
                        global_lsrinv = bool(comb->combInfo.flags & ArchCellInfo::COMB_RAM_WREINV);
                        found_global_dpram = true;
                    }
                }
            }
            // FF
            CellInfo *ff = lts->cells[(i << lc_idx_shift) | BEL_FF];
            if (ff != nullptr) {
                if (found_global_dpram) {
                    // Do not allow SLICEC to have FF if there is already RAMW in it
                    if (i == 4 || i == 5)
                        return false;
                    CHECK_EQUAL(bool(ff->ffInfo.flags & ArchCellInfo::FF_CLKINV), global_clkinv);
                    CHECK_EQUAL(bool(ff->ffInfo.flags & ArchCellInfo::FF_LSRINV), global_lsrinv);
                }
                if (found_global_ff) {
                    CHECK_EQUAL(ff->ffInfo.clk_sig, clk_sig);
                    CHECK_EQUAL(ff->ffInfo.lsr_sig, lsr_sig);
                    CHECK_EQUAL(bool(ff->ffInfo.flags & ArchCellInfo::FF_CLKINV), global_clkinv);
                    CHECK_EQUAL(bool(ff->ffInfo.flags & ArchCellInfo::FF_LSRINV), global_lsrinv);
                    CHECK_EQUAL(bool(ff->ffInfo.flags & ArchCellInfo::FF_ASYNC), global_async);

                } else {
                    clk_sig = ff->ffInfo.clk_sig;
                    lsr_sig = ff->ffInfo.lsr_sig;
                    global_clkinv = bool(ff->ffInfo.flags & ArchCellInfo::FF_CLKINV);
                    global_lsrinv = bool(ff->ffInfo.flags & ArchCellInfo::FF_LSRINV);
                    global_async = bool(ff->ffInfo.flags & ArchCellInfo::FF_ASYNC);
                    found_global_ff = true;
                }
            }
        }
#undef CHECK_EQUAL
        lts->tile_valid = true;
    } else {
        if (!lts->tile_valid)
            return false;
    }

    return true;
}

bool Arch::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    IdString bel_type = getBelType(bel);
    if (bel_type.in(id_TRELLIS_COMB, id_TRELLIS_FF, id_TRELLIS_RAMW)) {
        return slices_compatible(tile_status.at(tile_index(bel)).lts);
    }
    return true;
}

void Arch::setup_wire_locations()
{
    wire_loc_overrides.clear();
    for (auto &cell : cells) {
        CellInfo *ci = cell.second.get();
        if (ci->bel == BelId())
            continue;
        if (ci->type.in(/*id_ALU54B, id_MULT18X18D, id_DCUA, id_DDRDLL, id_DQSBUFM,*/ id_EHXPLLJ)) {
            for (auto &port : ci->ports) {
                if (port.second.net == nullptr)
                    continue;
                WireId pw = getBelPinWire(ci->bel, port.first);
                if (pw == WireId())
                    continue;
                if (port.second.type == PORT_OUT) {
                    for (auto dh : getPipsDownhill(pw)) {
                        WireId pip_dst = getPipDstWire(dh);
                        wire_loc_overrides[pw] = std::make_pair(pip_dst.location.x, pip_dst.location.y);
                        break;
                    }
                } else {
                    for (auto uh : getPipsUphill(pw)) {
                        WireId pip_src = getPipSrcWire(uh);
                        wire_loc_overrides[pw] = std::make_pair(pip_src.location.x, pip_src.location.y);
                        break;
                    }
                }
            }
        }
    }
}

NEXTPNR_NAMESPACE_END
