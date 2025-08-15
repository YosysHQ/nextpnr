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

bool GateMateImpl::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const
{
    delay = DelayQuad{0};
    if (cell->type == id_CPE_L2T4) {
        return true;
    } else if (cell->type.in(id_CPE_FF, id_CPE_LATCH, id_CPE_FF_L, id_CPE_FF_U)) {
        return false;
    } else if (cell->type.in(id_CPE_RAMI, id_CPE_RAMO, id_CPE_RAMIO)) {
        if (fromPort == id_I && toPort == id_RAM_O)
            return true;
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
        return get_delay_from_tmg_db(ctx->idf("timing_io_sel_%s_%s", fromPort.c_str(ctx), toPort.c_str(ctx)), delay);
    } else if (cell->type.in(id_CLKIN)) {
        return get_delay_from_tmg_db(ctx->idf("timing_clkin_%s_%s", fromPort.c_str(ctx), toPort.c_str(ctx)), delay);
    } else if (cell->type.in(id_GLBOUT)) {
        return get_delay_from_tmg_db(ctx->idf("timing_glbout_%s_%s", fromPort.c_str(ctx), toPort.c_str(ctx)), delay);
    }
    return true;
    // return ctx->get_cell_delay_default(cell, fromPort, toPort, delay);
}

TimingPortClass GateMateImpl::getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
{
    auto disconnected = [cell](IdString p) { return !cell->ports.count(p) || cell->ports.at(p).net == nullptr; };
    clockInfoCount = 0;
    if (cell->type == id_CPE_L2T4) {
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
    } else if (cell->type == id_RAM) {
        std::string name = port.str(ctx);
        if (boost::starts_with(name, "CLKA[") || boost::starts_with(name, "CLKB[") || boost::starts_with(name, "CLOCK"))
            return TMG_CLOCK_INPUT;
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
        log_warning("cell type '%s' is unsupported (instantiated as '%s')\n", cell->type.c_str(ctx),
                    cell->name.c_str(ctx));
    }
    return TMG_IGNORE;
}

TimingClockingInfo GateMateImpl::getPortClockingInfo(const CellInfo *cell, IdString port, int index) const
{
    TimingClockingInfo info;
    info.setup = DelayPair(0);
    info.hold = DelayPair(0);
    info.clockToQ = DelayQuad(0);
    if (cell->type.in(id_CPE_FF, id_CPE_FF_L, id_CPE_FF_U)) {
        bool inverted = int_or_default(cell->params, id_C_CPE_CLK, 0) == 0b01;
        info.edge = inverted ? FALLING_EDGE : RISING_EDGE;
        info.clock_port = id_CLK;
        if (port.in(id_DIN, id_EN, id_SR))
            get_setuphold_from_tmg_db(id_timing_del_Setup_D_L, id_timing_del_Hold_D_L, info.setup, info.hold);
    }

    return info;
}

NEXTPNR_NAMESPACE_END
