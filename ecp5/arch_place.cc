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

bool Arch::slicesCompatible(const std::vector<const CellInfo *> &cells) const
{
    // TODO: allow different LSR/CLK and MUX/SRMODE settings once
    // routing details are worked out
    IdString clk_sig, lsr_sig;
    IdString CLKMUX, LSRMUX, SRMODE;
    bool first = true;
    for (auto cell : cells) {
        if (cell->sliceInfo.using_dff) {
            if (first) {
                clk_sig = cell->sliceInfo.clk_sig;
                lsr_sig = cell->sliceInfo.lsr_sig;
                CLKMUX = cell->sliceInfo.clkmux;
                LSRMUX = cell->sliceInfo.lsrmux;
                SRMODE = cell->sliceInfo.srmode;
            } else {
                if (cell->sliceInfo.clk_sig != clk_sig)
                    return false;
                if (cell->sliceInfo.lsr_sig != lsr_sig)
                    return false;
                if (cell->sliceInfo.clkmux != CLKMUX)
                    return false;
                if (cell->sliceInfo.lsrmux != LSRMUX)
                    return false;
                if (cell->sliceInfo.srmode != SRMODE)
                    return false;
            }
            first = false;
        }
    }
    return true;
}

bool Arch::isBelLocationValid(BelId bel) const
{
    if (getBelType(bel) == id_TRELLIS_SLICE) {
        std::vector<const CellInfo *> bel_cells;
        Loc bel_loc = getBelLocation(bel);
        for (auto bel_other : getBelsByTile(bel_loc.x, bel_loc.y)) {
            CellInfo *cell_other = getBoundBelCell(bel_other);
            if (cell_other != nullptr) {
                bel_cells.push_back(cell_other);
            }
        }
        if (getBoundBelCell(bel) != nullptr && getBoundBelCell(bel)->sliceInfo.has_l6mux && ((bel_loc.z % 2) == 1))
            return false;
        return slicesCompatible(bel_cells);
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

        std::vector<const CellInfo *> bel_cells;
        Loc bel_loc = getBelLocation(bel);

        if (cell->sliceInfo.has_l6mux && ((bel_loc.z % 2) == 1))
            return false;

        for (auto bel_other : getBelsByTile(bel_loc.x, bel_loc.y)) {
            CellInfo *cell_other = getBoundBelCell(bel_other);
            if (cell_other != nullptr && bel_other != bel) {
                bel_cells.push_back(cell_other);
            }
        }

        bel_cells.push_back(cell);
        return slicesCompatible(bel_cells);
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
                ci->params[id(p.str(this) + "MUX")] = "1";
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
        ci->params[id("LUT" + std::to_string(lut) + "_INITVAL")] = std::to_string(new_init);
    };

    for (auto cell : sorted(cells)) {
        CellInfo *ci = cell.second;
        if (ci->type == id_TRELLIS_SLICE && str_or_default(ci->params, id("MODE"), "LOGIC") == "LOGIC") {
            proc_lut(ci, 0);
            proc_lut(ci, 1);
        }
    }
}

NEXTPNR_NAMESPACE_END
