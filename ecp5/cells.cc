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

std::unique_ptr<CellInfo> create_ecp5_cell(Context *ctx, IdString type, std::string name)
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

    if (type == ctx->id("TRELLIS_SLICE")) {
        new_cell->params[ctx->id("MODE")] = std::string("LOGIC");
        new_cell->params[ctx->id("GSR")] = std::string("DISABLED");
        new_cell->params[ctx->id("SRMODE")] = std::string("LSR_OVER_CE");
        new_cell->params[ctx->id("CEMUX")] = std::string("1");
        new_cell->params[ctx->id("CLKMUX")] = std::string("CLK");
        new_cell->params[ctx->id("LSRMUX")] = std::string("LSR");
        new_cell->params[ctx->id("LUT0_INITVAL")] = Property(0, 16);
        new_cell->params[ctx->id("LUT1_INITVAL")] = Property(0, 16);
        new_cell->params[ctx->id("REG0_SD")] = std::string("0");
        new_cell->params[ctx->id("REG1_SD")] = std::string("0");
        new_cell->params[ctx->id("REG0_REGSET")] = std::string("RESET");
        new_cell->params[ctx->id("REG1_REGSET")] = std::string("RESET");
        new_cell->params[ctx->id("CCU2_INJECT1_0")] = std::string("NO");
        new_cell->params[ctx->id("CCU2_INJECT1_1")] = std::string("NO");
        new_cell->params[ctx->id("WREMUX")] = std::string("WRE");

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
        new_cell->params[ctx->id("DIR")] = std::string("INPUT");
        new_cell->attrs[ctx->id("IO_TYPE")] = std::string("LVCMOS33");
        new_cell->params[ctx->id("DATAMUX_ODDR")] = std::string("PADDO");
        new_cell->params[ctx->id("DATAMUX_MDDR")] = std::string("PADDO");

        add_port(ctx, new_cell.get(), "B", PORT_INOUT);
        add_port(ctx, new_cell.get(), "I", PORT_IN);
        add_port(ctx, new_cell.get(), "T", PORT_IN);
        add_port(ctx, new_cell.get(), "O", PORT_OUT);

        add_port(ctx, new_cell.get(), "IOLDO", PORT_IN);
        add_port(ctx, new_cell.get(), "IOLTO", PORT_IN);

    } else if (type == ctx->id("LUT4")) {
        new_cell->params[ctx->id("INIT")] = Property(0, 16);

        add_port(ctx, new_cell.get(), "A", PORT_IN);
        add_port(ctx, new_cell.get(), "B", PORT_IN);
        add_port(ctx, new_cell.get(), "C", PORT_IN);
        add_port(ctx, new_cell.get(), "D", PORT_IN);
        add_port(ctx, new_cell.get(), "Z", PORT_OUT);
    } else if (type == ctx->id("CCU2C")) {
        new_cell->params[ctx->id("INIT0")] = Property(0, 16);
        new_cell->params[ctx->id("INIT1")] = Property(0, 16);
        new_cell->params[ctx->id("INJECT1_0")] = std::string("YES");
        new_cell->params[ctx->id("INJECT1_1")] = std::string("YES");

        add_port(ctx, new_cell.get(), "CIN", PORT_IN);

        add_port(ctx, new_cell.get(), "A0", PORT_IN);
        add_port(ctx, new_cell.get(), "B0", PORT_IN);
        add_port(ctx, new_cell.get(), "C0", PORT_IN);
        add_port(ctx, new_cell.get(), "D0", PORT_IN);

        add_port(ctx, new_cell.get(), "A1", PORT_IN);
        add_port(ctx, new_cell.get(), "B1", PORT_IN);
        add_port(ctx, new_cell.get(), "C1", PORT_IN);
        add_port(ctx, new_cell.get(), "D1", PORT_IN);

        add_port(ctx, new_cell.get(), "S0", PORT_OUT);
        add_port(ctx, new_cell.get(), "S1", PORT_OUT);
        add_port(ctx, new_cell.get(), "COUT", PORT_OUT);

    } else if (type == ctx->id("DCCA")) {
        add_port(ctx, new_cell.get(), "CLKI", PORT_IN);
        add_port(ctx, new_cell.get(), "CLKO", PORT_OUT);
        add_port(ctx, new_cell.get(), "CE", PORT_IN);
    } else if (type == id_IOLOGIC || type == id_SIOLOGIC) {
        new_cell->params[ctx->id("MODE")] = std::string("NONE");
        new_cell->params[ctx->id("GSR")] = std::string("DISABLED");
        new_cell->params[ctx->id("CLKIMUX")] = std::string("CLK");
        new_cell->params[ctx->id("CLKOMUX")] = std::string("CLK");
        new_cell->params[ctx->id("LSRIMUX")] = std::string("0");
        new_cell->params[ctx->id("LSROMUX")] = std::string("0");
        new_cell->params[ctx->id("LSRMUX")] = std::string("LSR");

        new_cell->params[ctx->id("DELAY.OUTDEL")] = std::string("DISABLED");
        new_cell->params[ctx->id("DELAY.DEL_VALUE")] = Property(0, 7);
        new_cell->params[ctx->id("DELAY.WAIT_FOR_EDGE")] = std::string("DISABLED");

        if (type == id_IOLOGIC) {
            new_cell->params[ctx->id("IDDRXN.MODE")] = std::string("NONE");
            new_cell->params[ctx->id("ODDRXN.MODE")] = std::string("NONE");

            new_cell->params[ctx->id("MIDDRX.MODE")] = std::string("NONE");
            new_cell->params[ctx->id("MODDRX.MODE")] = std::string("NONE");
            new_cell->params[ctx->id("MTDDRX.MODE")] = std::string("NONE");

            new_cell->params[ctx->id("IOLTOMUX")] = std::string("NONE");
            new_cell->params[ctx->id("MTDDRX.DQSW_INVERT")] = std::string("DISABLED");
            new_cell->params[ctx->id("MTDDRX.REGSET")] = std::string("RESET");

            new_cell->params[ctx->id("MIDDRX_MODDRX.WRCLKMUX")] = std::string("NONE");
        }
        // Just copy ports from the Bel
        copy_bel_ports();
    } else if (type == id_TRELLIS_ECLKBUF) {
        add_port(ctx, new_cell.get(), "ECLKI", PORT_IN);
        add_port(ctx, new_cell.get(), "ECLKO", PORT_OUT);
    } else {
        log_error("unable to create ECP5 cell of type %s", type.c_str(ctx));
    }
    return new_cell;
}

