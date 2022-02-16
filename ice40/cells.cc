/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2018  Serge Bazanski <q3k@q3k.org>
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

std::unique_ptr<CellInfo> create_ice_cell(Context *ctx, IdString type, std::string name)
{
    static int auto_idx = 0;
    IdString name_id =
            name.empty() ? ctx->id("$nextpnr_" + type.str(ctx) + "_" + std::to_string(auto_idx++)) : ctx->id(name);
    auto new_cell = std::make_unique<CellInfo>(ctx, name_id, type);

    if (type == ctx->id("ICESTORM_LC")) {
        new_cell->params[ctx->id("LUT_INIT")] = Property(0, 16);
        new_cell->params[ctx->id("NEG_CLK")] = Property::State::S0;
        new_cell->params[ctx->id("CARRY_ENABLE")] = Property::State::S0;
        new_cell->params[ctx->id("DFF_ENABLE")] = Property::State::S0;
        new_cell->params[ctx->id("SET_NORESET")] = Property::State::S0;
        new_cell->params[ctx->id("ASYNC_SR")] = Property::State::S0;
        new_cell->params[ctx->id("CIN_CONST")] = Property::State::S0;
        new_cell->params[ctx->id("CIN_SET")] = Property::State::S0;

        new_cell->addInput(ctx->id("I0"));
        new_cell->addInput(ctx->id("I1"));
        new_cell->addInput(ctx->id("I2"));
        new_cell->addInput(ctx->id("I3"));
        new_cell->addInput(ctx->id("CIN"));

        new_cell->addInput(ctx->id("CLK"));
        new_cell->addInput(ctx->id("CEN"));
        new_cell->addInput(ctx->id("SR"));

        new_cell->addOutput(ctx->id("LO"));
        new_cell->addOutput(ctx->id("O"));
        new_cell->addOutput(ctx->id("COUT"));
    } else if (type == ctx->id("SB_IO")) {
        new_cell->params[ctx->id("PIN_TYPE")] = Property(0, 6);
        new_cell->params[ctx->id("PULLUP")] = Property::State::S0;
        new_cell->params[ctx->id("NEG_TRIGGER")] = Property::State::S0;
        new_cell->params[ctx->id("IO_STANDARD")] = Property("SB_LVCMOS");

        new_cell->addInout(ctx->id("PACKAGE_PIN"));

        new_cell->addInput(ctx->id("LATCH_INPUT_VALUE"));
        new_cell->addInput(ctx->id("CLOCK_ENABLE"));
        new_cell->addInput(ctx->id("INPUT_CLK"));
        new_cell->addInput(ctx->id("OUTPUT_CLK"));

        new_cell->addInput(ctx->id("OUTPUT_ENABLE"));
        new_cell->addInput(ctx->id("D_OUT_0"));
        new_cell->addInput(ctx->id("D_OUT_1"));

        new_cell->addOutput(ctx->id("D_IN_0"));
        new_cell->addOutput(ctx->id("D_IN_1"));
    } else if (type == ctx->id("ICESTORM_RAM")) {
        new_cell->params[ctx->id("NEG_CLK_W")] = Property::State::S0;
        new_cell->params[ctx->id("NEG_CLK_R")] = Property::State::S0;
        new_cell->params[ctx->id("WRITE_MODE")] = Property::State::S0;
        new_cell->params[ctx->id("READ_MODE")] = Property::State::S0;

        new_cell->addInput(ctx->id("RCLK"));
        new_cell->addInput(ctx->id("RCLKE"));
        new_cell->addInput(ctx->id("RE"));

        new_cell->addInput(ctx->id("WCLK"));
        new_cell->addInput(ctx->id("WCLKE"));
        new_cell->addInput(ctx->id("WE"));

        for (int i = 0; i < 16; i++) {
            new_cell->addInput(ctx->id("WDATA_" + std::to_string(i)));
            new_cell->addInput(ctx->id("MASK_" + std::to_string(i)));
            new_cell->addOutput(ctx->id("RDATA_" + std::to_string(i)));
        }

        for (int i = 0; i < 11; i++) {
            new_cell->addInput(ctx->id("RADDR_" + std::to_string(i)));
            new_cell->addInput(ctx->id("WADDR_" + std::to_string(i)));
        }
    } else if (type == ctx->id("ICESTORM_LFOSC")) {
        new_cell->addInput(ctx->id("CLKLFEN"));
        new_cell->addInput(ctx->id("CLKLFPU"));
        new_cell->addOutput(ctx->id("CLKLF"));
        new_cell->addOutput(ctx->id("CLKLF_FABRIC"));
    } else if (type == ctx->id("ICESTORM_HFOSC")) {
        new_cell->params[ctx->id("CLKHF_DIV")] = Property("0b00");
        new_cell->params[ctx->id("TRIM_EN")] = Property("0b0");

        new_cell->addInput(ctx->id("CLKHFEN"));
        new_cell->addInput(ctx->id("CLKHFPU"));
        new_cell->addOutput(ctx->id("CLKHF"));
        new_cell->addOutput(ctx->id("CLKHF_FABRIC"));
        for (int i = 0; i < 10; i++)
            new_cell->addInput(ctx->id("TRIM" + std::to_string(i)));
    } else if (type == ctx->id("SB_GB")) {
        new_cell->addInput(ctx->id("USER_SIGNAL_TO_GLOBAL_BUFFER"));
        new_cell->addOutput(ctx->id("GLOBAL_BUFFER_OUTPUT"));
    } else if (type == ctx->id("ICESTORM_SPRAM")) {
        new_cell->addInput(ctx->id("WREN"));
        new_cell->addInput(ctx->id("CHIPSELECT"));
        new_cell->addInput(ctx->id("CLOCK"));
        new_cell->addInput(ctx->id("STANDBY"));
        new_cell->addInput(ctx->id("SLEEP"));
        new_cell->addInput(ctx->id("POWEROFF"));

        for (int i = 0; i < 16; i++) {
            new_cell->addInput(ctx->id("DATAIN_" + std::to_string(i)));
            new_cell->addOutput(ctx->id("DATAOUT_" + std::to_string(i)));
        }
        for (int i = 0; i < 14; i++) {
            new_cell->addInput(ctx->id("ADDRESS_" + std::to_string(i)));
        }
        for (int i = 0; i < 4; i++) {
            new_cell->addInput(ctx->id("MASKWREN_" + std::to_string(i)));
        }
    } else if (type == ctx->id("ICESTORM_DSP")) {
        new_cell->params[ctx->id("NEG_TRIGGER")] = Property::State::S0;

        new_cell->params[ctx->id("C_REG")] = Property::State::S0;
        new_cell->params[ctx->id("A_REG")] = Property::State::S0;
        new_cell->params[ctx->id("B_REG")] = Property::State::S0;
        new_cell->params[ctx->id("D_REG")] = Property::State::S0;
        new_cell->params[ctx->id("TOP_8x8_MULT_REG")] = Property::State::S0;
        new_cell->params[ctx->id("BOT_8x8_MULT_REG")] = Property::State::S0;
        new_cell->params[ctx->id("PIPELINE_16x16_MULT_REG1")] = Property::State::S0;
        new_cell->params[ctx->id("PIPELINE_16x16_MULT_REG2")] = Property::State::S0;

        new_cell->params[ctx->id("TOPOUTPUT_SELECT")] = Property(0, 2);
        new_cell->params[ctx->id("TOPADDSUB_LOWERINPUT")] = Property(0, 2);
        new_cell->params[ctx->id("TOPADDSUB_UPPERINPUT")] = Property::State::S0;
        new_cell->params[ctx->id("TOPADDSUB_CARRYSELECT")] = Property(0, 2);

        new_cell->params[ctx->id("BOTOUTPUT_SELECT")] = Property(0, 2);
        new_cell->params[ctx->id("BOTADDSUB_LOWERINPUT")] = Property(0, 2);
        new_cell->params[ctx->id("BOTADDSUB_UPPERINPUT")] = Property::State::S0;
        new_cell->params[ctx->id("BOTADDSUB_CARRYSELECT")] = Property(0, 2);

        new_cell->params[ctx->id("MODE_8x8")] = Property::State::S0;
        new_cell->params[ctx->id("A_SIGNED")] = Property::State::S0;
        new_cell->params[ctx->id("B_SIGNED")] = Property::State::S0;

        new_cell->addInput(ctx->id("CLK"));
        new_cell->addInput(ctx->id("CE"));
        for (int i = 0; i < 16; i++) {
            new_cell->addInput(ctx->id("C_" + std::to_string(i)));
            new_cell->addInput(ctx->id("A_" + std::to_string(i)));
            new_cell->addInput(ctx->id("B_" + std::to_string(i)));
            new_cell->addInput(ctx->id("D_" + std::to_string(i)));
        }
        new_cell->addInput(ctx->id("AHOLD"));
        new_cell->addInput(ctx->id("BHOLD"));
        new_cell->addInput(ctx->id("CHOLD"));
        new_cell->addInput(ctx->id("DHOLD"));

        new_cell->addInput(ctx->id("IRSTTOP"));
        new_cell->addInput(ctx->id("IRSTBOT"));
        new_cell->addInput(ctx->id("ORSTTOP"));
        new_cell->addInput(ctx->id("ORSTBOT"));

        new_cell->addInput(ctx->id("OLOADTOP"));
        new_cell->addInput(ctx->id("OLOADBOT"));

        new_cell->addInput(ctx->id("ADDSUBTOP"));
        new_cell->addInput(ctx->id("ADDSUBBOT"));

        new_cell->addInput(ctx->id("OHOLDTOP"));
        new_cell->addInput(ctx->id("OHOLDBOT"));

        new_cell->addInput(ctx->id("CI"));
        new_cell->addInput(ctx->id("ACCUMCI"));
        new_cell->addInput(ctx->id("SIGNEXTIN"));

        for (int i = 0; i < 32; i++) {
            new_cell->addOutput(ctx->id("O_" + std::to_string(i)));
        }

        new_cell->addOutput(ctx->id("CO"));
        new_cell->addOutput(ctx->id("ACCUMCO"));
        new_cell->addOutput(ctx->id("SIGNEXTOUT"));

    } else if (type == ctx->id("ICESTORM_PLL")) {
        new_cell->params[ctx->id("DELAY_ADJMODE_FB")] = Property::State::S0;
        new_cell->params[ctx->id("DELAY_ADJMODE_REL")] = Property::State::S0;

        new_cell->params[ctx->id("DIVF")] = Property(0, 7);
        new_cell->params[ctx->id("DIVQ")] = Property(0, 3);
        new_cell->params[ctx->id("DIVR")] = Property(0, 4);

        new_cell->params[ctx->id("FDA_FEEDBACK")] = Property(0, 4);
        new_cell->params[ctx->id("FDA_RELATIVE")] = Property(0, 4);
        new_cell->params[ctx->id("FEEDBACK_PATH")] = Property(1, 3);
        new_cell->params[ctx->id("FILTER_RANGE")] = Property(0, 3);

        new_cell->params[ctx->id("PLLOUT_SELECT_A")] = Property(0, 2);
        new_cell->params[ctx->id("PLLOUT_SELECT_B")] = Property(0, 2);

        new_cell->params[ctx->id("PLLTYPE")] = Property(0, 3);
        new_cell->params[ctx->id("SHIFTREG_DIVMODE")] = Property::State::S0;
        new_cell->params[ctx->id("TEST_MODE")] = Property::State::S0;

        new_cell->addInput(ctx->id("BYPASS"));
        for (int i = 0; i < 8; i++)
            new_cell->addInput(ctx->id("DYNAMICDELAY_" + std::to_string(i)));
        new_cell->addInput(ctx->id("EXTFEEDBACK"));
        new_cell->addInput(ctx->id("LATCHINPUTVALUE"));
        new_cell->addInput(ctx->id("REFERENCECLK"));
        new_cell->addInput(ctx->id("RESETB"));

        new_cell->addInput(ctx->id("SCLK"));
        new_cell->addInput(ctx->id("SDI"));
        new_cell->addOutput(ctx->id("SDO"));

        new_cell->addOutput(ctx->id("LOCK"));
        new_cell->addOutput(ctx->id("PLLOUT_A"));
        new_cell->addOutput(ctx->id("PLLOUT_B"));
        new_cell->addOutput(ctx->id("PLLOUT_A_GLOBAL"));
        new_cell->addOutput(ctx->id("PLLOUT_B_GLOBAL"));
    } else if (type == ctx->id("SB_RGBA_DRV")) {
        new_cell->params[ctx->id("CURRENT_MODE")] = std::string("0b0");
        new_cell->params[ctx->id("RGB0_CURRENT")] = std::string("0b000000");
        new_cell->params[ctx->id("RGB1_CURRENT")] = std::string("0b000000");
        new_cell->params[ctx->id("RGB2_CURRENT")] = std::string("0b000000");

        new_cell->addInput(ctx->id("CURREN"));
        new_cell->addInput(ctx->id("RGBLEDEN"));
        new_cell->addInput(ctx->id("RGB0PWM"));
        new_cell->addInput(ctx->id("RGB1PWM"));
        new_cell->addInput(ctx->id("RGB2PWM"));
        new_cell->addOutput(ctx->id("RGB0"));
        new_cell->addOutput(ctx->id("RGB1"));
        new_cell->addOutput(ctx->id("RGB2"));
    } else if (type == ctx->id("SB_LED_DRV_CUR")) {
        new_cell->addInput(ctx->id("EN"));
        new_cell->addOutput(ctx->id("LEDPU"));
    } else if (type == ctx->id("SB_RGB_DRV")) {
        new_cell->params[ctx->id("RGB0_CURRENT")] = std::string("0b000000");
        new_cell->params[ctx->id("RGB1_CURRENT")] = std::string("0b000000");
        new_cell->params[ctx->id("RGB2_CURRENT")] = std::string("0b000000");

        new_cell->addInput(ctx->id("RGBPU"));
        new_cell->addInput(ctx->id("RGBLEDEN"));
        new_cell->addInput(ctx->id("RGB0PWM"));
        new_cell->addInput(ctx->id("RGB1PWM"));
        new_cell->addInput(ctx->id("RGB2PWM"));
        new_cell->addOutput(ctx->id("RGB0"));
        new_cell->addOutput(ctx->id("RGB1"));
        new_cell->addOutput(ctx->id("RGB2"));
    } else if (type == ctx->id("SB_LEDDA_IP")) {
        new_cell->addInput(ctx->id("LEDDCS"));
        new_cell->addInput(ctx->id("LEDDCLK"));
        for (int i = 0; i < 8; i++)
            new_cell->addInput(ctx->id("LEDDDAT" + std::to_string(i)));
        for (int i = 0; i < 3; i++)
            new_cell->addInput(ctx->id("LEDDADDR" + std::to_string(i)));
        new_cell->addInput(ctx->id("LEDDDEN"));
        new_cell->addInput(ctx->id("LEDDEXE"));
        new_cell->addInput(ctx->id("LEDDRST")); // doesn't actually exist, for icecube code compatibility
                                                // only
        new_cell->addOutput(ctx->id("PWMOUT0"));
        new_cell->addOutput(ctx->id("PWMOUT1"));
        new_cell->addOutput(ctx->id("PWMOUT2"));
        new_cell->addOutput(ctx->id("LEDDON"));
    } else if (type == ctx->id("SB_I2C")) {
        new_cell->params[ctx->id("I2C_SLAVE_INIT_ADDR")] = std::string("0b1111100001");
        new_cell->params[ctx->id("BUS_ADDR74")] = std::string("0b0001");
        for (int i = 0; i < 8; i++) {
            new_cell->addInput(ctx->id("SBADRI" + std::to_string(i)));
            new_cell->addInput(ctx->id("SBDATI" + std::to_string(i)));
            new_cell->addOutput(ctx->id("SBDATO" + std::to_string(i)));
        }
        new_cell->addInput(ctx->id("SBCLKI"));
        new_cell->addInput(ctx->id("SBRWI"));
        new_cell->addInput(ctx->id("SBSTBI"));
        new_cell->addInput(ctx->id("SCLI"));
        new_cell->addInput(ctx->id("SDAI"));
        new_cell->addOutput(ctx->id("SBACKO"));
        new_cell->addOutput(ctx->id("I2CIRQ"));
        new_cell->addOutput(ctx->id("I2CWKUP"));
        new_cell->addOutput(ctx->id("SCLO"));
        new_cell->addOutput(ctx->id("SCLOE"));
        new_cell->addOutput(ctx->id("SDAO"));
        new_cell->addOutput(ctx->id("SDAOE"));
    } else if (type == ctx->id("SB_SPI")) {
        new_cell->params[ctx->id("BUS_ADDR74")] = std::string("0b0000");
        for (int i = 0; i < 8; i++) {
            new_cell->addInput(ctx->id("SBADRI" + std::to_string(i)));
            new_cell->addInput(ctx->id("SBDATI" + std::to_string(i)));
            new_cell->addOutput(ctx->id("SBDATO" + std::to_string(i)));
        }
        new_cell->addInput(ctx->id("SBCLKI"));
        new_cell->addInput(ctx->id("SBRWI"));
        new_cell->addInput(ctx->id("SBSTBI"));
        new_cell->addInput(ctx->id("MI"));
        new_cell->addInput(ctx->id("SI"));
        new_cell->addInput(ctx->id("SCKI"));
        new_cell->addInput(ctx->id("SCSNI"));
        new_cell->addOutput(ctx->id("SBACKO"));
        new_cell->addOutput(ctx->id("SPIIRQ"));
        new_cell->addOutput(ctx->id("SPIWKUP"));
        new_cell->addOutput(ctx->id("SO"));
        new_cell->addOutput(ctx->id("SOE"));
        new_cell->addOutput(ctx->id("MO"));
        new_cell->addOutput(ctx->id("MOE"));
        new_cell->addOutput(ctx->id("SCKO"));
        new_cell->addOutput(ctx->id("SCKOE"));
        for (int i = 0; i < 4; i++) {
            new_cell->addOutput(ctx->id("MCSNO" + std::to_string(i)));
            new_cell->addOutput(ctx->id("MCSNOE" + std::to_string(i)));
        }
    } else {
        log_error("unable to create iCE40 cell of type %s", type.c_str(ctx));
    }
    return new_cell;
}

