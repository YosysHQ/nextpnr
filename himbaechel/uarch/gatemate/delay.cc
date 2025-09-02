/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
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

#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include "gatemate.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

delay_t GateMateImpl::estimateDelay(WireId src, WireId dst) const
{
    int sx, sy, dx, dy;
    tile_xy(ctx->chip_info, src.tile, sx, sy);
    tile_xy(ctx->chip_info, dst.tile, dx, dy);

    return 100 + 100 * (std::abs(dx - sx) + std::abs(dy - sy));
}

bool GateMateImpl::get_delay_from_tmg_db(IdString id, DelayQuad &delay) const
{
    auto fnd = timing.find(id);
    if (fnd != timing.end()) {
        delay = DelayQuad(fnd->second->delay.fast_min, fnd->second->delay.fast_max, fnd->second->delay.slow_min,
                          fnd->second->delay.slow_max);
        return true;
    }
    return false;
}

void GateMateImpl::get_setuphold_from_tmg_db(IdString id_setup, IdString id_hold, DelayPair &setup,
                                             DelayPair &hold) const
{
    auto fnd = timing.find(id_setup);
    if (fnd != timing.end()) {
        setup.min_delay = fnd->second->delay.fast_min;
        setup.max_delay = fnd->second->delay.fast_max;
    }
    fnd = timing.find(id_hold);
    if (fnd != timing.end()) {
        hold.min_delay = fnd->second->delay.fast_min;
        hold.max_delay = fnd->second->delay.fast_max;
    }
}

void GateMateImpl::get_setuphold_from_tmg_db(IdString id_setuphold, DelayPair &setup, DelayPair &hold) const
{
    auto fnd = timing.find(id_setuphold);
    if (fnd != timing.end()) {
        setup.min_delay = fnd->second->delay.fast_min;
        setup.max_delay = fnd->second->delay.fast_max;
        hold.min_delay = fnd->second->delay.slow_min;
        hold.max_delay = fnd->second->delay.slow_max;
    }
}

