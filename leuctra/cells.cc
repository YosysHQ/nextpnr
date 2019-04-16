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
#include <algorithm>
#include "design_utils.h"
#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

void add_port(const Context *ctx, CellInfo *cell, std::string name, PortType dir)
{
    IdString id = ctx->id(name);
    cell->ports[id] = PortInfo{id, nullptr, dir};
}

std::unique_ptr<CellInfo> create_leuctra_cell(Context *ctx, IdString type, std::string name)
{
    static int auto_idx = 0;
    std::unique_ptr<CellInfo> new_cell = std::unique_ptr<CellInfo>(new CellInfo());
    if (name.empty()) {
        new_cell->name = ctx->id("$nextpnr_" + type.str(ctx) + "_" + std::to_string(auto_idx++));
    } else {
        new_cell->name = ctx->id(name);
    }
    new_cell->type = type;

    auto copy_bel_ports = [&]() {
        // First find a Bel of the target type
        BelId tgt;
        for (auto bel : ctx->getBels()) {
            if (ctx->getBelType(bel) == type) {
                tgt = bel;
                break;
            }
        }
        NPNR_ASSERT(tgt != BelId());
        for (auto port : ctx->getBelPins(tgt)) {
            add_port(ctx, new_cell.get(), port.str(ctx), ctx->getBelPinType(tgt, port));
        }
    };

    if (type == ctx->id("LEUCTRA_FF")) {
        new_cell->params[ctx->id("MODE")] = "FFSYNC";
        new_cell->params[ctx->id("INIT")] = "0";
        add_port(ctx, new_cell.get(), "D", PORT_IN);
        add_port(ctx, new_cell.get(), "CLK", PORT_IN);
        add_port(ctx, new_cell.get(), "CE", PORT_IN);
        add_port(ctx, new_cell.get(), "SR", PORT_IN);
        add_port(ctx, new_cell.get(), "Q", PORT_OUT);
    } else if (type == ctx->id("LEUCTRA_LC")) {
        new_cell->params[ctx->id("INIT")] = "0000000000000000000000000000000000000000000000000000000000000000";
        add_port(ctx, new_cell.get(), "I1", PORT_IN);
        add_port(ctx, new_cell.get(), "I2", PORT_IN);
        add_port(ctx, new_cell.get(), "I3", PORT_IN);
        add_port(ctx, new_cell.get(), "I4", PORT_IN);
        add_port(ctx, new_cell.get(), "I5", PORT_IN);
        add_port(ctx, new_cell.get(), "I6", PORT_IN);
        add_port(ctx, new_cell.get(), "O6", PORT_OUT);
    } else if (type == ctx->id("IOB")) {
        new_cell->params[ctx->id("DIR")] = "INPUT";
        new_cell->attrs[ctx->id("IOSTANDARD")] = "LVCMOS33";

        add_port(ctx, new_cell.get(), "O", PORT_IN);
        add_port(ctx, new_cell.get(), "T", PORT_IN);
        add_port(ctx, new_cell.get(), "I", PORT_OUT);
    } else if (type == ctx->id("ILOGIC2")) {
        add_port(ctx, new_cell.get(), "D", PORT_IN);
        add_port(ctx, new_cell.get(), "FABRICOUT", PORT_OUT);
    } else if (type == ctx->id("OLOGIC2")) {
        add_port(ctx, new_cell.get(), "D1", PORT_IN);
        add_port(ctx, new_cell.get(), "OQ", PORT_OUT);
        add_port(ctx, new_cell.get(), "T1", PORT_IN);
        add_port(ctx, new_cell.get(), "TQ", PORT_OUT);
    } else if (type == ctx->id("LUT1")) {
        new_cell->params[ctx->id("INIT")] = "0";

        add_port(ctx, new_cell.get(), "I0", PORT_IN);
        add_port(ctx, new_cell.get(), "O", PORT_OUT);
    } else {
        log_error("unable to create Leuctra cell of type %s", type.c_str(ctx));
    }
    return new_cell;
}