void lut_to_lc(const Context *ctx, CellInfo *lut, CellInfo *lc, bool no_dff)
{
    if (lc->hierpath == IdString())
        lc->hierpath = lut->hierpath;
    lc->params[ctx->id("LUT_INIT")] = lut->params[ctx->id("LUT_INIT")].extract(0, 16, Property::State::S0);
    replace_port(lut, ctx->id("I0"), lc, ctx->id("I0"));
    replace_port(lut, ctx->id("I1"), lc, ctx->id("I1"));
    replace_port(lut, ctx->id("I2"), lc, ctx->id("I2"));
    replace_port(lut, ctx->id("I3"), lc, ctx->id("I3"));
    if (no_dff) {
        replace_port(lut, ctx->id("O"), lc, ctx->id("O"));
        lc->params[ctx->id("DFF_ENABLE")] = Property::State::S0;
    }
}

void dff_to_lc(const Context *ctx, CellInfo *dff, CellInfo *lc, bool pass_thru_lut)
{
    if (lc->hierpath == IdString())
        lc->hierpath = dff->hierpath;
    lc->params[ctx->id("DFF_ENABLE")] = Property::State::S1;
    std::string config = dff->type.str(ctx).substr(6);
    auto citer = config.begin();
    replace_port(dff, ctx->id("C"), lc, ctx->id("CLK"));

    if (citer != config.end() && *citer == 'N') {
        lc->params[ctx->id("NEG_CLK")] = Property::State::S1;
        ++citer;
    } else {
        lc->params[ctx->id("NEG_CLK")] = Property::State::S0;
    }

    if (citer != config.end() && *citer == 'E') {
        replace_port(dff, ctx->id("E"), lc, ctx->id("CEN"));
        ++citer;
    }

    if (citer != config.end()) {
        if ((config.end() - citer) >= 2) {
            char c = *(citer++);
            NPNR_ASSERT(c == 'S');
            lc->params[ctx->id("ASYNC_SR")] = Property::State::S0;
        } else {
            lc->params[ctx->id("ASYNC_SR")] = Property::State::S1;
        }

        if (*citer == 'S') {
            citer++;
            replace_port(dff, ctx->id("S"), lc, ctx->id("SR"));
            lc->params[ctx->id("SET_NORESET")] = Property::State::S1;
        } else {
            NPNR_ASSERT(*citer == 'R');
            citer++;
            replace_port(dff, ctx->id("R"), lc, ctx->id("SR"));
            lc->params[ctx->id("SET_NORESET")] = Property::State::S0;
        }
    }

    NPNR_ASSERT(citer == config.end());

    if (pass_thru_lut) {
        lc->params[ctx->id("LUT_INIT")] = Property(2, 16);
        replace_port(dff, ctx->id("D"), lc, ctx->id("I0"));
    }

    replace_port(dff, ctx->id("Q"), lc, ctx->id("O"));
}

