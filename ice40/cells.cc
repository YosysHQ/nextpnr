/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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
#include "design_utils.h"
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

void add_port(const Context *ctx, CellInfo *cell, std::string name, PortType dir)
{
    IdString id = ctx->id(name);
    cell->ports[id] = PortInfo{id, nullptr, dir};
}

CellInfo *create_ice_cell(Context *ctx, IdString type, std::string name)
{
    static int auto_idx = 0;
    CellInfo *new_cell = new CellInfo();
    if (name.empty()) {
        new_cell->name = ctx->id("$nextpnr_" + type.str(ctx) + "_" + std::to_string(auto_idx++));
    } else {
        new_cell->name = ctx->id(name);
    }
    new_cell->type = type;
    if (type == ctx->id("ICESTORM_LC")) {
        new_cell->params[ctx->id("LUT_INIT")] = "0";
        new_cell->params[ctx->id("NEG_CLK")] = "0";
        new_cell->params[ctx->id("CARRY_ENABLE")] = "0";
        new_cell->params[ctx->id("DFF_ENABLE")] = "0";
        new_cell->params[ctx->id("SET_NORESET")] = "0";
        new_cell->params[ctx->id("ASYNC_SR")] = "0";
        new_cell->params[ctx->id("CIN_CONST")] = "0";
        new_cell->params[ctx->id("CIN_SET")] = "0";

        add_port(ctx, new_cell, "I0", PORT_IN);
        add_port(ctx, new_cell, "I1", PORT_IN);
        add_port(ctx, new_cell, "I2", PORT_IN);
        add_port(ctx, new_cell, "I3", PORT_IN);
        add_port(ctx, new_cell, "CIN", PORT_IN);

        add_port(ctx, new_cell, "CLK", PORT_IN);
        add_port(ctx, new_cell, "CEN", PORT_IN);
        add_port(ctx, new_cell, "SR", PORT_IN);

        add_port(ctx, new_cell, "LO", PORT_OUT);
        add_port(ctx, new_cell, "O", PORT_OUT);
        add_port(ctx, new_cell, "OUT", PORT_OUT);
    } else if (type == ctx->id("SB_IO")) {
        new_cell->params[ctx->id("PIN_TYPE")] = "0";
        new_cell->params[ctx->id("PULLUP")] = "0";
        new_cell->params[ctx->id("NEG_TRIGGER")] = "0";
        new_cell->params[ctx->id("IOSTANDARD")] = "SB_LVCMOS";

        add_port(ctx, new_cell, "PACKAGE_PIN", PORT_INOUT);

        add_port(ctx, new_cell, "LATCH_INPUT_VALUE", PORT_IN);
        add_port(ctx, new_cell, "CLOCK_ENABLE", PORT_IN);
        add_port(ctx, new_cell, "INPUT_CLK", PORT_IN);
        add_port(ctx, new_cell, "OUTPUT_CLK", PORT_IN);

        add_port(ctx, new_cell, "OUTPUT_ENABLE", PORT_IN);
        add_port(ctx, new_cell, "D_OUT_0", PORT_IN);
        add_port(ctx, new_cell, "D_OUT_1", PORT_IN);

        add_port(ctx, new_cell, "D_IN_0", PORT_OUT);
        add_port(ctx, new_cell, "D_IN_1", PORT_OUT);
    } else if (type == ctx->id("ICESTORM_RAM")) {
        new_cell->params[ctx->id("NEG_CLK_W")] = "0";
        new_cell->params[ctx->id("NEG_CLK_R")] = "0";
        new_cell->params[ctx->id("WRITE_MODE")] = "0";
        new_cell->params[ctx->id("READ_MODE")] = "0";

        add_port(ctx, new_cell, "RCLK", PORT_IN);
        add_port(ctx, new_cell, "RCLKE", PORT_IN);
        add_port(ctx, new_cell, "RE", PORT_IN);

        add_port(ctx, new_cell, "WCLK", PORT_IN);
        add_port(ctx, new_cell, "WCLKE", PORT_IN);
        add_port(ctx, new_cell, "WE", PORT_IN);

        for (int i = 0; i < 16; i++) {
            add_port(ctx, new_cell, "WDATA_" + std::to_string(i), PORT_IN);
            add_port(ctx, new_cell, "MASK_" + std::to_string(i), PORT_IN);
            add_port(ctx, new_cell, "RDATA_" + std::to_string(i), PORT_OUT);
        }

        for (int i = 0; i < 11; i++) {
            add_port(ctx, new_cell, "RADDR_" + std::to_string(i), PORT_IN);
            add_port(ctx, new_cell, "WADDR_" + std::to_string(i), PORT_IN);
        }
    } else if (type == ctx->id("ICESTORM_LFOSC")) {
        add_port(ctx, new_cell, "CLKLFEN", PORT_IN);
        add_port(ctx, new_cell, "CLKLFPU", PORT_IN);
        add_port(ctx, new_cell, "CLKLF", PORT_OUT);
        add_port(ctx, new_cell, "CLKLF_FABRIC", PORT_OUT);
    } else if (type == ctx->id("ICESTORM_HFOSC")) {
        new_cell->params[ctx->id("CLKHF_DIV")] = "0";
        new_cell->params[ctx->id("TRIM_EN")] = "0";

        add_port(ctx, new_cell, "CLKHFEN", PORT_IN);
        add_port(ctx, new_cell, "CLKHFPU", PORT_IN);
        add_port(ctx, new_cell, "CLKHF", PORT_OUT);
        add_port(ctx, new_cell, "CLKHF_FABRIC", PORT_OUT);
        for (int i = 0; i < 10; i++)
            add_port(ctx, new_cell, "TRIM" + std::to_string(i), PORT_IN);
    } else if (type == ctx->id("SB_GB")) {
        add_port(ctx, new_cell, "USER_SIGNAL_TO_GLOBAL_BUFFER", PORT_IN);
        add_port(ctx, new_cell, "GLOBAL_BUFFER_OUTPUT", PORT_OUT);
    } else {
        log_error("unable to create iCE40 cell of type %s", type.c_str(ctx));
    }
    return new_cell;
}

