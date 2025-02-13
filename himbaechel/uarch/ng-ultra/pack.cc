/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Beyond Authors.
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

#include "pack.h"
#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <iterator>
#include <queue>
#include <unordered_set>
#include "chain_utils.h"
#include "design_utils.h"
#include "extra_data.h"
#include "log.h"
#include "nextpnr.h"

#define HIMBAECHEL_CONSTIDS "uarch/ng-ultra/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

// Return true if a cell is a LUT
inline bool is_lut(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_NX_LUT; }

// Return true if a cell is a flipflop
inline bool is_dff(const BaseCtx *ctx, const CellInfo *cell) { return cell->type.in(id_NX_DFF, id_NX_BFF); }

// Return true if a cell is a FE
inline bool is_fe(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_BEYOND_FE; }

// Return true if a cell is a DFR
inline bool is_dfr(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_NX_DFR; }

// Return true if a cell is a DDFR
inline bool is_ddfr(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_NX_DDFR_U; }

// Return true if a cell is a WFG/WFB
inline bool is_wfg(const BaseCtx *ctx, const CellInfo *cell) { return cell->type.in(id_WFB, id_WFG); }

// Return true if a cell is a GCK
inline bool is_gck(const BaseCtx *ctx, const CellInfo *cell) { return cell->type.in(id_GCK, id_NX_GCK_U); }

// Process the contents of packed_cells
void NgUltraPacker::flush_cells()
{
    for (auto pcell : packed_cells) {
        for (auto &port : ctx->cells[pcell]->ports) {
            ctx->cells[pcell]->disconnectPort(port.first);
        }
        ctx->cells.erase(pcell);
    }
    packed_cells.clear();
}

void NgUltraPacker::pack_constants(void)
{
    log_info("Packing constants..\n");
    // Replace constants with LUTs
    const dict<IdString, Property> vcc_params = {
            {id_lut_table, Property(0xFFFF, 16)}, {id_lut_used, Property(1, 1)}, {id_dff_used, Property(1, 1)}};
    const dict<IdString, Property> gnd_params = {
            {id_lut_table, Property(0x0000, 16)}, {id_lut_used, Property(1, 1)}, {id_dff_used, Property(1, 1)}};

    h.replace_constants(CellTypePort(id_BEYOND_FE, id_LO), CellTypePort(id_BEYOND_FE, id_LO), vcc_params, gnd_params);
}

void NgUltraImpl::remove_constants()
{
    log_info("Removing constants..\n");
    auto fnd_cell = ctx->cells.find(ctx->id("$PACKER_VCC_DRV"));
    if (fnd_cell != ctx->cells.end()) {
        auto fnd_net = ctx->nets.find(ctx->id("$PACKER_VCC"));
        if (fnd_net != ctx->nets.end() && fnd_net->second->users.entries() == 0) {
            BelId bel = (*fnd_cell).second.get()->bel;
            if (bel != BelId())
                ctx->unbindBel(bel);
            ctx->cells.erase(fnd_cell);
            ctx->nets.erase(fnd_net);
            log_info("    Removed unused VCC cell\n");
        }
    }
    fnd_cell = ctx->cells.find(ctx->id("$PACKER_GND_DRV"));
    if (fnd_cell != ctx->cells.end()) {
        auto fnd_net = ctx->nets.find(ctx->id("$PACKER_GND"));
        if (fnd_net != ctx->nets.end() && fnd_net->second->users.entries() == 0) {
            BelId bel = (*fnd_cell).second.get()->bel;
            if (bel != BelId())
                ctx->unbindBel(bel);
            ctx->cells.erase(fnd_cell);
            ctx->nets.erase(fnd_net);
            log_info("    Removed unused GND cell\n");
        }
    }
}

void NgUltraPacker::update_lut_init()
{
    log_info("Update LUT init...\n");

    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_LUT))
            continue;
        set_lut_input_if_constant(&ci, id_I1);
        set_lut_input_if_constant(&ci, id_I2);
        set_lut_input_if_constant(&ci, id_I3);
        set_lut_input_if_constant(&ci, id_I4);
        NetInfo *o = ci.getPort(id_O);
        if (!o) {
            // Remove those that do not output any value
            log_warning("Removing LUT '%s' since output is not connected.\n", ci.name.c_str(ctx));
            packed_cells.insert(ci.name);
        }
    }
    flush_cells();
}

void NgUltraPacker::dff_rewrite(CellInfo *cell)
{
    if (int_or_default(cell->params, id_dff_init, 0) == 0) {
        // Reset not used
        cell->disconnectPort(id_R);
    } else {
        // Reset used
        NetInfo *net = cell->getPort(id_R);
        if (net) {
            if (net->name == ctx->id("$PACKER_GND")) {
                log_warning("Removing reset on '%s' since it is always 0.\n", cell->name.c_str(ctx));
                cell->setParam(id_dff_init, Property(0, 1));
                cell->disconnectPort(id_R);
            } else if (net->name == ctx->id("$PACKER_VCC")) {
                log_error("Invalid DFF configuration, reset on '%s' is always 1.\n", cell->name.c_str(ctx));
            }
        }
    }

    if (int_or_default(cell->params, id_dff_load, 0) == 0) {
        // Load not used
        cell->disconnectPort(id_L);
    } else {
        // Load used
        NetInfo *net = cell->getPort(id_L);
        if (net) {
            if (net->name == ctx->id("$PACKER_VCC")) {
                log_warning("Removing load enable on '%s' since it is always 1.\n", cell->name.c_str(ctx));
                cell->setParam(id_dff_load, Property(0, 0));
                cell->disconnectPort(id_L);
            } else if (net->name == ctx->id("$PACKER_GND")) {
                log_warning("Converting to self loop, since load enable on '%s' is always 0.\n", cell->name.c_str(ctx));
                cell->setParam(id_dff_load, Property(0, 0));
                cell->disconnectPort(id_L);
                cell->disconnectPort(id_I);
                NetInfo *out = cell->getPort(id_O);
                cell->connectPort(id_I, out);
            }
        }
    }
}

void NgUltraPacker::ddfr_rewrite(CellInfo *cell)
{
    // Reversed logic in comparison to DFF
    if (int_or_default(cell->params, id_dff_load, 0) == 1) {
        // Load not used
        cell->disconnectPort(id_L);
    } else {
        // Load used
        NetInfo *net = cell->getPort(id_L);
        if (net) {
            if (net->name == ctx->id("$PACKER_VCC")) {
                log_warning("Removing load enable on '%s' since it is always 1.\n", cell->name.c_str(ctx));
                cell->setParam(id_dff_load, Property(0, 0));
                cell->disconnectPort(id_L);
            }
        }
    }
}

void NgUltraPacker::update_dffs()
{
    log_info("Update DFFs...\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_DFF))
            continue;
        dff_rewrite(&ci);
    }
}

int NgUltraPacker::make_init_with_const_input(int init, int input, bool value)
{
    int new_init = 0;
    for (int i = 0; i < 16; i++) {
        if (((i >> input) & 0x1) != value) {
            int other_i = (i & (~(1 << input))) | (value << input);
            if ((init >> other_i) & 0x1)
                new_init |= (1 << i);
        } else {
            if ((init >> i) & 0x1)
                new_init |= (1 << i);
        }
    }
    return new_init;
}

void NgUltraPacker::set_lut_input_if_constant(CellInfo *cell, IdString input)
{
    NetInfo *net = cell->getPort(input);
    if (!net)
        return;
    if (!net->name.in(ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC")))
        return;
    bool value = net->name == ctx->id("$PACKER_VCC");
    int index = std::string("1234").find(input.str(ctx));
    int init = int_or_default(cell->params, id_lut_table);
    int new_init = make_init_with_const_input(init, index, value);
    cell->params[id_lut_table] = Property(new_init, 16);
    cell->disconnectPort(input);
}

void NgUltraPacker::disconnect_if_gnd(CellInfo *cell, IdString input)
{
    NetInfo *net = cell->getPort(input);
    if (!net)
        return;
    if (net->name.in(ctx->id("$PACKER_GND"))) {
        cell->disconnectPort(input);
    }
}

void NgUltraPacker::connect_gnd_if_unconnected(CellInfo *cell, IdString input, bool warn = true)
{
    NetInfo *net = cell->getPort(input);
    if (net)
        return;
    if (!cell->ports.count(input))
        cell->addInput(input);
    auto fnd_net = ctx->nets.find(ctx->id("$PACKER_GND"));
    if (fnd_net != ctx->nets.end()) {
        cell->connectPort(input, fnd_net->second.get());
        if (warn)
            log_warning("Connected GND to mandatory port '%s' of cell '%s'(%s).\n", input.c_str(ctx),
                        cell->name.c_str(ctx), cell->type.c_str(ctx));
    }
}

void NgUltraPacker::lut_to_fe(CellInfo *lut, CellInfo *fe, bool no_dff, Property lut_table)
{
    fe->params[id_lut_table] = lut_table;
    fe->params[id_lut_used] = Property(1, 1);
    lut->movePortTo(id_I1, fe, id_I1);
    lut->movePortTo(id_I2, fe, id_I2);
    lut->movePortTo(id_I3, fe, id_I3);
    lut->movePortTo(id_I4, fe, id_I4);
    lut->movePortTo(id_O, fe, id_LO);
    if (no_dff) {
        fe->timing_index = ctx->get_cell_timing_idx(id_BEYOND_FE_LUT);
    }
}

void NgUltraPacker::dff_to_fe(CellInfo *dff, CellInfo *fe, bool pass_thru_lut)
{
    if (pass_thru_lut) {
        NetInfo *net = dff->getPort(id_I);
        if (net && net->name.in(ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC"))) {
            // special case if driver is constant
            fe->params[id_lut_table] = Property((net->name == ctx->id("$PACKER_GND")) ? 0x0000 : 0xffff, 16);
            dff->disconnectPort(id_I);
        } else {
            // otherwise just passthru
            fe->params[id_lut_table] = Property(0xaaaa, 16);
            dff->movePortTo(id_I, fe, id_I1);
        }
        fe->params[id_lut_used] = Property(1, 1);
    } else
        dff->movePortTo(id_I, fe, id_DI);
    fe->params[id_dff_used] = Property(1, 1);
    dff->movePortTo(id_O, fe, id_DO);
    if (dff->type == id_NX_BFF) {
        fe->setParam(id_type, Property("BFF"));
    } else {
        fe->setParam(id_type, Property("DFF"));

        dff->movePortTo(id_R, fe, id_R);
        dff->movePortTo(id_CK, fe, id_CK);
        dff->movePortTo(id_L, fe, id_L);

        if (dff->params.count(id_dff_ctxt))
            fe->setParam(id_dff_ctxt, dff->params[id_dff_ctxt]);
        if (dff->params.count(id_dff_edge))
            fe->setParam(id_dff_edge, dff->params[id_dff_edge]);
        if (dff->params.count(id_dff_init))
            fe->setParam(id_dff_init, dff->params[id_dff_init]);
        if (dff->params.count(id_dff_load))
            fe->setParam(id_dff_load, dff->params[id_dff_load]);
        if (dff->params.count(id_dff_sync))
            fe->setParam(id_dff_sync, dff->params[id_dff_sync]);
        if (dff->params.count(id_dff_type))
            fe->setParam(id_dff_type, dff->params[id_dff_type]);
    }
    if (pass_thru_lut) {
        NetInfo *new_out = ctx->createNet(ctx->idf("%s$LO", dff->name.c_str(ctx)));
        fe->connectPort(id_LO, new_out);
        fe->connectPort(id_DI, new_out);
    }
}

void NgUltraPacker::bind_attr_loc(CellInfo *cell, dict<IdString, Property> *attrs)
{
    if (attrs->count(id_LOC)) {
        std::string name = attrs->at(id_LOC).as_string();
        if (boost::starts_with(name, "TILE[")) {
            boost::replace_all(name, ".DFF", ".FE");
            boost::replace_all(name, ".LUT", ".FE");
        }
        if (!uarch->locations.count(name)) {
            log_error("Unable to find location %s\n", name.c_str());
        }
        BelId bel = uarch->locations.at(name);
        ctx->bindBel(bel, cell, PlaceStrength::STRENGTH_LOCKED);
    }
}

void NgUltraPacker::pack_xluts(void)
{
    log_info("Pack XLUTs...\n");
    int xlut_used = 0, lut_only = 0, lut_and_ff = 0;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_LUT))
            continue;
        if (!ci.params.count(id_lut_table))
            log_error("Cell '%s' missing lut_table\n", ci.name.c_str(ctx));

        if (ci.cluster != ClusterId())
            continue;
        CellInfo *lut[4];
        int inputs_used = 0;
        int dff_parts_used = 0;
        for (int i = 0; i < 4; i++) {
            NetInfo *net = ci.getPort(ctx->idf("I%d", i + 1));
            if (!net)
                continue;
            lut[i] = net_driven_by(ctx, net, is_lut, id_O);
            if (lut[i] == cell.second.get())
                continue;
            if (lut[i]) {
                if (net->users.entries() > 1)
                    dff_parts_used++;
                inputs_used++;
            }
        }
        if (inputs_used != 4)
            continue;
        // we must have a route out for xlut output signal
        if (dff_parts_used > 3)
            continue;
        ci.type = id_XLUT;
        bind_attr_loc(&ci, &ci.attrs);
        ci.cluster = ci.name;
        xlut_used++;

        NetInfo *o = ci.getPort(id_O);
        CellInfo *orig_dff = o ? net_only_drives(ctx, o, is_dff, id_I, true) : nullptr;

        for (int i = 0; i < 4; i++) {
            ci.constr_children.push_back(lut[i]);
            lut[i]->cluster = ci.cluster;
            lut[i]->type = id_BEYOND_FE;
            lut[i]->constr_z = PLACE_XLUT_FE1 + i;
            lut[i]->renamePort(id_O, id_LO);
            lut[i]->params[id_lut_used] = Property(1, 1);
            NetInfo *net = lut[i]->getPort(id_LO);
            if (net->users.entries() != 2) {
                if (orig_dff && net->users.entries() == 1) {
                    // we place DFF on XLUT output on unused DFF
                    dff_to_fe(orig_dff, lut[i], false);
                    packed_cells.insert(orig_dff->name);
                    lut_and_ff++;
                    orig_dff = nullptr;
                } else {
                    lut[i]->timing_index = ctx->get_cell_timing_idx(id_BEYOND_FE_LUT);
                    lut_only++;
                }
            } else {
                CellInfo *dff = (*net->users.begin()).cell;
                if (dff->type != id_NX_DFF)
                    dff = (*(++net->users.begin())).cell;
                if (dff->type == id_NX_DFF) {
                    dff_to_fe(dff, lut[i], false);
                    packed_cells.insert(dff->name);
                    lut_and_ff++;
                } else {
                    lut[i]->timing_index = ctx->get_cell_timing_idx(id_BEYOND_FE_LUT);
                    lut_only++;
                }
            }
        }
    }
    if (xlut_used)
        log_info("    %6d XLUTs used\n", xlut_used);
    if (lut_only)
        log_info("    %6d FEs used as LUT only\n", lut_only);
    if (lut_and_ff)
        log_info("    %6d FEs used as LUT and DFF\n", lut_and_ff);
    flush_cells();
}

