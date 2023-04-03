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

    if (type == id_TRELLIS_COMB) {
        new_cell->params[id_MODE] = std::string("LOGIC");
        new_cell->params[id_INITVAL] = Property(0, 16);
        new_cell->params[id_CCU2_INJECT1] = std::string("NO");
        new_cell->params[id_WREMUX] = std::string("WRE");

        new_cell->addInput(id_A);
        new_cell->addInput(id_B);
        new_cell->addInput(id_C);
        new_cell->addInput(id_D);

        new_cell->addInput(id_M);

        new_cell->addInput(id_F1);
        new_cell->addInput(id_FCI);
        new_cell->addInput(id_FXA);
        new_cell->addInput(id_FXB);

        new_cell->addInput(id_DI0);
        new_cell->addInput(id_DI1);

        new_cell->addInput(id_WD);
        new_cell->addInput(id_WAD0);
        new_cell->addInput(id_WAD1);
        new_cell->addInput(id_WAD2);
        new_cell->addInput(id_WAD3);
        new_cell->addInput(id_WRE);
        new_cell->addInput(id_WCK);

        new_cell->addOutput(id_F);

        new_cell->addOutput(id_FCO);
        new_cell->addOutput(id_OFX);
    } else if (type == id_TRELLIS_RAMW) {
        for (auto i : {id_A0, id_B0, id_C0, id_D0, id_A1, id_B1, id_C1, id_D1})
            new_cell->addInput(i);
        for (auto o : {id_WDO0, id_WDO1, id_WDO2, id_WDO3, id_WADO0, id_WADO1, id_WADO2, id_WADO3})
            new_cell->addOutput(o);
    } else if (type == id_TRELLIS_IO) {
        new_cell->params[id_DIR] = std::string("INPUT");
        new_cell->attrs[id_IO_TYPE] = std::string("LVCMOS33");
        new_cell->params[id_DATAMUX_ODDR] = std::string("PADDO");
        new_cell->params[id_DATAMUX_MDDR] = std::string("PADDO");

        new_cell->addInout(id_B);
        new_cell->addInput(id_I);
        new_cell->addInput(id_T);
        new_cell->addOutput(id_O);

        new_cell->addInput(id_IOLDO);
        new_cell->addInput(id_IOLTO);

    } else if (type == id_LUT4) {
        new_cell->params[id_INIT] = Property(0, 16);

        new_cell->addInput(id_A);
        new_cell->addInput(id_B);
        new_cell->addInput(id_C);
        new_cell->addInput(id_D);
        new_cell->addOutput(id_Z);
    } else if (type == id_CCU2D) {
        new_cell->params[id_INIT0] = Property(0, 16);
        new_cell->params[id_INIT1] = Property(0, 16);
        new_cell->params[id_INJECT1_0] = std::string("YES");
        new_cell->params[id_INJECT1_1] = std::string("YES");

        new_cell->addInput(id_CIN);

        new_cell->addInput(id_A0);
        new_cell->addInput(id_B0);
        new_cell->addInput(id_C0);
        new_cell->addInput(id_D0);

        new_cell->addInput(id_A1);
        new_cell->addInput(id_B1);
        new_cell->addInput(id_C1);
        new_cell->addInput(id_D1);

        new_cell->addOutput(id_S0);
        new_cell->addOutput(id_S1);
        new_cell->addOutput(id_COUT);
    } else {
        log_error("unable to create MachXO2 cell of type %s", type.c_str(ctx));
    }

    return new_cell;
}

static unsigned get_dram_init(const Context *ctx, const CellInfo *ram, int bit)
{
    auto init_prop = get_or_default(ram->params, id_INITVAL, Property(0, 64));
    NPNR_ASSERT(!init_prop.is_string);
    const std::string &idata = init_prop.str;
    NPNR_ASSERT(idata.length() == 64);
    unsigned value = 0;
    for (int i = 0; i < 16; i++) {
        char c = idata.at(4 * i + bit);
        if (c == '1')
            value |= (1 << i);
        else
            NPNR_ASSERT(c == '0' || c == 'x');
    }
    return value;
}

void lut_to_comb(Context *ctx, CellInfo *lut)
{
    lut->type = id_TRELLIS_COMB;
    lut->params[id_INITVAL] = get_or_default(lut->params, id_INIT, Property(0, 16));
    lut->params.erase(id_INIT);
    lut->renamePort(id_Z, id_F);
}