static void set_param_safe(bool has_ff, CellInfo *lc, IdString name, const std::string &value)
{
    NPNR_ASSERT(!has_ff || lc->params.at(name) == value);
    lc->params[name] = value;
}

static void replace_port_safe(bool has_ff, CellInfo *ff, IdString ff_port, CellInfo *lc, IdString lc_port)
{
    if (has_ff) {
        NPNR_ASSERT(lc->ports.at(lc_port).net == ff->ports.at(ff_port).net);
        NetInfo *ffnet = ff->ports.at(ff_port).net;
        if (ffnet != nullptr)
            ffnet->users.erase(
                    std::remove_if(ffnet->users.begin(), ffnet->users.end(),
                                   [ff, ff_port](PortRef port) { return port.cell == ff && port.port == ff_port; }),
                    ffnet->users.end());
    } else {
        replace_port(ff, ff_port, lc, lc_port);
    }
}

void ff_to_slice(Context *ctx, CellInfo *ff, CellInfo *lc, int index, bool driven_by_lut)
{
    if (lc->hierpath == IdString())
        lc->hierpath = ff->hierpath;
    bool has_ff = lc->ports.at(ctx->id("Q0")).net != nullptr || lc->ports.at(ctx->id("Q1")).net != nullptr;
    std::string reg = "REG" + std::to_string(index);
    set_param_safe(has_ff, lc, ctx->id("SRMODE"), str_or_default(ff->params, ctx->id("SRMODE"), "LSR_OVER_CE"));
    set_param_safe(has_ff, lc, ctx->id("GSR"), str_or_default(ff->params, ctx->id("GSR"), "DISABLED"));
    set_param_safe(has_ff, lc, ctx->id("CEMUX"), str_or_default(ff->params, ctx->id("CEMUX"), "1"));
    set_param_safe(has_ff, lc, ctx->id("LSRMUX"), str_or_default(ff->params, ctx->id("LSRMUX"), "LSR"));
    set_param_safe(has_ff, lc, ctx->id("CLKMUX"), str_or_default(ff->params, ctx->id("CLKMUX"), "CLK"));

    lc->params[ctx->id(reg + "_SD")] = std::string(driven_by_lut ? "1" : "0");
    lc->params[ctx->id(reg + "_REGSET")] = str_or_default(ff->params, ctx->id("REGSET"), "RESET");
    lc->params[ctx->id(reg + "_LSRMODE")] = str_or_default(ff->params, ctx->id("LSRMODE"), "LSR");
    replace_port_safe(has_ff, ff, ctx->id("CLK"), lc, ctx->id("CLK"));
    if (ff->ports.find(ctx->id("LSR")) != ff->ports.end())
        replace_port_safe(has_ff, ff, ctx->id("LSR"), lc, ctx->id("LSR"));
    if (ff->ports.find(ctx->id("CE")) != ff->ports.end())
        replace_port_safe(has_ff, ff, ctx->id("CE"), lc, ctx->id("CE"));

    replace_port(ff, ctx->id("Q"), lc, ctx->id("Q" + std::to_string(index)));
    if (get_net_or_empty(ff, ctx->id("M")) != nullptr) {
        // PRLD FFs that use both M and DI
        NPNR_ASSERT(!driven_by_lut);
        // As M is used; must route DI through a new LUT
        lc->params[ctx->id(reg + "_SD")] = std::string("1");
        lc->params[ctx->id("LUT" + std::to_string(index) + "_INITVAL")] = Property(0xFF00, 16);
        replace_port(ff, ctx->id("DI"), lc, ctx->id("D" + std::to_string(index)));
        replace_port(ff, ctx->id("M"), lc, ctx->id("M" + std::to_string(index)));
        connect_ports(ctx, lc, ctx->id("F" + std::to_string(index)), lc, ctx->id("DI" + std::to_string(index)));
    } else {
        if (driven_by_lut) {
            replace_port(ff, ctx->id("DI"), lc, ctx->id("DI" + std::to_string(index)));
        } else {
            replace_port(ff, ctx->id("DI"), lc, ctx->id("M" + std::to_string(index)));
        }
    }
}