void NgUltraPacker::pack_dff_chains(void)
{
    log_info("Pack DFF chains...\n");
    std::vector<std::pair<CellInfo *, std::vector<CellInfo *>>> dff_chain_start;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_DFF))
            continue;
        NetInfo *inp = ci.getPort(id_I);
        if (!inp || (inp->driver.cell && inp->driver.cell->type.in(id_NX_DFF)))
            continue;
        int cnt = 0;
        CellInfo *dff = &ci;
        std::vector<CellInfo *> chain;
        CellInfo *start_dff = &ci;
        while (1) {
            NetInfo *o = dff->getPort(id_O);
            if (!o)
                break;
            if (o->users.entries() != 1)
                break;
            dff = (*o->users.begin()).cell;
            if (dff->type == id_NX_DFF && (*o->users.begin()).port == id_I) {
                if (cnt == 95) { // note that start_dff is also part of chain
                    dff_chain_start.push_back(make_pair(start_dff, chain));
                    cnt = 0;
                    start_dff = dff;
                    chain.clear();
                } else {
                    chain.push_back(dff);
                    cnt++;
                }
            } else
                break;
        }
        if (cnt)
            dff_chain_start.push_back(make_pair(start_dff, chain));
    }

    int dff_only = 0, lut_and_ff = 0;
    for (auto ch : dff_chain_start) {
        CellInfo *dff = ch.first;
        CellInfo *root = create_cell_ptr(id_BEYOND_FE, ctx->idf("%s$fe", dff->name.c_str(ctx)));
        root->cluster = root->name;
        NetInfo *net = dff->getPort(id_I);
        if (net && net->driver.cell->type == id_NX_LUT && net->users.entries() == 1) {
            CellInfo *lut = net->driver.cell;
            if (!lut->params.count(id_lut_table))
                log_error("Cell '%s' missing lut_table\n", lut->name.c_str(ctx));
            lut_to_fe(lut, root, false, lut->params[id_lut_table]);
            packed_cells.insert(lut->name);
            dff_to_fe(dff, root, false);
            packed_cells.insert(dff->name);
            ++lut_and_ff;
        } else {
            dff_to_fe(dff, root, true);
            packed_cells.insert(dff->name);
            ++dff_only;
        }
        for (auto dff : ch.second) {
            CellInfo *new_cell = create_cell_ptr(id_BEYOND_FE, ctx->idf("%s$fe", dff->name.c_str(ctx)));
            dff_to_fe(dff, new_cell, true);
            ++dff_only;
            root->constr_children.push_back(new_cell);
            new_cell->cluster = root->cluster;
            new_cell->constr_z = PLACE_DFF_CHAIN;
            packed_cells.insert(dff->name);
        }
    }
    if (lut_and_ff)
        log_info("    %6d FEs used as LUT and DFF\n", lut_and_ff);
    if (dff_only)
        log_info("    %6d FEs used as DFF only\n", dff_only);
    flush_cells();
}

void NgUltraPacker::pack_lut_multi_dffs(void)
{
    log_info("Pack LUT-multi DFFs...\n");

    int dff_only = 0, lut_and_ff = 0, bff_only = 0;
    ;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_LUT))
            continue;
        if (!ci.params.count(id_lut_table))
            log_error("Cell '%s' missing lut_table\n", ci.name.c_str(ctx));

        NetInfo *o = ci.getPort(id_O);
        if (o) {
            if (o->users.entries() < 2)
                continue;

            int cnt = 0;
            for (auto u : o->users) {
                if (u.cell->type == id_NX_DFF && u.cell->getPort(id_I) == o)
                    cnt++;
            }
            if (cnt < 2)
                continue;

            CellInfo *root = create_cell_ptr(id_BEYOND_FE, ctx->idf("%s$fe", ci.name.c_str(ctx)));
            packed_cells.insert(ci.name);
            bind_attr_loc(root, &ci.attrs);
            lut_to_fe(&ci, root, false, ci.params[id_lut_table]);
            root->cluster = root->name;

            int max_use = (cnt == 4 && o->users.entries() == 4) ? 4 : 3;
            bool use_bff = max_use != 4 && cnt >= 4;
            int i = 0;
            std::vector<PortRef> users;
            for (auto u : o->users) {
                if (u.cell->type == id_NX_DFF && u.cell->getPort(id_I) == o) {
                    if (i == 0) {
                        packed_cells.insert(u.cell->name);
                        dff_to_fe(u.cell, root, false);
                        ++lut_and_ff;
                    } else if (i < max_use) {
                        packed_cells.insert(u.cell->name);
                        CellInfo *new_cell = create_cell_ptr(id_BEYOND_FE, ctx->idf("%s$fe", u.cell->name.c_str(ctx)));
                        dff_to_fe(u.cell, new_cell, false);
                        root->constr_children.push_back(new_cell);
                        new_cell->cluster = root->cluster;
                        new_cell->constr_z = PLACE_LUT_CHAIN;
                        ++dff_only;
                    } else {
                        use_bff = true;
                        users.push_back(u);
                        u.cell->disconnectPort(u.port);
                    }
                    i++;
                } else {
                    use_bff = true;
                    users.push_back(u);
                    u.cell->disconnectPort(u.port);
                }
            }
            if (use_bff) {
                CellInfo *new_cell = create_cell_ptr(id_BEYOND_FE, ctx->idf("%s$bff", ci.name.c_str(ctx)));
                new_cell->params[id_dff_used] = Property(1, 1);
                new_cell->setParam(id_type, Property("BFF"));
                new_cell->connectPort(id_DI, o);
                root->constr_children.push_back(new_cell);
                new_cell->cluster = root->cluster;
                new_cell->constr_z = PLACE_LUT_CHAIN;
                bff_only++;
                NetInfo *new_out = ctx->createNet(ctx->idf("%s$new", o->name.c_str(ctx)));
                new_cell->connectPort(id_DO, new_out);
                for (auto &user : users) {
                    user.cell->connectPort(user.port, new_out);
                }
            }
        }
    }
    if (dff_only)
        log_info("    %6d FEs used as DFF only\n", dff_only);
    if (bff_only)
        log_info("    %6d FEs used as BFF only\n", bff_only);
    if (lut_and_ff)
        log_info("    %6d FEs used as LUT and DFF\n", lut_and_ff);
    flush_cells();
}

void NgUltraPacker::pack_lut_dffs(void)
{
    log_info("Pack LUT-DFFs...\n");

    int lut_only = 0, lut_and_ff = 0;
    std::vector<CellInfo *> lut_list;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_LUT))
            continue;
        if (!ci.params.count(id_lut_table))
            log_error("Cell '%s' missing lut_table\n", ci.name.c_str(ctx));
        packed_cells.insert(ci.name);
        lut_list.push_back(&ci);
    }
    for (auto ci : lut_list) {
        CellInfo *packed = create_cell_ptr(id_BEYOND_FE, ctx->idf("%s$fe", ci->name.c_str(ctx)));
        bind_attr_loc(packed, &ci->attrs);

        bool packed_dff = false;
        NetInfo *o = ci->getPort(id_O);
        if (o) {
            CellInfo *dff = net_only_drives(ctx, o, is_dff, id_I, true);
            if (dff) {
                if (ctx->verbose)
                    log_info("found attached dff %s\n", dff->name.c_str(ctx));
                lut_to_fe(ci, packed, false, ci->params[id_lut_table]);
                dff_to_fe(dff, packed, false);
                ++lut_and_ff;
                packed_cells.insert(dff->name);
                if (ctx->verbose)
                    log_info("packed cell %s into %s\n", dff->name.c_str(ctx), packed->name.c_str(ctx));
                packed_dff = true;
            }
        }
        if (!packed_dff) {
            lut_to_fe(ci, packed, true, ci->params[id_lut_table]);
            ++lut_only;
        }
    }
    if (lut_only)
        log_info("    %6d FEs used as LUT only\n", lut_only);
    if (lut_and_ff)
        log_info("    %6d FEs used as LUT and DFF\n", lut_and_ff);
    flush_cells();
}

void NgUltraPacker::pack_dffs(void)
{
    int dff_only = 0;
    std::vector<CellInfo *> dff_list;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_DFF, id_NX_BFF))
            continue;
        packed_cells.insert(ci.name);
        dff_list.push_back(&ci);
    }
    for (auto ci : dff_list) {
        CellInfo *packed = create_cell_ptr(id_BEYOND_FE, ctx->idf("%s$fe", ci->name.c_str(ctx)));
        dff_to_fe(ci, packed, true);
        bind_attr_loc(packed, &ci->attrs);
        ++dff_only;
    }
    if (dff_only)
        log_info("    %6d FEs used as DFF only\n", dff_only);
    flush_cells();
}

