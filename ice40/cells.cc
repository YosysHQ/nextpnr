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

    if (type == id_ICESTORM_LC) {
        new_cell->params[id_LUT_INIT] = Property(0, 16);
        new_cell->params[id_NEG_CLK] = Property::State::S0;
        new_cell->params[id_CARRY_ENABLE] = Property::State::S0;
        new_cell->params[id_DFF_ENABLE] = Property::State::S0;
        new_cell->params[id_SET_NORESET] = Property::State::S0;
        new_cell->params[id_ASYNC_SR] = Property::State::S0;
        new_cell->params[id_CIN_CONST] = Property::State::S0;
        new_cell->params[id_CIN_SET] = Property::State::S0;

        new_cell->addInput(id_I0);
        new_cell->addInput(id_I1);
        new_cell->addInput(id_I2);
        new_cell->addInput(id_I3);
        new_cell->addInput(id_CIN);

        new_cell->addInput(id_CLK);
        new_cell->addInput(id_CEN);
        new_cell->addInput(id_SR);

        new_cell->addOutput(id_LO);
        new_cell->addOutput(id_O);
        new_cell->addOutput(id_COUT);
    } else if (type == id_SB_IO) {
        new_cell->params[id_PIN_TYPE] = Property(0, 6);
        new_cell->params[id_PULLUP] = Property::State::S0;
        new_cell->params[id_NEG_TRIGGER] = Property::State::S0;
        new_cell->params[id_IO_STANDARD] = Property("SB_LVCMOS");

        new_cell->addInout(id_PACKAGE_PIN);

        new_cell->addInput(id_LATCH_INPUT_VALUE);
        new_cell->addInput(id_CLOCK_ENABLE);
        new_cell->addInput(id_INPUT_CLK);
        new_cell->addInput(id_OUTPUT_CLK);

        new_cell->addInput(id_OUTPUT_ENABLE);
        new_cell->addInput(id_D_OUT_0);
        new_cell->addInput(id_D_OUT_1);

        new_cell->addOutput(id_D_IN_0);
        new_cell->addOutput(id_D_IN_1);
    } else if (type == id_ICESTORM_RAM) {
        new_cell->params[id_NEG_CLK_W] = Property::State::S0;
        new_cell->params[id_NEG_CLK_R] = Property::State::S0;
        new_cell->params[id_WRITE_MODE] = Property::State::S0;
        new_cell->params[id_READ_MODE] = Property::State::S0;

        new_cell->addInput(id_RCLK);
        new_cell->addInput(id_RCLKE);
        new_cell->addInput(id_RE);

        new_cell->addInput(id_WCLK);
        new_cell->addInput(id_WCLKE);
        new_cell->addInput(id_WE);

        for (int i = 0; i < 16; i++) {
            new_cell->addInput(ctx->id("WDATA_" + std::to_string(i)));
            new_cell->addInput(ctx->id("MASK_" + std::to_string(i)));
            new_cell->addOutput(ctx->id("RDATA_" + std::to_string(i)));
        }

        for (int i = 0; i < 11; i++) {
            new_cell->addInput(ctx->id("RADDR_" + std::to_string(i)));
            new_cell->addInput(ctx->id("WADDR_" + std::to_string(i)));
        }
    } else if (type == id_ICESTORM_LFOSC) {
        new_cell->addInput(id_CLKLFEN);
        new_cell->addInput(id_CLKLFPU);
        new_cell->addOutput(id_CLKLF);
        new_cell->addOutput(id_CLKLF_FABRIC);
    } else if (type == id_ICESTORM_HFOSC) {
        new_cell->params[id_CLKHF_DIV] = Property("0b00");
        new_cell->params[id_TRIM_EN] = Property("0b0");

        new_cell->addInput(id_CLKHFEN);
        new_cell->addInput(id_CLKHFPU);
        new_cell->addOutput(id_CLKHF);
        new_cell->addOutput(id_CLKHF_FABRIC);
        for (int i = 0; i < 10; i++)
            new_cell->addInput(ctx->id("TRIM" + std::to_string(i)));
    } else if (type == id_SB_GB) {
        new_cell->addInput(id_USER_SIGNAL_TO_GLOBAL_BUFFER);
        new_cell->addOutput(id_GLOBAL_BUFFER_OUTPUT);
    } else if (type == id_ICESTORM_SPRAM) {
        new_cell->addInput(id_WREN);
        new_cell->addInput(id_CHIPSELECT);
        new_cell->addInput(id_CLOCK);
        new_cell->addInput(id_STANDBY);
        new_cell->addInput(id_SLEEP);
        new_cell->addInput(id_POWEROFF);

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
    } else if (type == id_ICESTORM_DSP) {
        new_cell->params[id_NEG_TRIGGER] = Property::State::S0;

        new_cell->params[id_C_REG] = Property::State::S0;
        new_cell->params[id_A_REG] = Property::State::S0;
        new_cell->params[id_B_REG] = Property::State::S0;
        new_cell->params[id_D_REG] = Property::State::S0;
        new_cell->params[id_TOP_8x8_MULT_REG] = Property::State::S0;
        new_cell->params[id_BOT_8x8_MULT_REG] = Property::State::S0;
        new_cell->params[id_PIPELINE_16x16_MULT_REG1] = Property::State::S0;
        new_cell->params[id_PIPELINE_16x16_MULT_REG2] = Property::State::S0;

        new_cell->params[id_TOPOUTPUT_SELECT] = Property(0, 2);
        new_cell->params[id_TOPADDSUB_LOWERINPUT] = Property(0, 2);
        new_cell->params[id_TOPADDSUB_UPPERINPUT] = Property::State::S0;
        new_cell->params[id_TOPADDSUB_CARRYSELECT] = Property(0, 2);

        new_cell->params[id_BOTOUTPUT_SELECT] = Property(0, 2);
        new_cell->params[id_BOTADDSUB_LOWERINPUT] = Property(0, 2);
        new_cell->params[id_BOTADDSUB_UPPERINPUT] = Property::State::S0;
        new_cell->params[id_BOTADDSUB_CARRYSELECT] = Property(0, 2);

        new_cell->params[id_MODE_8x8] = Property::State::S0;
        new_cell->params[id_A_SIGNED] = Property::State::S0;
        new_cell->params[id_B_SIGNED] = Property::State::S0;

        new_cell->addInput(id_CLK);
        new_cell->addInput(id_CE);
        for (int i = 0; i < 16; i++) {
            new_cell->addInput(ctx->id("C_" + std::to_string(i)));
            new_cell->addInput(ctx->id("A_" + std::to_string(i)));
            new_cell->addInput(ctx->id("B_" + std::to_string(i)));
            new_cell->addInput(ctx->id("D_" + std::to_string(i)));
        }
        new_cell->addInput(id_AHOLD);
        new_cell->addInput(id_BHOLD);
        new_cell->addInput(id_CHOLD);
        new_cell->addInput(id_DHOLD);

        new_cell->addInput(id_IRSTTOP);
        new_cell->addInput(id_IRSTBOT);
        new_cell->addInput(id_ORSTTOP);
        new_cell->addInput(id_ORSTBOT);

        new_cell->addInput(id_OLOADTOP);
        new_cell->addInput(id_OLOADBOT);

        new_cell->addInput(id_ADDSUBTOP);
        new_cell->addInput(id_ADDSUBBOT);

        new_cell->addInput(id_OHOLDTOP);
        new_cell->addInput(id_OHOLDBOT);

        new_cell->addInput(id_CI);
        new_cell->addInput(id_ACCUMCI);
        new_cell->addInput(id_SIGNEXTIN);

        for (int i = 0; i < 32; i++) {
            new_cell->addOutput(ctx->id("O_" + std::to_string(i)));
        }

        new_cell->addOutput(id_CO);
        new_cell->addOutput(id_ACCUMCO);
        new_cell->addOutput(id_SIGNEXTOUT);

    } else if (type == id_ICESTORM_PLL) {
        new_cell->params[id_DELAY_ADJMODE_FB] = Property::State::S0;
        new_cell->params[id_DELAY_ADJMODE_REL] = Property::State::S0;

        new_cell->params[id_DIVF] = Property(0, 7);
        new_cell->params[id_DIVQ] = Property(0, 3);
        new_cell->params[id_DIVR] = Property(0, 4);

        new_cell->params[id_FDA_FEEDBACK] = Property(0, 4);
        new_cell->params[id_FDA_RELATIVE] = Property(0, 4);
        new_cell->params[id_FEEDBACK_PATH] = Property(1, 3);
        new_cell->params[id_FILTER_RANGE] = Property(0, 3);

        new_cell->params[id_PLLOUT_SELECT_A] = Property(0, 2);
        new_cell->params[id_PLLOUT_SELECT_B] = Property(0, 2);

        new_cell->params[id_ENABLE_ICEGATE_PORTA] = Property::State::S0;
        new_cell->params[id_ENABLE_ICEGATE_PORTB] = Property::State::S0;

        new_cell->params[id_PLLTYPE] = Property(0, 3);
        new_cell->params[id_SHIFTREG_DIVMODE] = Property::State::S0;
        new_cell->params[id_TEST_MODE] = Property::State::S0;

        new_cell->addInput(id_BYPASS);
        for (int i = 0; i < 8; i++)
            new_cell->addInput(ctx->id("DYNAMICDELAY_" + std::to_string(i)));
        new_cell->addInput(id_EXTFEEDBACK);
        new_cell->addInput(id_LATCHINPUTVALUE);
        new_cell->addInput(id_REFERENCECLK);
        new_cell->addInput(id_RESETB);

        new_cell->addInput(id_SCLK);
        new_cell->addInput(id_SDI);
        new_cell->addOutput(id_SDO);

        new_cell->addOutput(id_LOCK);
        new_cell->addOutput(id_PLLOUT_A);
        new_cell->addOutput(id_PLLOUT_B);
        new_cell->addOutput(id_PLLOUT_A_GLOBAL);
        new_cell->addOutput(id_PLLOUT_B_GLOBAL);
    } else if (type == id_SB_RGBA_DRV) {
        new_cell->params[id_CURRENT_MODE] = std::string("0b0");
        new_cell->params[id_RGB0_CURRENT] = std::string("0b000000");
        new_cell->params[id_RGB1_CURRENT] = std::string("0b000000");
        new_cell->params[id_RGB2_CURRENT] = std::string("0b000000");

        new_cell->addInput(id_CURREN);
        new_cell->addInput(id_RGBLEDEN);
        new_cell->addInput(id_RGB0PWM);
        new_cell->addInput(id_RGB1PWM);
        new_cell->addInput(id_RGB2PWM);
        new_cell->addOutput(id_RGB0);
        new_cell->addOutput(id_RGB1);
        new_cell->addOutput(id_RGB2);
    } else if (type == id_SB_LED_DRV_CUR) {
        new_cell->addInput(id_EN);
        new_cell->addOutput(id_LEDPU);
    } else if (type == id_SB_RGB_DRV) {
        new_cell->params[id_RGB0_CURRENT] = std::string("0b000000");
        new_cell->params[id_RGB1_CURRENT] = std::string("0b000000");
        new_cell->params[id_RGB2_CURRENT] = std::string("0b000000");

        new_cell->addInput(id_RGBPU);
        new_cell->addInput(id_RGBLEDEN);
        new_cell->addInput(id_RGB0PWM);
        new_cell->addInput(id_RGB1PWM);
        new_cell->addInput(id_RGB2PWM);
        new_cell->addOutput(id_RGB0);
        new_cell->addOutput(id_RGB1);
        new_cell->addOutput(id_RGB2);
    } else if (type == id_SB_LEDDA_IP) {
        new_cell->addInput(id_LEDDCS);
        new_cell->addInput(id_LEDDCLK);
        for (int i = 0; i < 8; i++)
            new_cell->addInput(ctx->id("LEDDDAT" + std::to_string(i)));
        for (int i = 0; i < 3; i++)
            new_cell->addInput(ctx->id("LEDDADDR" + std::to_string(i)));
        new_cell->addInput(id_LEDDDEN);
        new_cell->addInput(id_LEDDEXE);
        new_cell->addInput(id_LEDDRST); // doesn't actually exist, for icecube code compatibility
                                        // only
        new_cell->addOutput(id_PWMOUT0);
        new_cell->addOutput(id_PWMOUT1);
        new_cell->addOutput(id_PWMOUT2);
        new_cell->addOutput(id_LEDDON);
    } else if (type == id_SB_I2C) {
        new_cell->params[id_I2C_SLAVE_INIT_ADDR] = std::string("0b1111100001");
        new_cell->params[id_BUS_ADDR74] = std::string("0b0001");
        for (int i = 0; i < 8; i++) {
            new_cell->addInput(ctx->id("SBADRI" + std::to_string(i)));
            new_cell->addInput(ctx->id("SBDATI" + std::to_string(i)));
            new_cell->addOutput(ctx->id("SBDATO" + std::to_string(i)));
        }
        new_cell->addInput(id_SBCLKI);
        new_cell->addInput(id_SBRWI);
        new_cell->addInput(id_SBSTBI);
        new_cell->addInput(id_SCLI);
        new_cell->addInput(id_SDAI);
        new_cell->addOutput(id_SBACKO);
        new_cell->addOutput(id_I2CIRQ);
        new_cell->addOutput(id_I2CWKUP);
        new_cell->addOutput(id_SCLO);
        new_cell->addOutput(id_SCLOE);
        new_cell->addOutput(id_SDAO);
        new_cell->addOutput(id_SDAOE);
    } else if (type == id_SB_SPI) {
        new_cell->params[id_BUS_ADDR74] = std::string("0b0000");
        for (int i = 0; i < 8; i++) {
            new_cell->addInput(ctx->id("SBADRI" + std::to_string(i)));
            new_cell->addInput(ctx->id("SBDATI" + std::to_string(i)));
            new_cell->addOutput(ctx->id("SBDATO" + std::to_string(i)));
        }
        new_cell->addInput(id_SBCLKI);
        new_cell->addInput(id_SBRWI);
        new_cell->addInput(id_SBSTBI);
        new_cell->addInput(id_MI);
        new_cell->addInput(id_SI);
        new_cell->addInput(id_SCKI);
        new_cell->addInput(id_SCSNI);
        new_cell->addOutput(id_SBACKO);
        new_cell->addOutput(id_SPIIRQ);
        new_cell->addOutput(id_SPIWKUP);
        new_cell->addOutput(id_SO);
        new_cell->addOutput(id_SOE);
        new_cell->addOutput(id_MO);
        new_cell->addOutput(id_MOE);
        new_cell->addOutput(id_SCKO);
        new_cell->addOutput(id_SCKOE);
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
    lc->params[id_LUT_INIT] = lut->params[id_LUT_INIT].extract(0, 16, Property::State::S0);
    lut->movePortTo(id_I0, lc, id_I0);
    lut->movePortTo(id_I1, lc, id_I1);
    lut->movePortTo(id_I2, lc, id_I2);
    lut->movePortTo(id_I3, lc, id_I3);
    if (no_dff) {
        lut->movePortTo(id_O, lc, id_O);
        lc->params[id_DFF_ENABLE] = Property::State::S0;
    }
}