void lut_to_slice(Context *ctx, CellInfo *lut, CellInfo *lc, int index)
{
    if (lc->hierpath == IdString())
        lc->hierpath = lut->hierpath;
    lc->params[ctx->id("LUT" + std::to_string(index) + "_INITVAL")] =
            get_or_default(lut->params, ctx->id("INIT"), Property(0, 16));
    replace_port(lut, ctx->id("A"), lc, ctx->id("A" + std::to_string(index)));
    replace_port(lut, ctx->id("B"), lc, ctx->id("B" + std::to_string(index)));
    replace_port(lut, ctx->id("C"), lc, ctx->id("C" + std::to_string(index)));
    replace_port(lut, ctx->id("D"), lc, ctx->id("D" + std::to_string(index)));
    replace_port(lut, ctx->id("Z"), lc, ctx->id("F" + std::to_string(index)));
}

void ccu2c_to_slice(Context *ctx, CellInfo *ccu, CellInfo *lc)
{
    if (lc->hierpath == IdString())
        lc->hierpath = ccu->hierpath;
    lc->params[ctx->id("MODE")] = std::string("CCU2");
    lc->params[ctx->id("LUT0_INITVAL")] = get_or_default(ccu->params, ctx->id("INIT0"), Property(0, 16));
    lc->params[ctx->id("LUT1_INITVAL")] = get_or_default(ccu->params, ctx->id("INIT1"), Property(0, 16));

    lc->params[ctx->id("CCU2_INJECT1_0")] = str_or_default(ccu->params, ctx->id("INJECT1_0"), "YES");
    lc->params[ctx->id("CCU2_INJECT1_1")] = str_or_default(ccu->params, ctx->id("INJECT1_1"), "YES");

    replace_port(ccu, ctx->id("CIN"), lc, ctx->id("FCI"));

    replace_port(ccu, ctx->id("A0"), lc, ctx->id("A0"));
    replace_port(ccu, ctx->id("B0"), lc, ctx->id("B0"));
    replace_port(ccu, ctx->id("C0"), lc, ctx->id("C0"));
    replace_port(ccu, ctx->id("D0"), lc, ctx->id("D0"));

    replace_port(ccu, ctx->id("A1"), lc, ctx->id("A1"));
    replace_port(ccu, ctx->id("B1"), lc, ctx->id("B1"));
    replace_port(ccu, ctx->id("C1"), lc, ctx->id("C1"));
    replace_port(ccu, ctx->id("D1"), lc, ctx->id("D1"));

    replace_port(ccu, ctx->id("S0"), lc, ctx->id("F0"));
    replace_port(ccu, ctx->id("S1"), lc, ctx->id("F1"));

    replace_port(ccu, ctx->id("COUT"), lc, ctx->id("FCO"));
}