void NgUltraPacker::pack_iobs(void)
{
    log_info("Pack IOBs...\n");
    // Trim nextpnr IOBs - assume IO buffer insertion has been done in synthesis
    for (auto &port : ctx->ports) {
        if (!ctx->cells.count(port.first))
            log_error("Port '%s' doesn't seem to have a corresponding top level IO\n", ctx->nameOf(port.first));
        CellInfo *ci = ctx->cells.at(port.first).get();

        PortRef top_port;
        top_port.cell = nullptr;
        bool is_npnr_iob = false;

        if (ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_iobuf")) {
            // Might have an input buffer connected to it
            is_npnr_iob = true;
            NetInfo *o = ci->getPort(id_O);
            if (o == nullptr)
                ;
            else if (o->users.entries() > 1)
                log_error("Top level pin '%s' has multiple input buffers\n", ctx->nameOf(port.first));
            else if (o->users.entries() == 1)
                top_port = *o->users.begin();
        }
        if (ci->type == ctx->id("$nextpnr_obuf") || ci->type == ctx->id("$nextpnr_iobuf")) {
            // Might have an output buffer connected to it
            is_npnr_iob = true;
            NetInfo *i = ci->getPort(id_I);
            if (i != nullptr && i->driver.cell != nullptr) {
                if (top_port.cell != nullptr)
                    log_error("Top level pin '%s' has multiple input/output buffers\n", ctx->nameOf(port.first));
                top_port = i->driver;
            }
            // Edge case of a bidirectional buffer driving an output pin
            if (i->users.entries() > 2) {
                log_error("Top level pin '%s' has illegal buffer configuration\n", ctx->nameOf(port.first));
            } else if (i->users.entries() == 2) {
                if (top_port.cell != nullptr)
                    log_error("Top level pin '%s' has illegal buffer configuration\n", ctx->nameOf(port.first));
                for (auto &usr : i->users) {
                    if (usr.cell->type == ctx->id("$nextpnr_obuf") || usr.cell->type == ctx->id("$nextpnr_iobuf"))
                        continue;
                    top_port = usr;
                    break;
                }
            }
        }
        if (!is_npnr_iob)
            log_error("Port '%s' doesn't seem to have a corresponding top level IO (internal cell type mismatch)\n",
                      ctx->nameOf(port.first));

        if (top_port.cell == nullptr) {
            log_info("Trimming port '%s' as it is unused.\n", ctx->nameOf(port.first));
        } else {
            // Copy attributes to real IO buffer
            for (auto &attrs : ci->attrs)
                top_port.cell->attrs[attrs.first] = attrs.second;
            for (auto &params : ci->params)
                top_port.cell->params[params.first] = params.second;

            // Make sure that top level net is set correctly
            port.second.net = top_port.cell->ports.at(top_port.port).net;
        }
        // Now remove the nextpnr-inserted buffer
        ci->disconnectPort(id_I);
        ci->disconnectPort(id_O);
        ctx->cells.erase(port.first);
    }
    std::vector<CellInfo *> to_update;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_IOB_I, id_NX_IOB_O, id_NX_IOB))
            continue;
        if (ci.params.count(id_location) == 0) {
            log_error("Unconstrained IO:%s\n", ctx->nameOf(&ci));
        }
        std::string loc = ci.params.at(id_location).to_string();
        BelId bel = ctx->get_package_pin_bel(ctx->id(loc));
        if (bel == BelId())
            log_error("Unable to constrain IO '%s', device does not have a pin named '%s'\n", ci.name.c_str(ctx),
                      loc.c_str());
        log_info("    Constraining '%s' to pad '%s'\n", ci.name.c_str(ctx), loc.c_str());
        if (!ctx->checkBelAvail(bel)) {
            log_error("Can't place %s at %s because it's already taken by %s\n", ctx->nameOf(&ci), ctx->nameOfBel(bel),
                      ctx->nameOf(ctx->getBoundBelCell(bel)));
        }

        IdString new_type = id_IOP;
        disconnect_if_gnd(&ci, id_T);
        if (ci.getPort(id_T)) {
            // In case T input is used must use different types
            new_type = id_IOTP;
            if (ci.type == id_NX_IOB_O)
                new_type = id_OTP;
            if (ci.type == id_NX_IOB_I)
                new_type = id_ITP;
        } else {
            if (ci.type == id_NX_IOB_O)
                new_type = id_OP;
            if (ci.type == id_NX_IOB_I)
                new_type = id_IP;
        }
        ci.type = new_type;
        ctx->bindBel(bel, &ci, PlaceStrength::STRENGTH_LOCKED);
        if (!ctx->isValidBelForCellType(new_type, bel))
            log_error("Invalid type of IO for specified location %s %s.\n", new_type.c_str(ctx),
                      ctx->getBelType(bel).c_str(ctx));
        to_update.push_back(&ci);
    }
    int bfr_added = 0;
    int dfr_added = 0;
    int ddfr_added = 0;
    for (auto cell : to_update) {
        NetInfo *c_net = cell->getPort(id_C);
        if (!c_net)
            log_error("C input of IO primitive %s must be connected.\n", cell->name.c_str(ctx));
        if (c_net->name == ctx->id("$PACKER_GND") && !cell->getPort(id_O)) {
            log_warning("O port of IO primitive %s must be connected. Removing cell.\n", cell->name.c_str(ctx));
            packed_cells.emplace(cell->name);
            continue;
        }
        if (c_net->name == ctx->id("$PACKER_VCC") && !cell->getPort(id_I)) {
            log_warning("I port of IO primitive %s must be connected. Removing cell.\n", cell->name.c_str(ctx));
            packed_cells.emplace(cell->name);
            continue;
        }
        if (!cell->getPort(id_I) && !cell->getPort(id_O)) {
            log_warning("I or O port of IO primitive %s must be connected. Removing cell.\n", cell->name.c_str(ctx));
            packed_cells.emplace(cell->name);
            continue;
        }

        {
            CellInfo *iod = net_driven_by(ctx, c_net, is_dfr, id_O);
            if (iod && c_net->users.entries() != 1)
                log_error("NX_DFR '%s' can only directly drive IOB.\n", iod->name.c_str(ctx));
            if (!iod) {
                iod = net_driven_by(ctx, c_net, is_ddfr, id_O);
                if (iod && c_net->users.entries() != 1)
                    log_error("NX_DDFR '%s' can only directly drive IOB.\n", iod->name.c_str(ctx));
                if (!iod) {
                    bfr_added++;
                    iod = create_cell_ptr(id_BFR, ctx->idf("%s$iod_cd", cell->name.c_str(ctx)));
                    NetInfo *new_out = ctx->createNet(ctx->idf("%s$O", iod->name.c_str(ctx)));
                    iod->setParam(id_iobname, str_or_default(cell->params, id_iobname, ""));
                    cell->disconnectPort(id_C);
                    if (c_net->name == ctx->id("$PACKER_GND"))
                        iod->setParam(id_mode, Property(0, 2));
                    else if (c_net->name == ctx->id("$PACKER_VCC"))
                        iod->setParam(id_mode, Property(1, 2));
                    else {
                        iod->connectPort(id_I, c_net);
                        iod->setParam(id_mode, Property(2, 2));
                        iod->setParam(id_data_inv, Property(0, 1));
                    }
                    iod->connectPort(id_O, new_out);
                    cell->connectPort(id_C, new_out);
                } else {
                    ddfr_added++;
                    iod->type = id_DDFR;
                    iod->setParam(id_iobname, str_or_default(cell->params, id_iobname, ""));
                    iod->setParam(id_path, Property(2, 2));
                    ddfr_rewrite(iod);
                    disconnect_unused(iod, id_O2);
                    disconnect_if_gnd(iod, id_L);
                    disconnect_if_gnd(iod, id_R);
                }
            } else {
                dfr_added++;
                iod->type = id_DFR;
                iod->setParam(id_iobname, str_or_default(cell->params, id_iobname, ""));
                dff_rewrite(iod);
            }
            Loc cd_loc = cell->getLocation();
            cd_loc.z += 3;
            BelId bel = ctx->getBelByLocation(cd_loc);
            ctx->bindBel(bel, iod, PlaceStrength::STRENGTH_LOCKED);
        }
        NetInfo *i_net = cell->getPort(id_I);
        if (i_net) {
            CellInfo *iod = net_driven_by(ctx, i_net, is_dfr, id_O);
            if (iod && i_net->users.entries() != 1)
                log_error("NX_DFR '%s' can only directly drive IOB.\n", iod->name.c_str(ctx));
            if (!iod) {
                iod = net_driven_by(ctx, i_net, is_ddfr, id_O);
                if (iod && i_net->users.entries() != 1)
                    log_error("NX_DDFR '%s' can only directly drive IOB.\n", iod->name.c_str(ctx));
                if (!iod) {
                    bfr_added++;
                    iod = create_cell_ptr(id_BFR, ctx->idf("%s$iod_od", cell->name.c_str(ctx)));
                    NetInfo *new_out = ctx->createNet(ctx->idf("%s$O", iod->name.c_str(ctx)));
                    iod->setParam(id_iobname, str_or_default(cell->params, id_iobname, ""));
                    cell->disconnectPort(id_I);
                    if (i_net->name == ctx->id("$PACKER_GND"))
                        iod->setParam(id_mode, Property(0, 2));
                    else if (i_net->name == ctx->id("$PACKER_VCC"))
                        iod->setParam(id_mode, Property(1, 2));
                    else {
                        iod->connectPort(id_I, i_net);
                        iod->setParam(id_mode, Property(2, 2));
                        iod->setParam(id_data_inv, Property(0, 1));
                    }
                    iod->connectPort(id_O, new_out);
                    cell->connectPort(id_I, new_out);
                } else {
                    ddfr_added++;
                    iod->type = id_DDFR;
                    iod->setParam(id_iobname, str_or_default(cell->params, id_iobname, ""));
                    iod->setParam(id_path, Property(0, 2));
                    ddfr_rewrite(iod);
                    disconnect_unused(iod, id_O2);
                    disconnect_if_gnd(iod, id_L);
                    disconnect_if_gnd(iod, id_R);
                }
            } else {
                dfr_added++;
                iod->type = id_DFR;
                iod->setParam(id_iobname, str_or_default(cell->params, id_iobname, ""));
                dff_rewrite(iod);
            }
            Loc cd_loc = cell->getLocation();
            cd_loc.z += 2;
            BelId bel = ctx->getBelByLocation(cd_loc);
            ctx->bindBel(bel, iod, PlaceStrength::STRENGTH_LOCKED);
        }

        NetInfo *o_net = cell->getPort(id_O);
        if (o_net) {
            CellInfo *iod = net_only_drives(ctx, o_net, is_dfr, id_I, true);
            if (!(o_net->users.entries() == 1 && (*o_net->users.begin()).cell->type == id_NX_IOM_U)) {
                bool bfr_mode = false;
                bool ddfr_mode = false;
                if (!iod) {
                    iod = net_only_drives(ctx, o_net, is_ddfr, id_I, true);
                    if (!iod) {
                        bfr_added++;
                        iod = create_cell_ptr(id_BFR, ctx->idf("%s$iod_id", cell->name.c_str(ctx)));
                        NetInfo *new_in = ctx->createNet(ctx->idf("%s$I", iod->name.c_str(ctx)));
                        iod->setParam(id_iobname, str_or_default(cell->params, id_iobname, ""));
                        cell->disconnectPort(id_O);
                        iod->connectPort(id_O, o_net);
                        iod->setParam(id_mode, Property(2, 2));
                        iod->setParam(id_data_inv, Property(0, 1));
                        iod->connectPort(id_I, new_in);
                        cell->connectPort(id_O, new_in);
                        bfr_mode = true;
                    } else {
                        ddfr_mode = true;
                        ddfr_added++;
                        iod->type = id_DDFR;
                        iod->setParam(id_iobname, str_or_default(cell->params, id_iobname, ""));
                        iod->setParam(id_path, Property(1, 2));
                        ddfr_rewrite(iod);
                        disconnect_if_gnd(iod, id_I2);
                        disconnect_if_gnd(iod, id_L);
                        disconnect_if_gnd(iod, id_R);
                    }
                } else {
                    dfr_added++;
                    iod->type = id_DFR;
                    iod->setParam(id_iobname, str_or_default(cell->params, id_iobname, ""));
                    dff_rewrite(iod);
                }
                Loc cd_loc = cell->getLocation();
                cd_loc.z += 1;
                BelId bel = ctx->getBelByLocation(cd_loc);
                ctx->bindBel(bel, iod, PlaceStrength::STRENGTH_LOCKED);

                // Depending of DDFR mode we must use one of dedicated routes (ITCs)
                if (!ddfr_mode && ctx->getBelType(bel) == id_DDFR) {
                    WireId dwire = ctx->getBelPinWire(bel, id_O);
                    for (PipId pip : ctx->getPipsDownhill(dwire)) {
                        const auto &extra_data = *uarch->pip_extra_data(pip);
                        if (!extra_data.name)
                            continue;
                        if (extra_data.type != PipExtra::PIP_EXTRA_MUX)
                            continue;
                        if (bfr_mode && extra_data.input == 2) {
                            uarch->blocked_pips.emplace(pip);
                        } else if (!bfr_mode && extra_data.input == 1) {
                            uarch->blocked_pips.emplace(pip);
                        }
                    }
                }
            }
        }
    }
    if (ddfr_added)
        log_info("    %6d DDFRs used as DDFR\n", ddfr_added);
    if (dfr_added)
        log_info("    %6d DFRs/DDFRs used as DFR\n", dfr_added);
    if (bfr_added)
        log_info("    %6d DFRs/DDFRs used as BFR\n", bfr_added);
    flush_cells();
}

void NgUltraPacker::pack_ioms(void)
{
    log_info("Pack IOMs...\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_IOM_U))
            continue;
        ci.type = id_IOM;
        IdString iob;
        for (auto &port : ci.ports) {
            if (port.second.type == PORT_IN && port.second.net != nullptr) {
                if (port.second.net->name == ctx->id("$PACKER_GND"))
                    ci.disconnectPort(port.first);
                else if (port.second.net->driver.cell != nullptr &&
                         port.second.net->driver.cell->type.in(id_IP, id_OP, id_IOP, id_ITP, id_OTP, id_IOTP)) {
                    IdString loc = uarch->tile_name_id(port.second.net->driver.cell->bel.tile);
                    if (iob != IdString() && loc != iob) {
                        log_error("Unable to constrain IOM '%s', connection to multiple IO banks exist.\n",
                                  ci.name.c_str(ctx));
                    }
                    iob = loc;
                }
            }
        }
        if (iob == IdString())
            log_error("Unable to constrain IOM '%s', no connection to nearby IO banks found.\n", ci.name.c_str(ctx));
        log_info("    Constraining '%s' to bank '%s'\n", ci.name.c_str(ctx), iob.c_str(ctx));
        BelId bel = uarch->iom_bels[iob];
        if (!ctx->checkBelAvail(bel)) {
            log_error("Can't place %s at %s because it's already taken by %s\n", ctx->nameOf(&ci), ctx->nameOfBel(bel),
                      ctx->nameOf(ctx->getBoundBelCell(bel)));
        }
        ctx->bindBel(bel, &ci, PlaceStrength::STRENGTH_LOCKED);
    }
}

void NgUltraPacker::pack_cy_input_and_output(CellInfo *cy, IdString cluster, IdString in_port, IdString out_port,
                                             int placer, int &lut_only, int &lut_and_ff, int &dff_only)
{
    CellInfo *fe = create_cell_ptr(id_BEYOND_FE, ctx->idf("%s$%s", cy->name.c_str(ctx), in_port.c_str(ctx)));
    NetInfo *net = cy->getPort(in_port);
    if (net) {
        if (net->name.in(ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC"))) {
            fe->params[id_lut_table] = Property((net->name == ctx->id("$PACKER_GND")) ? 0x0000 : 0xffff, 16);
            fe->params[id_lut_used] = Property(1, 1);
            cy->disconnectPort(in_port);
            NetInfo *new_out = ctx->createNet(ctx->idf("%s$o", fe->name.c_str(ctx)));
            fe->connectPort(id_LO, new_out);
            cy->connectPort(in_port, new_out);
        } else {
            fe->params[id_lut_table] = Property(0xaaaa, 16);
            fe->params[id_lut_used] = Property(1, 1);
            cy->disconnectPort(in_port);
            NetInfo *new_out = ctx->createNet(ctx->idf("%s$o", fe->name.c_str(ctx)));
            fe->connectPort(id_I1, net);
            fe->connectPort(id_LO, new_out);
            cy->connectPort(in_port, new_out);
        }
        lut_only++;
    }
    net = cy->getPort(out_port);
    CellInfo *dff = net_only_drives(ctx, net, is_dff, id_I, true);
    if (dff) {
        if (ctx->verbose)
            log_info("found attached dff %s\n", dff->name.c_str(ctx));
        dff_to_fe(dff, fe, false);
        packed_cells.insert(dff->name);
        if (net) {
            lut_only--;
            lut_and_ff++;
        } else
            dff_only++;
    } else {
        fe->timing_index = ctx->get_cell_timing_idx(id_BEYOND_FE_LUT);
    }
    fe->cluster = cluster;
    fe->constr_z = placer;
    cy->constr_children.push_back(fe);
}

void NgUltraPacker::exchange_if_constant(CellInfo *cell, IdString input1, IdString input2)
{
    NetInfo *net1 = cell->getPort(input1);
    NetInfo *net2 = cell->getPort(input2);
    // GND on A
    if (net1->name.in(ctx->id("$PACKER_GND"))) {
        return;
    }
    // VCC on A -> exchange
    // if GND on B and not on A -> exchange
    if (net1->name.in(ctx->id("$PACKER_VCC")) || net2->name.in(ctx->id("$PACKER_GND"))) {
        cell->disconnectPort(input1);
        cell->disconnectPort(input2);
        cell->connectPort(input1, net2);
        cell->connectPort(input2, net1);
    }
}

void NgUltraPacker::pack_cys(void)
{
    log_info("Packing carries..\n");
    std::vector<CellInfo *> root_cys;
    int lut_only = 0, lut_and_ff = 0, dff_only = 0;
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type != id_NX_CY)
            continue;
        bind_attr_loc(ci, &ci->attrs);
        NetInfo *ci_net = ci->getPort(id_CI);
        if (!ci_net || !ci_net->driver.cell || ci_net->driver.cell->type != id_NX_CY) {
            root_cys.push_back(ci);
        }
        for (int i = 1; i <= 4; i++) {
            connect_gnd_if_unconnected(ci, ctx->idf("A%d", i), false);
            connect_gnd_if_unconnected(ci, ctx->idf("B%d", i), false);
            exchange_if_constant(ci, ctx->idf("A%d", i), ctx->idf("B%d", i));
        }
        NetInfo *co_net = ci->getPort(id_CO);
        if (!co_net) {
            disconnect_unused(ci, id_CO);
            // Disconnect unused ports on last CY in chain
            // at least id_A1 and id_B1 will be connected
            // Reverse direction, must stop if used, then
            // rest is used as well
            if (!ci->getPort(id_S4)) {
                disconnect_unused(ci, id_S4);
                disconnect_unused(ci, id_A4);
                disconnect_unused(ci, id_B4);
                if (!ci->getPort(id_S3)) {
                    disconnect_unused(ci, id_S3);
                    disconnect_unused(ci, id_A3);
                    disconnect_unused(ci, id_B3);
                    if (!ci->getPort(id_S2)) {
                        disconnect_unused(ci, id_S2);
                        disconnect_unused(ci, id_A2);
                        disconnect_unused(ci, id_B2);
                    };
                }
            }
        }
    }

    std::vector<std::vector<CellInfo *>> groups;
    for (auto root : root_cys) {
        std::vector<CellInfo *> group;
        CellInfo *cy = root;
        group.push_back(cy);
        while (true) {
            NetInfo *co_net = cy->getPort(id_CO);
            if (co_net && co_net->users.entries() > 0) {
                cy = (*co_net->users.begin()).cell;
                if (cy->type != id_NX_CY || co_net->users.entries() != 1) {
                    log_warning("Cells %s CO output connected to:\n", group.back()->name.c_str(ctx));
                    for (auto user : co_net->users)
                        log_warning("\t%s of type %s\n", user.cell->name.c_str(ctx), user.cell->type.c_str(ctx));
                    log_error("NX_CY can only be chained with one other NX_CY cell\n");
                }
                group.push_back(cy);
            } else
                break;
        }
        groups.push_back(group);
    }

    for (auto &grp : groups) {
        CellInfo *root = grp.front();
        root->type = id_CY;
        root->cluster = root->name;
        if (grp.size() > 24)
            log_error("NX_CY chains are limited to contain 24 elements maximum.\n");

        for (int i = 0; i < int(grp.size()); i++) {
            CellInfo *cy = grp.at(i);
            cy->type = id_CY;
            if (i != 0) {
                cy->cluster = root->name;
                root->constr_children.push_back(cy);
                cy->constr_z = PLACE_CY_CHAIN;
            }
            pack_cy_input_and_output(cy, root->name, id_B1, id_S1, PLACE_CY_FE1, lut_only, lut_and_ff, dff_only);
            // Must check for B input otherwise we have bogus FEs
            if (cy->getPort(id_B2))
                pack_cy_input_and_output(cy, root->name, id_B2, id_S2, PLACE_CY_FE2, lut_only, lut_and_ff, dff_only);
            if (cy->getPort(id_B3))
                pack_cy_input_and_output(cy, root->name, id_B3, id_S3, PLACE_CY_FE3, lut_only, lut_and_ff, dff_only);
            if (cy->getPort(id_B4))
                pack_cy_input_and_output(cy, root->name, id_B4, id_S4, PLACE_CY_FE4, lut_only, lut_and_ff, dff_only);
            NetInfo *net = cy->getPort(id_CI);
            if (net) {
                if (net->name.in(ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC"))) {
                    // Constant driver for CI is configuration
                    cy->disconnectPort(id_CI);
                } else {
                    if (net->driver.cell && net->driver.cell->type != id_CY)
                        log_error("CI must be constant or driven by CO in cell '%s'\n", cy->name.c_str(ctx));
                }
            }
        }
    }
    if (lut_only)
        log_info("    %6d FEs used as LUT only\n", lut_only);
    if (lut_and_ff)
        log_info("    %6d FEs used as LUT and DFF\n", lut_and_ff);
    if (dff_only)
        log_info("    %6d FEs used as DFF only\n", dff_only);
    flush_cells();
}

