/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@symbioticeda.com>
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
    NPNR_ASSERT(cell->ports.count(id) == 0);
    cell->ports[id] = PortInfo{id, nullptr, dir};
}

std::unique_ptr<CellInfo> create_ice_cell(Context *ctx, IdString type, std::string name)
{
    static int auto_idx = 0;
    std::unique_ptr<CellInfo> new_cell = std::unique_ptr<CellInfo>(new CellInfo());
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

        add_port(ctx, new_cell.get(), "I0", PORT_IN);
        add_port(ctx, new_cell.get(), "I1", PORT_IN);
        add_port(ctx, new_cell.get(), "I2", PORT_IN);
        add_port(ctx, new_cell.get(), "I3", PORT_IN);
        add_port(ctx, new_cell.get(), "CIN", PORT_IN);

        add_port(ctx, new_cell.get(), "CLK", PORT_IN);
        add_port(ctx, new_cell.get(), "CEN", PORT_IN);
        add_port(ctx, new_cell.get(), "SR", PORT_IN);

        add_port(ctx, new_cell.get(), "LO", PORT_OUT);
        add_port(ctx, new_cell.get(), "O", PORT_OUT);
        add_port(ctx, new_cell.get(), "COUT", PORT_OUT);
    } else if (type == ctx->id("SB_IO")) {
        new_cell->params[ctx->id("PIN_TYPE")] = "0";
        new_cell->params[ctx->id("PULLUP")] = "0";
        new_cell->params[ctx->id("NEG_TRIGGER")] = "0";
        new_cell->params[ctx->id("IOSTANDARD")] = "SB_LVCMOS";

        add_port(ctx, new_cell.get(), "PACKAGE_PIN", PORT_INOUT);

        add_port(ctx, new_cell.get(), "LATCH_INPUT_VALUE", PORT_IN);
        add_port(ctx, new_cell.get(), "CLOCK_ENABLE", PORT_IN);
        add_port(ctx, new_cell.get(), "INPUT_CLK", PORT_IN);
        add_port(ctx, new_cell.get(), "OUTPUT_CLK", PORT_IN);

        add_port(ctx, new_cell.get(), "OUTPUT_ENABLE", PORT_IN);
        add_port(ctx, new_cell.get(), "D_OUT_0", PORT_IN);
        add_port(ctx, new_cell.get(), "D_OUT_1", PORT_IN);

        add_port(ctx, new_cell.get(), "D_IN_0", PORT_OUT);
        add_port(ctx, new_cell.get(), "D_IN_1", PORT_OUT);
    } else if (type == ctx->id("ICESTORM_RAM")) {
        new_cell->params[ctx->id("NEG_CLK_W")] = "0";
        new_cell->params[ctx->id("NEG_CLK_R")] = "0";
        new_cell->params[ctx->id("WRITE_MODE")] = "0";
        new_cell->params[ctx->id("READ_MODE")] = "0";

        add_port(ctx, new_cell.get(), "RCLK", PORT_IN);
        add_port(ctx, new_cell.get(), "RCLKE", PORT_IN);
        add_port(ctx, new_cell.get(), "RE", PORT_IN);

        add_port(ctx, new_cell.get(), "WCLK", PORT_IN);
        add_port(ctx, new_cell.get(), "WCLKE", PORT_IN);
        add_port(ctx, new_cell.get(), "WE", PORT_IN);

        for (int i = 0; i < 16; i++) {
            add_port(ctx, new_cell.get(), "WDATA_" + std::to_string(i), PORT_IN);
            add_port(ctx, new_cell.get(), "MASK_" + std::to_string(i), PORT_IN);
            add_port(ctx, new_cell.get(), "RDATA_" + std::to_string(i), PORT_OUT);
        }

        for (int i = 0; i < 11; i++) {
            add_port(ctx, new_cell.get(), "RADDR_" + std::to_string(i), PORT_IN);
            add_port(ctx, new_cell.get(), "WADDR_" + std::to_string(i), PORT_IN);
        }
    } else if (type == ctx->id("ICESTORM_LFOSC")) {
        add_port(ctx, new_cell.get(), "CLKLFEN", PORT_IN);
        add_port(ctx, new_cell.get(), "CLKLFPU", PORT_IN);
        add_port(ctx, new_cell.get(), "CLKLF", PORT_OUT);
        add_port(ctx, new_cell.get(), "CLKLF_FABRIC", PORT_OUT);
    } else if (type == ctx->id("ICESTORM_HFOSC")) {
        new_cell->params[ctx->id("CLKHF_DIV")] = "0b00";
        new_cell->params[ctx->id("TRIM_EN")] = "0b0";

        add_port(ctx, new_cell.get(), "CLKHFEN", PORT_IN);
        add_port(ctx, new_cell.get(), "CLKHFPU", PORT_IN);
        add_port(ctx, new_cell.get(), "CLKHF", PORT_OUT);
        add_port(ctx, new_cell.get(), "CLKHF_FABRIC", PORT_OUT);
        for (int i = 0; i < 10; i++)
            add_port(ctx, new_cell.get(), "TRIM" + std::to_string(i), PORT_IN);
    } else if (type == ctx->id("SB_GB")) {
        add_port(ctx, new_cell.get(), "USER_SIGNAL_TO_GLOBAL_BUFFER", PORT_IN);
        add_port(ctx, new_cell.get(), "GLOBAL_BUFFER_OUTPUT", PORT_OUT);
    } else if (type == ctx->id("ICESTORM_SPRAM")) {
        add_port(ctx, new_cell.get(), "WREN", PORT_IN);
        add_port(ctx, new_cell.get(), "CHIPSELECT", PORT_IN);
        add_port(ctx, new_cell.get(), "CLOCK", PORT_IN);
        add_port(ctx, new_cell.get(), "STANDBY", PORT_IN);
        add_port(ctx, new_cell.get(), "SLEEP", PORT_IN);
        add_port(ctx, new_cell.get(), "POWEROFF", PORT_IN);

        for (int i = 0; i < 16; i++) {
            add_port(ctx, new_cell.get(), "DATAIN_" + std::to_string(i), PORT_IN);
            add_port(ctx, new_cell.get(), "DATAOUT_" + std::to_string(i), PORT_OUT);
        }
        for (int i = 0; i < 14; i++) {
            add_port(ctx, new_cell.get(), "ADDRESS_" + std::to_string(i), PORT_IN);
        }
        for (int i = 0; i < 4; i++) {
            add_port(ctx, new_cell.get(), "MASKWREN_" + std::to_string(i), PORT_IN);
        }
    } else if (type == ctx->id("ICESTORM_DSP")) {
        new_cell->params[ctx->id("NEG_TRIGGER")] = "0";

        new_cell->params[ctx->id("C_REG")] = "0";
        new_cell->params[ctx->id("A_REG")] = "0";
        new_cell->params[ctx->id("B_REG")] = "0";
        new_cell->params[ctx->id("D_REG")] = "0";
        new_cell->params[ctx->id("TOP_8x8_MULT_REG")] = "0";
        new_cell->params[ctx->id("BOT_8x8_MULT_REG")] = "0";
        new_cell->params[ctx->id("PIPELINE_16x16_MULT_REG1")] = "0";
        new_cell->params[ctx->id("PIPELINE_16x16_MULT_REG2")] = "0";

        new_cell->params[ctx->id("TOPOUTPUT_SELECT")] = "0";
        new_cell->params[ctx->id("TOPADDSUB_LOWERINPUT")] = "0";
        new_cell->params[ctx->id("TOPADDSUB_UPPERINPUT")] = "0";
        new_cell->params[ctx->id("TOPADDSUB_CARRYSELECT")] = "0";

        new_cell->params[ctx->id("BOTOUTPUT_SELECT")] = "0";
        new_cell->params[ctx->id("BOTADDSUB_LOWERINPUT")] = "0";
        new_cell->params[ctx->id("BOTADDSUB_UPPERINPUT")] = "0";
        new_cell->params[ctx->id("BOTADDSUB_CARRYSELECT")] = "0";

        new_cell->params[ctx->id("MODE_8x8")] = "0";
        new_cell->params[ctx->id("A_SIGNED")] = "0";
        new_cell->params[ctx->id("B_SIGNED")] = "0";

        add_port(ctx, new_cell.get(), "CLK", PORT_IN);
        add_port(ctx, new_cell.get(), "CE", PORT_IN);
        for (int i = 0; i < 16; i++) {
            add_port(ctx, new_cell.get(), "C_" + std::to_string(i), PORT_IN);
            add_port(ctx, new_cell.get(), "A_" + std::to_string(i), PORT_IN);
            add_port(ctx, new_cell.get(), "B_" + std::to_string(i), PORT_IN);
            add_port(ctx, new_cell.get(), "D_" + std::to_string(i), PORT_IN);
        }
        add_port(ctx, new_cell.get(), "AHOLD", PORT_IN);
        add_port(ctx, new_cell.get(), "BHOLD", PORT_IN);
        add_port(ctx, new_cell.get(), "CHOLD", PORT_IN);
        add_port(ctx, new_cell.get(), "DHOLD", PORT_IN);

        add_port(ctx, new_cell.get(), "IRSTTOP", PORT_IN);
        add_port(ctx, new_cell.get(), "IRSTBOT", PORT_IN);
        add_port(ctx, new_cell.get(), "ORSTTOP", PORT_IN);
        add_port(ctx, new_cell.get(), "ORSTBOT", PORT_IN);

        add_port(ctx, new_cell.get(), "OLOADTOP", PORT_IN);
        add_port(ctx, new_cell.get(), "OLOADBOT", PORT_IN);

        add_port(ctx, new_cell.get(), "ADDSUBTOP", PORT_IN);
        add_port(ctx, new_cell.get(), "ADDSUBBOT", PORT_IN);

        add_port(ctx, new_cell.get(), "OHOLDTOP", PORT_IN);
        add_port(ctx, new_cell.get(), "OHOLDBOT", PORT_IN);

        add_port(ctx, new_cell.get(), "CI", PORT_IN);
        add_port(ctx, new_cell.get(), "ACCUMCI", PORT_IN);
        add_port(ctx, new_cell.get(), "SIGNEXTIN", PORT_IN);

        for (int i = 0; i < 32; i++) {
            add_port(ctx, new_cell.get(), "O_" + std::to_string(i), PORT_OUT);
        }

        add_port(ctx, new_cell.get(), "CO", PORT_OUT);
        add_port(ctx, new_cell.get(), "ACCUMCO", PORT_OUT);
        add_port(ctx, new_cell.get(), "SIGNEXTOUT", PORT_OUT);

    } else if (type == ctx->id("ICESTORM_PLL")) {
        new_cell->params[ctx->id("DELAY_ADJMODE_FB")] = "0";
        new_cell->params[ctx->id("DELAY_ADJMODE_REL")] = "0";

        new_cell->params[ctx->id("DIVF")] = "0";
        new_cell->params[ctx->id("DIVQ")] = "0";
        new_cell->params[ctx->id("DIVR")] = "0";

        new_cell->params[ctx->id("FDA_FEEDBACK")] = "0";
        new_cell->params[ctx->id("FDA_RELATIVE")] = "0";
        new_cell->params[ctx->id("FEEDBACK_PATH")] = "1";
        new_cell->params[ctx->id("FILTER_RANGE")] = "0";

        new_cell->params[ctx->id("PLLOUT_SELECT_A")] = "0";
        new_cell->params[ctx->id("PLLOUT_SELECT_B")] = "0";

        new_cell->params[ctx->id("PLLTYPE")] = "0";
        new_cell->params[ctx->id("SHIFTREG_DIVMODE")] = "0";
        new_cell->params[ctx->id("TEST_MODE")] = "0";

        add_port(ctx, new_cell.get(), "BYPASS", PORT_IN);
        for (int i = 0; i < 8; i++)
            add_port(ctx, new_cell.get(), "DYNAMICDELAY_" + std::to_string(i), PORT_IN);
        add_port(ctx, new_cell.get(), "EXTFEEDBACK", PORT_IN);
        add_port(ctx, new_cell.get(), "LATCHINPUTVALUE", PORT_IN);
        add_port(ctx, new_cell.get(), "REFERENCECLK", PORT_IN);
        add_port(ctx, new_cell.get(), "RESETB", PORT_IN);

        add_port(ctx, new_cell.get(), "SCLK", PORT_IN);
        add_port(ctx, new_cell.get(), "SDI", PORT_IN);
        add_port(ctx, new_cell.get(), "SDO", PORT_OUT);

        add_port(ctx, new_cell.get(), "LOCK", PORT_OUT);
        add_port(ctx, new_cell.get(), "PLLOUT_A", PORT_OUT);
        add_port(ctx, new_cell.get(), "PLLOUT_B", PORT_OUT);
        add_port(ctx, new_cell.get(), "PLLOUT_A_GLOBAL", PORT_OUT);
        add_port(ctx, new_cell.get(), "PLLOUT_B_GLOBAL", PORT_OUT);
    } else if (type == ctx->id("SB_RGBA_DRV")) {
        new_cell->params[ctx->id("CURRENT_MODE")] = "0b0";
        new_cell->params[ctx->id("RGB0_CURRENT")] = "0b000000";
        new_cell->params[ctx->id("RGB1_CURRENT")] = "0b000000";
        new_cell->params[ctx->id("RGB2_CURRENT")] = "0b000000";

        add_port(ctx, new_cell.get(), "CURREN", PORT_IN);
        add_port(ctx, new_cell.get(), "RGBLEDEN", PORT_IN);
        add_port(ctx, new_cell.get(), "RGB0PWM", PORT_IN);
        add_port(ctx, new_cell.get(), "RGB1PWM", PORT_IN);
        add_port(ctx, new_cell.get(), "RGB2PWM", PORT_IN);
        add_port(ctx, new_cell.get(), "RGB0", PORT_OUT);
        add_port(ctx, new_cell.get(), "RGB1", PORT_OUT);
        add_port(ctx, new_cell.get(), "RGB2", PORT_OUT);
    } else if (type == ctx->id("SB_LEDDA_IP")) {
        add_port(ctx, new_cell.get(), "LEDDCS", PORT_IN);
        add_port(ctx, new_cell.get(), "LEDDCLK", PORT_IN);
        for (int i = 0; i < 8; i++)
            add_port(ctx, new_cell.get(), "LEDDDAT" + std::to_string(i), PORT_IN);
        for (int i = 0; i < 3; i++)
            add_port(ctx, new_cell.get(), "LEDDADDR" + std::to_string(i), PORT_IN);
        add_port(ctx, new_cell.get(), "LEDDDEN", PORT_IN);
        add_port(ctx, new_cell.get(), "LEDDEXE", PORT_IN);
        add_port(ctx, new_cell.get(), "LEDDRST", PORT_IN); // doesn't actually exist, for icecube code compatibility
                                                           // only
        add_port(ctx, new_cell.get(), "PWMOUT0", PORT_OUT);
        add_port(ctx, new_cell.get(), "PWMOUT1", PORT_OUT);
        add_port(ctx, new_cell.get(), "PWMOUT2", PORT_OUT);
        add_port(ctx, new_cell.get(), "LEDDON", PORT_OUT);
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
            NPNR_ASSERT(c == 'S');
            lc->params[ctx->id("ASYNC_SR")] = "0";
        } else {
            lc->params[ctx->id("ASYNC_SR")] = "1";
        }

        if (*citer == 'S') {
            citer++;
            replace_port(dff, ctx->id("S"), lc, ctx->id("SR"));
            lc->params[ctx->id("SET_NORESET")] = "1";
        } else {
            NPNR_ASSERT(*citer == 'R');
            citer++;
            replace_port(dff, ctx->id("R"), lc, ctx->id("SR"));
            lc->params[ctx->id("SET_NORESET")] = "0";
        }
    }

    NPNR_ASSERT(citer == config.end());

    if (pass_thru_lut) {
        lc->params[ctx->id("LUT_INIT")] = "2";
        replace_port(dff, ctx->id("D"), lc, ctx->id("I0"));
    }

    replace_port(dff, ctx->id("Q"), lc, ctx->id("O"));
}