void nxio_to_iob(Context *ctx, CellInfo *nxio, CellInfo *iob, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                std::unordered_set<IdString> &todelete_cells)
{
    if (nxio->type == ctx->id("$nextpnr_ibuf")) {
        iob->params[ctx->id("DIR")] = "INPUT";
        replace_port(nxio, ctx->id("O"), iob, ctx->id("I"));
    } else if (nxio->type == ctx->id("$nextpnr_obuf")) {
        iob->params[ctx->id("DIR")] = "OUTPUT";
        replace_port(nxio, ctx->id("I"), iob, ctx->id("O"));
    } else if (nxio->type == ctx->id("$nextpnr_iobuf")) {
        // N.B. tristate will be dealt with below
        iob->params[ctx->id("DIR")] = "BIDIR";
        replace_port(nxio, ctx->id("I"), iob, ctx->id("O"));
        replace_port(nxio, ctx->id("O"), iob, ctx->id("I"));
    } else {
        NPNR_ASSERT(false);
    }
    NetInfo *donet = iob->ports.at(ctx->id("O")).net;
    CellInfo *tbuf = net_driven_by(
            ctx, donet, [](const Context *ctx, const CellInfo *cell) { return cell->type == ctx->id("$_TBUF_"); },
            ctx->id("Y"));
    if (tbuf) {
        replace_port(tbuf, ctx->id("A"), iob, ctx->id("O"));
        // Need to invert E to form T
        std::unique_ptr<CellInfo> inv_lut = create_leuctra_cell(ctx, ctx->id("LUT1"), iob->name.str(ctx) + "$invert_T");
        replace_port(tbuf, ctx->id("E"), inv_lut.get(), ctx->id("I0"));
        inv_lut->params[ctx->id("INIT")] = "1";
        connect_ports(ctx, inv_lut.get(), ctx->id("O"), iob, ctx->id("T"));
        created_cells.push_back(std::move(inv_lut));

        if (donet->users.size() > 1) {
            for (auto user : donet->users)
                log_info("     remaining tristate user: %s.%s\n", user.cell->name.c_str(ctx), user.port.c_str(ctx));
            log_error("unsupported tristate IO pattern for IO buffer '%s', "
                      "instantiate IOBUF manually to ensure correct behaviour\n",
                      nxio->name.c_str(ctx));
        }
        ctx->nets.erase(donet->name);
        todelete_cells.insert(tbuf->name);
    }
}

void convert_ff(Context *ctx, CellInfo *orig, CellInfo *ff, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                std::unordered_set<IdString> &todelete_cells)
{
    if (orig->type == ctx->id("FDRE")) {
        ff->params[ctx->id("MODE")] = "FF_SYNC";
        ff->params[ctx->id("INIT")] = "0";
        replace_port(orig, ctx->id("D"), ff, ctx->id("D"));
        replace_port(orig, ctx->id("C"), ff, ctx->id("CLK"));
        replace_port(orig, ctx->id("CE"), ff, ctx->id("CE"));
        replace_port(orig, ctx->id("R"), ff, ctx->id("SR"));
        replace_port(orig, ctx->id("Q"), ff, ctx->id("Q"));
    } else {
        NPNR_ASSERT(false);
    }
}