void NgUltraPacker::pack_xrf_input_and_output(CellInfo *xrf, IdString cluster, IdString in_port, IdString out_port,
                                              ClusterPlacement placement, int &lut_only, int &lut_and_ff, int &dff_only)
{
    NetInfo *net = xrf->getPort(in_port);
    NetInfo *net_out = nullptr;
    if (out_port != IdString()) {
        net_out = xrf->getPort(out_port);
        if (net_out && net_out->users.entries() == 0) {
            xrf->disconnectPort(out_port);
            net_out = nullptr;
        }
    }
    if (!net && !net_out)
        return;
    IdString name = in_port;
    if (name == IdString())
        name = out_port;
    CellInfo *fe = create_cell_ptr(id_BEYOND_FE, ctx->idf("%s$%s", xrf->name.c_str(ctx), name.c_str(ctx)));

    if (net) {
        if (net->name.in(ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC"))) {
            fe->params[id_lut_table] = Property((net->name == ctx->id("$PACKER_GND")) ? 0x0000 : 0xffff, 16);
            fe->params[id_lut_used] = Property(1, 1);
            xrf->disconnectPort(in_port);
            NetInfo *new_out = ctx->createNet(ctx->idf("%s$o", fe->name.c_str(ctx)));
            fe->connectPort(id_LO, new_out);
            xrf->connectPort(in_port, new_out);
        } else {
            CellInfo *lut = net_driven_by(ctx, net, is_lut, id_O);
            if (lut && net->users.entries() == 1) {
                if (!lut->params.count(id_lut_table))
                    log_error("Cell '%s' missing lut_table\n", lut->name.c_str(ctx));
                lut_to_fe(lut, fe, false, lut->params[id_lut_table]);
                packed_cells.insert(lut->name);
            } else {
                fe->params[id_lut_table] = Property(0xaaaa, 16);
                fe->params[id_lut_used] = Property(1, 1);
                xrf->disconnectPort(in_port);
                NetInfo *new_out = ctx->createNet(ctx->idf("%s$o", fe->name.c_str(ctx)));
                fe->connectPort(id_I1, net);
                fe->connectPort(id_LO, new_out);
                xrf->connectPort(in_port, new_out);
            }
        }
        lut_only++;
    }
    if (net_out) {
        CellInfo *dff = net_only_drives(ctx, net_out, is_dff, id_I, true);
        if (dff) {
            if (ctx->verbose)
                log_info("found attached dff %s\n", dff->name.c_str(ctx));
            dff_to_fe(dff, fe, false);
            packed_cells.insert(dff->name);
            if (net) {
                lut_only--;
                lut_and_ff++;
            } else
                dff_only++;
        }
    }
    fe->cluster = cluster;
    fe->constr_z = placement;
    xrf->constr_children.push_back(fe);
}

void NgUltraPacker::disconnect_unused(CellInfo *cell, IdString port)
{
    NetInfo *net = cell->getPort(port);
    if (net) {
        // NanoXplore tools usually connects 0 to unused port, no need to warn
        // Sometimes there is unused nets, so number of entries is zero
        if (net->users.entries() != 0 && !net->name.in(ctx->id("$PACKER_GND")))
            log_warning("Disconnected unused port '%s' from cell '%s'.\n", port.c_str(ctx), cell->name.c_str(ctx));
        cell->disconnectPort(port);
    }
}

void NgUltraPacker::pack_rfs(void)
{
    log_info("Packing RFs..\n");
    int lut_only = 0, lut_and_ff = 0, dff_only = 0;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_RFB_U))
            continue;
        int mode = int_or_default(ci.params, id_mode, 0);
        switch (mode) {
        case 0:
            ci.type = id_RF;
            break;
        case 1:
            ci.type = id_RFSP;
            break;
        case 2:
            ci.type = id_XHRF;
            break;
        case 3:
            ci.type = id_XWRF;
            break;
        case 4:
            ci.type = id_XPRF;
            break;
        default:
            log_error("Unknown mode %d for cell '%s'.\n", mode, ci.name.c_str(ctx));
        }
        ci.cluster = ci.name;
        bind_attr_loc(&ci, &ci.attrs);

        for (int i = 1; i <= 18; i++) {
            connect_gnd_if_unconnected(&ci, ctx->idf("I%d", i));
            pack_xrf_input_and_output(&ci, ci.name, ctx->idf("I%d", i), ctx->idf("O%d", i),
                                      ClusterPlacement(PLACE_XRF_I1 + i - 1), lut_only, lut_and_ff, dff_only);
        }
        if (mode != 1) {
            for (int i = 1; i <= 5; i++) {
                connect_gnd_if_unconnected(&ci, ctx->idf("RA%d", i));
                pack_xrf_input_and_output(&ci, ci.name, ctx->idf("RA%d", i), IdString(),
                                          ClusterPlacement(PLACE_XRF_RA1 + i - 1), lut_only, lut_and_ff, dff_only);
            }
        } else {
            // SPREG mode does not use RA inputs
            for (int i = 1; i <= 5; i++)
                disconnect_unused(&ci, ctx->idf("RA%d", i));
        }

        if (mode == 2 || mode == 4) {
            connect_gnd_if_unconnected(&ci, id_RA6);
            pack_xrf_input_and_output(&ci, ci.name, id_RA6, IdString(), PLACE_XRF_RA6, lut_only, lut_and_ff, dff_only);
        } else {
            disconnect_unused(&ci, id_RA6);
        }

        if (mode == 4) {
            for (int i = 7; i <= 10; i++) {
                connect_gnd_if_unconnected(&ci, ctx->idf("RA%d", i));
                pack_xrf_input_and_output(&ci, ci.name, ctx->idf("RA%d", i), IdString(),
                                          ClusterPlacement(PLACE_XRF_RA1 + i - 1), lut_only, lut_and_ff, dff_only);
            }
        } else {
            for (int i = 7; i <= 10; i++)
                disconnect_unused(&ci, ctx->idf("RA%d", i));
        }

        for (int i = 1; i <= 5; i++) {
            connect_gnd_if_unconnected(&ci, ctx->idf("WA%d", i));
            pack_xrf_input_and_output(&ci, ci.name, ctx->idf("WA%d", i), IdString(),
                                      ClusterPlacement(PLACE_XRF_WA1 + i - 1), lut_only, lut_and_ff, dff_only);
        }

        if (mode == 2) {
            connect_gnd_if_unconnected(&ci, id_WA6);
            pack_xrf_input_and_output(&ci, ci.name, id_WA6, IdString(), PLACE_XRF_WA6, lut_only, lut_and_ff, dff_only);
        } else {
            disconnect_unused(&ci, id_WA6);
        }

        connect_gnd_if_unconnected(&ci, id_WE);
        pack_xrf_input_and_output(&ci, ci.name, id_WE, IdString(), PLACE_XRF_WE, lut_only, lut_and_ff, dff_only);

        disconnect_if_gnd(&ci, id_WEA);
        pack_xrf_input_and_output(&ci, ci.name, id_WEA, IdString(), PLACE_XRF_WEA, lut_only, lut_and_ff, dff_only);

        if (mode == 3) {
            for (int i = 19; i <= 36; i++) {
                connect_gnd_if_unconnected(&ci, ctx->idf("I%d", i));
                pack_xrf_input_and_output(&ci, ci.name, ctx->idf("I%d", i), ctx->idf("O%d", i),
                                          ClusterPlacement(PLACE_XRF_I1 + i - 1), lut_only, lut_and_ff, dff_only);
            }
        } else if (mode == 4) {
            for (int i = 19; i <= 36; i++) {
                disconnect_unused(&ci, ctx->idf("I%d", i));
                pack_xrf_input_and_output(&ci, ci.name, IdString(), ctx->idf("O%d", i),
                                          ClusterPlacement(PLACE_XRF_I1 + i - 1), lut_only, lut_and_ff, dff_only);
            }
        } else {
            for (int i = 19; i <= 36; i++) {
                disconnect_unused(&ci, ctx->idf("I%d", i));
                disconnect_unused(&ci, ctx->idf("O%d", i));
            }
        }

        if (mode > 1) {
            // XRF
            ci.ports[id_WCK1].name = id_WCK1;
            ci.ports[id_WCK1].type = PORT_IN;
            ci.ports[id_WCK2].name = id_WCK2;
            ci.ports[id_WCK2].type = PORT_IN;
            NetInfo *net = ci.getPort(id_WCK);
            if (net) {
                ci.disconnectPort(id_WCK);

                ci.connectPort(id_WCK1, net);
                ci.connectPort(id_WCK2, net);
            }
        }
    }
    if (lut_only)
        log_info("    %6d FEs used as LUT only\n", lut_only);
    if (lut_and_ff)
        log_info("    %6d FEs used as LUT and DFF\n", lut_and_ff);
    if (dff_only)
        log_info("    %6d FEs used as DFF only\n", dff_only);
    flush_cells();
}

