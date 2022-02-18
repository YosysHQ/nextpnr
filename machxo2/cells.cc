/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2021  William D. Jones <wjones@wdj-consulting.com>
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
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

std::unique_ptr<CellInfo> create_machxo2_cell(Context *ctx, IdString type, std::string name)
{
    static int auto_idx = 0;
    IdString name_id =
            name.empty() ? ctx->id("$nextpnr_" + type.str(ctx) + "_" + std::to_string(auto_idx++)) : ctx->id(name);
    auto new_cell = std::make_unique<CellInfo>(ctx, name_id, type);

    if (type == id_FACADE_SLICE) {
        new_cell->params[id_MODE] = std::string("LOGIC");
        new_cell->params[id_GSR] = std::string("ENABLED");
        new_cell->params[id_SRMODE] = std::string("LSR_OVER_CE");
        new_cell->params[id_CEMUX] = std::string("1");
        new_cell->params[id_CLKMUX] = std::string("0");
        new_cell->params[id_LSRMUX] = std::string("LSR");
        new_cell->params[id_LSRONMUX] = std::string("LSRMUX");
        new_cell->params[id_LUT0_INITVAL] = Property(0xFFFF, 16);
        new_cell->params[id_LUT1_INITVAL] = Property(0xFFFF, 16);
        new_cell->params[id_REGMODE] = std::string("FF");
        new_cell->params[id_REG0_SD] = std::string("1");
        new_cell->params[id_REG1_SD] = std::string("1");
        new_cell->params[id_REG0_REGSET] = std::string("SET");
        new_cell->params[id_REG1_REGSET] = std::string("SET");
        new_cell->params[id_CCU2_INJECT1_0] = std::string("YES");
        new_cell->params[id_CCU2_INJECT1_1] = std::string("YES");
        new_cell->params[id_WREMUX] = std::string("INV");

        new_cell->addInput(id_A0);
        new_cell->addInput(id_B0);
        new_cell->addInput(id_C0);
        new_cell->addInput(id_D0);

        new_cell->addInput(id_A1);
        new_cell->addInput(id_B1);
        new_cell->addInput(id_C1);
        new_cell->addInput(id_D1);

        new_cell->addInput(id_M0);
        new_cell->addInput(id_M1);

        new_cell->addInput(id_FCI);
        new_cell->addInput(id_FXA);
        new_cell->addInput(id_FXB);

        new_cell->addInput(id_CLK);
        new_cell->addInput(id_LSR);
        new_cell->addInput(id_CE);

        new_cell->addInput(id_DI0);
        new_cell->addInput(id_DI1);

        new_cell->addInput(id_WD0);
        new_cell->addInput(id_WD1);
        new_cell->addInput(id_WAD0);
        new_cell->addInput(id_WAD1);
        new_cell->addInput(id_WAD2);
        new_cell->addInput(id_WAD3);
        new_cell->addInput(id_WRE);
        new_cell->addInput(id_WCK);

        new_cell->addOutput(id_F0);
        new_cell->addOutput(id_Q0);
        new_cell->addOutput(id_F1);
        new_cell->addOutput(id_Q1);

        new_cell->addOutput(id_FCO);
        new_cell->addOutput(id_OFX0);
        new_cell->addOutput(id_OFX1);

        new_cell->addOutput(id_WDO0);
        new_cell->addOutput(id_WDO1);
        new_cell->addOutput(id_WDO2);
        new_cell->addOutput(id_WDO3);
        new_cell->addOutput(id_WADO0);
        new_cell->addOutput(id_WADO1);
        new_cell->addOutput(id_WADO2);
        new_cell->addOutput(id_WADO3);
    } else if (type == id_FACADE_IO) {
        new_cell->params[id_DIR] = std::string("INPUT");
        new_cell->attrs[id_IO_TYPE] = std::string("LVCMOS33");

        new_cell->addInout(id_PAD);
        new_cell->addInput(id_I);
        new_cell->addInput(id_EN);
        new_cell->addOutput(id_O);
    } else if (type == id_LUT4) {
        new_cell->params[id_INIT] = Property(0, 16);

        new_cell->addInput(id_A);
        new_cell->addInput(id_B);
        new_cell->addInput(id_C);
        new_cell->addInput(id_D);
        new_cell->addOutput(id_Z);
    } else {
        log_error("unable to create MachXO2 cell of type %s", type.c_str(ctx));
    }

    return new_cell;
}

void lut_to_lc(const Context *ctx, CellInfo *lut, CellInfo *lc, bool no_dff)
{
    lc->params[id_LUT0_INITVAL] = lut->params[id_INIT];

    for (std::string i : {"A", "B", "C", "D"}) {
        IdString lut_port = ctx->id(i);
        IdString lc_port = ctx->id(i + "0");
        lut->movePortTo(lut_port, lc, lc_port);
    }

    lut->movePortTo(id_Z, lc, id_F0);
}

void dff_to_lc(Context *ctx, CellInfo *dff, CellInfo *lc, LutType lut_type)
{
    // FIXME: This will have to change once we support FFs with reset value of 1.
    lc->params[id_REG0_REGSET] = std::string("RESET");

    dff->movePortTo(id_CLK, lc, id_CLK);
    dff->movePortTo(id_LSR, lc, id_LSR);
    dff->movePortTo(id_Q, lc, id_Q0);

    if (lut_type == LutType::PassThru) {
        // If a register's DI port is fed by a constant, options for placing are
        // limited. Use the LUT to get around this.
        // LUT output will go to F0, which will feed back to DI0 input.
        lc->params[id_LUT0_INITVAL] = Property(0xAAAA, 16);
        dff->movePortTo(id_DI, lc, id_A0);
        lc->connectPorts(id_F0, lc, id_DI0);
    } else if (lut_type == LutType::None) {
        // If there is no LUT, use the M0 input because DI0 requires
        // going through the LUTs.
        lc->params[id_REG0_SD] = std::string("0");
        dff->movePortTo(id_DI, lc, id_M0);
    } else {
        // Otherwise, there's a LUT being used in the slice and mapping DI to
        // DI0 input is fine.
        dff->movePortTo(id_DI, lc, id_DI0);
    }
}

void nxio_to_iob(Context *ctx, CellInfo *nxio, CellInfo *iob, pool<IdString> &todelete_cells) {}

NEXTPNR_NAMESPACE_END
