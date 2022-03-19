/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2020  Pepijn de Vos <pepijn@symbioticeda.com>
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
#include <iostream>
#include "design_utils.h"
#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

std::unique_ptr<CellInfo> create_generic_cell(Context *ctx, IdString type, std::string name)
{
    static int auto_idx = 0;
    IdString name_id =
            name.empty() ? ctx->id("$nextpnr_" + type.str(ctx) + "_" + std::to_string(auto_idx++)) : ctx->id(name);
    auto new_cell = std::make_unique<CellInfo>(ctx, name_id, type);
    if (type == id_SLICE) {
        new_cell->params[id_INIT] = 0;
        new_cell->params[id_FF_USED] = 0;
        new_cell->params[id_FF_TYPE] = id_DFF.str(ctx);

        IdString names[4] = {id_A, id_B, id_C, id_D};
        for (int i = 0; i < 4; i++) {
            new_cell->addInput(names[i]);
        }

        new_cell->addInput(id_CLK);

        new_cell->addOutput(id_F);
        new_cell->addOutput(id_Q);
        new_cell->addInput(id_CE);
        new_cell->addInput(id_LSR);
    } else if (type == id_GW_MUX2_LUT5 || type == id_GW_MUX2_LUT6 || type == id_GW_MUX2_LUT7 ||
               type == id_GW_MUX2_LUT7 || type == id_GW_MUX2_LUT8) {
        new_cell->addInput(id_I0);
        new_cell->addInput(id_I1);
        new_cell->addInput(id_SEL);
        new_cell->addOutput(id_OF);
    } else if (type == id_IOB || type == id_IOBS) {
        new_cell->params[id_INPUT_USED] = 0;
        new_cell->params[id_OUTPUT_USED] = 0;
        new_cell->params[id_ENABLE_USED] = 0;

        new_cell->addInout(id_PAD);
        new_cell->addInput(id_I);
        new_cell->addInput(id_OEN);
        new_cell->addOutput(id_O);
    } else if (type == id_GSR) {
        new_cell->addInput(id_GSRI);
    } else if (type == id_GND) {
        new_cell->addOutput(id_G);
    } else if (type == id_VCC) {
        new_cell->addOutput(id_V);
    } else {
        log_error("unable to create generic cell of type %s\n", type.c_str(ctx));
    }
    return new_cell;
}

void lut_to_lc(const Context *ctx, CellInfo *lut, CellInfo *lc, bool no_dff)
{
    lc->params[id_INIT] = lut->params[id_INIT];
    lc->cluster = lut->cluster;
    lc->constr_x = lut->constr_x;
    lc->constr_y = lut->constr_y;
    lc->constr_z = lut->constr_z;

    // add itself to the cluster root children list
    if (lc->cluster != ClusterId()) {
        CellInfo *cluster_root = ctx->cells.at(lc->cluster).get();
        lc->constr_x += cluster_root->constr_x;
        lc->constr_y += cluster_root->constr_y;
        lc->constr_z += cluster_root->constr_z;
        if (cluster_root->cluster != cluster_root->name) {
            lc->cluster = cluster_root->cluster;
            cluster_root = ctx->cells.at(cluster_root->cluster).get();
        }
        cluster_root->constr_children.push_back(lc);
    }

    IdString sim_names[4] = {id_I0, id_I1, id_I2, id_I3};
    IdString wire_names[4] = {id_A, id_B, id_C, id_D};
    for (int i = 0; i < 4; i++) {
        lut->movePortTo(sim_names[i], lc, wire_names[i]);
    }

    if (no_dff) {
        lc->params[id_FF_USED] = 0;
        lut->movePortTo(id_F, lc, id_F);
    }
}

void dff_to_lc(const Context *ctx, CellInfo *dff, CellInfo *lc, bool pass_thru_lut)
{
    lc->params[id_FF_USED] = 1;
    lc->params[id_FF_TYPE] = dff->type.str(ctx);
    dff->movePortTo(id_CLK, lc, id_CLK);
    dff->movePortTo(id_CE, lc, id_CE);
    dff->movePortTo(id_SET, lc, id_LSR);
    dff->movePortTo(id_RESET, lc, id_LSR);
    dff->movePortTo(id_CLEAR, lc, id_LSR);
    dff->movePortTo(id_PRESET, lc, id_LSR);
    if (pass_thru_lut) {
        // Fill LUT with alternating 10
        const int init_size = 1 << 4;
        std::string init;
        init.reserve(init_size);
        for (int i = 0; i < init_size; i += 2)
            init.append("10");
        lc->params[id_INIT] = Property::from_string(init);

        dff->movePortTo(id_D, lc, id_A);
    }

    dff->movePortTo(id_Q, lc, id_Q);
}

void gwio_to_iob(Context *ctx, CellInfo *nxio, CellInfo *iob, pool<IdString> &todelete_cells)
{
    if (nxio->type == id_IBUF) {
        if (iob->type == id_IOBS) {
            // VCC -> OEN
            iob->connectPort(id_OEN, ctx->nets[ctx->id("$PACKER_VCC_NET")].get());
        }
        iob->params[id_INPUT_USED] = 1;
        nxio->movePortTo(id_O, iob, id_O);
    } else if (nxio->type == id_OBUF) {
        if (iob->type == id_IOBS) {
            // VSS -> OEN
            iob->connectPort(id_OEN, ctx->nets[ctx->id("$PACKER_GND_NET")].get());
        }
        iob->params[id_OUTPUT_USED] = 1;
        nxio->movePortTo(id_I, iob, id_I);
    } else if (nxio->type == id_TBUF) {
        iob->params[id_ENABLE_USED] = 1;
        iob->params[id_OUTPUT_USED] = 1;
        nxio->movePortTo(id_I, iob, id_I);
        nxio->movePortTo(id_OEN, iob, id_OEN);
    } else if (nxio->type == id_IOBUF) {
        iob->params[id_ENABLE_USED] = 1;
        iob->params[id_INPUT_USED] = 1;
        iob->params[id_OUTPUT_USED] = 1;
        nxio->movePortTo(id_I, iob, id_I);
        nxio->movePortTo(id_O, iob, id_O);
        nxio->movePortTo(id_OEN, iob, id_OEN);
    } else {
        NPNR_ASSERT(false);
    }
}

NEXTPNR_NAMESPACE_END
