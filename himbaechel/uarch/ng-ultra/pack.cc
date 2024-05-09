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

void NgUltraPacker::remove_constants()
{
    log_info("Removing constants..\n");
    auto fnd_cell = ctx->cells.find(ctx->id("$PACKER_VCC_DRV"));
    if (fnd_cell != ctx->cells.end()) {
        auto fnd_net = ctx->nets.find(ctx->id("$PACKER_VCC"));
        if (fnd_net != ctx->nets.end() && fnd_net->second->users.entries()==0) {
            ctx->cells.erase(fnd_cell);
            ctx->nets.erase(fnd_net);
            log_info("    Removed unused VCC cell\n");
        }
    }
    fnd_cell = ctx->cells.find(ctx->id("$PACKER_GND_DRV"));
    if (fnd_cell != ctx->cells.end()) {
        auto fnd_net = ctx->nets.find(ctx->id("$PACKER_GND"));
        if (fnd_net != ctx->nets.end() && fnd_net->second->users.entries()==0) {
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
        dff->movePortTo(id_I, fe, id_I1);
        fe->params[id_lut_table] = Property(0xaaaa, 16);
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

        char last = loc.back();
        IdString new_type = (last == 'N' or last == 'P') ? id_IOTP : id_IOP;

        std::string subtype = "IOP";
        if (ci.type==id_NX_IOB_O) subtype = "OP";
        if (ci.type==id_NX_IOB_I) subtype = "IP";

        ci.setParam(ctx->id("type"), Property(subtype));

        ci.type = new_type;

        ctx->bindBel(bel, &ci, PlaceStrength::STRENGTH_LOCKED);
        to_update.push_back(&ci);
    }
    int dfr_as_bfr = 0, ddrf_as_bfr = 0;
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
                if (cell->type==id_IOTP) ddrf_as_bfr++; else dfr_as_bfr++;
                iod = create_cell_ptr((cell->type==id_IOTP) ? id_DDFR : id_DFR, ctx->id(cell->name.str(ctx) + "$iod_cd"));
                NetInfo *new_out = ctx->createNet(ctx->id(iod->name.str(ctx) + "$O"));
                iod->setParam(ctx->id("iobname"),str_or_default(cell->params, ctx->id("iobname"), ""));
                iod->setParam(ctx->id("type"), Property("BFR"));
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
                if (cell->type==id_IOTP) ddrf_as_bfr++; else dfr_as_bfr++;
                iod = create_cell_ptr((cell->type==id_IOTP) ? id_DDFR : id_DFR, ctx->id(cell->name.str(ctx) + "$iod_od"));
                NetInfo *new_out = ctx->createNet(ctx->id(iod->name.str(ctx) + "$O"));
                iod->setParam(ctx->id("iobname"),str_or_default(cell->params, ctx->id("iobname"), ""));
                iod->setParam(ctx->id("type"), Property("BFR"));
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
            if (!iod) {
                if (cell->type==id_IOTP) ddrf_as_bfr++; else dfr_as_bfr++;
                iod = create_cell_ptr((cell->type==id_IOTP) ? id_DDFR : id_DFR, ctx->id(cell->name.str(ctx) + "$iod_id"));
                NetInfo *new_in = ctx->createNet(ctx->id(iod->name.str(ctx) + "$I"));
                iod->setParam(ctx->id("iobname"),str_or_default(cell->params, ctx->id("iobname"), ""));
                iod->setParam(ctx->id("type"), Property("BFR"));
                cell->disconnectPort(id_O);
                iod->connectPort(id_O, o_net);
                iod->setParam(ctx->id("mode"), Property(2, 2));
                iod->setParam(ctx->id("data_inv"), Property(0, 1));
                iod->connectPort(id_I, new_in);
                cell->connectPort(id_O,new_in);
            } else log_error("TODO handle DFR");
            Loc cd_loc = cell->getLocation();
            cd_loc.z += 1;
            BelId bel = ctx->getBelByLocation(cd_loc);
            ctx->bindBel(bel, iod, PlaceStrength::STRENGTH_LOCKED);
        }
    }
    if (dfr_as_bfr)
        log_info("    %6d DFRs used as BFR\n", dfr_as_bfr);
    if (ddrf_as_bfr)
        log_info("    %6d DDFRs used as BFR\n", ddrf_as_bfr);
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
                         port.second.net->driver.cell->type == id_IOTP) {
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
            CellInfo *lut = net_driven_by(ctx, net, is_lut, id_O);
            if (lut && net->users.entries()==1) {
                if (!lut->params.count(id_lut_table))
                    log_error("Cell '%s' missing lut_table\n", lut->name.c_str(ctx));
                lut_to_fe(lut, fe, false, lut->params[id_lut_table]);
                packed_cells.insert(lut->name);
            } else {
                fe->params[id_lut_table] = Property(0xaaaa, 16);
                fe->params[id_lut_used] = Property(1,1);
                cy->disconnectPort(in_port);
                NetInfo *new_out = ctx->createNet(ctx->id(fe->name.str(ctx) + "$o"));
                fe->connectPort(id_I1, net);
                fe->connectPort(id_LO, new_out);
                cy->connectPort(in_port, new_out);
            }
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
    }
    fe->cluster = cluster;
    fe->constr_z = placer;
    cy->constr_children.push_back(fe);
}

