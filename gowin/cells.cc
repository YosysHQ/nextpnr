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

void add_port(const Context *ctx, CellInfo *cell, IdString id, PortType dir)
{
    NPNR_ASSERT(cell->ports.count(id) == 0);
    cell->ports[id] = PortInfo{id, nullptr, dir};
}

std::unique_ptr<CellInfo> create_generic_cell(Context *ctx, IdString type, std::string name)
{
    static int auto_idx = 0;
    std::unique_ptr<CellInfo> new_cell = std::unique_ptr<CellInfo>(new CellInfo());
    if (name.empty()) {
        new_cell->name = ctx->id("$nextpnr_" + type.str(ctx) + "_" + std::to_string(auto_idx++));
    } else {
        new_cell->name = ctx->id(name);
    }
    new_cell->type = type;
    if (type == id_SLICE) {
        new_cell->params[id_INIT] = 0;
        new_cell->params[id_FF_USED] = 0;
        new_cell->params[id_FF_TYPE] = id_DFF.str(ctx);

        IdString names[4] = {id_A, id_B, id_C, id_D};
        for (int i = 0; i < 4; i++) {
            add_port(ctx, new_cell.get(), names[i], PORT_IN);
        }

        add_port(ctx, new_cell.get(), id_CLK, PORT_IN);

        add_port(ctx, new_cell.get(), id_F, PORT_OUT);
        add_port(ctx, new_cell.get(), id_Q, PORT_OUT);
        add_port(ctx, new_cell.get(), id_CE, PORT_IN);
        add_port(ctx, new_cell.get(), id_LSR, PORT_IN);
    } else if (type == id_IOB) {
        new_cell->params[id_INPUT_USED] = 0;
        new_cell->params[id_OUTPUT_USED] = 0;
        new_cell->params[id_ENABLE_USED] = 0;

        add_port(ctx, new_cell.get(), id_PAD, PORT_INOUT);
        add_port(ctx, new_cell.get(), id_I, PORT_IN);
        add_port(ctx, new_cell.get(), id_EN, PORT_IN);
        add_port(ctx, new_cell.get(), id_O, PORT_OUT);
    } else {
        log_error("unable to create generic cell of type %s", type.c_str(ctx));
    }
    return new_cell;
}

void lut_to_lc(const Context *ctx, CellInfo *lut, CellInfo *lc, bool no_dff)
{
    lc->params[id_INIT] = lut->params[id_INIT];

    IdString sim_names[4] = {id_I0, id_I1, id_I2, id_I3};
    IdString wire_names[4] = {id_A, id_B, id_C, id_D};
    for (int i = 0; i < 4; i++) {
        replace_port(lut, sim_names[i], lc, wire_names[i]);
    }

    if (no_dff) {
        lc->params[id_FF_USED] = 0;
        replace_port(lut, id_F, lc, id_F);
    }
}

void dff_to_lc(const Context *ctx, CellInfo *dff, CellInfo *lc, bool pass_thru_lut)
{
    lc->params[id_FF_USED] = 1;
    lc->params[id_FF_TYPE] = dff->type.str(ctx);
    replace_port(dff, id_CLK, lc, id_CLK);
    replace_port(dff, id_CE, lc, id_CE);
    replace_port(dff, id_SET, lc, id_LSR);
    replace_port(dff, id_RESET, lc, id_LSR);
    replace_port(dff, id_CLEAR, lc, id_LSR);
    replace_port(dff, id_PRESET, lc, id_LSR);
    if (pass_thru_lut) {
        // Fill LUT with alternating 10
        const int init_size = 1 << 4;
        std::string init;
        init.reserve(init_size);
        for (int i = 0; i < init_size; i += 2)
            init.append("10");
        lc->params[id_INIT] = Property::from_string(init);

        replace_port(dff, id_D, lc, id_A);
    }

    replace_port(dff, id_Q, lc, id_Q);
}

void gwio_to_iob(Context *ctx, CellInfo *nxio, CellInfo *iob, pool<IdString> &todelete_cells)
{
    if (nxio->type == id_IBUF) {
        iob->params[id_INPUT_USED] = 1;
        replace_port(nxio, id_O, iob, id_O);
    } else if (nxio->type == id_OBUF) {
        iob->params[id_OUTPUT_USED] = 1;
        replace_port(nxio, id_I, iob, id_I);
    } else if (nxio->type == id_TBUF) {
        iob->params[id_ENABLE_USED] = 1;
        iob->params[id_OUTPUT_USED] = 1;
        replace_port(nxio, id_I, iob, id_I);
        replace_port(nxio, id_OEN, iob, id_OEN);
    } else if (nxio->type == id_IOBUF) {
        iob->params[id_ENABLE_USED] = 1;
        iob->params[id_INPUT_USED] = 1;
        iob->params[id_OUTPUT_USED] = 1;
        replace_port(nxio, id_I, iob, id_I);
        replace_port(nxio, id_O, iob, id_O);
        replace_port(nxio, id_OEN, iob, id_OEN);
    } else {
        NPNR_ASSERT(false);
    }
}

NEXTPNR_NAMESPACE_END