void nxio_to_sb(Context *ctx, CellInfo *nxio, CellInfo *sbio, pool<IdString> &todelete_cells)
{
    if (nxio->type == ctx->id("$nextpnr_ibuf")) {
        sbio->params[ctx->id("PIN_TYPE")] = 1;
        auto pu_attr = nxio->attrs.find(ctx->id("PULLUP"));
        if (pu_attr != nxio->attrs.end())
            sbio->params[ctx->id("PULLUP")] = pu_attr->second;
        replace_port(nxio, ctx->id("O"), sbio, ctx->id("D_IN_0"));
    } else if (nxio->type == ctx->id("$nextpnr_obuf")) {
        sbio->params[ctx->id("PIN_TYPE")] = 25;
        replace_port(nxio, ctx->id("I"), sbio, ctx->id("D_OUT_0"));
    } else if (nxio->type == ctx->id("$nextpnr_iobuf")) {
        // N.B. tristate will be dealt with below
        NetInfo *i = get_net_or_empty(nxio, ctx->id("I"));
        if (i == nullptr || i->driver.cell == nullptr)
            sbio->params[ctx->id("PIN_TYPE")] = 1;
        else
            sbio->params[ctx->id("PIN_TYPE")] = 25;
        auto pu_attr = nxio->attrs.find(ctx->id("PULLUP"));
        if (pu_attr != nxio->attrs.end())
            sbio->params[ctx->id("PULLUP")] = pu_attr->second;
        replace_port(nxio, ctx->id("I"), sbio, ctx->id("D_OUT_0"));
        replace_port(nxio, ctx->id("O"), sbio, ctx->id("D_IN_0"));
    } else {
        NPNR_ASSERT(false);
    }
    NetInfo *donet = sbio->ports.at(ctx->id("D_OUT_0")).net, *dinet = sbio->ports.at(ctx->id("D_IN_0")).net;

    // Rename I/O nets to avoid conflicts
    if (donet != nullptr && donet->name == nxio->name)
        rename_net(ctx, donet, ctx->id(donet->name.str(ctx) + "$SB_IO_OUT"));
    if (dinet != nullptr && dinet->name == nxio->name)
        rename_net(ctx, dinet, ctx->id(dinet->name.str(ctx) + "$SB_IO_IN"));

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
        ctx->net_aliases.erase(tn_netname);
        NetInfo *toplevel_net = ctx->createNet(tn_netname);
        connect_port(ctx, toplevel_net, sbio, ctx->id("PACKAGE_PIN"));
        ctx->ports[nxio->name].net = toplevel_net;
    }

    CellInfo *tbuf = net_driven_by(
            ctx, donet, [](const Context *ctx, const CellInfo *cell) { return cell->type == ctx->id("$_TBUF_"); },
            ctx->id("Y"));
    if (tbuf) {
        sbio->params[ctx->id("PIN_TYPE")] = 41;
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
