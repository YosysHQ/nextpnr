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
#include "log.h"
#include "util.h"
#include "design_utils.h"

NEXTPNR_NAMESPACE_BEGIN

void add_port(const Context *ctx, CellInfo *cell, std::string name, PortType dir)
{
    IdString id = ctx->id(name);
    cell->ports[id] = PortInfo{id, nullptr, dir};
}


std::unique_ptr<CellInfo> create_ecp5_cell(Context *ctx, IdString type, std::string name = "") {
    static int auto_idx = 0;
    std::unique_ptr<CellInfo> new_cell = std::unique_ptr<CellInfo>(new CellInfo());
    if (name.empty()) {
        new_cell->name = ctx->id("$nextpnr_" + type.str(ctx) + "_" + std::to_string(auto_idx++));
    } else {
        new_cell->name = ctx->id(name);
    }
    new_cell->type = type;
    if (type == ctx->id("TRELLIS_LC")) {
        new_cell->params[ctx->id("MODE")] = "LOGIC";
        new_cell->params[ctx->id("GSR")] = "DISABLED";
        new_cell->params[ctx->id("SRMODE")] = "LSR_OVER_CE";
        new_cell->params[ctx->id("CEMUX")] = "1";
        new_cell->params[ctx->id("CLKMUX")] = "CLK";
        new_cell->params[ctx->id("LSRMUX")] = "LSR";
        new_cell->params[ctx->id("LUT0_INITVAL")] = "0";
        new_cell->params[ctx->id("LUT1_INITVAL")] = "0";
        new_cell->params[ctx->id("REG0_SD")] = "0";
        new_cell->params[ctx->id("REG1_SD")] = "0";
        new_cell->params[ctx->id("REG0_REGSET")] = "RESET";
        new_cell->params[ctx->id("REG1_REGSET")] = "RESET";
        new_cell->params[ctx->id("CCU2_INJECT1_0")] = "NO";
        new_cell->params[ctx->id("CCU2_INJECT1_1")] = "NO";
        new_cell->params[ctx->id("WREMUX")] = "WRE";

        add_port(ctx, new_cell.get(), "A0", PORT_IN);
        add_port(ctx, new_cell.get(), "B0", PORT_IN);
        add_port(ctx, new_cell.get(), "C0", PORT_IN);
        add_port(ctx, new_cell.get(), "D0", PORT_IN);

        add_port(ctx, new_cell.get(), "A1", PORT_IN);
        add_port(ctx, new_cell.get(), "B1", PORT_IN);
        add_port(ctx, new_cell.get(), "C1", PORT_IN);
        add_port(ctx, new_cell.get(), "D1", PORT_IN);

        add_port(ctx, new_cell.get(), "M0", PORT_IN);
        add_port(ctx, new_cell.get(), "M1", PORT_IN);

        add_port(ctx, new_cell.get(), "FCI", PORT_IN);
        add_port(ctx, new_cell.get(), "FXA", PORT_IN);
        add_port(ctx, new_cell.get(), "FXB", PORT_IN);

        add_port(ctx, new_cell.get(), "CLK", PORT_IN);
        add_port(ctx, new_cell.get(), "LSR", PORT_IN);
        add_port(ctx, new_cell.get(), "CE", PORT_IN);

        add_port(ctx, new_cell.get(), "DI0", PORT_IN);
        add_port(ctx, new_cell.get(), "DI1", PORT_IN);

        add_port(ctx, new_cell.get(), "WD0", PORT_IN);
        add_port(ctx, new_cell.get(), "WD1", PORT_IN);
        add_port(ctx, new_cell.get(), "WAD0", PORT_IN);
        add_port(ctx, new_cell.get(), "WAD1", PORT_IN);
        add_port(ctx, new_cell.get(), "WAD2", PORT_IN);
        add_port(ctx, new_cell.get(), "WAD3", PORT_IN);
        add_port(ctx, new_cell.get(), "WRE", PORT_IN);
        add_port(ctx, new_cell.get(), "WCK", PORT_IN);

        add_port(ctx, new_cell.get(), "F0", PORT_OUT);
        add_port(ctx, new_cell.get(), "Q0", PORT_OUT);
        add_port(ctx, new_cell.get(), "F1", PORT_OUT);
        add_port(ctx, new_cell.get(), "Q1", PORT_OUT);

        add_port(ctx, new_cell.get(), "FCO", PORT_OUT);
        add_port(ctx, new_cell.get(), "OFX0", PORT_OUT);
        add_port(ctx, new_cell.get(), "OFX1", PORT_OUT);

        add_port(ctx, new_cell.get(), "WDO0", PORT_OUT);
        add_port(ctx, new_cell.get(), "WDO1", PORT_OUT);
        add_port(ctx, new_cell.get(), "WDO2", PORT_OUT);
        add_port(ctx, new_cell.get(), "WDO3", PORT_OUT);
        add_port(ctx, new_cell.get(), "WADO0", PORT_OUT);
        add_port(ctx, new_cell.get(), "WADO1", PORT_OUT);
        add_port(ctx, new_cell.get(), "WADO2", PORT_OUT);
        add_port(ctx, new_cell.get(), "WADO3", PORT_OUT);
    } else if (type == ctx->id("TRELLIS_IO")) {
        new_cell->params[ctx->id("DIR")] = "INPUT";
        new_cell->attrs[ctx->id("IO_TYPE")] = "LVCMOS33";

        add_port(ctx, new_cell.get(), "B", PORT_INOUT);
        add_port(ctx, new_cell.get(), "I", PORT_IN);
        add_port(ctx, new_cell.get(), "T", PORT_IN);
        add_port(ctx, new_cell.get(), "O", PORT_OUT);
    } else {
        log_error("unable to create ECP5 cell of type %s", type.c_str(ctx));
    }
    return new_cell;
}

NEXTPNR_NAMESPACE_END