void NgUltraPacker::pack_cdcs(void)
{
    log_info("Packing CDCs..\n");
    int lut_only = 0, lut_and_ff = 0, dff_only = 0;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_CDC_U))
            continue;
        int mode = int_or_default(ci.params, id_mode, 0);
        switch (mode) {
        case 0:
            ci.type = id_DDE;
            break;
        case 1:
            ci.type = id_TDE;
            break;
        case 2:
            ci.type = id_CDC;
            break;
        case 3:
            ci.type = id_BGC;
            break;
        case 4:
            ci.type = id_GBC;
            break;
        case 5:
            ci.type = id_XCDC;
            break;
        default:
            log_error("Unknown mode %d for cell '%s'.\n", mode, ci.name.c_str(ctx));
        }
        ci.cluster = ci.name;

        // If unconnected, connect GND to inputs that are actually used as outputs
        for (int i = 1; i <= 6; i++) {
            if (ci.getPort(ctx->idf("AO%d", i))) {
                connect_gnd_if_unconnected(&ci, ctx->idf("AI%d", i));
                pack_xrf_input_and_output(&ci, ci.name, ctx->idf("AI%d", i), ctx->idf("AO%d", i),
                                          ClusterPlacement(PLACE_CDC_AI1 + i - 1), lut_only, lut_and_ff, dff_only);
            } else
                disconnect_unused(&ci, ctx->idf("AI%d", i));
            if (ci.getPort(ctx->idf("BO%d", i))) {
                connect_gnd_if_unconnected(&ci, ctx->idf("BI%d", i));
                pack_xrf_input_and_output(&ci, ci.name, ctx->idf("BI%d", i), ctx->idf("BO%d", i),
                                          ClusterPlacement(PLACE_CDC_BI1 + i - 1), lut_only, lut_and_ff, dff_only);
            } else
                disconnect_unused(&ci, ctx->idf("BI%d", i));
            if (ci.type.in(id_XCDC)) {
                if (ci.getPort(ctx->idf("CO%d", i))) {
                    connect_gnd_if_unconnected(&ci, ctx->idf("CI%d", i));
                    pack_xrf_input_and_output(&ci, ci.name, ctx->idf("CI%d", i), ctx->idf("CO%d", i),
                                              ClusterPlacement(PLACE_CDC_CI1 + i - 1), lut_only, lut_and_ff, dff_only);
                } else
                    disconnect_unused(&ci, ctx->idf("CI%d", i));
                if (ci.getPort(ctx->idf("DO%d", i))) {
                    connect_gnd_if_unconnected(&ci, ctx->idf("DI%d", i));
                    pack_xrf_input_and_output(&ci, ci.name, ctx->idf("DI%d", i), ctx->idf("DO%d", i),
                                              ClusterPlacement(PLACE_CDC_DI1 + i - 1), lut_only, lut_and_ff, dff_only);
                } else
                    disconnect_unused(&ci, ctx->idf("DI%d", i));
            }
        }

        // Remove inputs and outputs that are not used for specific types
        if (ci.type.in(id_BGC, id_GBC)) {
            disconnect_unused(&ci, id_CK1);
            disconnect_unused(&ci, id_CK2);
            disconnect_unused(&ci, id_ADRSTI);
            disconnect_unused(&ci, id_ADRSTO);
            disconnect_unused(&ci, id_BDRSTI);
            disconnect_unused(&ci, id_BDRSTO);
        } else {
            connect_gnd_if_unconnected(&ci, id_ADRSTI);
            pack_xrf_input_and_output(&ci, ci.name, id_ADRSTI, id_ADRSTO, PLACE_CDC_ADRSTI, lut_only, lut_and_ff,
                                      dff_only);
            connect_gnd_if_unconnected(&ci, id_BDRSTI);
            pack_xrf_input_and_output(&ci, ci.name, id_BDRSTI, id_BDRSTO, PLACE_CDC_BDRSTI, lut_only, lut_and_ff,
                                      dff_only);
        }
        if (ci.type.in(id_BGC, id_GBC, id_DDE)) {
            disconnect_unused(&ci, id_ASRSTI);
            disconnect_unused(&ci, id_ASRSTO);
            disconnect_unused(&ci, id_BSRSTI);
            disconnect_unused(&ci, id_BSRSTO);
        } else {
            connect_gnd_if_unconnected(&ci, id_ASRSTI);
            pack_xrf_input_and_output(&ci, ci.name, id_ASRSTI, id_ASRSTO, PLACE_CDC_ASRSTI, lut_only, lut_and_ff,
                                      dff_only);
            connect_gnd_if_unconnected(&ci, id_BSRSTI);
            pack_xrf_input_and_output(&ci, ci.name, id_BSRSTI, id_BSRSTO, PLACE_CDC_BSRSTI, lut_only, lut_and_ff,
                                      dff_only);
        }

        // Only XCDC is using these ports, remove from others if used
        if (!ci.type.in(id_XCDC)) {
            disconnect_unused(&ci, id_CDRSTI);
            disconnect_unused(&ci, id_CDRSTO);
            for (int i = 1; i <= 6; i++) {
                disconnect_unused(&ci, ctx->idf("CI%d", i));
                disconnect_unused(&ci, ctx->idf("CO%d", i));
            }
            disconnect_unused(&ci, id_CSRSTI);
            disconnect_unused(&ci, id_CSRSTO);

            disconnect_unused(&ci, id_DDRSTI);
            disconnect_unused(&ci, id_DDRSTO);
            for (int i = 1; i <= 6; i++) {
                disconnect_unused(&ci, ctx->idf("DI%d", i));
                disconnect_unused(&ci, ctx->idf("DO%d", i));
            }
            disconnect_unused(&ci, id_DSRSTI);
            disconnect_unused(&ci, id_DSRSTO);
        } else {
            connect_gnd_if_unconnected(&ci, id_CDRSTI);
            pack_xrf_input_and_output(&ci, ci.name, id_CDRSTI, id_CDRSTO, PLACE_CDC_CDRSTI, lut_only, lut_and_ff,
                                      dff_only);
            connect_gnd_if_unconnected(&ci, id_DDRSTI);
            pack_xrf_input_and_output(&ci, ci.name, id_DDRSTI, id_DDRSTO, PLACE_CDC_DDRSTI, lut_only, lut_and_ff,
                                      dff_only);
            connect_gnd_if_unconnected(&ci, id_CSRSTI);
            pack_xrf_input_and_output(&ci, ci.name, id_CSRSTI, id_CSRSTO, PLACE_CDC_CSRSTI, lut_only, lut_and_ff,
                                      dff_only);
            connect_gnd_if_unconnected(&ci, id_DSRSTI);
            pack_xrf_input_and_output(&ci, ci.name, id_DSRSTI, id_DSRSTO, PLACE_CDC_DSRSTI, lut_only, lut_and_ff,
                                      dff_only);
        }
    }
    if (lut_only)
        log_info("    %6d FEs used as LUT only\n", lut_only);
    if (lut_and_ff)
        log_info("    %6d FEs used as LUT and DFF\n", lut_and_ff);
    if (dff_only)
        log_info("    %6d FEs used as DFF only\n", dff_only);
    flush_cells();
}

void NgUltraPacker::pack_fifos(void)
{
    log_info("Packing FIFOs..\n");
    int lut_only = 0, lut_and_ff = 0, dff_only = 0;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_FIFO_U))
            continue;
        int mode = int_or_default(ci.params, id_mode, 0);
        switch (mode) {
        case 0:
            ci.type = id_FIFO;
            break;
        case 1:
            ci.type = id_XHFIFO;
            break;
        case 2:
            ci.type = id_XWFIFO;
            break;
        default:
            log_error("Unknown mode %d for cell '%s'.\n", mode, ci.name.c_str(ctx));
        }
        ci.cluster = ci.name;
        bool use_write_arst = bool_or_default(ci.params, id_use_write_arst, false);
        bool use_read_arst = bool_or_default(ci.params, id_use_read_arst, false);

        int rsti = (ci.type == id_FIFO) ? 2 : 4;
        for (int i = 1; i <= rsti; i++) {
            IdString port = ctx->idf("WRSTI%d", i);
            ci.ports[port].name = port;
            ci.ports[port].type = PORT_IN;
            port = ctx->idf("RRSTI%d", i);
            ci.ports[port].name = port;
            ci.ports[port].type = PORT_IN;
        }

        if (use_write_arst) {
            IdString port = id_WRSTI;
            connect_gnd_if_unconnected(&ci, port);
            NetInfo *wrsti_net = ci.getPort(port);
            ci.disconnectPort(port);
            for (int i = 1; i <= rsti; i++)
                ci.connectPort(ctx->idf("WRSTI%d", i), wrsti_net);
            if (mode != 0)
                disconnect_unused(&ci, id_WRSTO);
            pack_xrf_input_and_output(&ci, ci.name, id_WRSTI1, id_WRSTO, PLACE_FIFO_WRSTI1, lut_only, lut_and_ff,
                                      dff_only);
            pack_xrf_input_and_output(&ci, ci.name, id_WRSTI2, IdString(), PLACE_FIFO_WRSTI2, lut_only, lut_and_ff,
                                      dff_only);
            if (mode != 0) {
                pack_xrf_input_and_output(&ci, ci.name, id_WRSTI3, IdString(), PLACE_FIFO_WRSTI3, lut_only, lut_and_ff,
                                          dff_only);
                pack_xrf_input_and_output(&ci, ci.name, id_WRSTI4, IdString(), PLACE_FIFO_WRSTI4, lut_only, lut_and_ff,
                                          dff_only);
            }
        } else {
            disconnect_unused(&ci, id_WRSTI);
        }
        if (use_read_arst) {
            IdString port = id_RRSTI;
            connect_gnd_if_unconnected(&ci, port);
            NetInfo *rrsti_net = ci.getPort(port);
            ci.disconnectPort(port);
            for (int i = 1; i <= rsti; i++)
                ci.connectPort(ctx->idf("RRSTI%d", i), rrsti_net);
            if (mode != 0)
                disconnect_unused(&ci, id_RRSTO);
            pack_xrf_input_and_output(&ci, ci.name, id_RRSTI1, id_RRSTO, PLACE_FIFO_RRSTI1, lut_only, lut_and_ff,
                                      dff_only);
            pack_xrf_input_and_output(&ci, ci.name, id_RRSTI2, IdString(), PLACE_FIFO_RRSTI2, lut_only, lut_and_ff,
                                      dff_only);
            if (mode != 0) {
                pack_xrf_input_and_output(&ci, ci.name, id_RRSTI3, IdString(), PLACE_FIFO_RRSTI3, lut_only, lut_and_ff,
                                          dff_only);
                pack_xrf_input_and_output(&ci, ci.name, id_RRSTI4, IdString(), PLACE_FIFO_RRSTI4, lut_only, lut_and_ff,
                                          dff_only);
            }
        } else {
            disconnect_unused(&ci, id_RRSTI);
        }

        for (int i = 1; i <= 18; i++) {
            connect_gnd_if_unconnected(&ci, ctx->idf("I%d", i));
            pack_xrf_input_and_output(&ci, ci.name, ctx->idf("I%d", i), ctx->idf("O%d", i),
                                      ClusterPlacement(PLACE_FIFO_I1 + i - 1), lut_only, lut_and_ff, dff_only);
        }

        if (mode == 0) {
            for (int i = 19; i <= 36; i++) {
                disconnect_unused(&ci, ctx->idf("I%d", i));
                disconnect_unused(&ci, ctx->idf("O%d", i));
            }
        } else {
            for (int i = 19; i <= 36; i++) {
                connect_gnd_if_unconnected(&ci, ctx->idf("I%d", i));
                pack_xrf_input_and_output(&ci, ci.name, ctx->idf("I%d", i), ctx->idf("O%d", i),
                                          ClusterPlacement(PLACE_FIFO_I1 + i - 1), lut_only, lut_and_ff, dff_only);
            }
        }
        for (int i = 1; i <= 6; i++) {
            connect_gnd_if_unconnected(&ci, ctx->idf("RAI%d", i));
            pack_xrf_input_and_output(&ci, ci.name, ctx->idf("RAI%d", i), ctx->idf("RAO%d", i),
                                      ClusterPlacement(PLACE_FIFO_RAI1 + i - 1), lut_only, lut_and_ff, dff_only);

            connect_gnd_if_unconnected(&ci, ctx->idf("WAI%d", i));
            pack_xrf_input_and_output(&ci, ci.name, ctx->idf("WAI%d", i), ctx->idf("WAO%d", i),
                                      ClusterPlacement(PLACE_FIFO_WAI1 + i - 1), lut_only, lut_and_ff, dff_only);
        }

        if (mode == 0) {
            disconnect_unused(&ci, id_RAI7);
            disconnect_unused(&ci, id_WAI7);
        } else {
            connect_gnd_if_unconnected(&ci, id_RAI7);
            pack_xrf_input_and_output(&ci, ci.name, id_RAI7, id_RAO7, PLACE_FIFO_RAI7, lut_only, lut_and_ff, dff_only);

            connect_gnd_if_unconnected(&ci, id_WAI7);
            pack_xrf_input_and_output(&ci, ci.name, id_WAI7, id_WAO7, PLACE_FIFO_WAI7, lut_only, lut_and_ff, dff_only);
        }

        connect_gnd_if_unconnected(&ci, id_WE);
        pack_xrf_input_and_output(&ci, ci.name, id_WE, IdString(), PLACE_FIFO_WE, lut_only, lut_and_ff, dff_only);

        disconnect_if_gnd(&ci, id_WEA);
        pack_xrf_input_and_output(&ci, ci.name, id_WEA, IdString(), PLACE_FIFO_WEA, lut_only, lut_and_ff, dff_only);

        if (mode == 0) {
            // FIFO
            ci.renamePort(id_WEQ1, id_WEQ);
            pack_xrf_input_and_output(&ci, ci.name, IdString(), id_WEQ, PLACE_FIFO_WEQ1, lut_only, lut_and_ff,
                                      dff_only);
            disconnect_unused(&ci, id_WEQ2);

            ci.renamePort(id_REQ1, id_REQ);
            pack_xrf_input_and_output(&ci, ci.name, IdString(), id_REQ, PLACE_FIFO_REQ1, lut_only, lut_and_ff,
                                      dff_only);
            disconnect_unused(&ci, id_REQ2);
        } else {
            pack_xrf_input_and_output(&ci, ci.name, IdString(), id_WEQ1, PLACE_FIFO_WEQ1, lut_only, lut_and_ff,
                                      dff_only);
            pack_xrf_input_and_output(&ci, ci.name, IdString(), id_WEQ2, PLACE_FIFO_WEQ2, lut_only, lut_and_ff,
                                      dff_only);
            pack_xrf_input_and_output(&ci, ci.name, IdString(), id_WEQ1, PLACE_FIFO_REQ1, lut_only, lut_and_ff,
                                      dff_only);
            pack_xrf_input_and_output(&ci, ci.name, IdString(), id_WEQ2, PLACE_FIFO_REQ2, lut_only, lut_and_ff,
                                      dff_only);

            // XFIFO
            ci.ports[id_WCK1].name = id_WCK1;
            ci.ports[id_WCK1].type = PORT_IN;
            ci.ports[id_WCK2].name = id_WCK2;
            ci.ports[id_WCK2].type = PORT_IN;
            ci.ports[id_RCK1].name = id_RCK1;
            ci.ports[id_RCK1].type = PORT_IN;
            ci.ports[id_RCK2].name = id_RCK2;
            ci.ports[id_RCK2].type = PORT_IN;
            NetInfo *net = ci.getPort(id_WCK);
            if (net) {
                ci.disconnectPort(id_WCK);

                ci.connectPort(id_WCK1, net);
                ci.connectPort(id_WCK2, net);
            }
            net = ci.getPort(id_RCK);
            if (net) {
                ci.disconnectPort(id_RCK);

                ci.connectPort(id_RCK1, net);
                ci.connectPort(id_RCK2, net);
            }
        }
    }
    if (lut_only)
        log_info("    %6d FEs used as LUT only\n", lut_only);
    if (lut_and_ff)
        log_info("    %6d FEs used as LUT and DFF\n", lut_and_ff);
    if (dff_only)
        log_info("    %6d FEs used as DFF only\n", dff_only);
    flush_cells();
}