bool GateMateImpl::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const
{
    delay = DelayQuad{0};
    static dict<IdString, IdString> map_upper = {
            {id_OUT, id_OUT2},
            {id_RAM_O, id_RAM_O2},
            {id_RAM_I, id_RAM_I2},
            {id_CPOUT, id_CPOUT2},
    };
    static dict<IdString, IdString> map_lower = {
            {id_OUT, id_OUT1}, {id_RAM_O, id_RAM_O1}, {id_RAM_I, id_RAM_I1}, {id_CPOUT, id_CPOUT1},
            {id_IN1, id_IN5},  {id_IN2, id_IN6},      {id_IN3, id_IN7},      {id_IN4, id_IN8},
    };

    int z = (cell->bel != BelId()) ? (ctx->getBelLocation(cell->bel).z % 2) : 0;
    if (cell->type.in(id_CPE_L2T4, id_CPE_LT_L, id_CPE_LT_U)) {
        IdString fp = fromPort, tp = toPort;
        if (z == 0) {
            if (map_upper.count(fp))
                fp = map_upper[fp];
            if (map_upper.count(tp))
                tp = map_upper[tp];
        } else {
            if (map_lower.count(fp))
                fp = map_lower[fp];
            if (map_lower.count(tp))
                tp = map_lower[tp];
        }
        return get_delay_from_tmg_db(ctx->idf("timing__ARBLUT_%s_%s", fp.c_str(ctx), tp.c_str(ctx)), delay);
    } else if (cell->type.in(id_CPE_ADDF, id_CPE_ADDF2)) {
        return get_delay_from_tmg_db(ctx->idf("timing__ADDF2Y1_%s_%s", fromPort.c_str(ctx), toPort.c_str(ctx)), delay);
    } else if (cell->type.in(id_CPE_MX4)) {
        return get_delay_from_tmg_db(ctx->idf("timing__MX4A_%s_%s", fromPort.c_str(ctx), toPort.c_str(ctx)), delay);
    } else if (cell->type.in(id_CPE_MULT)) {
        return get_delay_from_tmg_db(ctx->idf("timing__MULT_%s_%s", fromPort.c_str(ctx), toPort.c_str(ctx)), delay);
    } else if (cell->type.in(id_CPE_FF, id_CPE_LATCH, id_CPE_FF_L, id_CPE_FF_U)) {
        return false;
    } else if (cell->type.in(id_CPE_CPLINES)) {
        return true;
    } else if (cell->type.in(id_CPE_COMP)) {
        return get_delay_from_tmg_db(fromPort == id_COMB1 ? id_timing_comb1_compout : id_timing_comb2_compout, delay);
    } else if (cell->type.in(id_CPE_RAMI, id_CPE_RAMO, id_CPE_RAMIO)) {
        if (fromPort == id_I && toPort == id_RAM_O)
            return get_delay_from_tmg_db(z ? id_timing_comb12_RAM_O1 : id_timing_comb12_RAM_O2, delay);
        if (fromPort == id_RAM_I && toPort == id_OUT)
            return true;
        return false;
    } else if (cell->type.in(id_CPE_IBUF, id_CPE_OBUF, id_CPE_TOBUF, id_CPE_IOBUF)) {
        if (fromPort == id_A && toPort == id_O)
            return get_delay_from_tmg_db(id_timing_del_OBF, delay);
        if (fromPort == id_T && toPort == id_O)
            return get_delay_from_tmg_db(id_timing_del_TOBF_ctrl, delay);
        if (fromPort == id_I && toPort == id_Y)
            return get_delay_from_tmg_db(id_timing_del_IBF, delay);
        return true;
    } else if (cell->type.in(id_CPE_LVDS_IBUF, id_CPE_LVDS_OBUF, id_CPE_LVDS_TOBUF, id_CPE_LVDS_IOBUF)) {
        if (fromPort == id_A && toPort.in(id_O_P, id_O_N))
            return get_delay_from_tmg_db(id_timing_del_LVDS_OBF, delay);
        if (fromPort == id_T && toPort.in(id_O_P, id_O_N))
            return get_delay_from_tmg_db(id_timing_del_LVDS_TOBF_ctrl, delay);
        if (fromPort.in(id_I_P, id_I_N) && toPort == id_Y)
            return get_delay_from_tmg_db(id_timing_del_LVDS_IBF, delay);
        return true;
    } else if (cell->type.in(id_IOSEL)) {
        bool output = bool_or_default(cell->params, id_OUT_SIGNAL);
        bool enable = bool_or_default(cell->params, id_OE_ENABLE);
        IdString o_s = bool_or_default(cell->params, id_OUT23_14_SEL)
                               ? (bool_or_default(cell->params, id_OUT2_3) ? id_OUT3 : id_OUT2)
                               : (bool_or_default(cell->params, id_OUT1_4) ? id_OUT4 : id_OUT1);
        int oe = int_or_default(cell->params, id_OE_SIGNAL);
        IdString oe_s = (oe & 2) ? ((oe & 1) ? id_OUT4 : id_OUT3) : ((oe & 1) ? id_OUT2 : id_OUT1);
        if (output && fromPort != o_s)
            return false;
        if (enable && fromPort != oe_s)
            return false;
        return get_delay_from_tmg_db(ctx->idf("timing_io_sel_%s_%s", fromPort.c_str(ctx), toPort.c_str(ctx)), delay);
    } else if (cell->type.in(id_CLKIN)) {
        return get_delay_from_tmg_db(ctx->idf("timing_clkin_%s_%s", fromPort.c_str(ctx), toPort.c_str(ctx)), delay);
    } else if (cell->type.in(id_GLBOUT)) {
        return get_delay_from_tmg_db(ctx->idf("timing_glbout_%s_%s", fromPort.c_str(ctx), toPort.c_str(ctx)), delay);
    } else if (cell->type.in(id_RAM, id_RAM_HALF)) {
        return false;
    }
    return false;
}

