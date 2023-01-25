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
    } else {
        CellInfo *cell = getBoundBelCell(bel);
        if (cell == nullptr) {
            return true;
        } else if (cell->type.in(id_DCUA, id_EXTREFB, id_PCSCLKDIV)) {
            return args.type != ArchArgs::LFE5U_25F && args.type != ArchArgs::LFE5U_45F &&
                   args.type != ArchArgs::LFE5U_85F;
        } else if (cell->type.in(id_MULT18X18D, id_ALU54B)) {
            return is_dsp_location_valid(cell);
        } else {
            return true;
        }
    }
}

// Check if this DSP cell placement would result in more than four distinct
// CLK/CE/RST signals per block of two DSP slices.
bool Arch::is_dsp_location_valid(CellInfo *cell) const
{
    // Find the location of the DSP0 tile.
    int block_x = cell->getLocation().x - cell->getLocation().z;
    int block_y = cell->getLocation().y;

    const std::array<std::array<IdString, 4>, 3> block_ports = {{{id_CLK0, id_CLK1, id_CLK2, id_CLK3},
                                                                 {id_CE0, id_CE1, id_CE2, id_CE3},
                                                                 {id_RST0, id_RST1, id_RST2, id_RST3}}};
    const std::array<const char *, 3> port_names = {"CLK", "CE", "RST"};
    std::array<std::set<NetInfo *>, 3> block_nets = {};
    bool cells_locked = true;

    // Count the number of distinct CLK, CE, and RST signals used by
    // all the MULT18X18D and ALU54B bels in the DSP block.
    for (int dx : {0, 1, 3, 4, 5, 7}) {
        BelId dsp_bel = getBelByLocation(Loc(block_x + dx, block_y, dx));
        CellInfo *dsp_cell = getBoundBelCell(dsp_bel);
        if (dsp_cell == nullptr)
            continue;

        if (dsp_cell->belStrength < STRENGTH_LOCKED)
            cells_locked = false;

        for (size_t i = 0; i < block_ports.size(); i++) {
            auto nets = &block_nets[i];
            for (IdString port : block_ports[i]) {
                NetInfo *net = dsp_cell->ports.at(port).net;
                if (net == nullptr)
                    continue;
                nets->insert(net);
                if (nets->size() > 4) {
                    // When all cells considered so far are locked or manually
                    // placed, the placer cannot fix this problem, so report
                    // a specific error message.
                    if (cells_locked) {
                        log_error("DSP block containing %s '%s' has more than "
                                  "four distinct %s signals.\n",
                                  dsp_cell->type.c_str(getCtx()), dsp_cell->name.c_str(getCtx()), port_names[i]);
                    }
                    return false;
                }
            }
        }
    }
    return true;
}

// Check all cells in the design to locate used DSP blocks, then remap
// CLK, CE, and RST port and attribute assignments to ensure each port
// is connected to the same net throughout each block.
void Arch::remap_dsp_blocks()
{
    std::set<Location> processed_blocks;

    const std::array<std::array<IdString, 4>, 3> block_ports = {{
            {id_CLK0, id_CLK1, id_CLK2, id_CLK3},
            {id_CE0, id_CE1, id_CE2, id_CE3},
            {id_RST0, id_RST1, id_RST2, id_RST3},
    }};

    for (auto &cell : cells) {
        CellInfo *ci = cell.second.get();
        if (!ci->type.in(id_MULT18X18D, id_ALU54B))
            continue;

        // Locate DSP0 tile for block containing this cell.
        Loc loc = ci->getLocation();
        Location block_loc(loc.x - loc.z, loc.y);
        if (processed_blocks.count(block_loc) == 1)
            continue;
        processed_blocks.insert(block_loc);

        for (auto &ports : block_ports) {
            // Store assigned nets for each port.
            std::array<NetInfo *, 4> assigned_nets = {};

            // Process each possible MULT18X18D or ALU54B in this block.
            for (int dx : {0, 1, 3, 4, 5, 7}) {
                Loc dsp_loc = Loc(block_loc.x + dx, block_loc.y, dx);
                BelId dsp_bel = getBelByLocation(dsp_loc);
                CellInfo *dsp_cell = getBoundBelCell(dsp_bel);
                if (dsp_cell == nullptr)
                    continue;
                remap_dsp_cell(dsp_cell, ports, assigned_nets);
            }
        }
    }
}