void dff_to_lc(const Context *ctx, CellInfo *dff, CellInfo *lc, bool pass_thru_lut)
{
    if (lc->hierpath == IdString())
        lc->hierpath = dff->hierpath;
    lc->params[id_DFF_ENABLE] = Property::State::S1;
    std::string config = dff->type.str(ctx).substr(6);
    auto citer = config.begin();
    dff->movePortTo(id_C, lc, id_CLK);

    if (citer != config.end() && *citer == 'N') {
        lc->params[id_NEG_CLK] = Property::State::S1;
        ++citer;
    } else {
        lc->params[id_NEG_CLK] = Property::State::S0;
    }

    if (citer != config.end() && *citer == 'E') {
        dff->movePortTo(id_E, lc, id_CEN);
        ++citer;
    }

    if (citer != config.end()) {
        if ((config.end() - citer) >= 2) {
            char c = *(citer++);
            NPNR_ASSERT(c == 'S');
            lc->params[id_ASYNC_SR] = Property::State::S0;
        } else {
            lc->params[id_ASYNC_SR] = Property::State::S1;
        }

        if (*citer == 'S') {
            citer++;
            dff->movePortTo(id_S, lc, id_SR);
            lc->params[id_SET_NORESET] = Property::State::S1;
        } else {
            NPNR_ASSERT(*citer == 'R');
            citer++;
            dff->movePortTo(id_R, lc, id_SR);
            lc->params[id_SET_NORESET] = Property::State::S0;
        }
    }

    NPNR_ASSERT(citer == config.end());

    if (pass_thru_lut) {
        lc->params[id_LUT_INIT] = Property(2, 16);
        dff->movePortTo(id_D, lc, id_I0);
    }

    dff->movePortTo(id_Q, lc, id_O);
}