TimingPortClass GateMateImpl::getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
{
    auto disconnected = [cell](IdString p) { return !cell->ports.count(p) || cell->ports.at(p).net == nullptr; };
    clockInfoCount = 0;
    if (cell->type.in(id_CPE_L2T4, id_CPE_LT_L, id_CPE_LT_U)) {
        if (port.in(id_IN1, id_IN2, id_IN3, id_IN4, id_COMBIN, id_CINY1, id_CINY2, id_CINX, id_PINX))
            return TMG_COMB_INPUT;
        if (port == id_OUT && disconnected(id_IN1) && disconnected(id_IN2) && disconnected(id_IN3) &&
            disconnected(id_IN4))
            return TMG_IGNORE; // LUT with no inputs is a constant
        if (port.in(id_OUT))
            return TMG_COMB_OUTPUT;
        return TMG_IGNORE;
    } else if (cell->type.in(id_CPE_ADDF, id_CPE_ADDF2)) {
        if (port.in(id_IN1, id_IN2, id_IN3, id_IN4, id_IN5, id_IN6, id_IN7, id_IN8, id_CINX, id_CINY1))
            return TMG_COMB_INPUT;
        if (port.in(id_OUT1, id_OUT2, id_COUTY1))
            return TMG_COMB_OUTPUT;
        return TMG_IGNORE;
    } else if (cell->type.in(id_CPE_MX4)) {
        if (port.in(id_IN1, id_IN2, id_IN3, id_IN4, id_IN5, id_IN6, id_IN7, id_IN8))
            return TMG_COMB_INPUT;
        if (port.in(id_OUT1))
            return TMG_COMB_OUTPUT;
        return TMG_IGNORE;
    } else if (cell->type.in(id_CPE_MULT)) {
        if (port.in(id_IN1, id_IN5, id_IN8, id_CINX, id_PINX, id_CINY1, id_CINY2, id_PINY1, id_PINY2))
            return TMG_COMB_INPUT;
        return TMG_COMB_OUTPUT;
    } else if (cell->type.in(id_CPE_CPLINES)) {
        if (port.in(id_OUT1, id_OUT2, id_COMPOUT, id_CINX, id_PINX, id_CINY1, id_PINY1, id_CINY2, id_PINY2))
            return TMG_COMB_INPUT;
        return TMG_COMB_OUTPUT;
    } else if (cell->type.in(id_CPE_FF, id_CPE_FF_L, id_CPE_FF_U, id_CPE_LATCH)) {
        if (port == id_CLK)
            return TMG_CLOCK_INPUT;
        clockInfoCount = 1;
        if (port == id_DOUT)
            return TMG_REGISTER_OUTPUT;
        // DIN, EN and SR
        return TMG_REGISTER_INPUT;
    } else if (cell->type.in(id_CPE_RAMI, id_CPE_RAMO, id_CPE_RAMIO, id_CPE_RAMIO_U, id_CPE_RAMIO_L)) {
        if (port.in(id_I, id_RAM_I))
            return TMG_COMB_INPUT;
        if (port.in(id_O, id_RAM_O))
            return TMG_COMB_OUTPUT;
        return TMG_IGNORE;
    } else if (cell->type.in(id_CPE_COMP)) {
        if (port.in(id_COMB1, id_COMB2))
            return TMG_COMB_INPUT;
        return TMG_COMB_OUTPUT;
    } else if (cell->type.in(id_CPE_IBUF, id_CPE_OBUF, id_CPE_TOBUF, id_CPE_IOBUF)) {
        if (port.in(id_O))
            return TMG_ENDPOINT;
        if (port.in(id_Y))
            return TMG_STARTPOINT;
        return TMG_IGNORE;
    } else if (cell->type.in(id_CPE_LVDS_IBUF, id_CPE_LVDS_OBUF, id_CPE_LVDS_TOBUF, id_CPE_LVDS_IOBUF)) {
        if (port.in(id_O_P, id_O_N))
            return TMG_ENDPOINT;
        if (port.in(id_Y))
            return TMG_STARTPOINT;
        return TMG_IGNORE;
    } else if (cell->type.in(id_IOSEL)) {
        if (port.in(id_IN1, id_IN2, id_GPIO_EN, id_GPIO_OUT))
            return TMG_COMB_OUTPUT;
        if (port.in(id_OUT1, id_OUT2, id_OUT3, id_OUT4, id_GPIO_IN))
            return TMG_COMB_INPUT;
        return TMG_IGNORE;
    } else if (cell->type.in(id_PLL)) {
        if (port.in(id_CLK_REF, id_USR_CLK_REF))
            return TMG_CLOCK_INPUT;
        if (port.in(id_CLK0, id_CLK90, id_CLK180, id_CLK270))
            return TMG_GEN_CLOCK;
        return TMG_IGNORE;
    } else if (cell->type.in(id_CLKIN)) {
        if (port.in(id_CLK0, id_CLK1, id_CLK2, id_CLK3, id_SER_CLK))
            return TMG_CLOCK_INPUT;
        if (port.in(id_CLK_REF0, id_CLK_REF1, id_CLK_REF2, id_CLK_REF3))
            return TMG_GEN_CLOCK;
        return TMG_IGNORE;
    } else if (cell->type.in(id_GLBOUT)) {
        if (port.in(id_CLK0_0, id_CLK90_0, id_CLK180_0, id_CLK270_0, id_CLK_REF_OUT0, id_CLK0_1, id_CLK90_1,
                    id_CLK180_1, id_CLK270_1, id_CLK_REF_OUT1, id_CLK0_2, id_CLK90_2, id_CLK180_2, id_CLK270_2,
                    id_CLK_REF_OUT2, id_CLK0_3, id_CLK90_3, id_CLK180_3, id_CLK270_3, id_CLK_REF_OUT3, id_USR_GLB0,
                    id_USR_GLB1, id_USR_GLB2, id_USR_GLB3))
            return TMG_CLOCK_INPUT;
        if (port.in(id_GLB0, id_GLB1, id_GLB2, id_GLB3))
            return TMG_GEN_CLOCK;
        return TMG_IGNORE;
    } else if (cell->type.in(id_SERDES)) {
        return TMG_IGNORE;
    } else if (cell->type.in(id_USR_RSTN)) {
        return TMG_IGNORE;
    } else if (cell->type.in(id_CFG_CTRL)) {
        if (port.in(id_CLK))
            return TMG_CLOCK_INPUT;
        return TMG_IGNORE;
    } else if (cell->type.in(id_RAM, id_RAM_HALF)) {
        std::string name = port.str(ctx);
        if (boost::starts_with(name, "CLKA[") || boost::starts_with(name, "CLKB[") || boost::starts_with(name, "CLOCK"))
            return TMG_CLOCK_INPUT;
        if (boost::ends_with(name, "_CI") || boost::ends_with(name, "_CO"))
            return TMG_IGNORE;
        if (name[0] == 'F') // Ignore forward and FIFO pins
            return TMG_IGNORE;
        for (auto c : boost::adaptors::reverse(name)) {
            if (std::isdigit(c) || c == 'X' || c == '[' || c == ']')
                continue;
            if (c == 'A' || c == 'B')
                clockInfoCount = 1;
            else
                NPNR_ASSERT_FALSE_STR("bad ram port");
            return (cell->ports.at(port).type == PORT_OUT) ? TMG_REGISTER_OUTPUT : TMG_REGISTER_INPUT;
        }
        NPNR_ASSERT_FALSE_STR("no timing type for RAM port '" + port.str(ctx) + "'");
    } else {
        log_error("cell type '%s' is unsupported (instantiated as '%s')\n", cell->type.c_str(ctx),
                  cell->name.c_str(ctx));
    }
}

