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
#include <iostream>
#include "design_utils.h"
#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

struct LutData {
    int nbits;
    NetInfo *nets[6];
    Property init;
    CellInfo *cell;
};

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
        new_cell->params[ctx->id("MODE")] = Property("FF_SYNC");
        add_port(ctx, new_cell.get(), "D", PORT_IN);
        add_port(ctx, new_cell.get(), "CLK", PORT_IN);
        add_port(ctx, new_cell.get(), "CE", PORT_IN);
        add_port(ctx, new_cell.get(), "SR", PORT_IN);
        add_port(ctx, new_cell.get(), "Q", PORT_OUT);
    } else if (type == ctx->id("LEUCTRA_LC")) {
        new_cell->params[ctx->id("MODE")] = Property("ROM");
        new_cell->params[ctx->id("INIT")] = Property(0, 64);
        new_cell->attrs[ctx->id("NEEDS_L")] = Property(false);
        new_cell->attrs[ctx->id("NEEDS_M")] = Property(false);
        new_cell->attrs[ctx->id("LOCMASK")] = Property(0xf, 4);
        add_port(ctx, new_cell.get(), "I1", PORT_IN);
        add_port(ctx, new_cell.get(), "I2", PORT_IN);
        add_port(ctx, new_cell.get(), "I3", PORT_IN);
        add_port(ctx, new_cell.get(), "I4", PORT_IN);
        add_port(ctx, new_cell.get(), "I5", PORT_IN);
        add_port(ctx, new_cell.get(), "I6", PORT_IN);
        add_port(ctx, new_cell.get(), "RA1", PORT_IN);
        add_port(ctx, new_cell.get(), "RA2", PORT_IN);
        add_port(ctx, new_cell.get(), "RA3", PORT_IN);
        add_port(ctx, new_cell.get(), "RA4", PORT_IN);
        add_port(ctx, new_cell.get(), "RA5", PORT_IN);
        add_port(ctx, new_cell.get(), "RA6", PORT_IN);
        add_port(ctx, new_cell.get(), "WA1", PORT_IN);
        add_port(ctx, new_cell.get(), "WA2", PORT_IN);
        add_port(ctx, new_cell.get(), "WA3", PORT_IN);
        add_port(ctx, new_cell.get(), "WA4", PORT_IN);
        add_port(ctx, new_cell.get(), "WA5", PORT_IN);
        add_port(ctx, new_cell.get(), "WA6", PORT_IN);
        add_port(ctx, new_cell.get(), "WA7", PORT_IN);
        add_port(ctx, new_cell.get(), "WA8", PORT_IN);
        add_port(ctx, new_cell.get(), "WE", PORT_IN);
        add_port(ctx, new_cell.get(), "CLK", PORT_IN);
        add_port(ctx, new_cell.get(), "O6", PORT_OUT);
        add_port(ctx, new_cell.get(), "O5", PORT_OUT);
        add_port(ctx, new_cell.get(), "DMI0", PORT_IN);
        add_port(ctx, new_cell.get(), "DMI1", PORT_IN);
        add_port(ctx, new_cell.get(), "XI", PORT_IN);
        add_port(ctx, new_cell.get(), "DCI", PORT_IN);
        add_port(ctx, new_cell.get(), "MO", PORT_OUT);
        add_port(ctx, new_cell.get(), "XO", PORT_OUT);
        add_port(ctx, new_cell.get(), "CO", PORT_OUT);
        add_port(ctx, new_cell.get(), "DCO", PORT_OUT);
        add_port(ctx, new_cell.get(), "DMO", PORT_OUT);
        add_port(ctx, new_cell.get(), "DDI5", PORT_IN);
        add_port(ctx, new_cell.get(), "DDI7", PORT_IN);
        add_port(ctx, new_cell.get(), "DDI8", PORT_IN);
    } else if (type == ctx->id("IOB")) {
        add_port(ctx, new_cell.get(), "O", PORT_IN);
        add_port(ctx, new_cell.get(), "T", PORT_IN);
        add_port(ctx, new_cell.get(), "I", PORT_OUT);
        add_port(ctx, new_cell.get(), "PADOUT", PORT_OUT);
        add_port(ctx, new_cell.get(), "DIFFO_OUT", PORT_OUT);
        add_port(ctx, new_cell.get(), "DIFFO_IN", PORT_IN);
        add_port(ctx, new_cell.get(), "DIFFI_IN", PORT_IN);
    } else if (type == ctx->id("ILOGIC2")) {
        add_port(ctx, new_cell.get(), "D", PORT_IN);
        add_port(ctx, new_cell.get(), "FABRICOUT", PORT_OUT);
    } else if (type == ctx->id("OLOGIC2")) {
        add_port(ctx, new_cell.get(), "D1", PORT_IN);
        add_port(ctx, new_cell.get(), "D2", PORT_IN);
        add_port(ctx, new_cell.get(), "D3", PORT_IN);
        add_port(ctx, new_cell.get(), "D4", PORT_IN);
        add_port(ctx, new_cell.get(), "OQ", PORT_OUT);
        add_port(ctx, new_cell.get(), "T1", PORT_IN);
        add_port(ctx, new_cell.get(), "T2", PORT_IN);
        add_port(ctx, new_cell.get(), "T3", PORT_IN);
        add_port(ctx, new_cell.get(), "T4", PORT_IN);
        add_port(ctx, new_cell.get(), "TQ", PORT_OUT);
        add_port(ctx, new_cell.get(), "SR", PORT_IN);
        add_port(ctx, new_cell.get(), "REV", PORT_IN);
        add_port(ctx, new_cell.get(), "OCE", PORT_IN);
        add_port(ctx, new_cell.get(), "TCE", PORT_IN);
        add_port(ctx, new_cell.get(), "IOCE", PORT_IN);
        add_port(ctx, new_cell.get(), "TRAIN", PORT_IN);
        add_port(ctx, new_cell.get(), "CLK0", PORT_IN);
        add_port(ctx, new_cell.get(), "CLK1", PORT_IN);
        add_port(ctx, new_cell.get(), "CLKDIV", PORT_IN);
        add_port(ctx, new_cell.get(), "SHIFTIN1", PORT_IN);
        add_port(ctx, new_cell.get(), "SHIFTIN2", PORT_IN);
        add_port(ctx, new_cell.get(), "SHIFTIN3", PORT_IN);
        add_port(ctx, new_cell.get(), "SHIFTIN4", PORT_IN);
        add_port(ctx, new_cell.get(), "SHIFTOUT1", PORT_OUT);
        add_port(ctx, new_cell.get(), "SHIFTOUT2", PORT_OUT);
        add_port(ctx, new_cell.get(), "SHIFTOUT3", PORT_OUT);
        add_port(ctx, new_cell.get(), "SHIFTOUT4", PORT_OUT);
    } else {
        log_error("unable to create Leuctra cell of type %s", type.c_str(ctx));
    }
    return new_cell;
}