void nxio_to_sb(Context *ctx, CellInfo *nxio, CellInfo *sbio, pool<IdString> &todelete_cells)
{
    bool pull_up_attr = false;

    if (nxio->type == ctx->id("$nextpnr_ibuf")) {
        sbio->params[id_PIN_TYPE] = 1;
        nxio->movePortTo(id_O, sbio, id_D_IN_0);
        pull_up_attr = true;
    } else if (nxio->type == ctx->id("$nextpnr_obuf")) {
        NetInfo *i = nxio->getPort(id_I);
        if (i == nullptr || i->driver.cell == nullptr) {
            sbio->params[id_PIN_TYPE] = 1;
            pull_up_attr = true;
        } else
            sbio->params[id_PIN_TYPE] = 25;
        nxio->movePortTo(id_I, sbio, id_D_OUT_0);
    } else if (nxio->type == ctx->id("$nextpnr_iobuf")) {
        // N.B. tristate will be dealt with below
        NetInfo *i = nxio->getPort(id_I);
        if (i == nullptr || i->driver.cell == nullptr) {
            sbio->params[id_PIN_TYPE] = 1;
            pull_up_attr = true;
        } else
            sbio->params[id_PIN_TYPE] = 25;
        nxio->movePortTo(id_I, sbio, id_D_OUT_0);
        nxio->movePortTo(id_O, sbio, id_D_IN_0);
    } else {
        NPNR_ASSERT(false);
    }
    NetInfo *donet = sbio->ports.at(id_D_OUT_0).net, *dinet = sbio->ports.at(id_D_IN_0).net;

    // Rename I/O nets to avoid conflicts
    if (donet != nullptr && donet->name == nxio->name)
        if (donet)
            ctx->renameNet(donet->name, ctx->id(donet->name.str(ctx) + "$SB_IO_OUT"));
    if (dinet != nullptr && dinet->name == nxio->name)
        if (dinet)
            ctx->renameNet(dinet->name, ctx->id(dinet->name.str(ctx) + "$SB_IO_IN"));

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
        sbio->connectPort(id_PACKAGE_PIN, toplevel_net);
        ctx->ports[nxio->name].net = toplevel_net;
    }

    CellInfo *tbuf = net_driven_by(
            ctx, donet, [](const Context *ctx, const CellInfo *cell) { return cell->type == ctx->id("$_TBUF_"); },
            id_Y);
    if (tbuf) {
        sbio->params[id_PIN_TYPE] = 41;
        tbuf->movePortTo(id_A, sbio, id_D_OUT_0);
        tbuf->movePortTo(id_E, sbio, id_OUTPUT_ENABLE);
        pull_up_attr = true;

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

    // Copy pull-up attribute if there's any chance output driver isn't active
    if (pull_up_attr) {
        auto pu_attr = nxio->attrs.find(id_PULLUP);
        if (pu_attr != nxio->attrs.end())
            sbio->params[id_PULLUP] = pu_attr->second;
    }
}