void dram_to_ramw(Context *ctx, CellInfo *ram, CellInfo *lc)
{
    if (lc->hierpath == IdString())
        lc->hierpath = ram->hierpath;
    lc->params[ctx->id("MODE")] = std::string("RAMW");
    replace_port(ram, ctx->id("WAD[0]"), lc, ctx->id("D0"));
    replace_port(ram, ctx->id("WAD[1]"), lc, ctx->id("B0"));
    replace_port(ram, ctx->id("WAD[2]"), lc, ctx->id("C0"));
    replace_port(ram, ctx->id("WAD[3]"), lc, ctx->id("A0"));

    replace_port(ram, ctx->id("DI[0]"), lc, ctx->id("C1"));
    replace_port(ram, ctx->id("DI[1]"), lc, ctx->id("A1"));
    replace_port(ram, ctx->id("DI[2]"), lc, ctx->id("D1"));
    replace_port(ram, ctx->id("DI[3]"), lc, ctx->id("B1"));
}

static unsigned get_dram_init(const Context *ctx, const CellInfo *ram, int bit)
{
    auto init_prop = get_or_default(ram->params, ctx->id("INITVAL"), Property(0, 64));
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

void dram_to_ram_slice(Context *ctx, CellInfo *ram, CellInfo *lc, CellInfo *ramw, int index)
{
    if (lc->hierpath == IdString())
        lc->hierpath = ram->hierpath;
    lc->params[ctx->id("MODE")] = std::string("DPRAM");
    lc->params[ctx->id("WREMUX")] = str_or_default(ram->params, ctx->id("WREMUX"), "WRE");
    lc->params[ctx->id("WCKMUX")] = str_or_default(ram->params, ctx->id("WCKMUX"), "WCK");

    unsigned permuted_init0 = 0, permuted_init1 = 0;
    unsigned init0 = get_dram_init(ctx, ram, index * 2), init1 = get_dram_init(ctx, ram, index * 2 + 1);

    for (int i = 0; i < 16; i++) {
        int permuted_addr = 0;
        if (i & 1)
            permuted_addr |= 8;
        if (i & 2)
            permuted_addr |= 2;
        if (i & 4)
            permuted_addr |= 4;
        if (i & 8)
            permuted_addr |= 1;
        if (init0 & (1 << permuted_addr))
            permuted_init0 |= (1 << i);
        if (init1 & (1 << permuted_addr))
            permuted_init1 |= (1 << i);
    }

    lc->params[ctx->id("LUT0_INITVAL")] = Property(permuted_init0, 16);
    lc->params[ctx->id("LUT1_INITVAL")] = Property(permuted_init1, 16);

    if (ram->ports.count(ctx->id("RAD[0]"))) {
        connect_port(ctx, ram->ports.at(ctx->id("RAD[0]")).net, lc, ctx->id("D0"));
        connect_port(ctx, ram->ports.at(ctx->id("RAD[0]")).net, lc, ctx->id("D1"));
    }
    if (ram->ports.count(ctx->id("RAD[1]"))) {
        connect_port(ctx, ram->ports.at(ctx->id("RAD[1]")).net, lc, ctx->id("B0"));
        connect_port(ctx, ram->ports.at(ctx->id("RAD[1]")).net, lc, ctx->id("B1"));
    }
    if (ram->ports.count(ctx->id("RAD[2]"))) {
        connect_port(ctx, ram->ports.at(ctx->id("RAD[2]")).net, lc, ctx->id("C0"));
        connect_port(ctx, ram->ports.at(ctx->id("RAD[2]")).net, lc, ctx->id("C1"));
    }
    if (ram->ports.count(ctx->id("RAD[3]"))) {
        connect_port(ctx, ram->ports.at(ctx->id("RAD[3]")).net, lc, ctx->id("A0"));
        connect_port(ctx, ram->ports.at(ctx->id("RAD[3]")).net, lc, ctx->id("A1"));
    }

    if (ram->ports.count(ctx->id("WRE")))
        connect_port(ctx, ram->ports.at(ctx->id("WRE")).net, lc, ctx->id("WRE"));
    if (ram->ports.count(ctx->id("WCK")))
        connect_port(ctx, ram->ports.at(ctx->id("WCK")).net, lc, ctx->id("WCK"));

    connect_ports(ctx, ramw, id_WADO0, lc, id_WAD0);
    connect_ports(ctx, ramw, id_WADO1, lc, id_WAD1);
    connect_ports(ctx, ramw, id_WADO2, lc, id_WAD2);
    connect_ports(ctx, ramw, id_WADO3, lc, id_WAD3);

    if (index == 0) {
        connect_ports(ctx, ramw, id_WDO0, lc, id_WD0);
        connect_ports(ctx, ramw, id_WDO1, lc, id_WD1);

        replace_port(ram, ctx->id("DO[0]"), lc, id_F0);
        replace_port(ram, ctx->id("DO[1]"), lc, id_F1);

    } else if (index == 1) {
        connect_ports(ctx, ramw, id_WDO2, lc, id_WD0);
        connect_ports(ctx, ramw, id_WDO3, lc, id_WD1);

        replace_port(ram, ctx->id("DO[2]"), lc, id_F0);
        replace_port(ram, ctx->id("DO[3]"), lc, id_F1);
    } else {
        NPNR_ASSERT_FALSE("bad DPRAM index");
    }
}

void nxio_to_tr(Context *ctx, CellInfo *nxio, CellInfo *trio, std::vector<std::unique_ptr<CellInfo>> &created_cells,
                std::unordered_set<IdString> &todelete_cells)
{
    if (nxio->type == ctx->id("$nextpnr_ibuf")) {
        trio->params[ctx->id("DIR")] = std::string("INPUT");
        replace_port(nxio, ctx->id("O"), trio, ctx->id("O"));
    } else if (nxio->type == ctx->id("$nextpnr_obuf")) {
        trio->params[ctx->id("DIR")] = std::string("OUTPUT");
        replace_port(nxio, ctx->id("I"), trio, ctx->id("I"));
    } else if (nxio->type == ctx->id("$nextpnr_iobuf")) {
        // N.B. tristate will be dealt with below
        NetInfo *i = get_net_or_empty(nxio, ctx->id("I"));
        if (i == nullptr || i->driver.cell == nullptr)
            trio->params[ctx->id("DIR")] = std::string("INPUT");
        else {
            log_info("%s: %s.%s\n", ctx->nameOf(i), ctx->nameOf(i->driver.cell), ctx->nameOf(i->driver.port));
            trio->params[ctx->id("DIR")] = std::string("BIDIR");
        }
        replace_port(nxio, ctx->id("I"), trio, ctx->id("I"));
        replace_port(nxio, ctx->id("O"), trio, ctx->id("O"));
    } else {
        NPNR_ASSERT(false);
    }
    NetInfo *donet = trio->ports.at(ctx->id("I")).net, *dinet = trio->ports.at(ctx->id("O")).net;

    // Rename I/O nets to avoid conflicts
    if (donet != nullptr && donet->name == nxio->name)
        rename_net(ctx, donet, ctx->id(donet->name.str(ctx) + "$TRELLIS_IO_OUT"));
    if (dinet != nullptr && dinet->name == nxio->name)
        rename_net(ctx, dinet, ctx->id(dinet->name.str(ctx) + "$TRELLIS_IO_IN"));

    if (ctx->nets.count(nxio->name)) {
        int i = 0;
        IdString new_name;
        do {
            new_name = ctx->id(nxio->name.str(ctx) + "$rename$" + std::to_string(i++));
        } while (ctx->nets.count(new_name));
        rename_net(ctx, ctx->nets.at(nxio->name).get(), new_name);
    }

    // Create a new top port net for accurate IO timing analysis and simulation netlists
    if (ctx->ports.count(nxio->name)) {
        IdString tn_netname = nxio->name;
        NPNR_ASSERT(!ctx->nets.count(tn_netname));
        std::unique_ptr<NetInfo> toplevel_net{new NetInfo};
        toplevel_net->name = tn_netname;
        connect_port(ctx, toplevel_net.get(), trio, ctx->id("B"));
        ctx->ports[nxio->name].net = toplevel_net.get();
        ctx->nets[tn_netname] = std::move(toplevel_net);
    }

    CellInfo *tbuf = net_driven_by(
            ctx, donet, [](const Context *ctx, const CellInfo *cell) { return cell->type == ctx->id("$_TBUF_"); },
            ctx->id("Y"));
    if (tbuf) {
        replace_port(tbuf, ctx->id("A"), trio, ctx->id("I"));
        // Need to invert E to form T
        std::unique_ptr<CellInfo> inv_lut = create_ecp5_cell(ctx, ctx->id("LUT4"), trio->name.str(ctx) + "$invert_T");
        replace_port(tbuf, ctx->id("E"), inv_lut.get(), ctx->id("A"));
        inv_lut->params[ctx->id("INIT")] = Property(21845, 16);
        connect_ports(ctx, inv_lut.get(), ctx->id("Z"), trio, ctx->id("T"));
        created_cells.push_back(std::move(inv_lut));

        if (donet->users.size() > 1) {
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
