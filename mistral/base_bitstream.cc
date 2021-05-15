/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

#include "log.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
// Device-specific default config for the sx120f die
void default_sx120f(CycloneV *cv)
{
    // Default PMA config?
    cv->bmux_m_set(CycloneV::PMA3, CycloneV::xy2pos(0, 11), CycloneV::FFPLL_IQCLK_DIRECTION, 0, CycloneV::TRISTATE);
    cv->bmux_m_set(CycloneV::PMA3, CycloneV::xy2pos(0, 11), CycloneV::FFPLL_IQCLK_DIRECTION, 1, CycloneV::TRISTATE);
    cv->bmux_m_set(CycloneV::PMA3, CycloneV::xy2pos(0, 23), CycloneV::FFPLL_IQCLK_DIRECTION, 0, CycloneV::DOWN);
    cv->bmux_m_set(CycloneV::PMA3, CycloneV::xy2pos(0, 23), CycloneV::FFPLL_IQCLK_DIRECTION, 1, CycloneV::UP);
    cv->bmux_m_set(CycloneV::PMA3, CycloneV::xy2pos(0, 35), CycloneV::FFPLL_IQCLK_DIRECTION, 0, CycloneV::UP);
    cv->bmux_m_set(CycloneV::PMA3, CycloneV::xy2pos(0, 35), CycloneV::FFPLL_IQCLK_DIRECTION, 1, CycloneV::UP);
    cv->bmux_b_set(CycloneV::PMA3, CycloneV::xy2pos(0, 35), CycloneV::FPLL_DRV_EN, 0, 0);
    cv->bmux_m_set(CycloneV::PMA3, CycloneV::xy2pos(0, 35), CycloneV::HCLK_TOP_OUT_DRIVER, 0, CycloneV::TRISTATE);
    // Default PLL config
    cv->bmux_b_set(CycloneV::FPLL, CycloneV::xy2pos(0, 73), CycloneV::PL_AUX_ATB_EN0, 0, 1);
    cv->bmux_b_set(CycloneV::FPLL, CycloneV::xy2pos(0, 73), CycloneV::PL_AUX_ATB_EN0_PRECOMP, 0, 1);
    cv->bmux_b_set(CycloneV::FPLL, CycloneV::xy2pos(0, 73), CycloneV::PL_AUX_ATB_EN1, 0, 1);
    cv->bmux_b_set(CycloneV::FPLL, CycloneV::xy2pos(0, 73), CycloneV::PL_AUX_ATB_EN1_PRECOMP, 0, 1);
    cv->bmux_b_set(CycloneV::FPLL, CycloneV::xy2pos(0, 73), CycloneV::PL_AUX_BG_KICKSTART, 0, 1);
    cv->bmux_b_set(CycloneV::FPLL, CycloneV::xy2pos(0, 73), CycloneV::PL_AUX_VBGMON_POWERDOWN, 0, 1);
    // Default TERM config
    cv->bmux_b_set(CycloneV::TERM, CycloneV::xy2pos(89, 34), CycloneV::INTOSC_2_EN, 0, 0);

    // TODO: what if these pins are used? where do these come from
    for (int z = 0; z < 4; z++) {
        cv->bmux_m_set(CycloneV::GPIO, CycloneV::xy2pos(89, 43), CycloneV::IOCSR_STD, z, CycloneV::NVR_LOW);
        cv->bmux_m_set(CycloneV::GPIO, CycloneV::xy2pos(89, 66), CycloneV::IOCSR_STD, z, CycloneV::NVR_LOW);
    }
    for (int y : {38, 44, 51, 58, 65, 73, 79}) {
        // TODO: Why only these upper DQS? is there a pattern?
        cv->bmux_b_set(CycloneV::DQS16, CycloneV::xy2pos(89, y), CycloneV::RB_2X_CLK_DQS_INV, 0, 1);
        cv->bmux_b_set(CycloneV::DQS16, CycloneV::xy2pos(89, y), CycloneV::RB_ACLR_LFIFO_EN, 0, 1);
        cv->bmux_b_set(CycloneV::DQS16, CycloneV::xy2pos(89, y), CycloneV::RB_LFIFO_BYPASS, 0, 0);
    }

    // Discover these mux values using
    //   grep 'i [_A-Z0-9.]* 1' empty.bt
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(0, 12), 69), true);
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(0, 13), 4), true);
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(0, 34), 69), true);
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(0, 35), 4), true);
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(0, 37), 31), true);
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(0, 40), 43), true);
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(0, 46), 69), true);
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(0, 47), 43), true);
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(0, 53), 69), true);
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(0, 54), 4), true);
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(0, 73), 68), true);

    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(9, 18), 66), true);
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(9, 20), 8), true);
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(9, 27), 69), true);
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(9, 28), 43), true);
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(9, 59), 66), true);
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(9, 61), 8), true);
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(9, 68), 69), true);
    cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(9, 69), 43), true);

    for (int z = 10; z <= 45; z++)
        cv->inv_set(CycloneV::rnode(CycloneV::GOUT, CycloneV::xy2pos(51, 80), z), true);
}
} // namespace

void Arch::init_base_bitstream()
{
    switch (cyclonev->current_model()->variant.die.type) {
    case CycloneV::SX120F:
        default_sx120f(cyclonev);
        break;
    default:
        log_error("FIXME: die type %s currently unsupported for bitgen.\n",
                  cyclonev->current_model()->variant.die.name);
    }
}

NEXTPNR_NAMESPACE_END