void NgUltraPacker::insert_ioms()
{
    std::vector<IdString> pins_needing_iom;
    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        // Skip undriven nets
        if (ni->driver.cell == nullptr)
            continue;
        if (!ni->driver.cell->type.in(id_BFR)) {
            continue;
        }
        Loc iotp_loc = ni->driver.cell->getLocation();
        iotp_loc.z -= 1;
        BelId bel = ctx->getBelByLocation(iotp_loc);
        if (uarch->global_capable_bels.count(bel) == 0)
            continue;
        for (const auto &usr : ni->users) {
            if (uarch->is_fabric_lowskew_sink(usr) || uarch->is_ring_clock_sink(usr) ||
                uarch->is_tube_clock_sink(usr) || uarch->is_ring_over_tile_clock_sink(usr)) {
                pins_needing_iom.emplace_back(ni->name);
                break;
            }
        }
    }
    // Sort clocks by max fanout
    log_info("Inserting IOMs...\n");
    int bfr_removed = 0;
    for (size_t i = 0; i < pins_needing_iom.size(); i++) {
        NetInfo *net = ctx->nets.at(pins_needing_iom.at(i)).get();
        Loc iotp_loc = net->driver.cell->getLocation();
        iotp_loc.z -= 1;
        BelId iotp_bel = ctx->getBelByLocation(iotp_loc);

        IdString iob = uarch->tile_name_id(iotp_bel.tile);
        BelId bel = uarch->iom_bels[iob];

        CellInfo *iom = nullptr;
        IdString port = uarch->global_capable_bels.at(iotp_bel);
        CellInfo *input_pad = ctx->getBoundBelCell(iotp_bel);
        std::string iobname = str_or_default(input_pad->params, id_iobname, "");
        if (!ctx->checkBelAvail(bel)) {
            iom = ctx->getBoundBelCell(bel);
            log_info("    Reusing IOM in bank '%s' for signal '%s'\n", iob.c_str(ctx), iobname.c_str());
        } else {
            iom = create_cell_ptr(id_IOM, ctx->idf("%s$iom", iob.c_str(ctx)));
            log_info("    Adding IOM in bank '%s' for signal '%s'\n", iob.c_str(ctx), iobname.c_str());
        }
        if (iom->getPort(port)) {
            log_error("Port '%s' of IOM cell '%s' is already used.\n", port.c_str(ctx), iom->name.c_str(ctx));
        }
        NetInfo *iom_to_clk = ctx->createNet(ctx->idf("%s$iom", net->name.c_str(ctx)));
        for (const auto &usr : net->users) {
            IdString port = usr.port;
            usr.cell->disconnectPort(port);
            usr.cell->connectPort(port, iom_to_clk);
        }
        iom->connectPort(port, input_pad->getPort(id_O));
        iom->connectPort((port == id_P17RI) ? id_CKO1 : id_CKO2, iom_to_clk);
        if (ctx->checkBelAvail(bel))
            ctx->bindBel(bel, iom, PlaceStrength::STRENGTH_LOCKED);
        CellInfo *bfr = net->driver.cell;
        if (bfr->type == id_BFR && bfr->getPort(id_O)->users.empty()) {
            bfr->disconnectPort(id_O);
            bfr->disconnectPort(id_I);
            bfr_removed++;
            ctx->cells.erase(bfr->name);
        }
    }
    if (bfr_removed)
        log_info("    Removed %d unused BFR\n", bfr_removed);
}

void NgUltraPacker::insert_wfbs()
{
    log_info("Inserting WFBs...\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (ci.type.in(id_IOM)) {
            insert_wfb(&ci, id_CKO1);
            insert_wfb(&ci, id_CKO2);
        } else if (ci.type.in(id_PLL)) {
            insert_wfb(&ci, id_OSC);
            insert_wfb(&ci, id_VCO);
            insert_wfb(&ci, id_REFO);
            insert_wfb(&ci, id_LDFO);
            insert_wfb(&ci, id_CLK_DIV1);
            insert_wfb(&ci, id_CLK_DIV2);
            insert_wfb(&ci, id_CLK_DIV3);
            insert_wfb(&ci, id_CLK_DIV4);
            insert_wfb(&ci, id_CLK_DIVD1);
            insert_wfb(&ci, id_CLK_DIVD2);
            insert_wfb(&ci, id_CLK_DIVD3);
            insert_wfb(&ci, id_CLK_DIVD4);
            insert_wfb(&ci, id_CLK_DIVD5);
            insert_wfb(&ci, id_CLK_CAL_DIV);
        }
    }
}

void NgUltraPacker::mandatory_param(CellInfo *cell, IdString param)
{
    if (!cell->params.count(param))
        log_error("Mandatory parameter '%s' of cell '%s'(%s) is missing.\n", param.c_str(ctx), cell->name.c_str(ctx),
                  cell->type.c_str(ctx));
}

int NgUltraPacker::memory_width(int config, bool ecc)
{
    if (ecc) {
        if (config == 4)
            return 18;
        else
            log_error("ECC mode only support width of 18.\n");
    } else {
        switch (config) {
        case 0:
            return 1; // NOECC_48kx1
        case 1:
            return 2; // NOECC_24kx2
        case 2:
            return 4; // NOECC_12kx4
        case 3:
            return 8; // NOECC_6kx8
        case 4:
            return 12; // NOECC_4kx12
        case 5:
            return 24; // NOECC_2kx24
        case 6:
            return 3; // NOECC_16kx3
        case 7:
            return 6; // NOECC_8kx6
        }
        log_error("Unknown memory configuration width config '%d'.\n", config);
    }
}

int NgUltraPacker::memory_addr_bits(int config, bool ecc)
{
    if (ecc) {
        if (config == 4)
            return 11;
        else
            log_error("ECC mode only support width of 18.\n");
    } else {
        switch (config) {
        case 0:
            return 16; // NOECC_48kx1
        case 1:
            return 15; // NOECC_24kx2
        case 2:
            return 14; // NOECC_12kx4
        case 3:
            return 13; // NOECC_6kx8
        case 4:
            return 12; // NOECC_4kx12
        case 5:
            return 11; // NOECC_2kx24
        case 6:
            return 14; // NOECC_16kx3
        case 7:
            return 13; // NOECC_8kx6
        }
        log_error("Unknown memory configuration width config '%d'.\n", config);
    }
}

void NgUltraPacker::insert_wfb(CellInfo *cell, IdString port)
{
    NetInfo *net = cell->getPort(port);
    if (!net)
        return;

    CellInfo *wfg = net_only_drives(ctx, net, is_wfg, id_ZI, true);
    if (wfg)
        return;
    bool in_fabric = false;
    bool in_ring = false;
    for (const auto &usr : net->users) {
        if (uarch->is_fabric_lowskew_sink(usr) || uarch->is_tube_clock_sink(usr) ||
            uarch->is_ring_over_tile_clock_sink(usr))
            in_fabric = true;
        else
            in_ring = true;
    }
    // If all in ring and none in fabric no need for WFB
    if (in_ring && !in_fabric)
        return;
    log_info("    Inserting WFB for cell '%s' port '%s'\n", cell->name.c_str(ctx), port.c_str(ctx));
    CellInfo *wfb = create_cell_ptr(id_WFB, ctx->idf("%s$%s", cell->name.c_str(ctx), port.c_str(ctx)));
    if (in_ring && in_fabric) {
        // If both in ring and in fabric create new signal
        wfb->connectPort(id_ZI, net);
        NetInfo *net_zo = ctx->createNet(ctx->idf("%s$ZO", net->name.c_str(ctx)));
        wfb->connectPort(id_ZO, net_zo);
        for (const auto &usr : net->users) {
            if (uarch->is_fabric_lowskew_sink(usr) || uarch->is_ring_over_tile_clock_sink(usr)) {
                usr.cell->disconnectPort(usr.port);
                usr.cell->connectPort(usr.port, net_zo);
            }
        }
    } else {
        // Only in fabric, reconnect wire directly to WFB
        cell->disconnectPort(port);
        wfb->connectPort(id_ZO, net);
        NetInfo *new_out = ctx->createNet(ctx->idf("%s$%s", net->name.c_str(ctx), port.c_str(ctx)));
        cell->connectPort(port, new_out);
        wfb->connectPort(id_ZI, new_out);
    }
}

void NgUltraPacker::constrain_location(CellInfo *cell)
{
    std::string location = str_or_default(cell->params, id_location, "");
    if (!location.empty()) {
        if (uarch->locations.count(location)) {
            BelId bel = uarch->locations[location];
            if (ctx->getBelType(bel) != cell->type) {
                log_error("Location '%s' is wrong for bel type '%s'.\n", location.c_str(), cell->type.c_str(ctx));
            }
            if (ctx->checkBelAvail(bel)) {
                log_info("    Constraining %s '%s' to '%s'\n", cell->type.c_str(ctx), cell->name.c_str(ctx),
                         location.c_str());
                ctx->bindBel(bel, cell, PlaceStrength::STRENGTH_LOCKED);
            } else {
                log_error("Bel at location '%s' is already used by other cell.\n", location.c_str());
            }

        } else {
            log_error("Unknown location '%s' for cell '%s'.\n", location.c_str(), cell->name.c_str(ctx));
        }
    }
}

void NgUltraPacker::pack_plls(void)
{
    log_info("Packing PLLs..\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_PLL_U))
            continue;
        ci.type = id_PLL;
        constrain_location(&ci);

        disconnect_if_gnd(&ci, id_FBK);
        disconnect_if_gnd(&ci, id_CLK_CAL);
        disconnect_if_gnd(&ci, id_R);
        disconnect_if_gnd(&ci, id_EXT_CAL1);
        disconnect_if_gnd(&ci, id_EXT_CAL2);
        disconnect_if_gnd(&ci, id_EXT_CAL3);
        disconnect_if_gnd(&ci, id_EXT_CAL4);
        disconnect_if_gnd(&ci, id_EXT_CAL5);
        disconnect_if_gnd(&ci, id_EXT_CAL_LOCKED);
        disconnect_if_gnd(&ci, id_ARST_CAL);
    }
}

void NgUltraPacker::pack_wfgs(void)
{
    log_info("Packing WFGs..\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_WFG_U))
            continue;
        ci.type = id_WFG;
        constrain_location(&ci);
        int mode = int_or_default(ci.params, id_mode, 1);
        if (mode == 0) { // WFB - bypass mode
            ci.type = id_WFB;
            // must not be used, zero is tollerated
            disconnect_unused(&ci, id_SI);
            disconnect_unused(&ci, id_SO);
            disconnect_unused(&ci, id_R);
        } else if (mode == 1) {
            // Can be unused, if zero it is unused
            disconnect_if_gnd(&ci, id_SI);
            disconnect_if_gnd(&ci, id_R);
        } else if (mode == 2) {
            // Allow mode 2
        } else {
            log_error("Unknown mode %d for cell '%s'.\n", mode, ci.name.c_str(ctx));
        }
        NetInfo *zi = ci.getPort(id_ZI);
        if (!zi || !zi->driver.cell)
            log_error("WFG port ZI of '%s' must be driven.\n", ci.name.c_str(ctx));
        NetInfo *zo = ci.getPort(id_ZO);
        if (!zo || zo->users.entries() == 0)
            log_error("WFG port ZO of '%s' must be connected.\n", ci.name.c_str(ctx));
    }
}

void NgUltraPacker::pack_gcks(void)
{
    log_info("Packing GCKs..\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_GCK_U))
            continue;
        ci.type = id_GCK;
        std::string mode = str_or_default(ci.params, id_std_mode, "BYPASS");
        if (mode == "BYPASS") {
            disconnect_unused(&ci, id_SI2);
            disconnect_unused(&ci, id_CMD);
        } else if (mode == "CSC") {
            disconnect_unused(&ci, id_SI1);
            disconnect_unused(&ci, id_SI2);
        } else if (mode == "CKS") {
            disconnect_unused(&ci, id_SI2);
        } else if (mode == "MUX") {
            // all used
        } else
            log_error("Unknown mode '%s' for cell '%s'.\n", mode.c_str(), ci.name.c_str(ctx));

        if (net_driven_by(ctx, ci.getPort(id_SI1), is_gck, id_SO) ||
            net_driven_by(ctx, ci.getPort(id_SI2), is_gck, id_SO) ||
            net_driven_by(ctx, ci.getPort(id_CMD), is_gck, id_SO)) {
            log_error("Cascaded GCKs are not allowed.\n");
        }
    }
}

