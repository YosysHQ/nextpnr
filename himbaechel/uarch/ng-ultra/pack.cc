/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2023  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2023  Miodrag Milanovic <micko@yosyshq.com>
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
inline bool is_dff(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_NX_DFF; }

// Return true if a cell is a FE
inline bool is_fe(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_BEYOND_FE; }

// Return true if a cell is a DFR
inline bool is_dfr(const BaseCtx *ctx, const CellInfo *cell) { return cell->type == id_NX_DFR; }

// Return true if a cell is a WFG/WFB
inline bool is_wfg(const BaseCtx *ctx, const CellInfo *cell) { return cell->type.in(id_WFB, id_WFG); }

// Process the contents of packed_cells
void NgUltraPacker::flush_cells()
{
    for (auto pcell : packed_cells) {
        for (auto &port : ctx->cells[pcell]->ports) {
            ctx->cells[pcell]->disconnectPort(port.first);
        }
        ctx->cells.erase(pcell);
    }
    for (auto &ncell : new_cells) {
        ctx->cells[ncell->name] = std::move(ncell);
    }
    packed_cells.clear();
    new_cells.clear();
}

void NgUltraPacker::pack_constants(void)
{
    log_info("Packing constants..\n");
    // Replace constants with LUTs
    const dict<IdString, Property> vcc_params = {{id_lut_table, Property(0xFFFF, 16)}, {id_lut_used, Property(1,1)}, {id_dff_used, Property(1,1)}};
    const dict<IdString, Property> gnd_params = {{id_lut_table, Property(0x0000, 16)}, {id_lut_used, Property(1,1)}, {id_dff_used, Property(1,1)}};

    h.replace_constants(CellTypePort(id_BEYOND_FE, id_LO), CellTypePort(id_BEYOND_FE, id_LO), vcc_params, gnd_params);
}

