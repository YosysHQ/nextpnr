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

void add_port(const Context *ctx, CellInfo *cell, std::string name, PortType dir)
{
    IdString id = ctx->id(name);
    NPNR_ASSERT(cell->ports.count(id) == 0);
    cell->ports[id] = PortInfo{id, nullptr, dir};
}

void add_port(const Context *ctx, CellInfo *cell, IdString id, PortType dir)
{
    NPNR_ASSERT(cell->ports.count(id) == 0);
    cell->ports[id] = PortInfo{id, nullptr, dir};
}

std::unique_ptr<CellInfo> create_machxo2_cell(Context *ctx, IdString type, std::string name)
{
    static int auto_idx = 0;
    std::unique_ptr<CellInfo> new_cell = std::unique_ptr<CellInfo>(new CellInfo());
    if (name.empty()) {
        new_cell->name = ctx->id("$nextpnr_" + type.str(ctx) + "_" + std::to_string(auto_idx++));
    } else {
        new_cell->name = ctx->id(name);
    }
    new_cell->type = type;

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

        add_port(ctx, new_cell.get(), id_A0, PORT_IN);
        add_port(ctx, new_cell.get(), id_B0, PORT_IN);
        add_port(ctx, new_cell.get(), id_C0, PORT_IN);
        add_port(ctx, new_cell.get(), id_D0, PORT_IN);

        add_port(ctx, new_cell.get(), id_A1, PORT_IN);
        add_port(ctx, new_cell.get(), id_B1, PORT_IN);
        add_port(ctx, new_cell.get(), id_C1, PORT_IN);
        add_port(ctx, new_cell.get(), id_D1, PORT_IN);

        add_port(ctx, new_cell.get(), id_M0, PORT_IN);
        add_port(ctx, new_cell.get(), id_M1, PORT_IN);

        add_port(ctx, new_cell.get(), id_FCI, PORT_IN);
        add_port(ctx, new_cell.get(), id_FXA, PORT_IN);
        add_port(ctx, new_cell.get(), id_FXB, PORT_IN);

        add_port(ctx, new_cell.get(), id_CLK, PORT_IN);
        add_port(ctx, new_cell.get(), id_LSR, PORT_IN);
        add_port(ctx, new_cell.get(), id_CE, PORT_IN);

        add_port(ctx, new_cell.get(), id_DI0, PORT_IN);
        add_port(ctx, new_cell.get(), id_DI1, PORT_IN);

        add_port(ctx, new_cell.get(), id_WD0, PORT_IN);
        add_port(ctx, new_cell.get(), id_WD1, PORT_IN);
        add_port(ctx, new_cell.get(), id_WAD0, PORT_IN);
        add_port(ctx, new_cell.get(), id_WAD1, PORT_IN);
        add_port(ctx, new_cell.get(), id_WAD2, PORT_IN);
        add_port(ctx, new_cell.get(), id_WAD3, PORT_IN);
        add_port(ctx, new_cell.get(), id_WRE, PORT_IN);
        add_port(ctx, new_cell.get(), id_WCK, PORT_IN);

        add_port(ctx, new_cell.get(), id_F0, PORT_OUT);
        add_port(ctx, new_cell.get(), id_Q0, PORT_OUT);
        add_port(ctx, new_cell.get(), id_F1, PORT_OUT);
        add_port(ctx, new_cell.get(), id_Q1, PORT_OUT);

        add_port(ctx, new_cell.get(), id_FCO, PORT_OUT);
        add_port(ctx, new_cell.get(), id_OFX0, PORT_OUT);
        add_port(ctx, new_cell.get(), id_OFX1, PORT_OUT);

        add_port(ctx, new_cell.get(), id_WDO0, PORT_OUT);
        add_port(ctx, new_cell.get(), id_WDO1, PORT_OUT);
        add_port(ctx, new_cell.get(), id_WDO2, PORT_OUT);
        add_port(ctx, new_cell.get(), id_WDO3, PORT_OUT);
        add_port(ctx, new_cell.get(), id_WADO0, PORT_OUT);
        add_port(ctx, new_cell.get(), id_WADO1, PORT_OUT);
        add_port(ctx, new_cell.get(), id_WADO2, PORT_OUT);
        add_port(ctx, new_cell.get(), id_WADO3, PORT_OUT);
    } else if (type == id_FACADE_IO) {
        new_cell->params[id_DIR] = std::string("INPUT");
        new_cell->attrs[ctx->id("IO_TYPE")] = std::string("LVCMOS33");

        add_port(ctx, new_cell.get(), "PAD", PORT_INOUT);
        add_port(ctx, new_cell.get(), "I", PORT_IN);
        add_port(ctx, new_cell.get(), "EN", PORT_IN);
        add_port(ctx, new_cell.get(), "O", PORT_OUT);
    } else if (type == id_LUT4) {
        new_cell->params[id_INIT] = Property(0, 16);

        add_port(ctx, new_cell.get(), id_A, PORT_IN);
        add_port(ctx, new_cell.get(), id_B, PORT_IN);
        add_port(ctx, new_cell.get(), id_C, PORT_IN);
        add_port(ctx, new_cell.get(), id_D, PORT_IN);
        add_port(ctx, new_cell.get(), id_Z, PORT_OUT);
    } else {
        log_error("unable to create MachXO2 cell of type %s", type.c_str(ctx));
    }

    return new_cell;
}

void lut_to_lc(const Context *ctx, CellInfo *lut, CellInfo *lc, bool no_dff)
{
    lc->params[ctx->id("LUT0_INITVAL")] = lut->params[ctx->id("INIT")];

    for (std::string i : {"A", "B", "C", "D"}) {
        IdString lut_port = ctx->id(i);
        IdString lc_port = ctx->id(i + "0");
        replace_port(lut, lut_port, lc, lc_port);
    }

    replace_port(lut, ctx->id("Z"), lc, ctx->id("F0"));
}

void dff_to_lc(Context *ctx, CellInfo *dff, CellInfo *lc, LutType lut_type)
{
    // FIXME: This will have to change once we support FFs with reset value of 1.
    lc->params[ctx->id("REG0_REGSET")] = std::string("RESET");

    replace_port(dff, ctx->id("CLK"), lc, ctx->id("CLK"));
    replace_port(dff, ctx->id("LSR"), lc, ctx->id("LSR"));
    replace_port(dff, ctx->id("Q"), lc, ctx->id("Q0"));

    if (lut_type == LutType::PassThru) {
        // If a register's DI port is fed by a constant, options for placing are
        // limited. Use the LUT to get around this.
        // LUT output will go to F0, which will feed back to DI0 input.
        lc->params[ctx->id("LUT0_INITVAL")] = Property(0xAAAA, 16);
        replace_port(dff, ctx->id("DI"), lc, ctx->id("A0"));
        connect_ports(ctx, lc, ctx->id("F0"), lc, ctx->id("DI0"));
    } else if (lut_type == LutType::None) {
        // If there is no LUT, use the M0 input because DI0 requires
        // going through the LUTs.
        lc->params[ctx->id("REG0_SD")] = std::string("0");
        replace_port(dff, ctx->id("DI"), lc, ctx->id("M0"));
    } else {
        // Otherwise, there's a LUT being used in the slice and mapping DI to
        // DI0 input is fine.
        replace_port(dff, ctx->id("DI"), lc, ctx->id("DI0"));
    }
}

void nxio_to_iob(Context *ctx, CellInfo *nxio, CellInfo *iob, pool<IdString> &todelete_cells) {}

NEXTPNR_NAMESPACE_END