void NgUltraPacker::pack_rams(void)
{
    log_info("Packing RAMs..\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_RAM))
            continue;
        ci.type = id_RAM;
        bind_attr_loc(&ci, &ci.attrs);
        // These ACKx and BCKx exists for NX_RAM, but are not available on NX_ULTRA
        ci.disconnectPort(id_ACKC);
        ci.disconnectPort(id_ACKD);
        ci.disconnectPort(id_ACKR);
        ci.disconnectPort(id_BCKC);
        ci.disconnectPort(id_BCKD);
        ci.disconnectPort(id_BCKR);
        mandatory_param(&ci, id_raw_config0);
        mandatory_param(&ci, id_raw_config1);
        Property extr = ci.params[id_raw_config1].extract(0, 16);
        std::vector<bool> bits = extr.as_bits();
        // int ecc_mode = (bits[12] ? 1 : 0) | (bits[13] ? 2 : 0) | (bits[14] ? 4 : 0) | (bits[15] ? 8 : 0);
        bool ecc = bits[12];
        // int a_in_width = memory_width((bits[0] ? 1 : 0) | (bits[1] ? 2 : 0) | (bits[2] ? 4 : 0), ecc);
        // int b_in_width = memory_width((bits[3] ? 1 : 0) | (bits[4] ? 2 : 0) | (bits[5] ? 4 : 0), ecc);
        int a_out_width = memory_width((bits[6] ? 1 : 0) | (bits[7] ? 2 : 0) | (bits[8] ? 4 : 0), ecc);
        int b_out_width = memory_width((bits[9] ? 1 : 0) | (bits[10] ? 2 : 0) | (bits[11] ? 4 : 0), ecc);

        int a_addr = std::max(memory_addr_bits((bits[0] ? 1 : 0) | (bits[1] ? 2 : 0) | (bits[2] ? 4 : 0), ecc),
                              memory_addr_bits((bits[6] ? 1 : 0) | (bits[7] ? 2 : 0) | (bits[8] ? 4 : 0), ecc));
        int b_addr = std::max(memory_addr_bits((bits[3] ? 1 : 0) | (bits[4] ? 2 : 0) | (bits[5] ? 4 : 0), ecc),
                              memory_addr_bits((bits[9] ? 1 : 0) | (bits[10] ? 2 : 0) | (bits[11] ? 4 : 0), ecc));

        NetInfo *a_cs = ci.getPort(id_ACS);
        if (!a_cs || a_cs->name.in(ctx->id("$PACKER_GND"))) {
            // If there is no chip-select disconnect all
            disconnect_unused(&ci, id_ACK);
            for (int i = 0; i < 24; i++) {
                disconnect_unused(&ci, ctx->idf("AI%d", i + 1));
                disconnect_unused(&ci, ctx->idf("AO%d", i + 1));
            }
            for (int i = 0; i < 16; i++)
                disconnect_unused(&ci, ctx->idf("AA%d", i + 1));
        } else {
            for (int i = a_out_width; i < 24; i++)
                disconnect_unused(&ci, ctx->idf("AO%d", i + 1));
            for (int i = a_addr; i < 16; i++)
                disconnect_unused(&ci, ctx->idf("AA%d", i + 1));
        }

        NetInfo *b_cs = ci.getPort(id_BCS);
        if (!b_cs || b_cs->name.in(ctx->id("$PACKER_GND"))) {
            // If there is no chip-select disconnect all
            disconnect_unused(&ci, id_BCK);
            for (int i = 0; i < 24; i++) {
                disconnect_unused(&ci, ctx->idf("BI%d", i + 1));
                disconnect_unused(&ci, ctx->idf("BO%d", i + 1));
            }
            for (int i = 0; i < 16; i++)
                disconnect_unused(&ci, ctx->idf("BA%d", i + 1));
        } else {
            for (int i = b_out_width; i < 24; i++)
                disconnect_unused(&ci, ctx->idf("BO%d", i + 1));
            for (int i = b_addr; i < 16; i++)
                disconnect_unused(&ci, ctx->idf("BA%d", i + 1));
        }

        for (auto &p : ci.ports) {
            if (p.second.type == PortType::PORT_IN)
                disconnect_if_gnd(&ci, p.first);
        }
    }
}

void NgUltraPacker::dsp_same_driver(IdString port, CellInfo *cell, CellInfo **target)
{
    if (cell->getPort(port)) {
        CellInfo *driver = cell->getPort(port)->driver.cell;
        if (!driver->type.in(id_DSP, id_NX_DSP_U))
            log_error("Port '%s' of '%s' can only be driven by DSP.\n", port.c_str(ctx), cell->name.c_str(ctx));
        if (*target && *target != driver)
            log_error("CAI1-24, CBI1-18, CZI1..56 and CCI must be from same DSP for '%s'.\n", cell->name.c_str(ctx));
        *target = driver;
    }
}

void NgUltraPacker::dsp_same_sink(IdString port, CellInfo *cell, CellInfo **target)
{
    if (cell->getPort(port)) {
        if (cell->getPort(port)->users.entries() != 1)
            log_error("Port '%s' of '%s' can only drive one DSP.\n", port.c_str(ctx), cell->name.c_str(ctx));
        CellInfo *driver = (*cell->getPort(port)->users.begin()).cell;
        if (!driver->type.in(id_DSP, id_NX_DSP_U))
            log_error("Port '%s' of '%s' can only drive DSP.\n", port.c_str(ctx), cell->name.c_str(ctx));
        if (*target && *target != driver)
            log_error("CAI1-24, CBI1-18, CZI1..56 and CCI must be from same DSP for '%s'.\n", cell->name.c_str(ctx));
        *target = driver;
    }
}

void NgUltraPacker::pack_dsps(void)
{
    log_info("Packing DSPs..\n");
    dict<IdString, CellInfo *> dsp_output;
    std::vector<CellInfo *> root_dsps;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_DSP_U))
            continue;
        ci.type = id_DSP;
        bind_attr_loc(&ci, &ci.attrs);
        mandatory_param(&ci, id_raw_config0);
        mandatory_param(&ci, id_raw_config1);
        mandatory_param(&ci, id_raw_config2);
        mandatory_param(&ci, id_raw_config3);

        for (auto &p : ci.ports) {
            if (p.second.type == PortType::PORT_IN)
                disconnect_if_gnd(&ci, p.first);
        }

        // CAI1-24, CBI1-18, CZI1..56 and CCI must be from same DSP
        CellInfo *dsp = nullptr;
        for (int i = 1; i <= 24; i++)
            dsp_same_driver(ctx->idf("CAI%d", i), &ci, &dsp);
        for (int i = 1; i <= 18; i++)
            dsp_same_driver(ctx->idf("CBI%d", i), &ci, &dsp);
        for (int i = 1; i <= 56; i++)
            dsp_same_driver(ctx->idf("CZI%d", i), &ci, &dsp);
        dsp_same_driver(id_CCI, &ci, &dsp);
        if (!dsp)
            root_dsps.push_back(&ci);

        // CAO1-24, CBO1-18, CZO1..56 and CCO must go to same DSP
        dsp = nullptr;
        for (int i = 1; i <= 24; i++)
            dsp_same_sink(ctx->idf("CAO%d", i), &ci, &dsp);
        for (int i = 1; i <= 18; i++)
            dsp_same_sink(ctx->idf("CBO%d", i), &ci, &dsp);
        for (int i = 1; i <= 56; i++)
            dsp_same_sink(ctx->idf("CZO%d", i), &ci, &dsp);
        dsp_same_sink(id_CCO, &ci, &dsp);
        if (dsp)
            dsp_output.emplace(ci.name, dsp);
    }
    for (auto root : root_dsps) {
        CellInfo *dsp = root;
        if (dsp_output.count(dsp->name) == 0)
            continue;
        root->cluster = root->name;
        while (true) {
            if (dsp_output.count(dsp->name) == 0)
                break;
            dsp = dsp_output[dsp->name];
            dsp->cluster = root->name;
            root->constr_children.push_back(dsp);
            dsp->constr_z = PLACE_DSP_CHAIN;
        }
    }
}

void NgUltraPacker::remove_not_used()
{
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        for (auto &p : ci.ports) {
            if (p.second.type == PortType::PORT_OUT) {
                NetInfo *net = ci.getPort(p.first);
                if (net && net->users.entries() == 0) {
                    ci.disconnectPort(p.first);
                }
            }
        }
    }
}

void NgUltraImpl::pack()
{
    const ArchArgs &args = ctx->args;
    if (args.options.count("csv")) {
        parse_csv(args.options.at("csv"));
    }

    // Setup
    NgUltraPacker packer(ctx, this);
    packer.pack_constants();
    packer.remove_not_used();
    packer.update_lut_init();
    packer.update_dffs();

    // CGB
    packer.pack_rams();
    packer.pack_dsps();

    // TILE
    packer.pack_rfs();
    packer.pack_cdcs();
    packer.pack_fifos();
    packer.pack_cys();
    if (!args.options.count("no-xlut"))
        packer.pack_xluts();
    if (!args.options.count("no-lut-chains"))
        packer.pack_lut_multi_dffs();
    if (!args.options.count("no-dff-chains"))
        packer.pack_dff_chains();
    packer.pack_lut_dffs();
    packer.pack_dffs();

    // Tube
    packer.pack_gcks();

    // Ring
    packer.pack_iobs();
    packer.pack_ioms();
    packer.pack_plls();
    packer.pack_wfgs();
    packer.insert_ioms();
    packer.insert_wfbs();

    packer.pre_place();
}

IdString NgUltraPacker::assign_wfg(IdString ckg, IdString ckg2, CellInfo *cell)
{
    for (auto &item : uarch->unused_wfg) {
        BelId bel = item.first;
        if (item.second == ckg || item.second == ckg2) {
            IdString ckg = item.second;
            uarch->unused_wfg.erase(bel);
            log_info("    Using '%s:%s' for cell '%s'.\n", uarch->tile_name(bel.tile).c_str(),
                     ctx->getBelName(bel)[1].c_str(ctx), cell->name.c_str(ctx));
            ctx->bindBel(bel, cell, PlaceStrength::STRENGTH_LOCKED);
            return ckg;
        }
    }
    log_error("    No more available WFGs for cell '%s'.\n", cell->name.c_str(ctx));
}

void NgUltraPacker::extract_lowskew_signals(CellInfo *cell,
                                            dict<IdString, dict<IdString, std::vector<PortRef>>> &lowskew_signals)
{
    IdString loc;
    if (cell->bel != BelId())
        loc = uarch->tile_name_id(cell->bel.tile);

    PortRef ref;
    ref.cell = cell;
    auto &sinks = uarch->get_fabric_lowskew_sinks();
    if (sinks.count(cell->type)) {
        for (auto &port : sinks.at(cell->type)) {
            NetInfo *clock = cell->getPort(port);
            if (clock && !global_lowskew.count(clock->name)) {
                ref.port = port;
                lowskew_signals[loc][clock->name].push_back(ref);
            }
        }
    }
}

void NgUltraPacker::pre_place(void)
{
    log_info("Pre-placing PLLs..\n");

    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_PLL))
            continue;
        // Remove from list those that are used
        if (ci.bel != BelId()) {
            uarch->unused_pll.erase(ci.bel);
        }
    }
    // First process those on dedicated clock pins
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_PLL) || ci.bel != BelId())
            continue;

        NetInfo *ref = ci.getPort(id_REF);
        if (ref && ref->driver.cell && ref->driver.cell->type == id_IOM) {
            IdString bank = uarch->tile_name_id(ref->driver.cell->bel.tile);
            // bool found = false;
            for (auto &item : uarch->unused_pll) {
                BelId bel = item.first;
                std::pair<IdString, IdString> &ckgs = uarch->bank_to_ckg[bank];
                if (item.second == ckgs.first || item.second == ckgs.second) {
                    uarch->unused_pll.erase(bel);
                    log_info("    Using PLL in '%s' for cell '%s'.\n", uarch->tile_name(bel.tile).c_str(),
                             ci.name.c_str(ctx));
                    ctx->bindBel(bel, &ci, PlaceStrength::STRENGTH_LOCKED);
                    // found = true;
                    break;
                }
            }
        }
    }
    // If PLL use any other pin, location is not relevant, so we pick available
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_PLL) || ci.bel != BelId())
            continue;
        if (uarch->unused_pll.empty())
            log_error("    No more available PLLs for driving '%s'.\n", ci.name.c_str(ctx));
        BelId bel = uarch->unused_pll.begin()->first;
        uarch->unused_pll.erase(bel);
        log_info("    Using PLL in '%s' for cell '%s'.\n", uarch->tile_name(bel.tile).c_str(), ci.name.c_str(ctx));
        ctx->bindBel(bel, &ci, PlaceStrength::STRENGTH_LOCKED);
    }

    log_info("Pre-placing WFB/WFGs..\n");

    std::vector<CellInfo *> root_wfgs;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_WFG, id_WFB))
            continue;
        NetInfo *zi = ci.getPort(id_ZI);
        if (!zi || !zi->driver.cell || !zi->driver.cell->type.in(id_WFG, id_WFB))
            root_wfgs.push_back(&ci);
    }

    std::vector<std::vector<CellInfo *>> groups;
    for (auto root : root_wfgs) {
        std::vector<CellInfo *> group;
        CellInfo *wfg = root;
        group.push_back(wfg);
        while (true) {
            NetInfo *zo_net = wfg->getPort(id_ZO);
            if (zo_net && zo_net->users.entries() > 0) {
                wfg = (*zo_net->users.begin()).cell;
                if (wfg->type.in(id_WFG, id_WFB)) {
                    if (zo_net->users.entries() != 1)
                        log_error("WFG can only be chained with one other WFG cell\n");
                    group.push_back(wfg);
                } else
                    break;
            } else
                break;
        }
        groups.push_back(group);
    }

    // First pre-place those depending of PLL
    for (auto &grp : groups) {
        CellInfo *root = grp.front();
        NetInfo *zi = root->getPort(id_ZI);
        if (zi->driver.cell->type.in(id_PLL)) {
            IdString ckg = uarch->tile_name_id(zi->driver.cell->bel.tile);
            assign_wfg(ckg, IdString(), root);
            for (int i = 1; i < int(grp.size()); i++) {
                assign_wfg(ckg, IdString(), grp.at(i));
            }
        }
    }
    // Then those that depend on IOM
    for (auto &grp : groups) {
        CellInfo *root = grp.front();
        NetInfo *zi = root->getPort(id_ZI);
        if (zi->driver.cell->type.in(id_IOM)) {
            IdString bank = uarch->tile_name_id(zi->driver.cell->bel.tile);
            std::pair<IdString, IdString> &ckgs = uarch->bank_to_ckg[bank];
            IdString ckg = assign_wfg(ckgs.first, ckgs.second, root);
            for (int i = 1; i < int(grp.size()); i++) {
                assign_wfg(ckg, IdString(), grp.at(i));
            }
        }
    }
    for (auto &grp : groups) {
        CellInfo *root = grp.front();
        if (root->bel != BelId())
            continue;
        // Assign first available
        BelId bel = uarch->unused_pll.begin()->first;
        IdString ckg = uarch->unused_pll.begin()->second;
        uarch->unused_pll.erase(bel);
        for (int i = 1; i < int(grp.size()); i++) {
            assign_wfg(ckg, IdString(), grp.at(i));
        }
    }

    dict<IdString, dict<IdString, std::vector<PortRef>>> lowskew_signals;
    for (auto &cell : ctx->cells)
        extract_lowskew_signals(cell.second.get(), lowskew_signals);

    log_info("Adding GCK for lowskew signals..\n");
    for (auto &n : lowskew_signals[IdString()]) {
        NetInfo *net = ctx->nets.at(n.first).get();
        if (net->driver.cell->type.in(id_BFR, id_DFR, id_DDFR)) {
            CellInfo *bfr = net->driver.cell;
            CellInfo *gck_cell = create_cell_ptr(id_GCK, ctx->idf("%s$csc", bfr->name.c_str(ctx)));
            gck_cell->params[id_std_mode] = Property("CSC");
            NetInfo *new_out = ctx->createNet(ctx->idf("%s$bfr", bfr->name.c_str(ctx)));
            NetInfo *old = bfr->getPort(id_O);
            bfr->disconnectPort(id_O);
            gck_cell->connectPort(id_SO, old);
            gck_cell->connectPort(id_CMD, new_out);
            bfr->connectPort(id_O, new_out);
            log_info("    Create GCK '%s' for signal '%s'\n", gck_cell->name.c_str(ctx), n.first.c_str(ctx));
        }
        if (net->driver.cell->type.in(id_BEYOND_FE)) {
            CellInfo *fe = net->driver.cell;
            if (!fe->params.count(id_lut_table))
                continue;
            if (fe->params.count(id_dff_used))
                continue;
            if (fe->params[id_lut_table] != Property(0x5555, 16))
                continue;
            if (!fe->getPort(id_I1))
                continue;
            CellInfo *bfr = fe->getPort(id_I1)->driver.cell;
            if (bfr->type.in(id_BFR, id_DFR, id_DDFR)) {
                CellInfo *gck_cell = create_cell_ptr(id_GCK, ctx->idf("%s$csc", bfr->name.c_str(ctx)));
                gck_cell->params[id_std_mode] = Property("CSC");
                gck_cell->params[id_inv_out] = Property(true);
                NetInfo *old_out = fe->getPort(id_LO);
                NetInfo *old_in = fe->getPort(id_I1);
                fe->disconnectPort(id_LO);
                fe->disconnectPort(id_I1);
                gck_cell->connectPort(id_SO, old_out);
                gck_cell->connectPort(id_CMD, old_in);
                packed_cells.insert(fe->name);
                log_info("    Create GCK '%s' for signal '%s'\n", gck_cell->name.c_str(ctx), n.first.c_str(ctx));
            }
        }
    }
    flush_cells();
}

