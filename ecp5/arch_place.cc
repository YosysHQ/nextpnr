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

bool Arch::slicesCompatible(LogicTileStatus *lts) const
{
    if (lts == nullptr)
        return true;
    for (int sl = 0; sl < 4; sl++) {
        if (!lts->slices[sl].dirty)
            continue;
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
                if (ramw_used)
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
                uint8_t flags = comb->ffInfo.flags;
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
                    if (found_global_ff) {
                        CHECK_EQUAL(comb->combInfo.ram_wck, clk_sig);
                        CHECK_EQUAL(comb->combInfo.ram_wre, lsr_sig);
                        CHECK_EQUAL(bool(comb->combInfo.flags & ArchCellInfo::COMB_RAM_WCKINV), global_clkinv);
                        CHECK_EQUAL(bool(comb->combInfo.flags & ArchCellInfo::COMB_RAM_WREINV), global_lsrinv);
                    } else {
                        clk_sig = comb->combInfo.ram_wck;
                        lsr_sig = comb->combInfo.ram_wre;
                        global_clkinv = bool(comb->combInfo.flags & ArchCellInfo::COMB_RAM_WCKINV);
                        global_lsrinv = bool(comb->combInfo.flags & ArchCellInfo::COMB_RAM_WREINV);
                        found_global_ff = true;
                    }
                }
            }
            // FF
            CellInfo *ff = lts->cells[(i << lc_idx_shift) | BEL_FF];
            if (ff != nullptr) {
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
    }

    return true;
}

bool Arch::isBelLocationValid(BelId bel) const
{
    if (getBelType(bel) == id_TRELLIS_SLICE) {
        return slicesCompatible(tileStatus.at(tile_index(bel)).lts);
    } else {
        CellInfo *cell = getBoundBelCell(bel);
        if (cell == nullptr)
            return true;
        else
            return isValidBelForCell(cell, bel);
    }
}

bool Arch::isValidBelForCell(CellInfo *cell, BelId bel) const
{
    if (cell->type == id_TRELLIS_SLICE) {
        NPNR_ASSERT(getBelType(bel) == id_TRELLIS_SLICE);

        LogicTileStatus lts = *(tileStatus.at(tile_index(bel)).lts);
        int z = locInfo(bel)->bel_data[bel.index].z;
        lts.cells[z] = cell;
        return slicesCompatible(&lts);
    } else if (cell->type == id_DCUA || cell->type == id_EXTREFB || cell->type == id_PCSCLKDIV) {
        return args.type != ArchArgs::LFE5U_25F && args.type != ArchArgs::LFE5U_45F && args.type != ArchArgs::LFE5U_85F;
    } else {
        // other checks
        return true;
    }
}

void Arch::permute_luts()
{
    NetCriticalityMap nc;
    get_criticalities(getCtx(), &nc);

    std::unordered_map<PortInfo *, size_t> port_to_user;
    for (auto net : sorted(nets)) {
        NetInfo *ni = net.second;
        for (size_t i = 0; i < ni->users.size(); i++) {
            auto &usr = ni->users.at(i);
            port_to_user[&(usr.cell->ports.at(usr.port))] = i;
        }
    }

    auto proc_lut = [&](CellInfo *ci, int lut) {
        std::vector<IdString> port_names;
        for (int i = 0; i < 4; i++)
            port_names.push_back(id(std::string("ABCD").substr(i, 1) + std::to_string(lut)));

        std::vector<std::pair<float, int>> inputs;
        std::vector<NetInfo *> orig_nets;

        for (int i = 0; i < 4; i++) {
            if (!ci->ports.count(port_names.at(i))) {
                ci->ports[port_names.at(i)].name = port_names.at(i);
                ci->ports[port_names.at(i)].type = PORT_IN;
            }
            auto &port = ci->ports.at(port_names.at(i));
            float crit = 0;
            if (port.net != nullptr && nc.count(port.net->name)) {
                auto &n = nc.at(port.net->name);
                size_t usr = port_to_user.at(&port);
                if (usr < n.criticality.size())
                    crit = n.criticality.at(usr);
            }
            orig_nets.push_back(port.net);
            inputs.emplace_back(crit, i);
        }
        // Least critical first (A input is slowest)

        // Avoid permuting locked LUTs (e.g. from an OOC submodule)
        if (ci->belStrength <= STRENGTH_STRONG)
            std::sort(inputs.begin(), inputs.end());
        for (int i = 0; i < 4; i++) {
            IdString p = port_names.at(i);
            // log_info("%s %s %f\n", p.c_str(ctx), port_names.at(inputs.at(i).second).c_str(ctx), inputs.at(i).first);
            disconnect_port(getCtx(), ci, p);
            ci->ports.at(p).net = nullptr;
            if (orig_nets.at(inputs.at(i).second) != nullptr) {
                connect_port(getCtx(), orig_nets.at(inputs.at(i).second), ci, p);
                ci->params[id(p.str(this) + "MUX")] = p.str(this);
            } else {
                ci->params[id(p.str(this) + "MUX")] = std::string("1");
            }
        }
        // Rewrite function
        int old_init = int_or_default(ci->params, id("LUT" + std::to_string(lut) + "_INITVAL"), 0);
        int new_init = 0;
        for (int i = 0; i < 16; i++) {
            int old_index = 0;
            for (int k = 0; k < 4; k++) {
                if (i & (1 << k))
                    old_index |= (1 << inputs.at(k).second);
            }
            if (old_init & (1 << old_index))
                new_init |= (1 << i);
        }
        ci->params[id("LUT" + std::to_string(lut) + "_INITVAL")] = Property(new_init, 16);
    };

    for (auto cell : sorted(cells)) {
        CellInfo *ci = cell.second;
        if (ci->type == id_TRELLIS_SLICE && str_or_default(ci->params, id("MODE"), "LOGIC") == "LOGIC") {
            proc_lut(ci, 0);
            proc_lut(ci, 1);
        }
    }
}

void Arch::setupWireLocations()
{
    wire_loc_overrides.clear();
    for (auto cell : sorted(cells)) {
        CellInfo *ci = cell.second;
        if (ci->bel == BelId())
            continue;
        if (ci->type == id_MULT18X18D || ci->type == id_DCUA || ci->type == id_DDRDLL || ci->type == id_DQSBUFM ||
            ci->type == id_EHXPLLL) {
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