void nxio_to_sb(Context *ctx, CellInfo *nxio, CellInfo *sbio, std::unordered_set<IdString> &todelete_cells)
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
        NPNR_ASSERT(false);
    }
    NetInfo *donet = sbio->ports.at(ctx->id("D_OUT_0")).net;
    CellInfo *tbuf = net_driven_by(
            ctx, donet, [](const Context *ctx, const CellInfo *cell) { return cell->type == ctx->id("$_TBUF_"); },
            ctx->id("Y"));
    if (tbuf) {
        sbio->params[ctx->id("PIN_TYPE")] = "41";
        replace_port(tbuf, ctx->id("A"), sbio, ctx->id("D_OUT_0"));
        replace_port(tbuf, ctx->id("E"), sbio, ctx->id("OUTPUT_ENABLE"));

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

uint8_t sb_pll40_type(const BaseCtx *ctx, const CellInfo *cell)
{
    if (cell->type == ctx->id("SB_PLL40_PAD"))
        return 2;
    if (cell->type == ctx->id("SB_PLL40_2_PAD"))
        return 4;
    if (cell->type == ctx->id("SB_PLL40_2F_PAD"))
        return 6;
    if (cell->type == ctx->id("SB_PLL40_CORE"))
        return 3;
    if (cell->type == ctx->id("SB_PLL40_2F_CORE"))
        return 7;
    NPNR_ASSERT(0);
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
        return port.port == ctx->id("RCLK") || port.port == ctx->id("WCLK") || port.port == ctx->id("RCLKN") ||
               port.port == ctx->id("WCLKN");
    if (is_sb_mac16(ctx, port.cell) || port.cell->type == ctx->id("ICESTORM_DSP"))
        return port.port == ctx->id("CLK");
    if (is_sb_spram(ctx, port.cell) || port.cell->type == ctx->id("ICESTORM_SPRAM"))
        return port.port == id_CLOCK;
    if (is_sb_io(ctx, port.cell))
        return port.port == id_INPUT_CLK || port.port == id_OUTPUT_CLK;
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
    if (is_sb_mac16(ctx, port.cell) || port.cell->type == ctx->id("ICESTORM_DSP"))
        return port.port == ctx->id("IRSTTOP") || port.port == ctx->id("IRSTBOT") || port.port == ctx->id("ORSTTOP") ||
               port.port == ctx->id("ORSTBOT");
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
    // FIXME
    // if (is_sb_mac16(ctx, port.cell) || port.cell->type == ctx->id("ICESTORM_DSP"))
    //    return port.port == ctx->id("CE");
    return false;
}

NEXTPNR_NAMESPACE_END