void nxio_to_iob(Context *ctx, CellInfo *nxio, CellInfo *iob, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                std::unordered_set<IdString> &todelete_cells)
{
    if (nxio->type == ctx->id("$nextpnr_ibuf")) {
        iob->params[ctx->id("DIR")] = Property("INPUT");
        replace_port(nxio, ctx->id("O"), iob, ctx->id("I"));
    } else if (nxio->type == ctx->id("$nextpnr_obuf")) {
        iob->params[ctx->id("DIR")] = Property("OUTPUT");
        replace_port(nxio, ctx->id("I"), iob, ctx->id("O"));
    } else if (nxio->type == ctx->id("$nextpnr_iobuf")) {
        // N.B. tristate will be dealt with below
        iob->params[ctx->id("DIR")] = Property("BIDIR");
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
        inv_lut->params[ctx->id("INIT")] = Property(1, 2);
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

void convert_ff(Context *ctx, CellInfo *orig, CellInfo *ff, std::vector<std::unique_ptr<CellInfo>> &new_cells,
                std::unordered_set<IdString> &todelete_cells)
{
    bool is_latch;
    IdString sr_pin;
    IdString ce_pin;
    IdString clk_pin;
    bool clk_inv;
    Property mode;
    bool srval;
    if (orig->type == ctx->id("FDRE")) {
	mode = Property("FF_SYNC");
	srval = false;
	sr_pin = ctx->id("R");
	is_latch = false;
	clk_inv = false;
    } else if (orig->type == ctx->id("FDSE")) {
	mode = Property("FF_SYNC");
	srval = true;
	sr_pin = ctx->id("S");
	is_latch = false;
	clk_inv = false;
    } else if (orig->type == ctx->id("FDCE")) {
	mode = Property("FF_ASYNC");
	srval = false;
	sr_pin = ctx->id("CLR");
	is_latch = false;
	clk_inv = false;
    } else if (orig->type == ctx->id("FDPE")) {
	mode = Property("FF_ASYNC");
	srval = true;
	sr_pin = ctx->id("PRE");
	is_latch = false;
	clk_inv = false;
    } else if (orig->type == ctx->id("LDCE")) {
	mode = Property("LATCH");
	srval = false;
	sr_pin = ctx->id("CLR");
	is_latch = true;
	clk_inv = true;
    } else if (orig->type == ctx->id("LDPE")) {
	mode = Property("LATCH");
	srval = true;
	sr_pin = ctx->id("PRE");
	is_latch = true;
	clk_inv = true;
    } else if (orig->type == ctx->id("FDRE_1")) {
	mode = Property("FF_SYNC");
	srval = false;
	sr_pin = ctx->id("R");
	is_latch = false;
	clk_inv = true;
    } else if (orig->type == ctx->id("FDSE_1")) {
	mode = Property("FF_SYNC");
	srval = true;
	sr_pin = ctx->id("S");
	is_latch = false;
	clk_inv = true;
    } else if (orig->type == ctx->id("FDCE_1")) {
	mode = Property("FF_ASYNC");
	srval = false;
	sr_pin = ctx->id("CLR");
	is_latch = false;
	clk_inv = true;
    } else if (orig->type == ctx->id("FDPE_1")) {
	mode = Property("FF_ASYNC");
	srval = true;
	sr_pin = ctx->id("PRE");
	is_latch = false;
	clk_inv = true;
    } else if (orig->type == ctx->id("LDCE_1")) {
	mode = Property("LATCH");
	srval = false;
	sr_pin = ctx->id("CLR");
	is_latch = true;
	clk_inv = false;
    } else if (orig->type == ctx->id("LDPE_1")) {
	mode = Property("LATCH");
	srval = true;
	sr_pin = ctx->id("PRE");
	is_latch = true;
	clk_inv = false;
    } else {
	NPNR_ASSERT_FALSE("WEIRD FF TYPE");
    }
    if (is_latch) {
	clk_pin = ctx->id("G");
	ce_pin = ctx->id("GE");
    } else {
	clk_pin = ctx->id("C");
	ce_pin = ctx->id("CE");
    }

    ff->params[ctx->id("MODE")] = mode;
    ff->params[ctx->id("SRVAL")] = Property(srval, 1);
    if (orig->params.count(ctx->id("INIT"))) {
	if (orig->params[ctx->id("INIT")].str[0] != 'x')
		ff->params[ctx->id("INIT")] = orig->params[ctx->id("INIT")];
	orig->params.erase(ctx->id("INIT"));
    }

    NetInfo *net;
    bool net_inv = false, cval = false;

    if (get_invertible_port(ctx, orig, ctx->id("D"), false, false, net, net_inv))
	set_invertible_port(ctx, ff, ctx->id("D"), net, net_inv, false, new_cells);

    if (get_invertible_port(ctx, orig, sr_pin, false, false, net, net_inv)) {
	if (get_const_val(ctx, net, cval) && (cval ^ net_inv) == false) {
	    // SR tied to 0 — remove it.
	} else {
	    set_invertible_port(ctx, ff, ctx->id("SR"), net, net_inv, false, new_cells);
	    ff->params[ctx->id("SRUSED")] = Property(true);
	}
    }

    if (get_invertible_port(ctx, orig, ce_pin, false, false, net, net_inv)) {
	if (get_const_val(ctx, net, cval) && (cval ^ net_inv) == true) {
	    // CE tied to 1 — remove it.
	} else {
	    set_invertible_port(ctx, ff, ctx->id("CE"), net, net_inv, false, new_cells);
	    ff->params[ctx->id("CEUSED")] = Property(true);
	}
    }

    if (get_invertible_port(ctx, orig, clk_pin, clk_inv, true, net, net_inv)) {
	set_invertible_port(ctx, ff, ctx->id("CLK"), net, net_inv, true, new_cells);
    }

    replace_port(orig, ctx->id("Q"), ff, ctx->id("Q"));

    for (auto &param : orig->params) {
	log_error("FF %s has leftover param %s = %s\n", orig->name.c_str(ctx), param.first.c_str(ctx), param.second.str.c_str());
    }
}

LutData get_lut(Context *ctx, NetInfo *net) {
    LutData res;
    res.cell = net->driver.cell;
    if (!res.cell || res.cell->type == ctx->id("GND")) {
	res.nbits = 0;
	res.init = Property(0, 1);
    } else if (res.cell->type == ctx->id("VCC")) {
	res.nbits = 0;
	res.init = Property(1, 1);
    } else if (res.cell->type == ctx->id("INV")) {
	res.nbits = 1;
	res.nets[0] = res.cell->ports[ctx->id("I")].net;
	res.init = Property(1, 2);
    } else if (res.cell->type == ctx->id("LUT1")) {
	res.nbits = 1;
	res.nets[0] = res.cell->ports[ctx->id("I0")].net;
	res.init = res.cell->params[ctx->id("INIT")];
    } else if (res.cell->type == ctx->id("LUT2")) {
	res.nbits = 2;
	res.nets[0] = res.cell->ports[ctx->id("I0")].net;
	res.nets[1] = res.cell->ports[ctx->id("I1")].net;
	res.init = res.cell->params[ctx->id("INIT")];
    } else if (res.cell->type == ctx->id("LUT3")) {
	res.nbits = 3;
	res.nets[0] = res.cell->ports[ctx->id("I0")].net;
	res.nets[1] = res.cell->ports[ctx->id("I1")].net;
	res.nets[2] = res.cell->ports[ctx->id("I2")].net;
	res.init = res.cell->params[ctx->id("INIT")];
    } else if (res.cell->type == ctx->id("LUT4")) {
	res.nbits = 4;
	res.nets[0] = res.cell->ports[ctx->id("I0")].net;
	res.nets[1] = res.cell->ports[ctx->id("I1")].net;
	res.nets[2] = res.cell->ports[ctx->id("I2")].net;
	res.nets[3] = res.cell->ports[ctx->id("I3")].net;
	res.init = res.cell->params[ctx->id("INIT")];
    } else if (res.cell->type == ctx->id("LUT5")) {
	res.nbits = 5;
	res.nets[0] = res.cell->ports[ctx->id("I0")].net;
	res.nets[1] = res.cell->ports[ctx->id("I1")].net;
	res.nets[2] = res.cell->ports[ctx->id("I2")].net;
	res.nets[3] = res.cell->ports[ctx->id("I3")].net;
	res.nets[4] = res.cell->ports[ctx->id("I4")].net;
	res.init = res.cell->params[ctx->id("INIT")];
    } else if (res.cell->type == ctx->id("LUT6")) {
	res.nbits = 6;
	res.nets[0] = res.cell->ports[ctx->id("I0")].net;
	res.nets[1] = res.cell->ports[ctx->id("I1")].net;
	res.nets[2] = res.cell->ports[ctx->id("I2")].net;
	res.nets[3] = res.cell->ports[ctx->id("I3")].net;
	res.nets[4] = res.cell->ports[ctx->id("I4")].net;
	res.nets[5] = res.cell->ports[ctx->id("I5")].net;
	res.init = res.cell->params[ctx->id("INIT")];
    } else {
	res.cell = nullptr;
	res.nbits = 1;
	res.nets[0] = net;
	res.init = Property(2, 2);
    }
    return res;
}

void kill_lut(Context *ctx, const LutData &ld, std::unordered_set<IdString> &todelete_cells) {
    if (!ld.cell)
	return;
    todelete_cells.insert(ld.cell->name);
    for (auto &port : ld.cell->ports)
	if (port.second.net)
	    disconnect_port(ctx, ld.cell, port.first);
}

CellInfo *convert_lut(Context *ctx, NetInfo *net, std::string name, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                std::unordered_set<IdString> &todelete_cells)
{
    LutData ld = get_lut(ctx, net);
    Property new_init(0, 64);
    for (int i = 0; i < 64; i++) {
        new_init.str[i] = ld.init.str[i % (1 << ld.nbits)];
    }
    new_init.update_intval();
    kill_lut(ctx, ld, todelete_cells);

    std::unique_ptr<CellInfo> lut_cell =
	create_leuctra_cell(ctx, ctx->id("LEUCTRA_LC"), name);
    created_cells.push_back(std::move(lut_cell));
    CellInfo *lut = created_cells.back().get();
    lut->params[ctx->id("INIT")] = std::move(new_init);

    if (ld.nbits >= 1)
        connect_port(ctx, ld.nets[0], lut, ctx->id("I1"));
    if (ld.nbits >= 2)
        connect_port(ctx, ld.nets[1], lut, ctx->id("I2"));
    if (ld.nbits >= 3)
        connect_port(ctx, ld.nets[2], lut, ctx->id("I3"));
    if (ld.nbits >= 4)
        connect_port(ctx, ld.nets[3], lut, ctx->id("I4"));
    if (ld.nbits >= 5)
        connect_port(ctx, ld.nets[4], lut, ctx->id("I5"));
    if (ld.nbits >= 6)
        connect_port(ctx, ld.nets[5], lut, ctx->id("I6"));

    if (ld.cell)
        connect_port(ctx, net, lut, ctx->id("O6"));

    return lut;
}

std::pair<CellInfo *, CellInfo *> convert_muxf7(Context *ctx, NetInfo *net, std::string name, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                std::unordered_set<IdString> &todelete_cells)
{
    CellInfo *drv = net->driver.cell;
    if (drv && drv->type == ctx->id("MUXF7")) {
	// Good.
        NetInfo *net0 = drv->ports.at(ctx->id("I0")).net;
        NetInfo *net1 = drv->ports.at(ctx->id("I1")).net;
        NetInfo *netsel = drv->ports.at(ctx->id("S")).net;
	CellInfo *lc0 = convert_lut(ctx, net0, name + "$i0", created_cells, todelete_cells);
	CellInfo *lc1 = convert_lut(ctx, net1, name + "$i1", created_cells, todelete_cells);
        connect_ports(ctx, lc0, ctx->id("O6"), lc1, ctx->id("DMI0"));
        connect_ports(ctx, lc1, ctx->id("O6"), lc1, ctx->id("DMI1"));
	disconnect_port(ctx, drv, ctx->id("I0"));
	disconnect_port(ctx, drv, ctx->id("I1"));
	disconnect_port(ctx, drv, ctx->id("S"));
	disconnect_port(ctx, drv, ctx->id("O"));
	todelete_cells.insert(drv->name);
        connect_port(ctx, netsel, lc1, ctx->id("XI"));
        connect_port(ctx, net, lc1, ctx->id("MO"));
        lc1->attrs[ctx->id("LOCMASK")] = Property(0x5, 4);
        lc1->attrs[ctx->id("NEEDS_L")] = Property(true);
	lc0->constr_parent = lc1;
	lc0->constr_z = 3;
	lc1->constr_children.push_back(lc0);
	return std::make_pair(lc0, lc1);
    } else {
	// Not so good.
	CellInfo *lc1 = convert_lut(ctx, net, name, created_cells, todelete_cells);
        connect_ports(ctx, lc1, ctx->id("O6"), lc1, ctx->id("DMI1"));
	set_const_port(ctx, lc1, ctx->id("XI"), true, created_cells);
        lc1->attrs[ctx->id("LOCMASK")] = Property(0x5, 4);
        lc1->attrs[ctx->id("NEEDS_L")] = Property(true);
	return std::make_pair(nullptr, lc1);
    }
}

void convert_muxf8(Context *ctx, NetInfo *net, std::string name, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                std::unordered_set<IdString> &todelete_cells)
{
    CellInfo *drv = net->driver.cell;
    CellInfo *lc00, *lc01, *lc10, *lc11;
    if (drv && drv->type == ctx->id("MUXF8")) {
	// Good.
        NetInfo *net0 = drv->ports.at(ctx->id("I0")).net;
        NetInfo *net1 = drv->ports.at(ctx->id("I1")).net;
        NetInfo *netsel = drv->ports.at(ctx->id("S")).net;
	std::tie(lc00, lc01) = convert_muxf7(ctx, net0, name + "$i0", created_cells, todelete_cells);
	std::tie(lc10, lc11) = convert_muxf7(ctx, net1, name + "$i1", created_cells, todelete_cells);
	if (!lc10) {
	    lc10 = convert_lut(ctx, nullptr, name + "$f8", created_cells, todelete_cells);
	    lc10->constr_parent = lc11;
	    lc10->constr_z = 3;
	    lc11->constr_children.push_back(lc10);
	}
        connect_ports(ctx, lc01, ctx->id("DMO"), lc10, ctx->id("DMI0"));
        connect_ports(ctx, lc11, ctx->id("DMO"), lc10, ctx->id("DMI1"));
	disconnect_port(ctx, drv, ctx->id("I0"));
	disconnect_port(ctx, drv, ctx->id("I1"));
	disconnect_port(ctx, drv, ctx->id("S"));
	disconnect_port(ctx, drv, ctx->id("O"));
	todelete_cells.insert(drv->name);
        connect_port(ctx, netsel, lc10, ctx->id("XI"));
        connect_port(ctx, net, lc10, ctx->id("MO"));
        lc11->attrs[ctx->id("LOCMASK")] = Property(0x1, 4);
	lc01->constr_parent = lc11;
	lc01->constr_z = 6;
	lc11->constr_children.push_back(lc01);
    } else {
	NPNR_ASSERT_FALSE("WEIRD MUXF8");
    }
}

CellInfo *convert_carry4(Context *ctx, CellInfo *c4, CellInfo *link, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                std::unordered_set<IdString> &todelete_cells)
{
    NetInfo *co[4];
    NetInfo *xo[4];
    NetInfo *di[4];
    NetInfo *s[4];
    NetInfo *cyinit;
    cyinit = c4->ports[ctx->id("CYINIT")].net;
    s[0] = c4->ports[ctx->id("S[0]")].net;
    s[1] = c4->ports[ctx->id("S[1]")].net;
    s[2] = c4->ports[ctx->id("S[2]")].net;
    s[3] = c4->ports[ctx->id("S[3]")].net;
    di[0] = c4->ports[ctx->id("DI[0]")].net;
    di[1] = c4->ports[ctx->id("DI[1]")].net;
    di[2] = c4->ports[ctx->id("DI[2]")].net;
    di[3] = c4->ports[ctx->id("DI[3]")].net;
    co[0] = c4->ports[ctx->id("CO[0]")].net;
    co[1] = c4->ports[ctx->id("CO[1]")].net;
    co[2] = c4->ports[ctx->id("CO[2]")].net;
    co[3] = c4->ports[ctx->id("CO[3]")].net;
    xo[0] = c4->ports[ctx->id("O[0]")].net;
    xo[1] = c4->ports[ctx->id("O[1]")].net;
    xo[2] = c4->ports[ctx->id("O[2]")].net;
    xo[3] = c4->ports[ctx->id("O[3]")].net;
    disconnect_port(ctx, c4, ctx->id("CO[0]"));
    disconnect_port(ctx, c4, ctx->id("CO[1]"));
    disconnect_port(ctx, c4, ctx->id("CO[2]"));
    disconnect_port(ctx, c4, ctx->id("CO[3]"));
    disconnect_port(ctx, c4, ctx->id("O[0]"));
    disconnect_port(ctx, c4, ctx->id("O[1]"));
    disconnect_port(ctx, c4, ctx->id("O[2]"));
    disconnect_port(ctx, c4, ctx->id("O[3]"));
    disconnect_port(ctx, c4, ctx->id("S[0]"));
    disconnect_port(ctx, c4, ctx->id("S[1]"));
    disconnect_port(ctx, c4, ctx->id("S[2]"));
    disconnect_port(ctx, c4, ctx->id("S[3]"));
    disconnect_port(ctx, c4, ctx->id("DI[0]"));
    disconnect_port(ctx, c4, ctx->id("DI[1]"));
    disconnect_port(ctx, c4, ctx->id("DI[2]"));
    disconnect_port(ctx, c4, ctx->id("DI[3]"));
    disconnect_port(ctx, c4, ctx->id("CI"));
    disconnect_port(ctx, c4, ctx->id("CYINIT"));
    todelete_cells.insert(c4->name);
    CellInfo *lcs[4];
    int num = 0;
    for (int i = 0; i < 4; i++) {
	if (!co[i]->users.size())
	    co[i] = nullptr;
	if (!xo[i]->users.size())
	    xo[i] = nullptr;
	if (co[i] || xo[i])
	    num = i + 1;
    }
    NetInfo *vcc_net = nullptr;
    for (int i = 0; i < num; i++) {
	// XXX more cases
	if (i == 0 && !link) {
	    bool cval, is_const;
	    is_const = get_const_val(ctx, cyinit, cval);
	    if (is_const) {
	        lcs[i] = convert_lut(ctx, s[i], c4->name.str(ctx) + "$lc" + std::to_string(i), created_cells, todelete_cells);
	        connect_port(ctx, di[i], lcs[i], ctx->id("XI"));
	        lcs[i]->params[ctx->id("CYMUX")] = Property("XI");
		if (cval)
	            lcs[i]->params[ctx->id("CYINIT")] = Property("1");
		else
	            lcs[i]->params[ctx->id("CYINIT")] = Property("0");
	    } else {
                std::unique_ptr<CellInfo> lut_cell =
	            create_leuctra_cell(ctx, ctx->id("LEUCTRA_LC"), c4->name.str(ctx) + "$lc" + std::to_string(i));
		created_cells.push_back(std::move(lut_cell));
		lcs[i] = created_cells.back().get();
	        lcs[i]->params[ctx->id("INIT")] = Property::from_string("1010101010101010101010101010101011001100110011001100110011001100");
		connect_port(ctx, s[i], lcs[i], ctx->id("I1"));
		connect_port(ctx, di[i], lcs[i], ctx->id("I2"));
		set_const_port(ctx, lcs[i], ctx->id("RA6"), true, created_cells);
		connect_port(ctx, cyinit, lcs[i], ctx->id("XI"));
	        lcs[i]->params[ctx->id("CYINIT")] = Property("XI");
	        lcs[i]->params[ctx->id("CYMUX")] = Property("O5");
	    }
            lcs[0]->attrs[ctx->id("LOCMASK")] = Property(1, 4);
            lcs[0]->attrs[ctx->id("NEEDS_L")] = Property(true);
	} else {
	    lcs[i] = convert_lut(ctx, s[i], c4->name.str(ctx) + "$lc" + std::to_string(i), created_cells, todelete_cells);
	    connect_port(ctx, di[i], lcs[i], ctx->id("XI"));
	    lcs[i]->params[ctx->id("CYMUX")] = Property("XI");

	    connect_ports(ctx, link, ctx->id("DCO"), lcs[i], ctx->id("DCI"));
	    lcs[i]->constr_parent = link;
	    if (i == 0) {
		// XXX does this work lol
	        //lcs[i]->constr_spec = 0;
	        lcs[i]->constr_z = -9;
	        lcs[i]->constr_y = 1;
	    } else {
	        lcs[i]->constr_z = 3;
	    }
	    link->constr_children.push_back(lcs[i]);
	}
	if (xo[i])
	    connect_port(ctx, xo[i], lcs[i], ctx->id("XO"));
	if (co[i]) {
	    if (!xo[i]) {
	        connect_port(ctx, co[i], lcs[i], ctx->id("CO"));
	    } else {
                std::unique_ptr<CellInfo> ff_cell =
	            create_leuctra_cell(ctx, ctx->id("LEUCTRA_FF"), c4->name.str(ctx) + "$lc" + std::to_string(i) + "$ff");
		created_cells.push_back(std::move(ff_cell));
		CellInfo *ff = created_cells.back().get();
		ff->constr_parent = lcs[i];
		ff->constr_z = 1;
		lcs[i]->constr_children.push_back(ff);
		ff->params[ctx->id("MODE")] = Property("COMB");
		ff->params[ctx->id("CLKINV")] = Property("CLK_B");
		if (vcc_net) {
		    connect_port(ctx, vcc_net, ff, ctx->id("CLK"));
		} else {
		    set_const_port(ctx, ff, ctx->id("CLK"), true, created_cells);
		    vcc_net = ff->ports[ctx->id("CLK")].net;
		}
	        connect_ports(ctx, lcs[i], ctx->id("CO"), ff, ctx->id("D"));
	        connect_port(ctx, co[i], ff, ctx->id("Q"));
	    }
	}
	link = lcs[i];
    }
    return link;
}

void insert_ilogic_pass(Context *ctx, CellInfo *iob, CellInfo *ilogic)
{
    replace_port(iob, ctx->id("I"), ilogic, ctx->id("FABRICOUT"));
    connect_ports(ctx, iob, ctx->id("I"), ilogic, ctx->id("D"));
    ilogic->params[ctx->id("IMUX")] = Property("1");
    ilogic->params[ctx->id("FABRICOUTUSED")] = Property("0");
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
    ologic->params[ctx->id("OMUX")] = Property("D1");
    ologic->params[ctx->id("D1USED")] = Property("0");
    ologic->params[ctx->id("O1USED")] = Property("0");
    if (net_t != nullptr) {
        replace_port(iob, ctx->id("T"), ologic, ctx->id("T1"));
        connect_ports(ctx, ologic, ctx->id("TQ"), iob, ctx->id("T"));
        ologic->params[ctx->id("TMUX")] = Property("T1");
        ologic->params[ctx->id("T1USED")] = Property("0");
    }
    ologic->constr_parent = iob;
    iob->constr_children.push_back(ologic);
    // XXX enum
    ologic->constr_spec = 2;
}

bool get_const_val(Context *ctx, NetInfo *net, bool &out) {
    if (!net->driver.cell)
	return false;
    if (net->driver.cell->type == ctx->id("GND")) {
	out = false;
	return true;
    }
    if (net->driver.cell->type == ctx->id("VCC")) {
	out = true;
	return true;
    }
    return false;
}

void set_const_port(Context *ctx, CellInfo *cell, IdString port, bool val, std::vector<std::unique_ptr<CellInfo>> &new_cells) {
    if (!cell->ports.count(port)) {
        cell->ports[port].name = port;
        cell->ports[port].type = PORT_IN;
    }

    std::unique_ptr<CellInfo> const_cell{new CellInfo};
    std::unique_ptr<NetInfo> const_net{new NetInfo};
    IdString name = ctx->id(cell->name.str(ctx) + "$const$" + port.str(ctx));
    IdString const_port;
    if (val) {
        const_cell->type = ctx->id("VCC");
	const_port = ctx->id("P");
    } else {
        const_cell->type = ctx->id("GND");
	const_port = ctx->id("G");
    }
    const_cell->name = name;
    const_net->name = name;
    const_cell->ports[const_port].type = PORT_OUT;
    connect_port(ctx, const_net.get(), const_cell.get(), const_port);
    connect_port(ctx, const_net.get(), cell, port);
    ctx->nets[name] = std::move(const_net);
    new_cells.push_back(std::move(const_cell));
}

bool get_invertible_port(Context *ctx, CellInfo *cell, IdString port, bool invert, bool invertible, NetInfo *&net, bool &invert_out) {
    invert_out = invert;
    if (!cell->ports.count(port))
	return false;
    net = cell->ports.at(port).net;
    if (!net)
	return false;
    disconnect_port(ctx, cell, port);
    // XXX support buses
    IdString param_name = ctx->id("IS_" + port.str(ctx) + "_INVERTED");
    if (cell->params.count(param_name)) {
	Property val = cell->params[param_name];
	invert_out ^= val.as_bool();
	cell->params.erase(param_name);
    }

    if (invertible) {
	    while (net->driver.cell && net->driver.cell->type == ctx->id("INV")) {
		CellInfo *icell = net->driver.cell;
		if (!icell->ports.count(ctx->id("I")))
		    return false;
		net = icell->ports.at(ctx->id("I")).net;
		if (!net)
		    return false;
		invert_out ^= true;
	    }
    }
    return true;
}

void set_invertible_port(Context *ctx, CellInfo *cell, IdString port, NetInfo *net, bool invert, bool invertible, std::vector<std::unique_ptr<CellInfo>> &new_cells) {
    if (!net)
	return;
    cell->ports[port].name = port;
    cell->ports[port].type = PORT_IN;

    if (invert && !invertible) {
        std::unique_ptr<CellInfo> inv_cell{new CellInfo};
        std::unique_ptr<NetInfo> inv_net{new NetInfo};
        IdString name = ctx->id(cell->name.str(ctx) + "$inv$" + port.str(ctx));
        inv_cell->type = ctx->id("INV");
        inv_cell->name = name;
        inv_net->name = name;
	add_port(ctx, inv_cell.get(), "O", PORT_OUT);
	add_port(ctx, inv_cell.get(), "I", PORT_IN);
        connect_port(ctx, inv_net.get(), inv_cell.get(), ctx->id("O"));
        connect_port(ctx, inv_net.get(), cell, port);
	connect_port(ctx, net, inv_cell.get(), ctx->id("I"));
        ctx->nets[name] = std::move(inv_net);
        new_cells.push_back(std::move(inv_cell));
    } else {
        connect_port(ctx, net, cell, port);
	if (invertible) {
	    std::string val;
	    if (invert)
		val = port.str(ctx) + "_B";
	    else
		val = port.str(ctx);
	    IdString param = ctx->id(port.str(ctx) + "INV");
	    cell->params[param] = Property(val);
	}
    }
}

bool handle_invertible_port(Context *ctx, CellInfo *cell, IdString port, bool invert, bool invertible, std::vector<std::unique_ptr<CellInfo>> &new_cells) {
	NetInfo *net;
	bool net_inv;
	if (get_invertible_port(ctx, cell, port, invert, invertible, net, net_inv)) {
		set_invertible_port(ctx, cell, port, net, net_inv, invertible, new_cells);
		return true;
	} else {
		return false;
	}
}

void fixup_ramb16(Context *ctx, CellInfo *cell, std::vector<std::unique_ptr<CellInfo>> &new_cells,
                std::unordered_set<IdString> &todelete_cells) {
	bool swizzle = false;
	if (cell->params.count(ctx->id("DATA_WIDTH_A")) && cell->params.at(ctx->id("DATA_WIDTH_A")).as_int64() == 36)
		swizzle = true;
	if (cell->params.count(ctx->id("DATA_WIDTH_B")) && cell->params.at(ctx->id("DATA_WIDTH_B")).as_int64() == 36)
		swizzle = true;
	if (!cell->params.count(ctx->id("RAM_MODE")))
		cell->params[ctx->id("RAM_MODE")] = Property("TDP");
	if (!cell->params.count(ctx->id("EN_RSTRAM_A")))
		cell->params[ctx->id("EN_RSTRAM_A")] = Property("TRUE");
	if (!cell->params.count(ctx->id("EN_RSTRAM_B")))
		cell->params[ctx->id("EN_RSTRAM_B")] = Property("TRUE");
	if (!cell->params.count(ctx->id("RST_PRIORITY_A")))
		cell->params[ctx->id("RST_PRIORITY_A")] = Property("CE");
	if (!cell->params.count(ctx->id("RST_PRIORITY_B")))
		cell->params[ctx->id("RST_PRIORITY_B")] = Property("CE");
	handle_invertible_port(ctx, cell, ctx->id("CLKA"), false, true, new_cells);
	handle_invertible_port(ctx, cell, ctx->id("CLKB"), false, true, new_cells);
	handle_invertible_port(ctx, cell, ctx->id("ENA"), false, true, new_cells);
	handle_invertible_port(ctx, cell, ctx->id("ENB"), false, true, new_cells);
	handle_invertible_port(ctx, cell, ctx->id("REGCEA"), false, true, new_cells);
	handle_invertible_port(ctx, cell, ctx->id("REGCEB"), false, true, new_cells);
	handle_invertible_port(ctx, cell, ctx->id("RSTA"), false, true, new_cells);
	handle_invertible_port(ctx, cell, ctx->id("RSTB"), false, true, new_cells);
	for (int i = 0; i < 32; i++) {
		rename_port(ctx, cell, ctx->id("DOA[" + std::to_string(i) + "]"), ctx->id("DOA" + std::to_string(i)));
		rename_port(ctx, cell, ctx->id("DOB[" + std::to_string(i) + "]"), ctx->id("DOB" + std::to_string(i)));
		rename_port(ctx, cell, ctx->id("DIA[" + std::to_string(i) + "]"), ctx->id("DIA" + std::to_string(i)));
		rename_port(ctx, cell, ctx->id("DIB[" + std::to_string(i) + "]"), ctx->id("DIB" + std::to_string(i)));
	}
	for (int i = 0; i < 4; i++) {
		rename_port(ctx, cell, ctx->id("DOPA[" + std::to_string(i) + "]"), ctx->id("DOPA" + std::to_string(i)));
		rename_port(ctx, cell, ctx->id("DOPB[" + std::to_string(i) + "]"), ctx->id("DOPB" + std::to_string(i)));
		rename_port(ctx, cell, ctx->id("DIPA[" + std::to_string(i) + "]"), ctx->id("DIPA" + std::to_string(i)));
		rename_port(ctx, cell, ctx->id("DIPB[" + std::to_string(i) + "]"), ctx->id("DIPB" + std::to_string(i)));
	}
	for (int i = 0; i < 14; i++) {
		int si = i;
		if (swizzle) {
			if (i == 4)
				si = 13;
			else if (i == 13)
				si = 4;
		}
		rename_port(ctx, cell, ctx->id("ADDRA[" + std::to_string(i) + "]"), ctx->id("ADDRA" + std::to_string(si)));
		rename_port(ctx, cell, ctx->id("ADDRB[" + std::to_string(i) + "]"), ctx->id("ADDRB" + std::to_string(si)));
	}
	for (int i = 0; i < 4; i++) {
		NetInfo *net;
		bool net_inv;
		if (get_invertible_port(ctx, cell, ctx->id("WEA[" + std::to_string(i) + "]"), false, true, net, net_inv)) {
			set_invertible_port(ctx, cell, ctx->id("WEA" + std::to_string(i)),  net, net_inv, true, new_cells);
		}
		if (get_invertible_port(ctx, cell, ctx->id("WEB[" + std::to_string(i) + "]"), false, true, net, net_inv)) {
			set_invertible_port(ctx, cell, ctx->id("WEB" + std::to_string(i)),  net, net_inv, true, new_cells);
		}
	}
}

void fixup_ramb8(Context *ctx, CellInfo *cell, std::vector<std::unique_ptr<CellInfo>> &new_cells,
                std::unordered_set<IdString> &todelete_cells) {
	if (!cell->params.count(ctx->id("RAM_MODE")))
		cell->params[ctx->id("RAM_MODE")] = Property("TDP");
	if (!cell->params.count(ctx->id("EN_RSTRAM_A")))
		cell->params[ctx->id("EN_RSTRAM_A")] = Property("TRUE");
	if (!cell->params.count(ctx->id("EN_RSTRAM_B")))
		cell->params[ctx->id("EN_RSTRAM_B")] = Property("TRUE");
	if (!cell->params.count(ctx->id("RST_PRIORITY_A")))
		cell->params[ctx->id("RST_PRIORITY_A")] = Property("CE");
	if (!cell->params.count(ctx->id("RST_PRIORITY_B")))
		cell->params[ctx->id("RST_PRIORITY_B")] = Property("CE");
	handle_invertible_port(ctx, cell, ctx->id("CLKAWRCLK"), false, true, new_cells);
	handle_invertible_port(ctx, cell, ctx->id("CLKBRDCLK"), false, true, new_cells);
	handle_invertible_port(ctx, cell, ctx->id("ENAWREN"), false, true, new_cells);
	handle_invertible_port(ctx, cell, ctx->id("ENBRDEN"), false, true, new_cells);
	handle_invertible_port(ctx, cell, ctx->id("REGCEA"), false, true, new_cells);
	handle_invertible_port(ctx, cell, ctx->id("REGCEBREGCE"), false, true, new_cells);
	handle_invertible_port(ctx, cell, ctx->id("RSTA"), false, true, new_cells);
	handle_invertible_port(ctx, cell, ctx->id("RSTBRST"), false, true, new_cells);
	for (int i = 0; i < 16; i++) {
		rename_port(ctx, cell, ctx->id("DOADO[" + std::to_string(i) + "]"), ctx->id("DOADO" + std::to_string(i)));
		rename_port(ctx, cell, ctx->id("DOBDO[" + std::to_string(i) + "]"), ctx->id("DOBDO" + std::to_string(i)));
		rename_port(ctx, cell, ctx->id("DIADI[" + std::to_string(i) + "]"), ctx->id("DIADI" + std::to_string(i)));
		rename_port(ctx, cell, ctx->id("DIBDI[" + std::to_string(i) + "]"), ctx->id("DIBDI" + std::to_string(i)));
	}
	for (int i = 0; i < 2; i++) {
		rename_port(ctx, cell, ctx->id("DOPADOP[" + std::to_string(i) + "]"), ctx->id("DOPADOP" + std::to_string(i)));
		rename_port(ctx, cell, ctx->id("DOPBDOP[" + std::to_string(i) + "]"), ctx->id("DOPBDOP" + std::to_string(i)));
		rename_port(ctx, cell, ctx->id("DIPADIP[" + std::to_string(i) + "]"), ctx->id("DIPADIP" + std::to_string(i)));
		rename_port(ctx, cell, ctx->id("DIPBDIP[" + std::to_string(i) + "]"), ctx->id("DIPBDIP" + std::to_string(i)));
	}
	for (int i = 0; i < 13; i++) {
		rename_port(ctx, cell, ctx->id("ADDRAWRADDR[" + std::to_string(i) + "]"), ctx->id("ADDRAWRADDR" + std::to_string(i)));
		rename_port(ctx, cell, ctx->id("ADDRBRDADDR[" + std::to_string(i) + "]"), ctx->id("ADDRBRDADDR" + std::to_string(i)));
	}
	for (int i = 0; i < 2; i++) {
		NetInfo *net;
		bool net_inv;
		if (get_invertible_port(ctx, cell, ctx->id("WEAWEL[" + std::to_string(i) + "]"), false, true, net, net_inv)) {
			set_invertible_port(ctx, cell, ctx->id("WEAWEL" + std::to_string(i)),  net, net_inv, true, new_cells);
		}
		if (get_invertible_port(ctx, cell, ctx->id("WEBWEU[" + std::to_string(i) + "]"), false, true, net, net_inv)) {
			set_invertible_port(ctx, cell, ctx->id("WEBWEU" + std::to_string(i)),  net, net_inv, true, new_cells);
		}
	}
}


NEXTPNR_NAMESPACE_END