void lut_to_lc(const Context *ctx, CellInfo *lut, CellInfo *lc, bool no_dff)
{
    lc->params[ctx->id("LUT_INIT")] = lut->params[ctx->id("LUT_INIT")];
    replace_port(lut, ctx->id("I0"), lc, ctx->id("I0"));
    replace_port(lut, ctx->id("I1"), lc, ctx->id("I1"));
    replace_port(lut, ctx->id("I2"), lc, ctx->id("I2"));
    replace_port(lut, ctx->id("I3"), lc, ctx->id("I3"));
    if (no_dff) {
        replace_port(lut, ctx->id("O"), lc, ctx->id("O"));
        lc->params[ctx->id("DFF_ENABLE")] = "0";
    }
}

void dff_to_lc(const Context *ctx, CellInfo *dff, CellInfo *lc, bool pass_thru_lut)
{
    lc->params[ctx->id("DFF_ENABLE")] = "1";
    std::string config = dff->type.str(ctx).substr(6);
    auto citer = config.begin();
    replace_port(dff, ctx->id("C"), lc, ctx->id("CLK"));

    if (citer != config.end() && *citer == 'N') {
        lc->params[ctx->id("NEG_CLK")] = "1";
        ++citer;
    } else {
        lc->params[ctx->id("NEG_CLK")] = "0";
    }

    if (citer != config.end() && *citer == 'E') {
        replace_port(dff, ctx->id("E"), lc, ctx->id("CEN"));
        ++citer;
    }

    if (citer != config.end()) {
        if ((config.end() - citer) >= 2) {
            char c = *(citer++);
            assert(c == 'S');
            lc->params[ctx->id("ASYNC_SR")] = "0";
        } else {
            lc->params[ctx->id("ASYNC_SR")] = "1";
        }

        if (*citer == 'S') {
            citer++;
            replace_port(dff, ctx->id("S"), lc, ctx->id("SR"));
            lc->params[ctx->id("SET_NORESET")] = "1";
        } else {
            assert(*citer == 'R');
            citer++;
            replace_port(dff, ctx->id("R"), lc, ctx->id("SR"));
            lc->params[ctx->id("SET_NORESET")] = "0";
        }
    }

    assert(citer == config.end());

    if (pass_thru_lut) {
        lc->params[ctx->id("LUT_INIT")] = "2";
        replace_port(dff, ctx->id("D"), lc, ctx->id("I0"));
    }

    replace_port(dff, ctx->id("Q"), lc, ctx->id("O"));
}