void dram_to_ramw_split(Context *ctx, CellInfo *ram, CellInfo *ramw)
{
    if (ramw->hierpath == IdString())
        ramw->hierpath = ramw->hierpath;
    ram->movePortTo(ctx->id("WAD[0]"), ramw, id_A0);
    ram->movePortTo(ctx->id("WAD[1]"), ramw, id_B0);
    ram->movePortTo(ctx->id("WAD[2]"), ramw, id_C0);
    ram->movePortTo(ctx->id("WAD[3]"), ramw, id_D0);

    ram->movePortTo(ctx->id("DI[0]"), ramw, id_A1);
    ram->movePortTo(ctx->id("DI[1]"), ramw, id_B1);
    ram->movePortTo(ctx->id("DI[2]"), ramw, id_C1);
    ram->movePortTo(ctx->id("DI[3]"), ramw, id_D1);
}

void ccu2_to_comb(Context *ctx, CellInfo *ccu, CellInfo *comb, NetInfo *internal_carry, int i)
{
    std::string ii = std::to_string(i);
    if (comb->hierpath == IdString())
        comb->hierpath = ccu->hierpath;

    comb->params[id_MODE] = std::string("CCU2");
    comb->params[id_INITVAL] = get_or_default(ccu->params, ctx->id("INIT" + ii), Property(0, 16));
    comb->params[id_CCU2_INJECT1] = str_or_default(ccu->params, ctx->id("INJECT1_" + ii), "YES");

    ccu->movePortTo(ctx->id("A" + ii), comb, id_A);
    ccu->movePortTo(ctx->id("B" + ii), comb, id_B);
    ccu->movePortTo(ctx->id("C" + ii), comb, id_C);
    ccu->movePortTo(ctx->id("D" + ii), comb, id_D);

    ccu->movePortTo(ctx->id("S" + ii), comb, id_F);

    if (i == 0) {
        ccu->movePortTo(id_CIN, comb, id_FCI);
        comb->connectPort(id_FCO, internal_carry);
    } else if (i == 1) {
        comb->connectPort(id_FCI, internal_carry);
        ccu->movePortTo(id_COUT, comb, id_FCO);
    } else {
        NPNR_ASSERT_FALSE("bad carry index!");
    }

    for (auto &attr : ccu->attrs)
        comb->attrs[attr.first] = attr.second;
}

void dram_to_comb(Context *ctx, CellInfo *ram, CellInfo *comb, CellInfo *ramw, int index)
{
    if (comb->hierpath == IdString())
        comb->hierpath = ram->hierpath;
    comb->params[id_MODE] = std::string("DPRAM");
    comb->params[id_WREMUX] = str_or_default(ram->params, id_WREMUX, "WRE");
    comb->params[id_WCKMUX] = str_or_default(ram->params, id_WCKMUX, "WCK");

    unsigned init = get_dram_init(ctx, ram, index);

    comb->params[ctx->id("INITVAL")] = Property(init, 16);

    if (ram->ports.count(ctx->id("RAD[0]")))
        comb->connectPort(id_A, ram->ports.at(ctx->id("RAD[0]")).net);

    if (ram->ports.count(ctx->id("RAD[1]")))
        comb->connectPort(id_B, ram->ports.at(ctx->id("RAD[1]")).net);

    if (ram->ports.count(ctx->id("RAD[2]")))
        comb->connectPort(id_C, ram->ports.at(ctx->id("RAD[2]")).net);

    if (ram->ports.count(ctx->id("RAD[3]")))
        comb->connectPort(id_D, ram->ports.at(ctx->id("RAD[3]")).net);

    if (ram->ports.count(id_WRE))
        comb->connectPort(id_WRE, ram->ports.at(id_WRE).net);
    if (ram->ports.count(id_WCK))
        comb->connectPort(id_WCK, ram->ports.at(id_WCK).net);

    ramw->connectPorts(id_WADO0, comb, id_WAD0);
    ramw->connectPorts(id_WADO1, comb, id_WAD1);
    ramw->connectPorts(id_WADO2, comb, id_WAD2);
    ramw->connectPorts(id_WADO3, comb, id_WAD3);

    NPNR_ASSERT(index < 4);
    std::string ii = std::to_string(index);
    ramw->connectPorts(ctx->id("WDO" + ii), comb, id_WD);
    ram->movePortTo(ctx->id("DO[" + ii + "]"), comb, id_F);

    for (auto &attr : ram->attrs)
        comb->attrs[attr.first] = attr.second;
}