void NgUltraImpl::remove_constants()
{
    log_info("Removing constants..\n");
    auto fnd_cell = ctx->cells.find(ctx->id("$PACKER_VCC_DRV"));
    if (fnd_cell != ctx->cells.end()) {
        auto fnd_net = ctx->nets.find(ctx->id("$PACKER_VCC"));
        if (fnd_net != ctx->nets.end() && fnd_net->second->users.entries()==0) {
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
        if (fnd_net != ctx->nets.end() && fnd_net->second->users.entries()==0) {
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

void NgUltraPacker::update_dffs()
{
    log_info("Update DFFs...\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_DFF))
            continue;
        
        if (int_or_default(ci.params, ctx->id("dff_init"), 0)==0) {
            // Reset not used
            ci.disconnectPort(id_R);
        } else {
            // Reset used
            NetInfo *net = ci.getPort(id_R);
            if (net) {
                if (net->name == ctx->id("$PACKER_GND")) {
                    log_warning("Removing reset on '%s' since it is always 0.\n", ci.name.c_str(ctx));
                    ci.disconnectPort(id_R);
                } else if (net->name == ctx->id("$PACKER_VCC")) {
                    log_error("Invalid DFF configuration, reset on '%s' is always 1.\n", ci.name.c_str(ctx));
                }
            }
        }

        if (int_or_default(ci.params, ctx->id("dff_load"), 0)==0) {
            // Load not used
            ci.disconnectPort(id_L);
        } else {
            // Load used
            NetInfo *net = ci.getPort(id_L);
            if (net) {
                if (net->name == ctx->id("$PACKER_VCC")) {
                    log_warning("Removing load enable on '%s' since it is always 1.\n", ci.name.c_str(ctx));
                    ci.disconnectPort(id_L);
                } else if (net->name == ctx->id("$PACKER_GND")) {
                    log_warning("Converting to self loop, since load enable on '%s' is always 0.\n", ci.name.c_str(ctx));
                    ci.disconnectPort(id_L);
                    ci.disconnectPort(id_I);
                    NetInfo *out = ci.getPort(id_O);
                    ci.connectPort(id_I, out);
                }
            }
        }
    }
}

int make_init_with_const_input(int init, int input, bool value)
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
            log_warning("Connected GND to mandatory port '%s' of cell '%s'(%s).\n", input.c_str(ctx), cell->name.c_str(ctx), cell->type.c_str(ctx));
    }
}

void NgUltraPacker::lut_to_fe(CellInfo *lut, CellInfo *fe, bool no_dff, Property lut_table)
{
    fe->params[id_lut_table] = lut_table;
    fe->params[id_lut_used] = Property(1,1);
    lut->movePortTo(id_I1, fe, id_I1);
    lut->movePortTo(id_I2, fe, id_I2);
    lut->movePortTo(id_I3, fe, id_I3);
    lut->movePortTo(id_I4, fe, id_I4);
    lut->movePortTo(id_O, fe, id_LO);
    if (no_dff) {
        fe->timing_index = ctx->get_cell_timing_idx(ctx->id("BEYOND_FE_LUT"));
    }
}

void NgUltraPacker::dff_to_fe(CellInfo *dff, CellInfo *fe, bool pass_thru_lut)
{
    if (pass_thru_lut) {
        NetInfo *net = dff->getPort(id_I);
        if (net && net->name.in(ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC"))) {
            // special case if driver is constant
            fe->params[id_lut_table] = Property((net->name ==ctx->id("$PACKER_GND")) ? 0x0000 : 0xffff, 16);
            dff->disconnectPort(id_I);
        } else {
            // otherwise just passthru
            fe->params[id_lut_table] = Property(0xaaaa, 16);
            dff->movePortTo(id_I, fe, id_I1);
        }
        fe->params[id_lut_used] = Property(1,1);
    }
    else
        dff->movePortTo(id_I, fe, id_DI);
    fe->params[id_dff_used] = Property(1,1);
    fe->setParam(ctx->id("type"), Property("DFF"));
    dff->movePortTo(id_R, fe, id_R);
    dff->movePortTo(id_CK, fe, id_CK);
    dff->movePortTo(id_L, fe, id_L);
    dff->movePortTo(id_O, fe, id_DO);

    if (dff->params.count(ctx->id("dff_ctxt"))) fe->setParam(ctx->id("dff_ctxt"),dff->params[ctx->id("dff_ctxt")]);
    if (dff->params.count(ctx->id("dff_edge"))) fe->setParam(ctx->id("dff_edge"),dff->params[ctx->id("dff_edge")]);
    if (dff->params.count(ctx->id("dff_init"))) fe->setParam(ctx->id("dff_init"),dff->params[ctx->id("dff_init")]);
    if (dff->params.count(ctx->id("dff_load"))) fe->setParam(ctx->id("dff_load"),dff->params[ctx->id("dff_load")]);
    if (dff->params.count(ctx->id("dff_sync"))) fe->setParam(ctx->id("dff_sync"),dff->params[ctx->id("dff_sync")]);
    if (dff->params.count(ctx->id("dff_type"))) fe->setParam(ctx->id("dff_type"),dff->params[ctx->id("dff_type")]);

    if (pass_thru_lut) {
        NetInfo *new_out = ctx->createNet(ctx->id(dff->name.str(ctx) + "$LO"));
        fe->connectPort(id_LO, new_out);
        fe->connectPort(id_DI, new_out);
    }
}

void NgUltraPacker::bind_attr_loc(CellInfo *cell, dict<IdString, Property> *attrs)
{
    if (attrs->count(id_LOC)) {
        std::string name = attrs->at(id_LOC).as_string();
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
    int xlut_used = 0, lut_only = 0;//, lut_and_ff = 0;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_LUT))
            continue;
        if (!ci.params.count(id_lut_table))
            log_error("Cell '%s' missing lut_table\n", ci.name.c_str(ctx));

        if (ci.cluster!=ClusterId())
            continue;
        CellInfo *lut[4];
        if (!ci.getPort(id_I1))
            continue;
        if (!ci.getPort(id_I2))
            continue;
        if (!ci.getPort(id_I3))
            continue;
        if (!ci.getPort(id_I4))
            continue;
        lut[0] = net_driven_by(ctx, ci.getPort(id_I1), is_lut, id_O);
        if (!lut[0] || ci.getPort(id_I1)->users.entries()!=1)
            continue;
        lut[1] = net_driven_by(ctx, ci.getPort(id_I2), is_lut, id_O);
        if (!lut[1] || ci.getPort(id_I2)->users.entries()!=1)
            continue;
        lut[2] = net_driven_by(ctx, ci.getPort(id_I3), is_lut, id_O);
        if (!lut[2] || ci.getPort(id_I3)->users.entries()!=1)
            continue;
        lut[3] = net_driven_by(ctx, ci.getPort(id_I4), is_lut, id_O);
        if (!lut[3] || ci.getPort(id_I4)->users.entries()!=1)
            continue;
        
        ci.type = id_XLUT;
        bind_attr_loc(&ci, &ci.attrs);
        ci.cluster = ci.name;
        xlut_used++;
        for (int i=0;i<4;i++) {
            ci.constr_children.push_back(lut[i]);
            lut[i]->cluster = ci.cluster;
            lut[i]->type = id_BEYOND_FE;
            lut[i]->constr_z = PLACE_XLUT_FE1 + i;
            lut[i]->renamePort(id_O, id_LO);
            lut[i]->params[id_lut_used] = Property(1,1);
            lut[i]->timing_index = ctx->get_cell_timing_idx(ctx->id("BEYOND_FE_LUT"));
            lut_only++;
        }
    }
    if (xlut_used)
        log_info("    %6d XLUTs used\n", xlut_used);
    if (lut_only)
        log_info("    %6d FEs used as LUT only\n", lut_only);
    //if (lut_and_ff)
    //    log_info("    %6d FEs used as LUT and DFF\n", lut_and_ff);
    flush_cells();
}

void NgUltraPacker::pack_lut_dffs(void)
{
    log_info("Pack LUT-DFFs...\n");

    int lut_only = 0, lut_and_ff = 0;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_LUT))
            continue;
        if (!ci.params.count(id_lut_table))
            log_error("Cell '%s' missing lut_table\n", ci.name.c_str(ctx));

        std::unique_ptr<CellInfo> packed = create_cell(id_BEYOND_FE, ctx->id(ci.name.str(ctx) + "$fe"));
        packed_cells.insert(ci.name);
        bind_attr_loc(packed.get(), &ci.attrs);

        bool packed_dff = false;
        NetInfo *o = ci.getPort(id_O);
        if (o) {
            CellInfo *dff = net_only_drives(ctx, o, is_dff, id_I, true);
            if (dff) {
                if (ctx->verbose)
                    log_info("found attached dff %s\n", dff->name.c_str(ctx));
                lut_to_fe(&ci, packed.get(), false, ci.params[id_lut_table]);
                dff_to_fe(dff, packed.get(), false);
                ++lut_and_ff;
                packed_cells.insert(dff->name);
                if (ctx->verbose)
                    log_info("packed cell %s into %s\n", dff->name.c_str(ctx), packed->name.c_str(ctx));
                packed_dff = true;
            }
        }
        if (!packed_dff) {
            lut_to_fe(&ci, packed.get(), true, ci.params[id_lut_table]);
            ++lut_only;
        }
        new_cells.push_back(std::move(packed));
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
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_NX_DFF))
            continue;
        std::unique_ptr<CellInfo> packed = create_cell(id_BEYOND_FE, ctx->id(ci.name.str(ctx) + "$fe"));
        packed_cells.insert(ci.name);
        dff_to_fe(&ci, packed.get(), true);
        bind_attr_loc(packed.get(), &ci.attrs);
        ++dff_only;
        new_cells.push_back(std::move(packed));
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
    std::vector<CellInfo*> to_update;
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
            if (ci.type==id_NX_IOB_O) new_type = id_OTP;
            if (ci.type==id_NX_IOB_I) new_type = id_ITP;
            log_error("JSON import currently does not support IOs with termination input.\n");
        } else {
            if (ci.type==id_NX_IOB_O) new_type = id_OP;
            if (ci.type==id_NX_IOB_I) new_type = id_IP;
        }
        ci.type = new_type;
        ctx->bindBel(bel, &ci, PlaceStrength::STRENGTH_LOCKED);
        if (!ctx->isValidBelForCellType(ctx->getBelBucketForCellType(new_type),bel))
            log_error("Invalid type of IO for specified location %s %s.\n", new_type.c_str(ctx), ctx->getBelType(bel).c_str(ctx));
        to_update.push_back(&ci);
    }
    int bfr_added = 0;
    for (auto cell : to_update) {
        NetInfo *c_net = cell->getPort(id_C);
        if (!c_net)
            log_error("C input of IO primitive %s must be connected.\n", cell->name.c_str(ctx));
        if (c_net->name == ctx->id("$PACKER_GND") && !cell->getPort(id_O))
            log_error("O port of IO primitive %s must be connected.\n", cell->name.c_str(ctx));
        if (c_net->name == ctx->id("$PACKER_VCC") && !cell->getPort(id_I))
            log_error("I port of IO primitive %s must be connected.\n", cell->name.c_str(ctx));
        if (!cell->getPort(id_I) && !cell->getPort(id_O))
            log_error("I or O port of IO primitive %s must be connected.\n", cell->name.c_str(ctx));

        {
            CellInfo *iod = net_driven_by(ctx, c_net, is_dfr, id_O);
            if (iod && c_net->users.entries()!=1)
                log_error("NX_DFR '%s can only directly drive IOB.\n", iod->name.c_str(ctx));
            if (!iod) {
                bfr_added++;
                iod = create_cell_ptr(id_BFR, ctx->id(cell->name.str(ctx) + "$iod_cd"));
                NetInfo *new_out = ctx->createNet(ctx->id(iod->name.str(ctx) + "$O"));
                iod->setParam(ctx->id("iobname"),str_or_default(cell->params, ctx->id("iobname"), ""));
                cell->disconnectPort(id_C);
                if (c_net->name == ctx->id("$PACKER_GND"))
                    iod->setParam(ctx->id("mode"), Property(0, 2));
                else if (c_net->name == ctx->id("$PACKER_VCC"))
                    iod->setParam(ctx->id("mode"), Property(1, 2));
                else {
                    iod->connectPort(id_I, c_net);
                    iod->setParam(ctx->id("mode"), Property(2, 2));
                    iod->setParam(ctx->id("data_inv"), Property(0, 1));
                }
                iod->connectPort(id_O, new_out);
                cell->connectPort(id_C,new_out);
            } else log_error("TODO handle DFR");
            Loc cd_loc = cell->getLocation();
            cd_loc.z += 3;
            BelId bel = ctx->getBelByLocation(cd_loc);
            ctx->bindBel(bel, iod, PlaceStrength::STRENGTH_LOCKED);
        }
        NetInfo *i_net = cell->getPort(id_I);
        if (i_net) {
            CellInfo *iod = net_driven_by(ctx, i_net, is_dfr, id_O);
            if (iod && i_net->users.entries()!=1)
                log_error("NX_DFR '%s can only directly drive IOB.\n", iod->name.c_str(ctx));
            if (!iod) {
                bfr_added++;
                iod = create_cell_ptr(id_BFR, ctx->id(cell->name.str(ctx) + "$iod_od"));
                NetInfo *new_out = ctx->createNet(ctx->id(iod->name.str(ctx) + "$O"));
                iod->setParam(ctx->id("iobname"),str_or_default(cell->params, ctx->id("iobname"), ""));
                cell->disconnectPort(id_I);
                if (i_net->name == ctx->id("$PACKER_GND"))
                    iod->setParam(ctx->id("mode"), Property(0, 2));
                else if (i_net->name == ctx->id("$PACKER_VCC"))
                    iod->setParam(ctx->id("mode"), Property(1, 2));
                else {
                    iod->connectPort(id_I, i_net);
                    iod->setParam(ctx->id("mode"), Property(2, 2));
                    iod->setParam(ctx->id("data_inv"), Property(0, 1));
                }
                iod->connectPort(id_O, new_out);
                cell->connectPort(id_I,new_out);
            } else log_error("TODO handle DFR");
            Loc cd_loc = cell->getLocation();
            cd_loc.z += 2;
            BelId bel = ctx->getBelByLocation(cd_loc);
            ctx->bindBel(bel, iod, PlaceStrength::STRENGTH_LOCKED);
        }

        NetInfo *o_net = cell->getPort(id_O);
        if (o_net) {
            CellInfo *iod = net_only_drives(ctx, o_net, is_dfr, id_I, true);
            if (!(o_net->users.entries()==1 && (*o_net->users.begin()).cell->type == id_NX_IOM_U)) {
                bool bfr_mode = false;
                if (!iod) {
                    bfr_added++;
                    iod = create_cell_ptr(id_BFR, ctx->id(cell->name.str(ctx) + "$iod_id"));
                    NetInfo *new_in = ctx->createNet(ctx->id(iod->name.str(ctx) + "$I"));
                    iod->setParam(ctx->id("iobname"),str_or_default(cell->params, ctx->id("iobname"), ""));
                    cell->disconnectPort(id_O);
                    iod->connectPort(id_O, o_net);
                    iod->setParam(ctx->id("mode"), Property(2, 2));
                    iod->setParam(ctx->id("data_inv"), Property(0, 1));
                    iod->connectPort(id_I, new_in);
                    cell->connectPort(id_O,new_in);
                    bfr_mode = true;
                } else log_error("TODO handle DFR");
                Loc cd_loc = cell->getLocation();
                cd_loc.z += 1;
                BelId bel = ctx->getBelByLocation(cd_loc);
                ctx->bindBel(bel, iod, PlaceStrength::STRENGTH_LOCKED);

                // Depending of DDFR mode we must use one of dedicated routes (ITCs)
                if (ctx->getBelType(bel)==id_DDFR) {
                    WireId dwire = ctx->getBelPinWire(bel, id_O);
                    for (PipId pip : ctx->getPipsDownhill(dwire)) {
                        const auto &pip_data = chip_pip_info(ctx->chip_info, pip);
                        const auto &extra_data = *reinterpret_cast<const NGUltraPipExtraDataPOD *>(pip_data.extra_data.get());
                        if (!extra_data.name) continue;
                        if (extra_data.type != PipExtra::PIP_EXTRA_MUX) continue;
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
    if (bfr_added)
        log_info("    %6d DFRs/DDFRs used as BFR\n", bfr_added);
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

void NgUltraPacker::pack_cy_input_and_output(CellInfo *cy, IdString cluster, IdString in_port, IdString out_port, int placer, int &lut_only, int &lut_and_ff, int &dff_only)
{
    CellInfo *fe = create_cell_ptr(id_BEYOND_FE, ctx->id(cy->name.str(ctx) + "$" + in_port.c_str(ctx)));
    NetInfo *net = cy->getPort(in_port);
    if (net) {
        if (net->name.in(ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC"))) {
            fe->params[id_lut_table] = Property((net->name ==ctx->id("$PACKER_GND")) ? 0x0000 : 0xffff, 16);
            fe->params[id_lut_used] = Property(1,1);
            cy->disconnectPort(in_port);
            NetInfo *new_out = ctx->createNet(ctx->id(fe->name.str(ctx) + "$o"));
            fe->connectPort(id_LO, new_out);
            cy->connectPort(in_port, new_out);
        } else {
            // TODO: This is too constrained, since makes rest of logic
            // separated from input
            /*
            CellInfo *lut = net_driven_by(ctx, net, is_lut, id_O);
            if (lut && net->users.entries()==1) {
                if (!lut->params.count(id_lut_table))
                    log_error("Cell '%s' missing lut_table\n", lut->name.c_str(ctx));
                lut_to_fe(lut, fe, false, lut->params[id_lut_table]);
                packed_cells.insert(lut->name);
            } else {
            */
                fe->params[id_lut_table] = Property(0xaaaa, 16);
                fe->params[id_lut_used] = Property(1,1);
                cy->disconnectPort(in_port);
                NetInfo *new_out = ctx->createNet(ctx->id(fe->name.str(ctx) + "$o"));
                fe->connectPort(id_I1, net);
                fe->connectPort(id_LO, new_out);
                cy->connectPort(in_port, new_out);
            //}
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
        fe->timing_index = ctx->get_cell_timing_idx(ctx->id("BEYOND_FE_LUT"));
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
        for (int i=1;i<=4;i++) {
            connect_gnd_if_unconnected(ci, ctx->idf("A%d",i), false);
            connect_gnd_if_unconnected(ci, ctx->idf("B%d",i), false);
            exchange_if_constant(ci, ctx->idf("A%d",i), ctx->idf("B%d",i));
        }
        NetInfo *co_net = ci->getPort(id_CO);
        if (!co_net) {
            disconnect_unused(ci,id_CO);
            // Disconnect unused ports on last CY in chain
            // at least id_A1 and id_B1 will be connected
            // Reverse direction, must stop if used, then
            // rest is used as well
            if (!ci->getPort(id_S4)) {
                disconnect_unused(ci,id_S4);
                disconnect_unused(ci,id_A4);
                disconnect_unused(ci,id_B4);
                if (!ci->getPort(id_S3)) {
                    disconnect_unused(ci,id_S3);
                    disconnect_unused(ci,id_A3);
                    disconnect_unused(ci,id_B3);
                    if (!ci->getPort(id_S2)) {
                        disconnect_unused(ci,id_S2);
                        disconnect_unused(ci,id_A2);
                        disconnect_unused(ci,id_B2);
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
                    log_warning("Cells %s CO output connected to:\n",group.back()->name.c_str(ctx));
                    for(auto user : co_net->users)
                        log_warning("\t%s of type %s\n",user.cell->name.c_str(ctx), user.cell->type.c_str(ctx));
                    log_error("NX_CY can only be chained with one other NX_CY cell\n");
                }
                group.push_back(cy);
            } else break;
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


void NgUltraPacker::pack_xrf_input_and_output(CellInfo *xrf, IdString cluster, IdString in_port, IdString out_port, ClusterPlacement placement, int &lut_only, int &lut_and_ff, int &dff_only)
{
    NetInfo *net = xrf->getPort(in_port);
    NetInfo *net_out = nullptr;
    if (out_port != IdString()) {
        net_out = xrf->getPort(out_port);
        if (net_out && net_out->users.entries()==0) {
            xrf->disconnectPort(out_port);
            net_out = nullptr;
        }
    }
    if (!net && !net_out) return;
    IdString name = in_port;
    if (name == IdString()) name = out_port;
    CellInfo *fe = create_cell_ptr(id_BEYOND_FE, ctx->id(xrf->name.str(ctx) + "$" + name.c_str(ctx)));
    
    if (net) {
        if (net->name.in(ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC"))) {
            fe->params[id_lut_table] = Property((net->name ==ctx->id("$PACKER_GND")) ? 0x0000 : 0xffff, 16);
            fe->params[id_lut_used] = Property(1,1);
            xrf->disconnectPort(in_port);
            NetInfo *new_out = ctx->createNet(ctx->id(fe->name.str(ctx) + "$o"));
            fe->connectPort(id_LO, new_out);
            xrf->connectPort(in_port, new_out);
        } else {
            CellInfo *lut = net_driven_by(ctx, net, is_lut, id_O);
            if (lut && net->users.entries()==1) {
                if (!lut->params.count(id_lut_table))
                    log_error("Cell '%s' missing lut_table\n", lut->name.c_str(ctx));
                lut_to_fe(lut, fe, false, lut->params[id_lut_table]);
                packed_cells.insert(lut->name);
            } else {
                fe->params[id_lut_table] = Property(0xaaaa, 16);
                fe->params[id_lut_used] = Property(1,1);
                xrf->disconnectPort(in_port);
                NetInfo *new_out = ctx->createNet(ctx->id(fe->name.str(ctx) + "$o"));
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
        if (net->users.entries()!=0 && !net->name.in(ctx->id("$PACKER_GND")))
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
        int mode = int_or_default(ci.params, ctx->id("mode"), 0);
        switch(mode) {
            case 0 : ci.type = id_RF; break;
            case 1 : ci.type = id_RFSP; break;
            case 2 : ci.type = id_XHRF; break;
            case 3 : ci.type = id_XWRF; break;
            case 4 : ci.type = id_XPRF; break;
            default:
                log_error("Unknown mode %d for cell '%s'.\n", mode, ci.name.c_str(ctx));
        }        
        ci.cluster = ci.name;
        bind_attr_loc(&ci, &ci.attrs);

        for (int i = 1; i <= 18; i++) {
            connect_gnd_if_unconnected(&ci, ctx->idf("I%d",i));
            pack_xrf_input_and_output(&ci, ci.name, ctx->idf("I%d",i), ctx->idf("O%d",i), ClusterPlacement(PLACE_XRF_I1 + i-1), lut_only, lut_and_ff, dff_only);
        }
        if (mode!=1) {
            for (int i = 1; i <= 5; i++) {
                connect_gnd_if_unconnected(&ci, ctx->idf("RA%d",i));
                pack_xrf_input_and_output(&ci, ci.name, ctx->idf("RA%d",i), IdString(), ClusterPlacement(PLACE_XRF_RA1 + i-1), lut_only, lut_and_ff, dff_only);
            }
        } else {
            // SPREG mode does not use RA inputs
            for (int i = 1; i <= 5; i++)
                disconnect_unused(&ci, ctx->idf("RA%d",i));
        }
        
        if (mode==2 || mode==4) {
            connect_gnd_if_unconnected(&ci, id_RA6);
            pack_xrf_input_and_output(&ci, ci.name, id_RA6, IdString(), PLACE_XRF_RA6, lut_only, lut_and_ff, dff_only);
        } else {
            disconnect_unused(&ci,id_RA6);
        }

        if (mode==4) {
            for (int i = 7; i <= 10; i++) {
                connect_gnd_if_unconnected(&ci, ctx->idf("RA%d",i));
                pack_xrf_input_and_output(&ci, ci.name, ctx->idf("RA%d",i), IdString(), ClusterPlacement(PLACE_XRF_RA1 + i-1), lut_only, lut_and_ff, dff_only);
            }
        } else {
            for (int i = 7; i <= 10; i++)
                disconnect_unused(&ci, ctx->idf("RA%d",i));
        }


        for (int i = 1; i <= 5; i++) {
            connect_gnd_if_unconnected(&ci, ctx->idf("WA%d",i));
            pack_xrf_input_and_output(&ci, ci.name, ctx->idf("WA%d",i), IdString(), ClusterPlacement(PLACE_XRF_WA1 + i-1), lut_only, lut_and_ff, dff_only);
        }
        
        if (mode==2) {
            connect_gnd_if_unconnected(&ci, id_WA6);
            pack_xrf_input_and_output(&ci, ci.name, id_WA6, IdString(), PLACE_XRF_WA6, lut_only, lut_and_ff, dff_only);
        } else {
            disconnect_unused(&ci,id_WA6);
        }

        connect_gnd_if_unconnected(&ci, id_WE);
        pack_xrf_input_and_output(&ci, ci.name, id_WE, IdString(), PLACE_XRF_WE, lut_only, lut_and_ff, dff_only);

        disconnect_if_gnd(&ci, id_WEA);
        pack_xrf_input_and_output(&ci, ci.name, id_WEA, IdString(), PLACE_XRF_WEA, lut_only, lut_and_ff, dff_only);

        if (mode == 3) {
            for (int i = 19; i <= 36; i++) {
                connect_gnd_if_unconnected(&ci, ctx->idf("I%d",i));
                pack_xrf_input_and_output(&ci, ci.name, ctx->idf("I%d",i), ctx->idf("O%d",i), ClusterPlacement(PLACE_XRF_I1 + i-1), lut_only, lut_and_ff, dff_only);
            }
        } else if (mode == 4) {
            for (int i = 19; i <= 36; i++) {
                disconnect_unused(&ci,ctx->idf("I%d",i));
                pack_xrf_input_and_output(&ci, ci.name, IdString(), ctx->idf("O%d",i), ClusterPlacement(PLACE_XRF_I1 + i-1), lut_only, lut_and_ff, dff_only);
            }
        } else {
            for (int i = 19; i <= 36; i++) {
                disconnect_unused(&ci,ctx->idf("I%d",i));
                disconnect_unused(&ci,ctx->idf("O%d",i));
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
        int mode = int_or_default(ci.params, ctx->id("mode"), 0);
        switch(mode) {
            case 0 : ci.type = id_DDE; break;
            case 1 : ci.type = id_TDE; break;
            case 2 : ci.type = id_CDC; break;
            case 3 : ci.type = id_BGC; break;
            case 4 : ci.type = id_GBC; break;
            case 5 : ci.type = id_XCDC; break;
            default:
                log_error("Unknown mode %d for cell '%s'.\n", mode, ci.name.c_str(ctx));
        }        
        ci.cluster = ci.name;

        // If unconnected, connect GND to inputs that are actually used as outputs
        for (int i = 1; i <= 6; i++) {
            if (ci.getPort(ctx->idf("AO%d",i))) {
                connect_gnd_if_unconnected(&ci, ctx->idf("AI%d",i));
                pack_xrf_input_and_output(&ci, ci.name, ctx->idf("AI%d",i), ctx->idf("AO%d",i), ClusterPlacement(PLACE_CDC_AI1 + i-1), lut_only, lut_and_ff, dff_only);
            } else
                disconnect_unused(&ci, ctx->idf("AI%d",i));
            if (ci.getPort(ctx->idf("BO%d",i))) {
                connect_gnd_if_unconnected(&ci, ctx->idf("BI%d",i));
                pack_xrf_input_and_output(&ci, ci.name, ctx->idf("BI%d",i), ctx->idf("BO%d",i), ClusterPlacement(PLACE_CDC_BI1 + i-1), lut_only, lut_and_ff, dff_only);
            } else 
                disconnect_unused(&ci, ctx->idf("BI%d",i));
            if (ci.type.in(id_XCDC)) {
                if (ci.getPort(ctx->idf("CO%d",i))) {
                    connect_gnd_if_unconnected(&ci, ctx->idf("CI%d",i));
                    pack_xrf_input_and_output(&ci, ci.name, ctx->idf("CI%d",i), ctx->idf("CO%d",i), ClusterPlacement(PLACE_CDC_CI1 + i-1), lut_only, lut_and_ff, dff_only);
                } else
                    disconnect_unused(&ci, ctx->idf("CI%d",i));
                if (ci.getPort(ctx->idf("DO%d",i))) {
                    connect_gnd_if_unconnected(&ci, ctx->idf("DI%d",i));
                    pack_xrf_input_and_output(&ci, ci.name, ctx->idf("DI%d",i), ctx->idf("DO%d",i), ClusterPlacement(PLACE_CDC_DI1 + i-1), lut_only, lut_and_ff, dff_only);
                } else 
                    disconnect_unused(&ci, ctx->idf("DI%d",i));
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
            pack_xrf_input_and_output(&ci, ci.name, id_ADRSTI, id_ADRSTO, PLACE_CDC_ADRSTI, lut_only, lut_and_ff, dff_only);
            connect_gnd_if_unconnected(&ci, id_BDRSTI);
            pack_xrf_input_and_output(&ci, ci.name, id_BDRSTI, id_BDRSTO, PLACE_CDC_BDRSTI, lut_only, lut_and_ff, dff_only);
        }
        if (ci.type.in(id_BGC, id_GBC, id_DDE)) {
            disconnect_unused(&ci, id_ASRSTI);
            disconnect_unused(&ci, id_ASRSTO);
            disconnect_unused(&ci, id_BSRSTI);
            disconnect_unused(&ci, id_BSRSTO);
        } else {
            connect_gnd_if_unconnected(&ci, id_ASRSTI);
            pack_xrf_input_and_output(&ci, ci.name, id_ASRSTI, id_ASRSTO, PLACE_CDC_ASRSTI, lut_only, lut_and_ff, dff_only);
            connect_gnd_if_unconnected(&ci, id_BSRSTI);
            pack_xrf_input_and_output(&ci, ci.name, id_BSRSTI, id_BSRSTO, PLACE_CDC_BSRSTI, lut_only, lut_and_ff, dff_only);
        }

        // Only XCDC is using these ports, remove from others if used
        if (!ci.type.in(id_XCDC)) {
            disconnect_unused(&ci, id_CDRSTI);
            disconnect_unused(&ci, id_CDRSTO);
            for (int i = 1; i <= 6; i++) {
                disconnect_unused(&ci,ctx->idf("CI%d",i));
                disconnect_unused(&ci,ctx->idf("CO%d",i));
            }
            disconnect_unused(&ci, id_CSRSTI);
            disconnect_unused(&ci, id_CSRSTO);

            disconnect_unused(&ci, id_DDRSTI);
            disconnect_unused(&ci, id_DDRSTO);
            for (int i = 1; i <= 6; i++) {
                disconnect_unused(&ci,ctx->idf("DI%d",i));
                disconnect_unused(&ci,ctx->idf("DO%d",i));
            }
            disconnect_unused(&ci, id_DSRSTI);
            disconnect_unused(&ci, id_DSRSTO);
        } else {
            connect_gnd_if_unconnected(&ci, id_CDRSTI);
            pack_xrf_input_and_output(&ci, ci.name, id_CDRSTI, id_CDRSTO, PLACE_CDC_CDRSTI, lut_only, lut_and_ff, dff_only);
            connect_gnd_if_unconnected(&ci, id_DDRSTI);
            pack_xrf_input_and_output(&ci, ci.name, id_DDRSTI, id_DDRSTO, PLACE_CDC_DDRSTI, lut_only, lut_and_ff, dff_only);
            connect_gnd_if_unconnected(&ci, id_CSRSTI);
            pack_xrf_input_and_output(&ci, ci.name, id_CSRSTI, id_CSRSTO, PLACE_CDC_CSRSTI, lut_only, lut_and_ff, dff_only);
            connect_gnd_if_unconnected(&ci, id_DSRSTI);
            pack_xrf_input_and_output(&ci, ci.name, id_DSRSTI, id_DSRSTO, PLACE_CDC_DSRSTI, lut_only, lut_and_ff, dff_only);
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
        int mode = int_or_default(ci.params, ctx->id("mode"), 0);
        switch(mode) {
            case 0 : ci.type = id_FIFO; break;
            case 1 : ci.type = id_XHFIFO; break;
            case 2 : ci.type = id_XWFIFO; break;
            default:
                log_error("Unknown mode %d for cell '%s'.\n", mode, ci.name.c_str(ctx));
        }        
        ci.cluster = ci.name;
        bool use_write_arst = bool_or_default(ci.params, ctx->id("use_write_arst"), false);
        bool use_read_arst = bool_or_default(ci.params, ctx->id("use_read_arst"), false);

        int rsti = (ci.type == id_FIFO) ? 2 : 4;
        for (int i=1;i<=rsti;i++) {
            IdString port = ctx->idf("WRSTI%d",i);
            ci.ports[port].name = port;
            ci.ports[port].type = PORT_IN;
            port = ctx->idf("RRSTI%d",i);
            ci.ports[port].name = port;
            ci.ports[port].type = PORT_IN;
        }

        if (use_write_arst) {
            IdString port = ctx->idf("WRSTI");
            connect_gnd_if_unconnected(&ci, port);
            NetInfo *wrsti_net = ci.getPort(port);
            ci.disconnectPort(port);
            for (int i=1;i<=rsti;i++)
                ci.connectPort(ctx->idf("WRSTI%d",i), wrsti_net);
            pack_xrf_input_and_output(&ci, ci.name, id_WRSTI1, id_WRSTO,   PLACE_FIFO_WRSTI1, lut_only, lut_and_ff, dff_only);
            pack_xrf_input_and_output(&ci, ci.name, id_WRSTI2, IdString(), PLACE_FIFO_WRSTI2, lut_only, lut_and_ff, dff_only);
            if (mode != 0) {
                pack_xrf_input_and_output(&ci, ci.name, id_WRSTI3, IdString(), PLACE_FIFO_WRSTI3, lut_only, lut_and_ff, dff_only);
                pack_xrf_input_and_output(&ci, ci.name, id_WRSTI4, IdString(), PLACE_FIFO_WRSTI4, lut_only, lut_and_ff, dff_only);
            }
        } else {
            disconnect_unused(&ci,ctx->id("WRSTI"));
        }
        if (use_read_arst) {
            IdString port = ctx->idf("RRSTI");
            connect_gnd_if_unconnected(&ci, port);
            NetInfo *rrsti_net = ci.getPort(port);
            ci.disconnectPort(port);
            for (int i=1;i<=rsti;i++)
                ci.connectPort(ctx->idf("RRSTI%d",i), rrsti_net);
            pack_xrf_input_and_output(&ci, ci.name, id_RRSTI1, id_RRSTO,   PLACE_FIFO_RRSTI1, lut_only, lut_and_ff, dff_only);
            pack_xrf_input_and_output(&ci, ci.name, id_RRSTI2, IdString(), PLACE_FIFO_RRSTI2, lut_only, lut_and_ff, dff_only);
            if (mode != 0) {
                pack_xrf_input_and_output(&ci, ci.name, id_RRSTI3, IdString(), PLACE_FIFO_RRSTI3, lut_only, lut_and_ff, dff_only);
                pack_xrf_input_and_output(&ci, ci.name, id_RRSTI4, IdString(), PLACE_FIFO_RRSTI4, lut_only, lut_and_ff, dff_only);
            }
        } else {
            disconnect_unused(&ci,ctx->id("RRSTI"));
        }

        for (int i = 1; i <= 18; i++) {
            connect_gnd_if_unconnected(&ci, ctx->idf("I%d",i));
            pack_xrf_input_and_output(&ci, ci.name, ctx->idf("I%d",i), ctx->idf("O%d",i), ClusterPlacement(PLACE_FIFO_I1 + i-1), lut_only, lut_and_ff, dff_only);
        }

        if (mode==0) {
            for (int i = 19; i <= 36; i++) {
                disconnect_unused(&ci,ctx->idf("I%d",i));
                disconnect_unused(&ci,ctx->idf("O%d",i));
            }
        } else {
            for (int i = 19; i <= 36; i++) {
                connect_gnd_if_unconnected(&ci, ctx->idf("I%d",i));
                pack_xrf_input_and_output(&ci, ci.name, ctx->idf("I%d",i), ctx->idf("O%d",i), ClusterPlacement(PLACE_FIFO_I1 + i-1), lut_only, lut_and_ff, dff_only);
            }
        }
        for (int i = 1; i <= 6; i++) {
            connect_gnd_if_unconnected(&ci, ctx->idf("RAI%d",i));
            pack_xrf_input_and_output(&ci, ci.name, ctx->idf("RAI%d",i), ctx->idf("RAO%d",i), ClusterPlacement(PLACE_FIFO_RAI1 + i-1), lut_only, lut_and_ff, dff_only);

            connect_gnd_if_unconnected(&ci, ctx->idf("WAI%d",i));
            pack_xrf_input_and_output(&ci, ci.name, ctx->idf("WAI%d",i), ctx->idf("WAO%d",i), ClusterPlacement(PLACE_FIFO_WAI1 + i-1), lut_only, lut_and_ff, dff_only);
        }

        if (mode==0) {
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

        if (mode==0) {
            // FIFO
            ci.renamePort(id_WEQ1, ctx->id("WEQ"));
            pack_xrf_input_and_output(&ci, ci.name, IdString(), ctx->id("WEQ"), PLACE_FIFO_WEQ1, lut_only, lut_and_ff, dff_only);
            disconnect_unused(&ci, id_WEQ2);

            ci.renamePort(id_REQ1, ctx->id("REQ"));
            pack_xrf_input_and_output(&ci, ci.name, IdString(), ctx->id("REQ"), PLACE_FIFO_REQ1, lut_only, lut_and_ff, dff_only);
            disconnect_unused(&ci, id_REQ2);
        } else {
            pack_xrf_input_and_output(&ci, ci.name, IdString(), id_WEQ1, PLACE_FIFO_WEQ1, lut_only, lut_and_ff, dff_only);
            pack_xrf_input_and_output(&ci, ci.name, IdString(), id_WEQ2, PLACE_FIFO_WEQ2, lut_only, lut_and_ff, dff_only);
            pack_xrf_input_and_output(&ci, ci.name, IdString(), id_WEQ1, PLACE_FIFO_REQ1, lut_only, lut_and_ff, dff_only);
            pack_xrf_input_and_output(&ci, ci.name, IdString(), id_WEQ2, PLACE_FIFO_REQ2, lut_only, lut_and_ff, dff_only);

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
        if (uarch->global_capable_bels.count(bel)==0)
            continue;
        for (const auto &usr : ni->users) {
            if (clock_sinks.count(usr.cell->type) && clock_sinks[usr.cell->type].count(usr.port)) {
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
        std::string iobname = str_or_default(input_pad->params, ctx->id("iobname"), "");
        if (!ctx->checkBelAvail(bel)) {
            iom = ctx->getBoundBelCell(bel);
            log_info("    Reusing IOM in bank '%s' for signal '%s'\n", iob.c_str(ctx), iobname.c_str());
        } else {
            iom = create_cell_ptr(id_IOM, ctx->id(std::string(iob.c_str(ctx)) + "$iom"));
            log_info("    Adding IOM in bank '%s' for signal '%s'\n", iob.c_str(ctx), iobname.c_str());
        }
        if (iom->getPort(port)) {
            log_error("Port '%s' of IOM cell '%s' is already used.\n", port.c_str(ctx), iom->name.c_str(ctx));
        }
        NetInfo *iom_to_clk = ctx->createNet(ctx->id(std::string(net->name.c_str(ctx)) + "$iom"));
        for (const auto &usr : net->users) {
            IdString port = usr.port;
            usr.cell->disconnectPort(port);
            usr.cell->connectPort(port, iom_to_clk);
        }       
        iom->connectPort(port, input_pad->getPort(id_O));
        iom->connectPort((port==id_P17RI) ?  id_CKO1 : id_CKO2, iom_to_clk);
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
            insert_wfb(&ci, id_VCO);
            insert_wfb(&ci, id_REFO);
            insert_wfb(&ci, id_LDFO);
            insert_wfb(&ci, id_CLK_DIV1);
            insert_wfb(&ci, id_CLK_DIV2);
            insert_wfb(&ci, id_CLK_DIV3);
            insert_wfb(&ci, id_CLK_DIVD1);
            insert_wfb(&ci, id_CLK_DIVD2);
            insert_wfb(&ci, id_CLK_DIVD3);
            insert_wfb(&ci, id_CLK_DIVD4);
            insert_wfb(&ci, id_CLK_DIVD5);
        }
    }
}

void NgUltraPacker::mandatory_param(CellInfo *cell, IdString param)
{
    if (!cell->params.count(param))
        log_error("Mandatory parameter '%s' of cell '%s'(%s) is missing.\n", param.c_str(ctx), cell->name.c_str(ctx), cell->type.c_str(ctx));
}

static int memory_width(int config, bool ecc)
{
    if (ecc) {
        if (config==4)
            return 18; 
        else
            log_error("ECC mode only support width of 18.\n");
    } else {
        switch(config) 
        {
            case 0: return 1;  // NOECC_48kx1
            case 1: return 2;  // NOECC_24kx2
            case 2: return 4;  // NOECC_12kx4
            case 3: return 8;  // NOECC_6kx8
            case 4: return 12; // NOECC_4kx12
            case 5: return 24; // NOECC_2kx24
            case 6: return 3;  // NOECC_16kx3
            case 7: return 6;  // NOECC_8kx6
        }
        log_error("Unknown memory configuration width config '%d'.\n", config);
    }
}

static int memory_addr_bits(int config,bool ecc)
{
    if (ecc) {
        if (config==4)
            return 11; 
        else
            log_error("ECC mode only support width of 18.\n");
    } else {
        switch(config) 
        {
            case 0: return 16; // NOECC_48kx1
            case 1: return 15; // NOECC_24kx2
            case 2: return 14; // NOECC_12kx4
            case 3: return 13; // NOECC_6kx8
            case 4: return 12; // NOECC_4kx12
            case 5: return 11; // NOECC_2kx24
            case 6: return 14; // NOECC_16kx3
            case 7: return 13; // NOECC_8kx6
        }
        log_error("Unknown memory configuration width config '%d'.\n", config);
    }
}

void NgUltraPacker::insert_wfb(CellInfo *cell, IdString port)
{
    NetInfo *net = cell->getPort(port);
    if (!net) return;

    CellInfo *wfg = net_only_drives(ctx, net, is_wfg, id_ZI, true);
    if (wfg) return;
    log_info("    Inserting WFB for cell '%s' port '%s'\n", cell->name.c_str(ctx), port.c_str(ctx));
    CellInfo *wfb = create_cell_ptr(id_WFB, ctx->id(std::string(cell->name.c_str(ctx)) + "$" + port.c_str(ctx)));
    cell->disconnectPort(port);
    wfb->connectPort(id_ZO, net);
    NetInfo *new_out = ctx->createNet(ctx->id(net->name.str(ctx) + "$" + port.c_str(ctx)));
    cell->connectPort(port, new_out);
    wfb->connectPort(id_ZI, new_out);
}

void NgUltraPacker::constrain_location(CellInfo *cell)
{
    std::string location = str_or_default(cell->params, ctx->id("location"), "");
    if (!location.empty()) {
        if (uarch->locations.count(location)) {
            BelId bel = uarch->locations[location];
            if (ctx->getBelType(bel)!= cell->type) {
                log_error("Location '%s' is wrong for bel type '%s'.\n", location.c_str(), cell->type.c_str(ctx));
            }
            if (ctx->checkBelAvail(bel)) {
                log_info("    Constraining %s '%s' to '%s'\n", cell->type.c_str(ctx), cell->name.c_str(ctx), location.c_str());
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
        int mode = int_or_default(ci.params, ctx->id("mode"), 1);
        if (mode == 0) { // WFB - bypass mode
            // must not be used, zero is tollerated
            disconnect_unused(&ci, id_SI);
            disconnect_unused(&ci, id_SO);
            disconnect_unused(&ci, id_R);
        } else {
            // Can be unused, if zero it is unused
            disconnect_if_gnd(&ci, id_SI);
            disconnect_if_gnd(&ci, id_R);
        }
        NetInfo *zi = ci.getPort(id_ZI);
        if (!zi || !zi->driver.cell)
            log_error("WFG port ZI of '%s' must be driven.\n", ci.name.c_str(ctx));
        NetInfo *zo = ci.getPort(id_ZO);
        if (!zo || zo->users.entries()==0)
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
        std::string mode = str_or_default(ci.params, ctx->id("std_mode"), "BYPASS");
        if (mode == "BYPASS") {
            disconnect_unused(&ci, id_SI2);
            disconnect_unused(&ci, id_CMD);
        } else if (mode == "CSC") {
            disconnect_unused(&ci, id_SI1);
            disconnect_unused(&ci, id_SI2);
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
        //int ecc_mode = (bits[12] ? 1 : 0) | (bits[13] ? 2 : 0) | (bits[14] ? 4 : 0) | (bits[15] ? 8 : 0);
        bool ecc = bits[12];
        //int a_in_width = memory_width((bits[0] ? 1 : 0) | (bits[1] ? 2 : 0) | (bits[2] ? 4 : 0), ecc);
        //int b_in_width = memory_width((bits[3] ? 1 : 0) | (bits[4] ? 2 : 0) | (bits[5] ? 4 : 0), ecc);
        int a_out_width = memory_width((bits[6] ? 1 : 0) | (bits[7] ? 2 : 0) | (bits[8] ? 4 : 0), ecc);
        int b_out_width = memory_width((bits[9] ? 1 : 0) | (bits[10] ? 2 : 0) | (bits[11] ? 4 : 0), ecc);

        int a_addr = std::max(memory_addr_bits((bits[0] ? 1 : 0) | (bits[1] ? 2 : 0) | (bits[2] ? 4 : 0), ecc) ,
                     memory_addr_bits((bits[6] ? 1 : 0) | (bits[7] ? 2 : 0) | (bits[8] ? 4 : 0), ecc));
        int b_addr = std::max(memory_addr_bits((bits[3] ? 1 : 0) | (bits[4] ? 2 : 0) | (bits[5] ? 4 : 0), ecc) ,
                     memory_addr_bits((bits[9] ? 1 : 0) | (bits[10] ? 2 : 0) | (bits[11] ? 4 : 0), ecc));


        NetInfo *a_cs = ci.getPort(id_ACS);
        if (!a_cs || a_cs->name.in(ctx->id("$PACKER_GND"))) {
            // If there is no chip-select disconnect all
            disconnect_unused(&ci, id_ACK);
            for(int i=0;i<24;i++) {
                disconnect_unused(&ci, ctx->idf("AI%d",i+1));
                disconnect_unused(&ci, ctx->idf("AO%d",i+1));
            }
            for(int i=0;i<16;i++)
                disconnect_unused(&ci, ctx->idf("AA%d",i+1));
        } else {
            for(int i=a_out_width;i<24;i++)
                disconnect_unused(&ci, ctx->idf("AO%d",i+1));
            for(int i=a_addr;i<16;i++)
                disconnect_unused(&ci, ctx->idf("AA%d",i+1));
        }

        NetInfo *b_cs = ci.getPort(id_BCS);
        if (!b_cs || b_cs->name.in(ctx->id("$PACKER_GND"))) {
            // If there is no chip-select disconnect all
            disconnect_unused(&ci, id_BCK);
            for(int i=0;i<24;i++) {
                disconnect_unused(&ci, ctx->idf("BI%d",i+1));
                disconnect_unused(&ci, ctx->idf("BO%d",i+1));
            }
            for(int i=0;i<16;i++)
                disconnect_unused(&ci, ctx->idf("BA%d",i+1));
        } else {
            for(int i=b_out_width;i<24;i++)
                disconnect_unused(&ci, ctx->idf("BO%d",i+1));
            for(int i=b_addr;i<16;i++)
                disconnect_unused(&ci, ctx->idf("BA%d",i+1));
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
        if (cell->getPort(port)->users.entries()!=1)
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
    dict<IdString, CellInfo*> dsp_output;
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
        for(int i=1;i<=24;i++)
            dsp_same_driver(ctx->idf("CAI%d",i), &ci, &dsp);
        for(int i=1;i<=18;i++)
            dsp_same_driver(ctx->idf("CBI%d",i), &ci, &dsp);
        for(int i=1;i<=56;i++)
            dsp_same_driver(ctx->idf("CZI%d",i), &ci, &dsp);
        dsp_same_driver(id_CCI, &ci, &dsp);
        if (!dsp)
            root_dsps.push_back(&ci);

        // CAO1-24, CBO1-18, CZO1..56 and CCO must go to same DSP
        dsp = nullptr;
        for(int i=1;i<=24;i++)
            dsp_same_sink(ctx->idf("CAO%d",i), &ci, &dsp);
        for(int i=1;i<=18;i++)
            dsp_same_sink(ctx->idf("CBO%d",i), &ci, &dsp);
        for(int i=1;i<=56;i++)
            dsp_same_sink(ctx->idf("CZO%d",i), &ci, &dsp);
        dsp_same_sink(id_CCO, &ci, &dsp);
        if (dsp)
            dsp_output.emplace(ci.name, dsp);
    }
    for (auto root : root_dsps) {
        CellInfo *dsp = root;
        if (dsp_output.count(dsp->name)==0) continue;
        root->cluster = root->name;
        while (true) {
            if (dsp_output.count(dsp->name)==0) break;
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
                if (net && net->users.entries()==0) {
                    ci.disconnectPort(p.first);
                }
            }
        }
    }
}

void NgUltraPacker::setup()
{
    // Note: These are per Cell type not Bel type
    clock_sinks[id_BEYOND_FE].insert(id_CK);
    //clock_sinks[id_DFF].insert(id_CK); // This is part of BEYOND_FE
    clock_sinks[id_RF].insert(id_WCK);
    clock_sinks[id_RFSP].insert(id_WCK);
    clock_sinks[id_XHRF].insert(id_WCK1);
    clock_sinks[id_XHRF].insert(id_WCK2);
    clock_sinks[id_XWRF].insert(id_WCK1);
    clock_sinks[id_XWRF].insert(id_WCK2);
    clock_sinks[id_XPRF].insert(id_WCK1);
    clock_sinks[id_XPRF].insert(id_WCK2);
    clock_sinks[id_RAM].insert(id_ACK);
    clock_sinks[id_RAM].insert(id_BCK);

    clock_sinks[id_CDC].insert(id_CK1);
    clock_sinks[id_CDC].insert(id_CK2);
    clock_sinks[id_DDE].insert(id_CK1);
    clock_sinks[id_DDE].insert(id_CK2);
    clock_sinks[id_TDE].insert(id_CK1);
    clock_sinks[id_TDE].insert(id_CK2);
    clock_sinks[id_XCDC].insert(id_CK1);
    clock_sinks[id_XCDC].insert(id_CK2);

    clock_sinks[id_FIFO].insert(id_RCK);
    clock_sinks[id_FIFO].insert(id_WCK);
    clock_sinks[id_XHFIFO].insert(id_RCK1);
    clock_sinks[id_XHFIFO].insert(id_RCK2);
    clock_sinks[id_XHFIFO].insert(id_WCK1);
    clock_sinks[id_XHFIFO].insert(id_WCK2);
    clock_sinks[id_XWFIFO].insert(id_RCK1);
    clock_sinks[id_XWFIFO].insert(id_RCK2);
    clock_sinks[id_XWFIFO].insert(id_WCK1);
    clock_sinks[id_XWFIFO].insert(id_WCK2);

    clock_sinks[id_DSP].insert(id_CK);

    clock_sinks[id_PLL].insert(id_CLK_CAL);
    clock_sinks[id_PLL].insert(id_FBK);
    clock_sinks[id_PLL].insert(id_REF);
    clock_sinks[id_GCK].insert(id_SI1);
    clock_sinks[id_GCK].insert(id_SI2);

    // clock_sinks[id_IOM].insert(id_ALCK1);
    // clock_sinks[id_IOM].insert(id_ALCK2);
    // clock_sinks[id_IOM].insert(id_ALCK3);
    // clock_sinks[id_IOM].insert(id_CCK);
    // clock_sinks[id_IOM].insert(id_FCK1);
    // clock_sinks[id_IOM].insert(id_FCK2);
    // clock_sinks[id_IOM].insert(id_FDCK);
    // clock_sinks[id_IOM].insert(id_LDSCK1);
    // clock_sinks[id_IOM].insert(id_LDSCK2);
    // clock_sinks[id_IOM].insert(id_LDSCK3);
    // clock_sinks[id_IOM].insert(id_SWRX1CK);
    // clock_sinks[id_IOM].insert(id_SWRX2CK);

    // clock_sinks[id_DFR].insert(id_CK);
    // clock_sinks[id_DDFR].insert(id_CK);
    // clock_sinks[id_DDFR].insert(id_CKF);   
    // clock_sinks[id_CRX].insert(id_LINK);
    // clock_sinks[id_CTX].insert(id_LINK);
    // clock_sinks[id_PMA].insert(id_hssl_clock_i1);
    // clock_sinks[id_PMA].insert(id_hssl_clock_i2);
    // clock_sinks[id_PMA].insert(id_hssl_clock_i3);
    // clock_sinks[id_PMA].insert(id_hssl_clock_i4);
    clock_sinks[id_WFB].insert(id_ZI);
    clock_sinks[id_WFG].insert(id_ZI);
}

void NgUltraImpl::pack()
{
    const ArchArgs &args = ctx->args;
    if (args.options.count("csv")) {
        parse_csv(args.options.at("csv"));
    }

    // Setup
    NgUltraPacker packer(ctx, this);
    packer.setup();
    packer.remove_not_used();
    packer.pack_constants();
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
    packer.pack_xluts();
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
            log_info("    Using '%s:%s' for cell '%s'.\n", uarch->tile_name(bel.tile).c_str(), ctx->getBelName(bel)[1].c_str(ctx), cell->name.c_str(ctx));
            ctx->bindBel(bel, cell, PlaceStrength::STRENGTH_LOCKED);
            return ckg;
        }
    }
    log_error("    No more available WFGs for cell '%s'.\n", cell->name.c_str(ctx));
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
            IdString bank= uarch->tile_name_id(ref->driver.cell->bel.tile);
            //bool found = false;
            for (auto &item : uarch->unused_pll) {
                BelId bel = item.first;
                std::pair<IdString,IdString>& ckgs = uarch->bank_to_ckg[bank];
                if (item.second == ckgs.first || item.second == ckgs.second) {
                    uarch->unused_pll.erase(bel);
                    log_info("    Using PLL in '%s' for cell '%s'.\n", uarch->tile_name(bel.tile).c_str(), ci.name.c_str(ctx));
                    ctx->bindBel(bel, &ci, PlaceStrength::STRENGTH_LOCKED);
                    //found = true;
                    break;
                }
            }
            //if (!found)
            //    log_error("    No more available PLLs for driving '%s'.\n", ci.name.c_str(ctx));
        }
    }
    // If PLL use any other pin, location is not relevant, so we pick available
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_PLL) || ci.bel != BelId())
            continue;
        //log_warning("    PLL '%s' is not driven by clock dedicated pin.\n", ci.name.c_str(ctx));
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
                } else break;
            } else break;
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
            std::pair<IdString,IdString>& ckgs = uarch->bank_to_ckg[bank];
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
}

void NgUltraImpl::postPlace()
{
    log_break();
    log_info("Limiting routing...\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CY))
            continue;
        // In case A input is actually used, but it is connectd to GND
        // it is considered that signal is comming from RI1 crossbar.
        // We need to prevent router to use that crossbar output for
        // any other signal.
        for (int i=1;i<=4;i++) {
            IdString port = ctx->idf("A%d",i);
            NetInfo *net = ci.getPort(port);
            if (!net)
                continue;
            if (net->name.in(ctx->id("$PACKER_GND"))) {
                WireId dwire = ctx->getBelPinWire(ci.bel, port);
                for (PipId pip : ctx->getPipsUphill(dwire)) {
                    WireId src = ctx->getPipSrcWire(pip);
                    const std::string src_name = ctx->getWireName(src)[1].str(ctx);
                    if (boost::starts_with(src_name,"RI1")) {
                        for (PipId pip2 : ctx->getPipsDownhill(src)) {
                            blocked_pips.emplace(pip2);
                        }
                    }
                }
                ci.disconnectPort(port); // Disconnect A
            }
        }
    }
    remove_constants();


    NgUltraPacker packer(ctx, this);
    log_break();
    log_info("Running post-placement ...\n");
    packer.duplicate_gck();
    packer.insert_bypass_gck();
    //log_info("Running post-placement legalisation...\n");
    log_break();
    ctx->assignArchInfo();
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


    dict<IdString,pool<IdString>> glb_sources;
    glb_sources[id_GCK].insert(id_SO);

    dict<IdString,pool<IdString>> clock_sinks;
    clock_sinks[id_BEYOND_FE].insert(id_CK);
    //clock_sinks[id_DFF].insert(id_CK); // This is part of BEYOND_FE
    clock_sinks[id_RF].insert(id_WCK);
    clock_sinks[id_RFSP].insert(id_WCK);
    clock_sinks[id_XHRF].insert(id_WCK1);
    clock_sinks[id_XHRF].insert(id_WCK2);
    clock_sinks[id_XWRF].insert(id_WCK1);
    clock_sinks[id_XWRF].insert(id_WCK2);
    clock_sinks[id_XPRF].insert(id_WCK1);
    clock_sinks[id_XPRF].insert(id_WCK2);
    clock_sinks[id_RAM].insert(id_ACK);
    clock_sinks[id_RAM].insert(id_BCK);
    clock_sinks[id_DSP].insert(id_CK);
    //glb_sources[id_BFR].insert(id_O);
    //glb_sources[id_GCK].insert(id_SO);
    clock_sinks[id_CDC].insert(id_CK1);
    clock_sinks[id_CDC].insert(id_CK2);
    clock_sinks[id_DDE].insert(id_CK1);
    clock_sinks[id_DDE].insert(id_CK2);
    clock_sinks[id_TDE].insert(id_CK1);
    clock_sinks[id_TDE].insert(id_CK2);
    clock_sinks[id_XCDC].insert(id_CK1);
    clock_sinks[id_XCDC].insert(id_CK2);

    clock_sinks[id_FIFO].insert(id_RCK);
    clock_sinks[id_FIFO].insert(id_WCK);
    clock_sinks[id_XHFIFO].insert(id_RCK1);
    clock_sinks[id_XHFIFO].insert(id_RCK2);
    clock_sinks[id_XHFIFO].insert(id_WCK1);
    clock_sinks[id_XHFIFO].insert(id_WCK2);
    clock_sinks[id_XWFIFO].insert(id_RCK1);
    clock_sinks[id_XWFIFO].insert(id_RCK2);
    clock_sinks[id_XWFIFO].insert(id_WCK1);
    clock_sinks[id_XWFIFO].insert(id_WCK2);

    log_info("Duplicating existing GCKs...\n");
    for (auto &net : ctx->nets) {
        NetInfo *glb_net = net.second.get();
        if (!glb_net->driver.cell)
            continue;

        // check if we have a global clock net, skip otherwise
        if (!(glb_sources.count(glb_net->driver.cell->type) && glb_sources[glb_net->driver.cell->type].count(glb_net->driver.port)))
            continue;

        log_info("    Global signal '%s'\n", glb_net->name.c_str(ctx));
        dict<int, std::vector<PortRef>> connections;
        for (const auto &usr : glb_net->users) {
            if (clock_sinks.count(usr.cell->type) && clock_sinks[usr.cell->type].count(usr.port)) {
                if (usr.cell->bel==BelId()) {
                    log_error("Cell '%s' not placed\n",usr.cell->name.c_str(ctx));
                }
                int lobe = uarch->tile_lobe(usr.cell->bel.tile);
                if (lobe > 0) {
                    connections[lobe].push_back(usr);
                    usr.cell->disconnectPort(usr.port);
                }
            }
        }
        
        if (connections.size()>1) 
            log_error("Unhandled\n");
        
        for (auto &conn : connections) {
            pool<BelId>& gck = uarch->gck_per_lobe[conn.first];
            if (gck.size()==0)
                log_error("No GCK left to promote global signal.\n");

            BelId bel = gck.pop();
            CellInfo *gck_cell = glb_net->driver.cell;
            log_info("        Assign GCK '%s' to lobe %d\n",gck_cell->name.c_str(ctx), conn.first);
            /*
            log_info("        Create GCK for lobe %d\n",conn.first);
            CellInfo *gck_cell = create_cell_ptr(id_GCK, ctx->id(glb_net->name.str(ctx) + "$gck_"+ std::to_string(conn.first)));
            gck_cell->params[id_std_mode] = Property("BYPASS");
            gck_cell->connectPort(id_SI1, glb_net);*/
            gck_cell->disconnectPort(id_SO);
            NetInfo *new_clk = ctx->createNet(ctx->id(gck_cell->name.str(ctx) + "$gck_"+ std::to_string(conn.first)));
            gck_cell->connectPort(id_SO, new_clk);
            for (const auto &usr : conn.second) {
                CellInfo *cell = usr.cell;
                IdString port = usr.port;
                cell->connectPort(port, new_clk);                
            }
            
            ctx->bindBel(bel, gck_cell, PlaceStrength::STRENGTH_LOCKED);
        }
    }

}

void NgUltraPacker::insert_bypass_gck()
{
    dict<IdString,pool<IdString>> glb_sources;
    glb_sources[id_IOM].insert(id_CKO1);
    glb_sources[id_IOM].insert(id_CKO2);
    glb_sources[id_WFB].insert(id_ZO);
    glb_sources[id_WFG].insert(id_ZO);

    dict<IdString,pool<IdString>> clock_sinks;
    clock_sinks[id_BEYOND_FE].insert(id_CK);
    //clock_sinks[id_DFF].insert(id_CK); // This is part of BEYOND_FE
    clock_sinks[id_RF].insert(id_WCK);
    clock_sinks[id_RFSP].insert(id_WCK);
    clock_sinks[id_XHRF].insert(id_WCK1);
    clock_sinks[id_XHRF].insert(id_WCK2);
    clock_sinks[id_XWRF].insert(id_WCK1);
    clock_sinks[id_XWRF].insert(id_WCK2);
    clock_sinks[id_XPRF].insert(id_WCK1);
    clock_sinks[id_XPRF].insert(id_WCK2);
    clock_sinks[id_RAM].insert(id_ACK);
    clock_sinks[id_RAM].insert(id_BCK);
    clock_sinks[id_DSP].insert(id_CK);
    //glb_sources[id_BFR].insert(id_O);
    //glb_sources[id_GCK].insert(id_SO);
    clock_sinks[id_CDC].insert(id_CK1);
    clock_sinks[id_CDC].insert(id_CK2);
    clock_sinks[id_DDE].insert(id_CK1);
    clock_sinks[id_DDE].insert(id_CK2);
    clock_sinks[id_TDE].insert(id_CK1);
    clock_sinks[id_TDE].insert(id_CK2);
    clock_sinks[id_XCDC].insert(id_CK1);
    clock_sinks[id_XCDC].insert(id_CK2);

    clock_sinks[id_FIFO].insert(id_RCK);
    clock_sinks[id_FIFO].insert(id_WCK);
    clock_sinks[id_XHFIFO].insert(id_RCK1);
    clock_sinks[id_XHFIFO].insert(id_RCK2);
    clock_sinks[id_XHFIFO].insert(id_WCK1);
    clock_sinks[id_XHFIFO].insert(id_WCK2);
    clock_sinks[id_XWFIFO].insert(id_RCK1);
    clock_sinks[id_XWFIFO].insert(id_RCK2);
    clock_sinks[id_XWFIFO].insert(id_WCK1);
    clock_sinks[id_XWFIFO].insert(id_WCK2);

    log_info("Inserting bypass GCKs...\n");
    for (auto &net : ctx->nets) {
        NetInfo *glb_net = net.second.get();
        if (!glb_net->driver.cell)
            continue;

        // check if we have a global clock net, skip otherwise
        if (!(glb_sources.count(glb_net->driver.cell->type) && glb_sources[glb_net->driver.cell->type].count(glb_net->driver.port)))
            continue;

        log_info("    Global signal '%s'\n", glb_net->name.c_str(ctx));
        dict<int, std::vector<PortRef>> connections;
        for (const auto &usr : glb_net->users) {
            if (clock_sinks.count(usr.cell->type) && clock_sinks[usr.cell->type].count(usr.port)) {
                if (usr.cell->bel==BelId()) {
                    log_error("Cell '%s' not placed\n",usr.cell->name.c_str(ctx));
                }
                int lobe = uarch->tile_lobe(usr.cell->bel.tile);
                if (lobe > 0) {
                    connections[lobe].push_back(usr);
                    usr.cell->disconnectPort(usr.port);
                }
            }
        }
        for (auto &conn : connections) {
            pool<BelId>& gck = uarch->gck_per_lobe[conn.first];
            if (gck.size()==0)
                log_error("No GCK left to promote global signal.\n");

            BelId bel = gck.pop();

            log_info("        Create GCK for lobe %d\n",conn.first);
            CellInfo *gck_cell = create_cell_ptr(id_GCK, ctx->id(glb_net->name.str(ctx) + "$gck_"+ std::to_string(conn.first)));
            gck_cell->params[id_std_mode] = Property("BYPASS");
            gck_cell->connectPort(id_SI1, glb_net);
            NetInfo *new_clk = ctx->createNet(ctx->id(gck_cell->name.str(ctx)));
            gck_cell->connectPort(id_SO, new_clk);
            for (const auto &usr : conn.second) {
                CellInfo *cell = usr.cell;
                IdString port = usr.port;
                cell->connectPort(port, new_clk);                
            }
            ctx->bindBel(bel, gck_cell, PlaceStrength::STRENGTH_LOCKED);
        }
    }
}
void NgUltraImpl::route_clocks()
{
    dict<IdString,pool<IdString>> glb_sources;
    glb_sources[id_IOM].insert(id_CKO1);
    glb_sources[id_IOM].insert(id_CKO2);
    glb_sources[id_WFB].insert(id_ZO);
    glb_sources[id_WFG].insert(id_ZO);
    glb_sources[id_GCK].insert(id_SO);

    log_info("Routing global nets...\n");
    for (auto &net : ctx->nets) {
        NetInfo *glb_net = net.second.get();
        if (!glb_net->driver.cell)
            continue;

        // check if we have a global clock net, skip otherwise
        if (!(glb_sources.count(glb_net->driver.cell->type) && glb_sources[glb_net->driver.cell->type].count(glb_net->driver.port)))
            continue;

        log_info("    routing net '%s'\n", glb_net->name.c_str(ctx));
        ctx->bindWire(ctx->getNetinfoSourceWire(glb_net), glb_net, STRENGTH_LOCKED);

        for (auto &usr : glb_net->users) {
            std::queue<WireId> visit;
            dict<WireId, PipId> backtrace;
            WireId dest = WireId();

            auto sink_wire = ctx->getNetinfoSinkWire(glb_net, usr, 0);
            if (ctx->debug) {
                auto sink_wire_name = "(uninitialized)";
                if (sink_wire != WireId())
                    sink_wire_name = ctx->nameOfWire(sink_wire);
                log_info("        routing arc to %s.%s (wire %s):\n", usr.cell->name.c_str(ctx), usr.port.c_str(ctx),
                         sink_wire_name);
            }
            visit.push(sink_wire);
            while (!visit.empty()) {
                WireId curr = visit.front();
                visit.pop();
                if (ctx->getBoundWireNet(curr) == glb_net) {
                    dest = curr;
                    break;
                }
                for (auto uh : ctx->getPipsUphill(curr)) {
                    if (!ctx->checkPipAvail(uh))
                        continue;
                    WireId src = ctx->getPipSrcWire(uh);
                    if (backtrace.count(src))
                        continue;
                    if (!ctx->checkWireAvail(src) && ctx->getBoundWireNet(src) != glb_net)
                        continue;
                    backtrace[src] = uh;
                    visit.push(src);
                }
            }
            if (dest == WireId()) {
                log_info("            failed to find a route using dedicated resources. %s -> %s\n",glb_net->driver.cell->name.c_str(ctx),usr.cell->name.c_str(ctx));
            }
            while (backtrace.count(dest)) {
                auto uh = backtrace[dest];
                dest = ctx->getPipDstWire(uh);
                if (ctx->getBoundWireNet(dest) == glb_net) {
                    NPNR_ASSERT(glb_net->wires.at(dest).pip == uh);
                    break;
                }
                if (ctx->debug)
                    log_info("            bind pip %s --> %s\n", ctx->nameOfPip(uh), ctx->nameOfWire(dest));
                ctx->bindPip(uh, glb_net, STRENGTH_LOCKED);
            }
        }
    }
}
NEXTPNR_NAMESPACE_END