void nxio_to_sb(Context *ctx, CellInfo *nxio, CellInfo *sbio)
{
    if (nxio->type == ctx->id("$nextpnr_ibuf")) {
        sbio->params[ctx->id("PIN_TYPE")] = "1";
        auto pu_attr = nxio->attrs.find(ctx->id("PULLUP"));
        if (pu_attr != nxio->attrs.end())
            sbio->params[ctx->id("PULLUP")] = pu_attr->second;
        replace_port(nxio, ctx->id("O"), sbio, ctx->id("D_IN_0"));
    } else if (nxio->type == ctx->id("$nextpnr_obuf")) {
        sbio->params[ctx->id("PIN_TYPE")] = "25";
        replace_port(nxio, ctx->id("I"), sbio, ctx->id("D_OUT_0"));
    } else if (nxio->type == ctx->id("$nextpnr_iobuf")) {
        // N.B. tristate will be dealt with below
        sbio->params[ctx->id("PIN_TYPE")] = "25";
        replace_port(nxio, ctx->id("I"), sbio, ctx->id("D_OUT_0"));
        replace_port(nxio, ctx->id("O"), sbio, ctx->id("D_IN_0"));
    } else {
        assert(false);
    }
    NetInfo *donet = sbio->ports.at(ctx->id("D_OUT_0")).net;
    CellInfo *tbuf = net_driven_by(
            ctx, donet, [](const Context *ctx, const CellInfo *cell) { return cell->type == ctx->id("$_TBUF_"); },
            ctx->id("Y"));
    if (tbuf) {
        sbio->params[ctx->id("PIN_TYPE")] = "41";
        replace_port(tbuf, ctx->id("A"), sbio, ctx->id("D_OUT_0"));
        replace_port(tbuf, ctx->id("E"), sbio, ctx->id("OUTPUT_ENABLE"));
        ctx->nets.erase(donet->name);
        if (!donet->users.empty())
            log_error("unsupported tristate IO pattern for IO buffer '%s', "
                      "instantiate SB_IO manually to ensure correct behaviour\n",
                      nxio->name.c_str(ctx));
        ctx->cells.erase(tbuf->name);
    }
}

bool is_clock_port(const BaseCtx *ctx, const PortRef &port)
{
    if (port.cell == nullptr)
        return false;
    if (is_ff(ctx, port.cell))
        return port.port == ctx->id("C");
    if (port.cell->type == ctx->id("ICESTORM_LC"))
        return port.port == ctx->id("CLK");
    if (is_ram(ctx, port.cell) || port.cell->type == ctx->id("ICESTORM_RAM"))
        return port.port == ctx->id("RCLK") || port.port == ctx->id("WCLK");
    return false;
}

bool is_reset_port(const BaseCtx *ctx, const PortRef &port)
{
    if (port.cell == nullptr)
        return false;
    if (is_ff(ctx, port.cell))
        return port.port == ctx->id("R") || port.port == ctx->id("S");
    if (port.cell->type == ctx->id("ICESTORM_LC"))
        return port.port == ctx->id("SR");
    return false;
}

bool is_enable_port(const BaseCtx *ctx, const PortRef &port)
{
    if (port.cell == nullptr)
        return false;
    if (is_ff(ctx, port.cell))
        return port.port == ctx->id("E");
    if (port.cell->type == ctx->id("ICESTORM_LC"))
        return port.port == ctx->id("CEN");
    return false;
}

NEXTPNR_NAMESPACE_END