void NgUltraPacker::exchange_if_constant(CellInfo *cell, IdString input1, IdString input2)
{
    NetInfo *net1 = cell->getPort(input1);
    NetInfo *net2 = cell->getPort(input2);
    if (!net1 || !net2)
        return;
    if (net1->name.in(ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC"))) {
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
        NetInfo *ci_net = ci->getPort(id_CI);
        if (!ci_net || !ci_net->driver.cell || ci_net->driver.cell->type != id_NX_CY) {
            root_cys.push_back(ci);
        }
        exchange_if_constant(ci, id_A1, id_B1);
        exchange_if_constant(ci, id_A2, id_B2);
        exchange_if_constant(ci, id_A3, id_B3);
        exchange_if_constant(ci, id_A4, id_B4);
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
            } else {
                // Disconnect unused ports on last CY in chain
                // at least id_A1 and id_B1 will be connected
                // Reverse direction, must stop if used, then
                // rest is used as well
                if (!cy->getPort(id_S4) || cy->getPort(id_S4)->users.entries()==0) {
                    cy->disconnectPort(id_A4);
                    cy->disconnectPort(id_B4);
                } else break;
                if (!cy->getPort(id_S3) || cy->getPort(id_S3)->users.entries()==0) {
                    cy->disconnectPort(id_A3);
                    cy->disconnectPort(id_B3);
                } else break;
                if (!cy->getPort(id_S2) || cy->getPort(id_S2)->users.entries()==0) {
                    cy->disconnectPort(id_A2);
                    cy->disconnectPort(id_B2);
                } else break;
                break;
            }
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
            pack_cy_input_and_output(cy, root->name, id_B2, id_S2, PLACE_CY_FE2, lut_only, lut_and_ff, dff_only);
            pack_cy_input_and_output(cy, root->name, id_B3, id_S3, PLACE_CY_FE3, lut_only, lut_and_ff, dff_only);
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

// There are 20 dedicated clock inputs capable of being routed using global network
// to be able to best route them, IOM needs to be used to propagate these clock signals
void NgUltraPacker::promote_globals()
{
    std::vector<std::pair<int, IdString>> glb_fanout;
    int available_globals = 20;
    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        // Skip undriven nets; and nets that are already global
        if (ni->driver.cell == nullptr)
            continue;
        if (ni->name.in(ctx->id("$PACKER_GND_NET"), ctx->id("$PACKER_VCC_NET")))
            continue;
        if (ni->driver.cell->type == id_IOM) {
            continue;
        }
        if (ni->driver.cell->type == id_GCK) {
            --available_globals;
            continue;
        }
        if (!ni->driver.cell->type.in(id_DDFR)) {
            continue;
        }
        Loc iotp_loc = ni->driver.cell->getLocation();
        iotp_loc.z -= 1;
        BelId bel = ctx->getBelByLocation(iotp_loc);
        if (uarch->global_capable_bels.count(bel)==0)
            continue;
        // Count the number of clock ports
        int glb_count = 0;
        for (const auto &usr : ni->users) {
            if (usr.cell->type == id_BEYOND_FE && usr.port == id_CK)
                glb_count++;
        }
        if (glb_count > 0)
            glb_fanout.emplace_back(glb_count, ni->name);
    }
    if (available_globals <= 0)
        return;
    // Sort clocks by max fanout
    std::sort(glb_fanout.begin(), glb_fanout.end(), std::greater<std::pair<int, IdString>>());
    log_info("Promoting globals...\n");
    int ddfr_removed = 0;
    // Promote the N highest fanout clocks
    for (size_t i = 0; i < std::min<size_t>(glb_fanout.size(), available_globals); i++) {
        NetInfo *net = ctx->nets.at(glb_fanout.at(i).second).get();
        log_info("    Promoting clock net '%s'\n", ctx->nameOf(net));
        Loc iotp_loc = net->driver.cell->getLocation();
        iotp_loc.z -= 1;
        BelId iotp_bel = ctx->getBelByLocation(iotp_loc);

        IdString iob = uarch->tile_name_id(iotp_bel.tile);
        BelId bel = uarch->iom_bels[iob];

        CellInfo *iom = nullptr;
        IdString port = uarch->global_capable_bels.at(iotp_bel);
        if (!ctx->checkBelAvail(bel)) {
            iom = ctx->getBoundBelCell(bel);
        } else {
            iom = create_cell_ptr(id_IOM, ctx->id(std::string(iob.c_str(ctx)) + "$iom"));
        }
        if (iom->getPort(port)) {
            log_error("Port '%s' of IOM cell '%s' is already used.\n", port.c_str(ctx), iom->name.c_str(ctx));
        }
        CellInfo *input_pad = ctx->getBoundBelCell(iotp_bel);
        NetInfo *iom_to_clk = ctx->createNet(ctx->id(std::string(net->name.c_str(ctx)) + "$iom"));
        for (const auto &usr : net->users) {
            if (usr.cell->type == id_BEYOND_FE && usr.port == id_CK) {
                usr.cell->disconnectPort(id_CK);
                usr.cell->connectPort(id_CK, iom_to_clk);
            }
        }       
        iom->connectPort(port, input_pad->getPort(id_O));
        iom->connectPort((port==id_P17RI) ?  id_CKO1 : id_CKO2, iom_to_clk);
        ctx->bindBel(bel, iom, PlaceStrength::STRENGTH_LOCKED);
        CellInfo *ddfr = net->driver.cell;
        if (ddfr->getPort(id_O)->users.empty() && str_or_default(ddfr->params, ctx->id("type"), "")=="BFR") {
            ddfr->disconnectPort(id_O);
            ddfr->disconnectPort(id_I);
            ddfr_removed++;
            ctx->cells.erase(ddfr->name);
        }
    }
    if (ddfr_removed)
        log_info("    Removed %d unused DDFRs\n", ddfr_removed);
}
void NgUltraImpl::pack()
{
    const ArchArgs &args = ctx->args;
    if (args.options.count("csv")) {
        parse_csv(args.options.at("csv"));
    }
    NgUltraPacker packer(ctx, this);
    packer.pack_constants();
    packer.update_lut_init();
    packer.update_dffs();
    packer.pack_iobs();
    packer.pack_ioms();
    packer.pack_cys();
    packer.pack_lut_dffs();
    packer.pack_dffs();
    packer.remove_constants();
    packer.promote_globals();
}

void NgUltraImpl::route_clocks()
{
    log_info("Routing global clocks...\n");
    for (auto &net : ctx->nets) {
        NetInfo *clk_net = net.second.get();
        if (!clk_net->driver.cell)
            continue;

        // check if we have a global clock net, skip otherwise
        bool is_global = false;
        if (clk_net->driver.cell->type.in(id_IOM) && clk_net->driver.port.in(id_CKO1, id_CKO2))
            is_global = true;
        if (!is_global)
            continue;

        log_info("    routing clock '%s'\n", clk_net->name.c_str(ctx));
        ctx->bindWire(ctx->getNetinfoSourceWire(clk_net), clk_net, STRENGTH_LOCKED);

        for (auto &usr : clk_net->users) {
            std::queue<WireId> visit;
            dict<WireId, PipId> backtrace;
            WireId dest = WireId();

            auto sink_wire = ctx->getNetinfoSinkWire(clk_net, usr, 0);
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
                if (ctx->getBoundWireNet(curr) == clk_net) {
                    dest = curr;
                    break;
                }
                for (auto uh : ctx->getPipsUphill(curr)) {
                    if (!ctx->checkPipAvail(uh))
                        continue;
                    WireId src = ctx->getPipSrcWire(uh);
                    if (backtrace.count(src))
                        continue;
                    if (!ctx->checkWireAvail(src) && ctx->getBoundWireNet(src) != clk_net)
                        continue;
                    backtrace[src] = uh;
                    visit.push(src);
                }
            }
            if (dest == WireId()) {
                log_info("            failed to find a route using dedicated resources.\n");
            }
            while (backtrace.count(dest)) {
                auto uh = backtrace[dest];
                dest = ctx->getPipDstWire(uh);
                if (ctx->getBoundWireNet(dest) == clk_net) {
                    NPNR_ASSERT(clk_net->wires.at(dest).pip == uh);
                    break;
                }
                if (ctx->debug)
                    log_info("            bind pip %s --> %s\n", ctx->nameOfPip(uh), ctx->nameOfWire(dest));
                ctx->bindPip(uh, clk_net, STRENGTH_LOCKED);
            }
        }
    }
}
NEXTPNR_NAMESPACE_END