void convert_lut(Context *ctx, CellInfo *orig, CellInfo *lut, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                std::unordered_set<IdString> &todelete_cells)
{
    auto &orig_init = orig->params[ctx->id("INIT")];
    std::string new_init = "0000000000000000000000000000000000000000000000000000000000000000";
    int nbits = 0;
    if (orig->type == ctx->id("LUT1")) {
	nbits = 2;
        replace_port(orig, ctx->id("I0"), lut, ctx->id("I1"));
        replace_port(orig, ctx->id("O"), lut, ctx->id("O6"));
    } else if (orig->type == ctx->id("LUT2")) {
	nbits = 4;
        replace_port(orig, ctx->id("I0"), lut, ctx->id("I1"));
        replace_port(orig, ctx->id("I1"), lut, ctx->id("I2"));
        replace_port(orig, ctx->id("O"), lut, ctx->id("O6"));
    } else if (orig->type == ctx->id("LUT3")) {
	nbits = 8;
        replace_port(orig, ctx->id("I0"), lut, ctx->id("I1"));
        replace_port(orig, ctx->id("I1"), lut, ctx->id("I2"));
        replace_port(orig, ctx->id("I2"), lut, ctx->id("I3"));
        replace_port(orig, ctx->id("O"), lut, ctx->id("O6"));
    } else if (orig->type == ctx->id("LUT4")) {
	nbits = 16;
        replace_port(orig, ctx->id("I0"), lut, ctx->id("I1"));
        replace_port(orig, ctx->id("I1"), lut, ctx->id("I2"));
        replace_port(orig, ctx->id("I2"), lut, ctx->id("I3"));
        replace_port(orig, ctx->id("I3"), lut, ctx->id("I4"));
        replace_port(orig, ctx->id("O"), lut, ctx->id("O6"));
    } else if (orig->type == ctx->id("LUT5")) {
	nbits = 32;
        replace_port(orig, ctx->id("I0"), lut, ctx->id("I1"));
        replace_port(orig, ctx->id("I1"), lut, ctx->id("I2"));
        replace_port(orig, ctx->id("I2"), lut, ctx->id("I3"));
        replace_port(orig, ctx->id("I3"), lut, ctx->id("I4"));
        replace_port(orig, ctx->id("I4"), lut, ctx->id("I5"));
        replace_port(orig, ctx->id("O"), lut, ctx->id("O6"));
    } else if (orig->type == ctx->id("LUT6")) {
	new_init = orig_init;
        replace_port(orig, ctx->id("I0"), lut, ctx->id("I1"));
        replace_port(orig, ctx->id("I1"), lut, ctx->id("I2"));
        replace_port(orig, ctx->id("I2"), lut, ctx->id("I3"));
        replace_port(orig, ctx->id("I3"), lut, ctx->id("I4"));
        replace_port(orig, ctx->id("I4"), lut, ctx->id("I5"));
        replace_port(orig, ctx->id("I5"), lut, ctx->id("I6"));
        replace_port(orig, ctx->id("O"), lut, ctx->id("O6"));
    } else {
        NPNR_ASSERT(false);
    }
    if (nbits) {
	unsigned long init = std::stoul(orig_init);
	for (int i = 0; i < 64; i++) {
	    int obit = i % nbits;
	    new_init[63-i] = '0' + (init >> obit & 1);
	}
    }
    lut->params[ctx->id("INIT")] = new_init;
}

void insert_ilogic_pass(Context *ctx, CellInfo *iob, CellInfo *ilogic)
{
    replace_port(iob, ctx->id("I"), ilogic, ctx->id("FABRICOUT"));
    connect_ports(ctx, iob, ctx->id("I"), ilogic, ctx->id("D"));
    ilogic->constr_parent = iob;
    iob->constr_children.push_back(ilogic);
    // XXX enum
    ilogic->constr_spec = 1;
}

void insert_ologic_pass(Context *ctx, CellInfo *iob, CellInfo *ologic)
{
    replace_port(iob, ctx->id("O"), ologic, ctx->id("D1"));
    connect_ports(ctx, ologic, ctx->id("OQ"), iob, ctx->id("O"));
    NetInfo *net_t = iob->ports.at(ctx->id("T")).net;
    if (net_t != nullptr) {
        replace_port(iob, ctx->id("T"), ologic, ctx->id("T1"));
        connect_ports(ctx, ologic, ctx->id("TQ"), iob, ctx->id("T"));
    }
    ologic->constr_parent = iob;
    iob->constr_children.push_back(ologic);
    // XXX enum
    ologic->constr_spec = 2;
}

NEXTPNR_NAMESPACE_END