uint8_t sb_pll40_type(const BaseCtx *ctx, const CellInfo *cell)
{
    if (cell->type == id_SB_PLL40_PAD)
        return 2;
    if (cell->type == id_SB_PLL40_2_PAD)
        return 4;
    if (cell->type == id_SB_PLL40_2F_PAD)
        return 6;
    if (cell->type == id_SB_PLL40_CORE)
        return 3;
    if (cell->type == id_SB_PLL40_2F_CORE)
        return 7;
    NPNR_ASSERT(0);
}

bool is_clock_port(const BaseCtx *ctx, const PortRef &port)
{
    if (port.cell == nullptr)
        return false;
    if (is_ff(ctx, port.cell))
        return port.port == id_C;
    if (port.cell->type == id_ICESTORM_LC)
        return port.port == id_CLK;
    if (is_ram(ctx, port.cell) || port.cell->type == id_ICESTORM_RAM)
        return port.port.in(id_RCLK, id_WCLK, id_RCLKN, id_WCLKN);
    if (is_sb_mac16(ctx, port.cell) || port.cell->type == id_ICESTORM_DSP)
        return port.port == id_CLK;
    if (is_sb_spram(ctx, port.cell) || port.cell->type == id_ICESTORM_SPRAM)
        return port.port == id_CLOCK;
    if (is_sb_io(ctx, port.cell))
        return port.port.in(id_INPUT_CLK, id_OUTPUT_CLK);
    return false;
}

bool is_reset_port(const BaseCtx *ctx, const PortRef &port)
{
    if (port.cell == nullptr)
        return false;
    if (is_ff(ctx, port.cell))
        return port.port.in(id_R, id_S);
    if (port.cell->type == id_ICESTORM_LC)
        return port.port == id_SR;
    if (is_sb_mac16(ctx, port.cell) || port.cell->type == id_ICESTORM_DSP)
        return port.port.in(id_IRSTTOP, id_IRSTBOT, id_ORSTTOP, id_ORSTBOT);
    return false;
}

bool is_enable_port(const BaseCtx *ctx, const PortRef &port)
{
    if (port.cell == nullptr)
        return false;
    if (is_ff(ctx, port.cell))
        return port.port == id_E;
    if (port.cell->type == id_ICESTORM_LC)
        return port.port == id_CEN;
    // FIXME
    // if (is_sb_mac16(ctx, port.cell) || port.cell->type == id_ICESTORM_DSP)
    //    return port.port == id_CE;
    return false;
}

NEXTPNR_NAMESPACE_END
