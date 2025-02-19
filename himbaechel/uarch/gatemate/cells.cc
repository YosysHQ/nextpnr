/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
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

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

CellInfo *GateMatePacker::create_cell_ptr(IdString type, IdString name)
{
    CellInfo *cell = ctx->createCell(name, type);

    auto add_port = [&](const IdString id, PortType dir) {
        cell->ports[id].name = id;
        cell->ports[id].type = dir;
    };
    if (type.in(id_CPE_HALF,id_CPE_HALF_U,id_CPE_HALF_L)) {
        add_port(id_I1, PORT_IN);
        add_port(id_I2, PORT_IN);
        add_port(id_I3, PORT_IN);
        add_port(id_I4, PORT_IN);
        add_port(id_RAM_I, PORT_IN);
        add_port(id_OUT, PORT_OUT);
        add_port(id_RAM_O, PORT_OUT);
        add_port(id_EN, PORT_IN);
        add_port(id_CLK, PORT_IN);
        add_port(id_SR, PORT_IN);
    } else {
        log_error("Trying to create unknown cell type %s\n", type.c_str(ctx));
    }
    return cell;
}

NEXTPNR_NAMESPACE_END
