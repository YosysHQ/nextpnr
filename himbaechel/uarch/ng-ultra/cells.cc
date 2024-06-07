/*
 *  nextpnr -- Next Generation Place and Route
 *
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

#include "nextpnr.h"
#include "pack.h"

#define HIMBAECHEL_CONSTIDS "uarch/ng-ultra/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

std::unique_ptr<CellInfo> NgUltraPacker::create_cell(IdString type, IdString name)
{
    auto cell = std::make_unique<CellInfo>(ctx, name, type);

    auto add_port = [&](const std::string &name, PortType dir) {
        IdString id = ctx->id(name);
        cell->ports[id].name = id;
        cell->ports[id].type = dir;
    };
    if (type == id_BEYOND_FE) {
        add_port("I1", PORT_IN);
        add_port("I2", PORT_IN);
        add_port("I3", PORT_IN);
        add_port("I4", PORT_IN);
        add_port("LO", PORT_OUT);
        add_port("DI", PORT_IN);
        add_port("L", PORT_IN);
        add_port("CK", PORT_IN);
        add_port("R", PORT_IN);
        add_port("DO", PORT_OUT);
    } else {
        log_error("Trying to create unknown cell type %s\n", type.c_str(ctx));
    }
    return cell;
}

CellInfo *NgUltraPacker::create_cell_ptr(IdString type, IdString name)
{
    CellInfo *cell = ctx->createCell(name, type);

    auto add_port = [&](const std::string &name, PortType dir) {
        IdString id = ctx->id(name);
        cell->ports[id].name = id;
        cell->ports[id].type = dir;
    };
    if (type == id_BEYOND_FE) {
        add_port("I1", PORT_IN);
        add_port("I2", PORT_IN);
        add_port("I3", PORT_IN);
        add_port("I4", PORT_IN);
        add_port("LO", PORT_OUT);
        add_port("DI", PORT_IN);
        add_port("L", PORT_IN);
        add_port("CK", PORT_IN);
        add_port("R", PORT_IN);
        add_port("DO", PORT_OUT);
    } else if (type == id_BFR) {
        add_port("I", PORT_IN);
        add_port("O", PORT_OUT);
    } else if (type == id_DFR) {
        add_port("I", PORT_IN);
        add_port("O", PORT_OUT);
        add_port("L", PORT_IN);
        add_port("CK", PORT_IN);
        add_port("R", PORT_IN);
    } else if (type == id_DDFR) {
        add_port("I", PORT_IN);
        add_port("O", PORT_OUT);
        add_port("L", PORT_IN);
        add_port("CK", PORT_IN);
        add_port("R", PORT_IN);
        add_port("I2", PORT_IN);
        add_port("O2", PORT_OUT);
        add_port("CKF", PORT_IN);
    } else if (type == id_IOM) {
        add_port("P17RI", PORT_IN);
        add_port("CKO1", PORT_OUT);
        add_port("P19RI", PORT_IN);
        add_port("CKO2", PORT_OUT);
    } else if (type == id_WFB) {
        add_port("ZI", PORT_IN);
        add_port("ZO", PORT_OUT);
    } else {
        log_error("Trying to create unknown cell type %s\n", type.c_str(ctx));
    }
    return cell;
}

NEXTPNR_NAMESPACE_END