void NgUltraImpl::disable_beyond_fe_s_output(BelId bel)
{
    WireId dwire = ctx->getBelPinWire(bel, id_DO);
    for (PipId pip : ctx->getPipsDownhill(dwire)) {
        for (PipId pip2 : ctx->getPipsDownhill(ctx->getPipDstWire(pip))) {
            IdString dst = ctx->getWireName(ctx->getPipDstWire(pip2))[1];
            if (boost::ends_with(dst.c_str(ctx), ".DS")) {
                blocked_pips.emplace(pip2);
                return;
            }
        }
    }
}

void NgUltraImpl::postPlace()
{
    log_break();
    log_info("Limiting routing...\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (ci.type == id_BEYOND_FE) {
            const auto &extra_data = *bel_extra_data(ci.bel);
            // Check if CSC mode only if FE is capable
            if ((extra_data.flags & BEL_EXTRA_FE_CSC)) {
                if (str_or_default(ci.params, id_type, "") == "CSC")
                    continue;
                // Disable routing to S output if CSC is not used
                disable_beyond_fe_s_output(ci.bel);
            }
        } else if (ci.type == id_CY) {
            // In case A input is actually used, but it is connectd to GND
            // it is considered that signal is comming from RI1 crossbar.
            // We need to prevent router to use that crossbar output for
            // any other signal.
            for (int i = 1; i <= 4; i++) {
                IdString port = ctx->idf("A%d", i);
                NetInfo *net = ci.getPort(port);
                if (!net)
                    continue;
                if (net->name.in(ctx->id("$PACKER_GND"))) {
                    WireId dwire = ctx->getBelPinWire(ci.bel, port);
                    for (PipId pip : ctx->getPipsUphill(dwire)) {
                        WireId src = ctx->getPipSrcWire(pip);
                        const std::string src_name = ctx->getWireName(src)[1].str(ctx);
                        if (boost::starts_with(src_name, "RI1")) {
                            for (PipId pip2 : ctx->getPipsDownhill(src)) {
                                blocked_pips.emplace(pip2);
                            }
                        }
                    }
                    ci.disconnectPort(port); // Disconnect A
                }
            }
        }
    }
    remove_constants();

    const ArchArgs &args = ctx->args;
    NgUltraPacker packer(ctx, this);
    log_break();
    log_info("Running post-placement ...\n");
    packer.duplicate_gck();
    packer.insert_bypass_gck();
    if (!args.options.count("no-csc-insertion"))
        packer.insert_csc();
    log_break();
    ctx->assignArchInfo();
}

BelId NgUltraPacker::getCSC(Loc l, int row)
{
    const int z_loc[] = {0, 15, 16, 31};
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 4; i++) {
            BelId bel = ctx->getBelByLocation(Loc(l.x + j + 1, l.y + j % 2 + 2, z_loc[i]));
            if (!ctx->getBoundBelCell(bel) && (row == 0 || row == (i + 1)))
                return bel;
        }
    }
    return BelId();
}

void NgUltraPacker::insert_csc()
{
    dict<IdString, dict<IdString, std::vector<PortRef>>> local_system_matrix;
    log_info("Inserting CSCs...\n");
    int insert_new_csc = 0, change_to_csc = 0;
    for (auto &cell : ctx->cells) {
        auto ci = cell.second.get();
        if (ci->bel != BelId()) {
            if (uarch->tile_type(ci->bel.tile) == TILE_EXTRA_FABRIC) {
                extract_lowskew_signals(ci, local_system_matrix);
            }
        }
    }
    for (auto &lsm : local_system_matrix) {
        std::string name = lsm.first.c_str(ctx);
        Loc loc = uarch->tile_locations[name];
        std::vector<std::pair<int, IdString>> fanout;
        for (auto &n : lsm.second) {
            fanout.push_back(std::make_pair(n.second.size(), n.first));
        }
        std::sort(fanout.begin(), fanout.end(), std::greater<std::pair<int, IdString>>());
        int available_csc = 12;
        for (std::size_t i = 0; i < std::min<std::size_t>(fanout.size(), available_csc); i++) {
            auto &n = fanout.at(i);

            NetInfo *net = ctx->nets.at(n.second).get();
            CellInfo *cell = net->driver.cell;
            if (uarch->tile_name(cell->bel.tile) == lsm.first.c_str(ctx) && !cell->params.count(id_dff_used) &&
                cell->cluster == ClusterId()) {
                BelId newbel = getCSC(loc, 0);
                if (newbel == BelId())
                    break;

                ctx->unbindBel(cell->bel);
                cell->disconnectPort(id_LO);
                NetInfo *new_out = ctx->createNet(ctx->idf("%s$o", cell->name.c_str(ctx)));
                cell->params[id_CSC] = Property(Property::State::S1);
                cell->params[id_type] = Property("CSC");
                cell->params[id_dff_used] = Property(1, 1);
                cell->connectPort(id_LO, new_out);
                cell->connectPort(id_DI, new_out);

                cell->connectPort(id_DO, net);
                ctx->bindBel(newbel, cell, PlaceStrength::STRENGTH_LOCKED);
                change_to_csc++;
                continue;
            }
            Loc cell_loc = ctx->getBelLocation(cell->bel);
            BelId newbel = getCSC(loc, (cell_loc.y & 3) + 1); // Take CSC from pefered row
            if (newbel == BelId())
                newbel = getCSC(loc, 0); // Try getting any other CSC
            if (newbel == BelId())
                break;
            if (lsm.second[n.second].size() < 4)
                break;

            CellInfo *fe =
                    create_cell_ptr(id_BEYOND_FE, ctx->idf("%s$%s$csc", net->name.c_str(ctx), lsm.first.c_str(ctx)));
            NetInfo *new_out = ctx->createNet(ctx->idf("%s$o", fe->name.c_str(ctx)));
            fe->params[id_lut_table] = Property(0xaaaa, 16);
            fe->params[id_lut_used] = Property(1, 1);
            fe->params[id_CSC] = Property(Property::State::S1);
            fe->params[id_type] = Property("CSC");
            fe->params[id_dff_used] = Property(1, 1);
            fe->connectPort(id_I1, net);
            fe->connectPort(id_DO, new_out);
            insert_new_csc++;

            for (auto &conn : lsm.second[n.second])
                conn.cell->disconnectPort(conn.port);
            for (auto &conn : lsm.second[n.second])
                conn.cell->connectPort(conn.port, new_out);

            ctx->bindBel(newbel, fe, PlaceStrength::STRENGTH_LOCKED);
        }
    }
    if (insert_new_csc)
        log_info("    %6d FEs inserted as CSC\n", insert_new_csc);
    if (change_to_csc)
        log_info("    %6d FEs converted to CSC\n", change_to_csc);
}
BelId NgUltraPacker::get_available_gck(int lobe, NetInfo *si1, NetInfo *si2)
{
    auto &gcks = uarch->gck_per_lobe[lobe];
    for (int i = 0; i < 20; i++) {
        auto &g = gcks.at(i);
        if (g.used)
            continue;
        if (si1 && g.si1 != IdString() && g.si1 != si1->name)
            continue;
        if (si2 && g.si2 != IdString() && g.si2 != si2->name)
            continue;
        if (si1)
            g.si1 = si1->name;
        if (si2)
            g.si2 = si2->name;
        g.used = true;
        if (i % 2 == 0) {
            // next GCK share inputs in reverse order
            if (si2)
                gcks.at(i + 1).si1 = si2->name;
            if (si1)
                gcks.at(i + 1).si2 = si1->name;
        }
        return g.bel;
    }
    log_error("No GCK left to promote lowskew signal.\n");
    return BelId();
}

void NgUltraPacker::duplicate_gck()
{
    // Unbind all GCKs that are inserted
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_GCK))
            continue;
        ctx->unbindBel(ci.bel);
    }

    log_info("Duplicating existing GCKs...\n");
    for (auto &net : ctx->nets) {
        NetInfo *glb_net = net.second.get();
        if (!glb_net->driver.cell)
            continue;

        if (!uarch->is_tube_clock_source(glb_net->driver))
            continue;

        log_info("    Lowskew signal '%s'\n", glb_net->name.c_str(ctx));
        dict<int, std::vector<PortRef>> connections;
        for (const auto &usr : glb_net->users) {
            if (uarch->is_fabric_lowskew_sink(usr) || uarch->is_ring_over_tile_clock_sink(usr)) {
                if (usr.cell->bel == BelId()) {
                    log_error("Cell '%s' not placed\n", usr.cell->name.c_str(ctx));
                }
                int lobe = uarch->tile_lobe(usr.cell->bel.tile);
                if (lobe > 0) {
                    connections[lobe].push_back(usr);
                    usr.cell->disconnectPort(usr.port);
                }
            }
        }
        int cnt = 0;
        CellInfo *driver = glb_net->driver.cell;
        NetInfo *si1 = driver->getPort(id_SI1);
        NetInfo *si2 = driver->getPort(id_SI2);
        NetInfo *cmd = driver->getPort(id_CMD);
        for (auto &conn : connections) {
            BelId bel = get_available_gck(conn.first, si1, si2);
            CellInfo *gck_cell = nullptr;
            if (cnt == 0) {
                gck_cell = driver;
                log_info("        Assign GCK '%s' to lobe %d\n", gck_cell->name.c_str(ctx), conn.first);
            } else {
                gck_cell = create_cell_ptr(id_GCK, ctx->idf("%s$gck_%d", driver->name.c_str(ctx), conn.first));
                log_info("        Create GCK '%s' for lobe %d\n", gck_cell->name.c_str(ctx), conn.first);
                for (auto &params : driver->params)
                    gck_cell->params[params.first] = params.second;
                if (si1)
                    gck_cell->connectPort(id_SI1, si1);
                if (si2)
                    gck_cell->connectPort(id_SI2, si2);
                if (cmd)
                    gck_cell->connectPort(id_CMD, cmd);
            }
            gck_cell->disconnectPort(id_SO);
            NetInfo *new_clk = ctx->createNet(ctx->id(gck_cell->name.str(ctx)));
            gck_cell->connectPort(id_SO, new_clk);
            for (const auto &usr : conn.second) {
                CellInfo *cell = usr.cell;
                IdString port = usr.port;
                cell->connectPort(port, new_clk);
            }
            global_lowskew.emplace(new_clk->name);
            ctx->bindBel(bel, gck_cell, PlaceStrength::STRENGTH_LOCKED);
            cnt++;
        }
    }
}

void NgUltraPacker::insert_bypass_gck()
{
    log_info("Inserting bypass GCKs...\n");
    for (auto &net : ctx->nets) {
        NetInfo *glb_net = net.second.get();
        if (!glb_net->driver.cell)
            continue;

        if (!uarch->is_ring_clock_source(glb_net->driver))
            continue;

        log_info("    Lowskew signal '%s'\n", glb_net->name.c_str(ctx));
        dict<int, std::vector<PortRef>> connections;
        for (const auto &usr : glb_net->users) {
            if (uarch->is_fabric_lowskew_sink(usr) || uarch->is_ring_over_tile_clock_sink(usr)) {
                if (usr.cell->bel == BelId()) {
                    log_error("Cell '%s' not placed\n", usr.cell->name.c_str(ctx));
                }
                int lobe = uarch->tile_lobe(usr.cell->bel.tile);
                if (lobe > 0) {
                    connections[lobe].push_back(usr);
                    usr.cell->disconnectPort(usr.port);
                }
            }
        }
        for (auto &conn : connections) {
            BelId bel = get_available_gck(conn.first, glb_net, nullptr);

            log_info("        Create GCK for lobe %d\n", conn.first);
            CellInfo *gck_cell = create_cell_ptr(id_GCK, ctx->idf("%s$gck_%d", glb_net->name.c_str(ctx), conn.first));
            gck_cell->params[id_std_mode] = Property("BYPASS");
            gck_cell->connectPort(id_SI1, glb_net);
            NetInfo *new_clk = ctx->createNet(ctx->id(gck_cell->name.str(ctx)));
            gck_cell->connectPort(id_SO, new_clk);
            for (const auto &usr : conn.second) {
                CellInfo *cell = usr.cell;
                IdString port = usr.port;
                cell->connectPort(port, new_clk);
            }
            global_lowskew.emplace(new_clk->name);
            ctx->bindBel(bel, gck_cell, PlaceStrength::STRENGTH_LOCKED);
        }
    }
}
NEXTPNR_NAMESPACE_END
