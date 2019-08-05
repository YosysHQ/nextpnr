/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019  David Shah <david@symbioticeda.com>
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
    if (type == ctx->id("GENERIC_SLICE")) {
        new_cell->params[ctx->id("K")] = std::to_string(ctx->args.K);
        new_cell->params[ctx->id("INIT")] = 0;
        new_cell->params[ctx->id("FF_USED")] = 0;

        for (int i = 0; i < ctx->args.K; i++)
            add_port(ctx, new_cell.get(), "I[" + std::to_string(i) + "]", PORT_IN);

        add_port(ctx, new_cell.get(), "CLK", PORT_IN);

        add_port(ctx, new_cell.get(), "Q", PORT_OUT);
    } else if (type == ctx->id("GENERIC_IOB")) {
        new_cell->params[ctx->id("INPUT_USED")] = 0;
        new_cell->params[ctx->id("OUTPUT_USED")] = 0;
        new_cell->params[ctx->id("ENABLE_USED")] = 0;

        add_port(ctx, new_cell.get(), "PAD", PORT_INOUT);
        add_port(ctx, new_cell.get(), "I", PORT_IN);
        add_port(ctx, new_cell.get(), "EN", PORT_IN);
        add_port(ctx, new_cell.get(), "O", PORT_OUT);
    } else {
        log_error("unable to create generic cell of type %s", type.c_str(ctx));
    }
    return new_cell;
}

void lut_to_lc(const Context *ctx, CellInfo *lut, CellInfo *lc, bool no_dff)
{
    lc->params[ctx->id("INIT")] = lut->params[ctx->id("INIT")];

    int lut_k = int_or_default(lut->params, ctx->id("K"), 4);
    NPNR_ASSERT(lut_k <= ctx->args.K);

    for (int i = 0; i < lut_k; i++) {
        IdString port = ctx->id("I[" + std::to_string(i) + "]");
        replace_port(lut, port, lc, port);
    }

    if (no_dff) {
        replace_port(lut, ctx->id("Q"), lc, ctx->id("Q"));
        lc->params[ctx->id("FF_USED")] = 0;
    }
}

void dff_to_lc(const Context *ctx, CellInfo *dff, CellInfo *lc, bool pass_thru_lut)
{
    lc->params[ctx->id("FF_USED")] = 1;
    replace_port(dff, ctx->id("CLK"), lc, ctx->id("CLK"));

    if (pass_thru_lut) {
        lc->params[ctx->id("INIT")] = 2;
        replace_port(dff, ctx->id("D"), lc, ctx->id("I[0]"));
    }

    replace_port(dff, ctx->id("Q"), lc, ctx->id("Q"));
}

void nxio_to_iob(Context *ctx, CellInfo *nxio, CellInfo *iob, std::unordered_set<IdString> &todelete_cells)
{
    if (nxio->type == ctx->id("$nextpnr_ibuf")) {
        iob->params[ctx->id("INPUT_USED")] = 1;
        replace_port(nxio, ctx->id("O"), iob, ctx->id("O"));
    } else if (nxio->type == ctx->id("$nextpnr_obuf")) {
        iob->params[ctx->id("OUTPUT_USED")] = 1;
        replace_port(nxio, ctx->id("I"), iob, ctx->id("I"));
    } else if (nxio->type == ctx->id("$nextpnr_iobuf")) {
        // N.B. tristate will be dealt with below
        iob->params[ctx->id("INPUT_USED")] = 1;
        iob->params[ctx->id("OUTPUT_USED")] = 1;
        replace_port(nxio, ctx->id("I"), iob, ctx->id("I"));
        replace_port(nxio, ctx->id("O"), iob, ctx->id("O"));
    } else {
        NPNR_ASSERT(false);
    }
    NetInfo *donet = iob->ports.at(ctx->id("I")).net;
    CellInfo *tbuf = net_driven_by(
            ctx, donet, [](const Context *ctx, const CellInfo *cell) { return cell->type == ctx->id("$_TBUF_"); },
            ctx->id("Y"));
    if (tbuf) {
        iob->params[ctx->id("ENABLE_USED")] = 1;
        replace_port(tbuf, ctx->id("A"), iob, ctx->id("I"));
        replace_port(tbuf, ctx->id("E"), iob, ctx->id("EN"));

        if (donet->users.size() > 1) {
            for (auto user : donet->users)
                log_info("     remaining tristate user: %s.%s\n", user.cell->name.c_str(ctx), user.port.c_str(ctx));
            log_error("unsupported tristate IO pattern for IO buffer '%s', "
                      "instantiate GENERIC_IOB manually to ensure correct behaviour\n",
                      nxio->name.c_str(ctx));
        }
        ctx->nets.erase(donet->name);
        todelete_cells.insert(tbuf->name);
    }
}

NEXTPNR_NAMESPACE_END
