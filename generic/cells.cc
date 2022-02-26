/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019  gatecat <gatecat@ds0.me>
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

std::unique_ptr<CellInfo> create_generic_cell(Context *ctx, IdString type, std::string name)
{
    static int auto_idx = 0;
    IdString name_id =
            name.empty() ? ctx->id("$nextpnr_" + type.str(ctx) + "_" + std::to_string(auto_idx++)) : ctx->id(name);
    auto new_cell = std::make_unique<CellInfo>(ctx, name_id, type);
    if (type == ctx->id("GENERIC_SLICE")) {
        new_cell->params[ctx->id("K")] = ctx->args.K;
        new_cell->params[ctx->id("INIT")] = 0;
        new_cell->params[ctx->id("FF_USED")] = 0;

        for (int i = 0; i < ctx->args.K; i++)
            new_cell->addInput(ctx->id("I[" + std::to_string(i) + "]"));

        new_cell->addInput(ctx->id("CLK"));

        new_cell->addOutput(ctx->id("F"));
        new_cell->addOutput(ctx->id("Q"));
    } else if (type == ctx->id("GENERIC_IOB")) {
        new_cell->params[ctx->id("INPUT_USED")] = 0;
        new_cell->params[ctx->id("OUTPUT_USED")] = 0;
        new_cell->params[ctx->id("ENABLE_USED")] = 0;

        new_cell->addInout(ctx->id("PAD"));
        new_cell->addInput(ctx->id("I"));
        new_cell->addInput(ctx->id("EN"));
        new_cell->addOutput(ctx->id("O"));
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
        lut->movePortTo(port, lc, port);
    }

    if (no_dff) {
        lc->params[ctx->id("FF_USED")] = 0;
        lut->movePortTo(ctx->id("Q"), lc, ctx->id("F"));
    }
}

void dff_to_lc(const Context *ctx, CellInfo *dff, CellInfo *lc, bool pass_thru_lut)
{
    lc->params[ctx->id("FF_USED")] = 1;
    dff->movePortTo(ctx->id("CLK"), lc, ctx->id("CLK"));

    if (pass_thru_lut) {
        // Fill LUT with alternating 10
        const int init_size = 1 << lc->params[ctx->id("K")].as_int64();
        std::string init;
        init.reserve(init_size);
        for (int i = 0; i < init_size; i += 2)
            init.append("10");
        lc->params[ctx->id("INIT")] = Property::from_string(init);

        dff->movePortTo(ctx->id("D"), lc, ctx->id("I[0]"));
    }

    dff->movePortTo(ctx->id("Q"), lc, ctx->id("Q"));
}

void nxio_to_iob(Context *ctx, CellInfo *nxio, CellInfo *iob, pool<IdString> &todelete_cells)
{
    if (nxio->type == ctx->id("$nextpnr_ibuf")) {
        iob->params[ctx->id("INPUT_USED")] = 1;
        nxio->movePortTo(ctx->id("O"), iob, ctx->id("O"));
    } else if (nxio->type == ctx->id("$nextpnr_obuf")) {
        iob->params[ctx->id("OUTPUT_USED")] = 1;
        nxio->movePortTo(ctx->id("I"), iob, ctx->id("I"));
    } else if (nxio->type == ctx->id("$nextpnr_iobuf")) {
        // N.B. tristate will be dealt with below
        iob->params[ctx->id("INPUT_USED")] = 1;
        iob->params[ctx->id("OUTPUT_USED")] = 1;
        nxio->movePortTo(ctx->id("I"), iob, ctx->id("I"));
        nxio->movePortTo(ctx->id("O"), iob, ctx->id("O"));
    } else {
        NPNR_ASSERT(false);
    }
    NetInfo *donet = iob->ports.at(ctx->id("I")).net;
    CellInfo *tbuf = net_driven_by(
            ctx, donet, [](const Context *ctx, const CellInfo *cell) { return cell->type == ctx->id("$_TBUF_"); },
            ctx->id("Y"));
    if (tbuf) {
        iob->params[ctx->id("ENABLE_USED")] = 1;
        tbuf->movePortTo(ctx->id("A"), iob, ctx->id("I"));
        tbuf->movePortTo(ctx->id("E"), iob, ctx->id("EN"));

        if (donet->users.entries() > 1) {
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