IdString clock(uint8_t val, IdString clk1, IdString clk2, IdString clk3, IdString clk4)
{
    switch (val) {
    case 0b00000000:
        return clk1;
    case 0b00000100:
        return clk2;
    case 0b00001000:
        return clk3;
    case 0b00001100:
        return clk4;
    case 0b00100011:
        return id_CLOCK1;
    case 0b00110011:
        return id_CLOCK2;
    case 0b00000011:
        return id_CLOCK3;
    case 0b00010011:
        return id_CLOCK4;
    default:
        return clk1;
    }
}

TimingClockingInfo GateMateImpl::getPortClockingInfo(const CellInfo *cell, IdString port, int index) const
{
    TimingClockingInfo info;
    info.setup = DelayPair(0);
    info.hold = DelayPair(0);
    info.clockToQ = DelayQuad(0);
    if (cell->type.in(id_CPE_FF, id_CPE_FF_L, id_CPE_FF_U, id_CPE_LATCH)) {
        bool inverted = int_or_default(cell->params, id_C_CPE_CLK, 0) == 0b01;
        info.edge = inverted ? FALLING_EDGE : RISING_EDGE;
        info.clock_port = id_CLK;
        if (port.in(id_DIN, id_EN, id_SR))
            get_setuphold_from_tmg_db(id_timing_del_Setup_D_L, id_timing_del_Hold_D_L, info.setup, info.hold);
        if (port.in(id_DOUT)) {
            bool is_upper = (cell->bel != BelId()) && (ctx->getBelLocation(cell->bel).z == CPE_LT_U_Z);
            get_delay_from_tmg_db(id_timing__SEQ_CLK_FF1_Q, info.clockToQ);
            DelayQuad delay = DelayQuad{0};
            get_delay_from_tmg_db(is_upper ? id_timing_Q2_OUT2 : id_timing_Q1_OUT1, delay);
            info.clockToQ += delay;
            get_delay_from_tmg_db(id_timing_del_CPE_CP_Q, delay);
            info.clockToQ += delay;
        }
    } else if (cell->type.in(id_RAM, id_RAM_HALF)) {
        std::string name = port.str(ctx);
        if (boost::starts_with(name, "CLOCK"))
            get_delay_from_tmg_db(id_timing_RAM_NOECC_IOPATH_1, info.clockToQ);
        if (boost::starts_with(name, "DOA"))
            get_delay_from_tmg_db(id_timing_RAM_NOECC_IOPATH_2, info.clockToQ);
        if (boost::starts_with(name, "DOB"))
            get_delay_from_tmg_db(id_timing_RAM_NOECC_IOPATH_3, info.clockToQ);
        if (boost::starts_with(name, "ECC"))
            get_delay_from_tmg_db(id_timing_RAM_NOECC_IOPATH_4, info.clockToQ);
        if (boost::starts_with(name, "ADDR"))
            get_setuphold_from_tmg_db(id_timing_RAM_NOECC_SETUPHOLD_1, info.setup, info.hold);
        if (boost::starts_with(name, "CLOCK1"))
            get_setuphold_from_tmg_db(id_timing_RAM_NOECC_SETUPHOLD_2, info.setup, info.hold);
        if (boost::starts_with(name, "DIA"))
            get_setuphold_from_tmg_db(id_timing_RAM_NOECC_SETUPHOLD_3, info.setup, info.hold);
        if (boost::starts_with(name, "DIB"))
            get_setuphold_from_tmg_db(id_timing_RAM_NOECC_SETUPHOLD_4, info.setup, info.hold);
        if (boost::starts_with(name, "ENA"))
            get_setuphold_from_tmg_db(id_timing_RAM_NOECC_SETUPHOLD_5, info.setup, info.hold);
        if (boost::starts_with(name, "ENB"))
            get_setuphold_from_tmg_db(id_timing_RAM_NOECC_SETUPHOLD_6, info.setup, info.hold);
        if (boost::starts_with(name, "GLWEA"))
            get_setuphold_from_tmg_db(id_timing_RAM_NOECC_SETUPHOLD_7, info.setup, info.hold);
        if (boost::starts_with(name, "GLWEB"))
            get_setuphold_from_tmg_db(id_timing_RAM_NOECC_SETUPHOLD_8, info.setup, info.hold);
        if (boost::starts_with(name, "WEA"))
            get_setuphold_from_tmg_db(id_timing_RAM_NOECC_SETUPHOLD_9, info.setup, info.hold);
        if (boost::starts_with(name, "WEB"))
            get_setuphold_from_tmg_db(id_timing_RAM_NOECC_SETUPHOLD_10, info.setup, info.hold);
        bool is_clk_b = false;
        for (auto c : boost::adaptors::reverse(name)) {
            if (std::isdigit(c) || c == 'X' || c == '[' || c == ']')
                continue;
            if (c == 'A')
                is_clk_b = false;
            else if (c == 'B')
                is_clk_b = true;
            else
                NPNR_ASSERT_FALSE_STR("bad ram port");
            break;
        }

        bool inverted = int_or_default(cell->params, id_A_CLK_INV, 0);
        if (is_clk_b)
            inverted = int_or_default(cell->params, id_B_CLK_INV, 0);

        info.edge = inverted ? FALLING_EDGE : RISING_EDGE;
        uint8_t a0_clk_val = int_or_default(cell->params, id_RAM_cfg_forward_a0_clk, 0);
        uint8_t a1_clk_val = int_or_default(cell->params, id_RAM_cfg_forward_a1_clk, 0);
        uint8_t b0_clk_val = int_or_default(cell->params, id_RAM_cfg_forward_b0_clk, 0);
        uint8_t b1_clk_val = int_or_default(cell->params, id_RAM_cfg_forward_b1_clk, 0);
        IdString a0_clk =
                clock(a0_clk_val, ctx->id("CLKA[0]"), ctx->id("CLKA[1]"), ctx->id("CLKB[0]"), ctx->id("CLKB[1]"));
        IdString a1_clk =
                clock(a1_clk_val, ctx->id("CLKA[2]"), ctx->id("CLKA[3]"), ctx->id("CLKB[2]"), ctx->id("CLKB[3]"));
        IdString b0_clk =
                clock(b0_clk_val, ctx->id("CLKB[0]"), ctx->id("CLKB[1]"), ctx->id("CLKA[0]"), ctx->id("CLKA[1]"));
        IdString b1_clk =
                clock(b1_clk_val, ctx->id("CLKB[2]"), ctx->id("CLKB[3]"), ctx->id("CLKA[2]"), ctx->id("CLKA[3]"));
        if (ram_signal_clk.count(port)) {
            switch (ram_signal_clk.at(port)) {
            case 0:
                info.clock_port = a0_clk;
                break;
            case 1:
                info.clock_port = a1_clk;
                break;
            case 2:
                info.clock_port = b0_clk;
                break;
            case 3:
                info.clock_port = b1_clk;
                break;
            }
        } else {
            log_error("Uknown clock signal for %s\n", name.c_str());
        }
    }

    return info;
}

NEXTPNR_NAMESPACE_END