// Remap CLK/CE/RST ports in a DSP cell so that:
// * if a port's slot in assigned_nets already matches its net, no action
//   is taken.
// * if a port's slot in assigned_nets is empty and that port's net isn't in
//   assigned_nets, the slot is set to that port's current net and no remapping
//   is performed.
// * if a port's currently connected net is already present in a different slot
//   to that port, then remap references to that port to the already assigned
//   port instead.
// * if a port's slot in assigned_nets refers to a different net than the one
//   the port is currently connected to, and the currently connected net isn't
//   present elsewhere in assigned_nets, then allocate a new port for this net
//   and remap references to the old port to refer to the new port.
// This method is called with the same assigned_nets array for each cell
// inside a single DSP block. The end result is to ensure that for all cells
// in a single DSP block, all CLK/CE/RST ports are connected to the same net.
//
// ports: array of port names to remap, either CLK0-3 or CE0-3 or RST0-3
// assigned_nets: array of final net assignments to those four ports for
//                the block this cell is in.
void Arch::remap_dsp_cell(CellInfo *ci, const std::array<IdString, 4> &ports, std::array<NetInfo *, 4> &assigned_nets)
{
    // New names to use in attributes that used to refer to an old port name.
    std::array<IdString, 4> remap_ports = {};

    // Parameters that might need updating when ports are remapped.
    const std::array<IdString, 48> remap_params = {
            id_REG_INPUTA_CLK,      id_REG_INPUTA_CE,       id_REG_INPUTA_RST,     id_REG_INPUTB_CLK,
            id_REG_INPUTB_CE,       id_REG_INPUTB_RST,      id_REG_INPUTC_CLK,     id_REG_INPUTC_CE,
            id_REG_INPUTC_RST,      id_REG_PIPELINE_CLK,    id_REG_PIPELINE_CE,    id_REG_PIPELINE_RST,
            id_REG_OUTPUT_CLK,      id_REG_OUTPUT_CE,       id_REG_OUTPUT_RST,     id_REG_INPUTC0_CLK,
            id_REG_INPUTC0_CE,      id_REG_INPUTC0_RST,     id_REG_INPUTC1_CLK,    id_REG_INPUTC1_CE,
            id_REG_INPUTC1_RST,     id_REG_OPCODEOP0_0_CLK, id_REG_OPCODEOP0_0_CE, id_REG_OPCODEOP0_0_RST,
            id_REG_OPCODEOP1_0_CLK, id_REG_OPCODEOP0_1_CLK, id_REG_OPCODEOP0_1_CE, id_REG_OPCODEOP0_1_RST,
            id_REG_OPCODEOP1_1_CLK, id_REG_OPCODEIN_0_CLK,  id_REG_OPCODEIN_0_CE,  id_REG_OPCODEIN_0_RST,
            id_REG_OPCODEIN_1_CLK,  id_REG_OPCODEIN_1_CE,   id_REG_OPCODEIN_1_RST, id_REG_OUTPUT0_CLK,
            id_REG_OUTPUT0_CE,      id_REG_OUTPUT0_RST,     id_REG_OUTPUT1_CLK,    id_REG_OUTPUT1_CE,
            id_REG_OUTPUT1_RST,     id_REG_FLAG_CLK,        id_REG_FLAG_CE,        id_REG_FLAG_RST,
            id_REG_INPUTCFB_CLK,    id_REG_INPUTCFB_CE,     id_REG_INPUTCFB_RST,   id_HIGHSPEED_CLK,
    };

    // First, go through each port and determine which new port to assign
    // its net to, and what to remap any parmeters that reference it.
    for (size_t i = 0; i < ports.size(); i++) {
        IdString port = ports[i];
        NetInfo *net = ci->ports.at(port).net;
        if (net == nullptr)
            continue;
        auto assigned = std::find(assigned_nets.cbegin(), assigned_nets.cend(), net);
        if (assigned == assigned_nets.cend()) {
            if (assigned_nets[i] == nullptr) {
                // If neither the net nor the port have been assigned
                // yet, we can simply assign the net to its original
                // port and don't need to change any params.
                assigned_nets[i] = net;
            } else {
                // If the net hasn't been assigned but the port has,
                // we need to assign the net to a different port and
                // update any attributes that refer to it, while
                // ensuring the net at the new port is preserved.
                size_t j = std::distance(assigned_nets.cbegin(),
                                         std::find(assigned_nets.cbegin(), assigned_nets.cend(), nullptr));
                if (j == assigned_nets.size()) {
                    log_error("DSP block containing %s '%s': no unused ports "
                              "to remap %s to; too many distinct signals in "
                              "block.\n",
                              ci->type.c_str(getCtx()), ci->name.c_str(getCtx()), port.c_str(getCtx()));
                }
                assigned_nets[j] = net;
                remap_ports[i] = ports[j];
                log_info("DSP: %s '%s': Connection to %s remapped to %s\n", ci->type.c_str(getCtx()),
                         ci->name.c_str(getCtx()), ports[i].c_str(getCtx()), ports[j].c_str(getCtx()));
            }
        } else if (*assigned != assigned_nets[i]) {
            // If the net has been assigned already and to a different
            // port than this one, we'll remap the port and attributes
            // to point to the already-assigned port.
            size_t j = std::distance(assigned_nets.cbegin(), assigned);
            remap_ports[i] = ports[j];
            log_info("DSP: %s '%s': Connection to %s remapped to %s\n", ci->type.c_str(getCtx()),
                     ci->name.c_str(getCtx()), ports[i].c_str(getCtx()), ports[j].c_str(getCtx()));
        }
    }

    // Second, connect each port to its assigned net.
    for (size_t i = 0; i < ports.size(); i++) {
        IdString port = ports[i];
        ci->disconnectPort(port);
        if (assigned_nets[i] != nullptr) {
            ci->connectPort(port, assigned_nets[i]);
        }
    }

    // Third, remap any parameters that refer to old ports to refer to the
    // new port instead.
    for (auto remap_param : remap_params) {
        auto param = ci->params.find(remap_param);
        if (param == ci->params.end())
            continue;
        for (size_t i = 0; i < remap_ports.size(); i++) {
            Property &prop = param->second;
            if (remap_ports[i] != IdString() && prop.is_string && prop.str == ports[i].str(getCtx())) {
                prop = Property(remap_ports[i].str(getCtx()));
                break;
            }
        }
    }

    // Finally, only when remapping CLK ports, also move any `CLKn_DIV`
    // to the new clock port.
    const std::array<IdString, 4> clk_div_params = {id_CLK0_DIV, id_CLK1_DIV, id_CLK2_DIV, id_CLK3_DIV};
    std::array<Property, 4> new_clk_div_props = {};
    if (ports[0] == id_CLK0) {
        for (size_t i = 0; i < 4; i++) {
            if (remap_ports[i] == IdString())
                continue;
            auto param = ci->params.find(clk_div_params[i]);
            if (param == ci->params.end())
                continue;
            size_t j = std::distance(ports.cbegin(), std::find(ports.cbegin(), ports.cend(), remap_ports[i]));
            if (j != ports.size()) {
                new_clk_div_props[j] = param->second;
            }
        }

        for (size_t i = 0; i < 4; i++) {
            if (new_clk_div_props[i] != Property()) {
                ci->params[clk_div_params[i]] = new_clk_div_props[i];
            } else {
                ci->params.erase(clk_div_params[i]);
            }
        }
    }
}

void Arch::setup_wire_locations()
{
    wire_loc_overrides.clear();
    for (auto &cell : cells) {
        CellInfo *ci = cell.second.get();
        if (ci->bel == BelId())
            continue;
        if (ci->type.in(id_ALU54B, id_MULT18X18D, id_DCUA, id_DDRDLL, id_DQSBUFM, id_EHXPLLL)) {
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