void nxio_to_tr(Context *ctx, CellInfo *nxio, CellInfo *trio, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                pool<IdString> &todelete_cells)
{
    if (nxio->type == ctx->id("$nextpnr_ibuf")) {
        trio->params[id_DIR] = std::string("INPUT");
        nxio->movePortTo(id_O, trio, id_O);
    } else if (nxio->type == ctx->id("$nextpnr_obuf")) {
        trio->params[id_DIR] = std::string("OUTPUT");
        nxio->movePortTo(id_I, trio, id_I);
    } else if (nxio->type == ctx->id("$nextpnr_iobuf")) {
        // N.B. tristate will be dealt with below
        NetInfo *i = nxio->getPort(id_I);
        if (i == nullptr || i->driver.cell == nullptr)
            trio->params[id_DIR] = std::string("INPUT");
        else {
            log_info("%s: %s.%s\n", ctx->nameOf(i), ctx->nameOf(i->driver.cell), ctx->nameOf(i->driver.port));
            trio->params[id_DIR] = std::string("BIDIR");
        }
        nxio->movePortTo(id_I, trio, id_I);
        nxio->movePortTo(id_O, trio, id_O);
    } else {
        NPNR_ASSERT(false);
    }
    NetInfo *donet = trio->ports.at(id_I).net, *dinet = trio->ports.at(id_O).net;

    // Rename I/O nets to avoid conflicts
    if (donet != nullptr && donet->name == nxio->name)
        if (donet)
            ctx->renameNet(donet->name, ctx->id(donet->name.str(ctx) + "$TRELLIS_IO_OUT"));
    if (dinet != nullptr && dinet->name == nxio->name)
        if (dinet)
            ctx->renameNet(dinet->name, ctx->id(dinet->name.str(ctx) + "$TRELLIS_IO_IN"));

    if (ctx->nets.count(nxio->name)) {
        int i = 0;
        IdString new_name;
        do {
            new_name = ctx->id(nxio->name.str(ctx) + "$rename$" + std::to_string(i++));
        } while (ctx->nets.count(new_name));
        if (ctx->nets.at(nxio->name).get())
            ctx->renameNet(ctx->nets.at(nxio->name).get()->name, new_name);
    }

    // Create a new top port net for accurate IO timing analysis and simulation netlists
    if (ctx->ports.count(nxio->name)) {
        IdString tn_netname = nxio->name;
        NPNR_ASSERT(!ctx->nets.count(tn_netname));
        ctx->net_aliases.erase(tn_netname);
        NetInfo *toplevel_net = ctx->createNet(tn_netname);
        toplevel_net->name = tn_netname;
        trio->connectPort(id_B, toplevel_net);
        ctx->ports[nxio->name].net = toplevel_net;
    }

    CellInfo *tbuf = net_driven_by(
            ctx, donet, [](const Context *ctx, const CellInfo *cell) { return cell->type == ctx->id("$_TBUF_"); },
            id_Y);
    if (tbuf) {
        tbuf->movePortTo(id_A, trio, id_I);
        // Need to invert E to form T
        std::unique_ptr<CellInfo> inv_lut = create_machxo2_cell(ctx, id_LUT4, trio->name.str(ctx) + "$invert_T");
        tbuf->movePortTo(id_E, inv_lut.get(), id_A);
        inv_lut->params[id_INIT] = Property(21845, 16);
        inv_lut->connectPorts(id_Z, trio, id_T);
        created_cells.push_back(std::move(inv_lut));

        if (donet->users.entries() > 1) {
            for (auto user : donet->users)
                log_info("     remaining tristate user: %s.%s\n", user.cell->name.c_str(ctx), user.port.c_str(ctx));
            log_error("unsupported tristate IO pattern for IO buffer '%s', "
                      "instantiate SB_IO manually to ensure correct behaviour\n",
                      nxio->name.c_str(ctx));
        }
        ctx->nets.erase(donet->name);
        todelete_cells.insert(tbuf->name);
    }
}

NEXTPNR_NAMESPACE_END
