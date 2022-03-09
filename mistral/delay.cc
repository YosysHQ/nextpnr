/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Lofty <dan.ravensloft@gmail.com>
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
 */

#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

TimingPortClass Arch::getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
{
    clockInfoCount = 0;
    if (cell->type.in(id_MISTRAL_NOT, id_MISTRAL_BUF, id_MISTRAL_ALUT2, id_MISTRAL_ALUT3, id_MISTRAL_ALUT4,
                      id_MISTRAL_ALUT5, id_MISTRAL_ALUT6)) {
        if (port.in(id_A, id_B, id_C, id_D, id_E, id_F))
            return TMG_COMB_INPUT;
        if (port == id_Q)
            return TMG_COMB_OUTPUT;
    } else if (cell->type == id_MISTRAL_ALUT_ARITH) {
        if (port.in(id_A, id_B, id_C, id_D0, id_D1, id_CI))
            return TMG_COMB_INPUT;
        if (port.in(id_SO, id_CO))
            return TMG_COMB_OUTPUT;
    } else if (cell->type == id_MISTRAL_FF) {
        if (port == id_CLK) {
            return TMG_CLOCK_INPUT;
        }
        // ACLR is considered synchronous for timing purposes.
        else if (port.in(id_DATAIN, id_ACLR, id_ENA, id_SCLR, id_SLOAD, id_SDATA)) {
            clockInfoCount = 1;
            return TMG_REGISTER_INPUT;
        } else if (port == id_Q) {
            clockInfoCount = 1;
            return TMG_REGISTER_OUTPUT;
        }
    } else if (cell->type == id_MISTRAL_MLAB) {
        if (port == id_CLK1) {
            return TMG_CLOCK_INPUT;
        } else if (port.in(id_A1DATA, id_A1EN) || port.str(this).find("A1ADDR") == 0) {
            clockInfoCount = 1;
            return TMG_REGISTER_INPUT;
        } else if (port.str(this).find("B1ADDR") == 0) {
            return TMG_COMB_INPUT;
        } else if (port.in(id_B1DATA)) {
            return TMG_COMB_OUTPUT;
        }
    } else if (cell->type == id_MISTRAL_M10K) {
        if (port == id_CLK1) {
            return TMG_CLOCK_INPUT;
        } else if (port.in(id_A1DATA, id_A1EN, id_B1EN) || port.str(this).find("A1ADDR") == 0) {
            clockInfoCount = 1;
            return TMG_REGISTER_INPUT;
        } else if (port.str(this).find("B1ADDR") == 0) {
            return TMG_REGISTER_INPUT;
        } else if (port.in(id_B1DATA)) {
            return TMG_REGISTER_OUTPUT;
        }
    }
    return TMG_IGNORE;
}

TimingClockingInfo Arch::getPortClockingInfo(const CellInfo *cell, IdString port, int index) const
{
    TimingClockingInfo timing{};
    if (cell->type == id_MISTRAL_FF) {
        timing.clock_port = id_CLK;
        timing.edge = RISING_EDGE;
        // ACLR is considered synchronous for timing purposes.
        if (port.in(id_DATAIN, id_ACLR, id_ENA, id_SCLR, id_SLOAD, id_SDATA)) {
            timing.setup = DelayPair{-196, -196};
            timing.hold = DelayPair{270, 270};
            timing.clockToQ = DelayQuad{};
        } else if (port == id_Q) {
            timing.setup = DelayPair{};
            timing.hold = DelayPair{};
            timing.clockToQ = DelayQuad{731};
        }
        return timing;
    } else if (cell->type == id_MISTRAL_MLAB) {
        timing.clock_port = id_CLK1;
        timing.edge = RISING_EDGE;
        if (port.in(id_A1DATA, id_A1EN) || port.str(this).find("A1ADDR") == 0) {
            timing.setup = DelayPair{86, 86};
            timing.hold = DelayPair{42, 42};
            timing.clockToQ = DelayQuad{};
        }
        return timing;
    } else if (cell->type == id_MISTRAL_M10K) {
        timing.clock_port = id_CLK1;
        timing.edge = RISING_EDGE;
        if (port.str(this).find("A1ADDR") == 0 || port.str(this).find("B1ADDR") == 0) {
            timing.setup = DelayPair{125, 125};
            timing.hold = DelayPair{42, 42};
            timing.clockToQ = DelayQuad{};
        } else if (port == id_A1DATA) {
            timing.setup = DelayPair{97, 97};
            timing.hold = DelayPair{42, 42};
            timing.clockToQ = DelayQuad{};
        } else if (port == id_A1EN) {
            timing.setup = DelayPair{140, 140};
            timing.hold = DelayPair{42, 42};
            timing.clockToQ = DelayQuad{};
        } else if (port == id_B1EN) {
            timing.setup = DelayPair{161, 161};
            timing.hold = DelayPair{42, 42};
            timing.clockToQ = DelayQuad{};
        } else if (port == id_B1DATA) {
            timing.setup = DelayPair{};
            timing.hold = DelayPair{};
            timing.clockToQ = DelayQuad{1004};
        }
        return timing;
    }
    NPNR_ASSERT_FALSE("unreachable");
}

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const
{
    // Based on 1.1V 100C timing corner of sx120f, using delays from LUT input to DFF input.

    // I have many regrets about naming my cell ports how I did...

    // TODO list:
    // - MLABs-as-LABs have different timings to LABs
    // - speed grades

    if (cell->type.in(id_MISTRAL_NOT, id_MISTRAL_BUF, id_MISTRAL_ALUT2, id_MISTRAL_ALUT3, id_MISTRAL_ALUT4,
                      id_MISTRAL_ALUT5, id_MISTRAL_ALUT6)) {
        if (toPort == id_Q) {
            if (cell->type == id_MISTRAL_ALUT6 && fromPort == id_A) {
                delay = DelayQuad{/* RF */ 592, /* RR */ 605, /* FF */ 567, /* FR */ 573};
                return true;
            } else if ((cell->type == id_MISTRAL_ALUT5 && fromPort == id_A) ||
                       (cell->type == id_MISTRAL_ALUT6 && fromPort == id_B)) {
                delay = DelayQuad{/* RF */ 580, /* RR */ 583, /* FF */ 560, /* FR */ 574};
                return true;
            } else if ((cell->type == id_MISTRAL_ALUT4 && fromPort == id_A) ||
                       (cell->type == id_MISTRAL_ALUT5 && fromPort == id_B) ||
                       (cell->type == id_MISTRAL_ALUT6 && fromPort == id_C)) {
                delay = DelayQuad{/* RR */ 429, /* RF */ 496, /* FR */ 440, /* FF */ 510};
                return true;
            } else if ((cell->type == id_MISTRAL_ALUT3 && fromPort == id_A) ||
                       (cell->type == id_MISTRAL_ALUT4 && fromPort == id_B) ||
                       (cell->type == id_MISTRAL_ALUT5 && fromPort == id_C) ||
                       (cell->type == id_MISTRAL_ALUT6 && fromPort == id_D)) {
                delay = DelayQuad{/* RR */ 432, /* RF */ 499, /* FR */ 444, /* FF */ 512};
                return true;
            } else if ((cell->type == id_MISTRAL_ALUT2 && fromPort == id_A) ||
                       (cell->type == id_MISTRAL_ALUT3 && fromPort == id_B) ||
                       (cell->type == id_MISTRAL_ALUT4 && fromPort == id_C) ||
                       (cell->type == id_MISTRAL_ALUT5 && fromPort == id_D) ||
                       (cell->type == id_MISTRAL_ALUT6 && fromPort == id_E)) {
                delay = DelayQuad{/* RR */ 263, /* RF */ 354, /* FF */ 362, /* FR */ 400};
                return true;
            } else if ((cell->type.in(id_MISTRAL_NOT, id_MISTRAL_BUF) && fromPort == id_A) ||
                       (cell->type == id_MISTRAL_ALUT2 && fromPort == id_B) ||
                       (cell->type == id_MISTRAL_ALUT3 && fromPort == id_C) ||
                       (cell->type == id_MISTRAL_ALUT4 && fromPort == id_D) ||
                       (cell->type == id_MISTRAL_ALUT5 && fromPort == id_E) ||
                       (cell->type == id_MISTRAL_ALUT6 && fromPort == id_F)) {
                delay = DelayQuad{/* RR */ 90, /* RF */ 96, /* FF */ 83, /* FR */ 97};
                return true;
            }
        }
    } else if (cell->type == id_MISTRAL_ALUT_ARITH) {
        if (toPort == id_CO) {
            if (fromPort == id_A) {
                delay = DelayQuad{/* RF */ 1005, /* RR */ 1082, /* FF */ 971, /* FR */ 1048};
                return true;
            } else if (fromPort == id_B) {
                delay = DelayQuad{/* RF */ 986, /* RR */ 1062, /* FF */ 976, /* FR */ 1052};
                return true;
            } else if (fromPort == id_C) {
                delay = DelayQuad{/* RF */ 736, /* RR */ 813, /* FF */ 775, /* FR */ 800};
                return true;
            } else if (fromPort == id_D0) {
                delay = DelayQuad{/* RF */ 822, /* RR */ 866, /* FF */ 837, /* FR */ 849};
                return true;
            } else if (fromPort == id_D1) {
                delay = DelayQuad{/* RF */ 1122, /* RR */ 1198, /* FF */ 1128, /* FR */ 1197};
                return true;
            } else if (fromPort == id_CI) {
                // Divided by 2 to account for delay being across ALM rather than across ALUT.
                // Maybe this should be a routing delay.
                delay = DelayQuad{/* RR */ 63 / 2, /* RR */ 71 / 2, /* FF */ 63 / 2, /* FR */ 71 / 2};
                return true;
            }
        } else if (toPort == id_SO) {
            if (fromPort == id_A) {
                delay = DelayQuad{/* RF */ 1300, /* RR */ 1342, /* FF */ 1266, /* FR */ 1308};
                return true;
            } else if (fromPort == id_B) {
                delay = DelayQuad{/* RF */ 1280, /* RR */ 1323, /* FF */ 1270, /* FR */ 1313};
                return true;
            } else if (fromPort == id_C) {
                delay = DelayQuad{/* RF */ 866, /* RR */ 892, /* FF */ 908, /* FR */ 927};
                return true;
            } else if (fromPort == id_D0) {
                delay = DelayQuad{/* RR */ 779, /* RF */ 887, /* FR */ 761, /* FF */ 883};
                return true;
            } else if (fromPort == id_D1) {
                delay = DelayQuad{/* RR */ 700, /* RF */ 785, /* FR */ 696, /* FF */ 782};
                return true;
            } else if (fromPort == id_CI) {
                delay = DelayQuad{/* RR */ 350, /* RF */ 352, /* FF */ 361, /* FR */ 368};
                return true;
            }
        }
    } else if (cell->type == id_MISTRAL_MLAB) {
        if (toPort == id_B1DATA) {
            if (fromPort.str(this) == "B1ADDR[0]") {
                delay = DelayQuad{/* RF */ 473, /* RR */ 487, /* FR */ 452, /* FF */ 476};
                return true;
            } else if (fromPort.str(this) == "B1ADDR[1]") {
                delay = DelayQuad{/* RF */ 472, /* RR */ 475, /* FR */ 444, /* FF */ 460};
                return true;
            } else if (fromPort.str(this) == "B1ADDR[2]") {
                delay = DelayQuad{/* RF */ 343, /* RR */ 347, /* FR */ 358, /* FF */ 382};
                return true;
            } else if (fromPort.str(this) == "B1ADDR[3]") {
                delay = DelayQuad{/* RF */ 263, /* RR */ 268, /* FF */ 256, /* FR */ 284};
                return true;
            } else if (fromPort.str(this) == "B1ADDR[4]") {
                delay = DelayQuad{/* RF */ 89, /* RR */ 96, /* FF */ 73, /* FR */ 93};
                return true;
            }
        }
    }

    return false;
}

DelayQuad Arch::getPipDelay(PipId pip) const
{
    WireId src = getPipSrcWire(pip), dst = getPipDstWire(pip);
    if ((src.is_nextpnr_created() && dst.is_nextpnr_created()) || dst.is_nextpnr_created())
        return DelayQuad{20};

    // This is guesswork based on average of (interconnect delay / number of pips)
    auto dst_type = CycloneV::rn2t(dst.node);

    switch (dst_type) {
    case CycloneV::rnode_type_t::H14:
        return DelayQuad{254};
    case CycloneV::rnode_type_t::H3:
        return DelayQuad{214};
    case CycloneV::rnode_type_t::H6:
        return DelayQuad{298};
    case CycloneV::rnode_type_t::V2:
        return DelayQuad{210};
    case CycloneV::rnode_type_t::V4:
        return DelayQuad{262};
    case CycloneV::rnode_type_t::DCMUX:
        return DelayQuad{0};
    case CycloneV::rnode_type_t::GIN:
        return DelayQuad{83}; // need to check with Sarayan
    case CycloneV::rnode_type_t::GOUT:
        return DelayQuad{123};
    case CycloneV::rnode_type_t::TCLK:
        return DelayQuad{46};
    default:
        return DelayQuad{308};
    }
}

delay_t Arch::predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const
{
    NPNR_UNUSED(src_pin);
    NPNR_UNUSED(dst_pin);
    Loc src_loc = getBelLocation(src_bel);
    Loc dst_loc = getBelLocation(dst_bel);
    return std::abs(dst_loc.y - src_loc.y) * 100 + std::abs(dst_loc.x - src_loc.x) * 100 + 100;
}

delay_t Arch::estimateDelay(WireId src, WireId dst) const
{
    int x0 = CycloneV::rn2x(src.node);
    int y0 = CycloneV::rn2y(src.node);
    int x1 = CycloneV::rn2x(dst.node);
    int y1 = CycloneV::rn2y(dst.node);
    return 300 * std::abs(y1 - y0) + 300 * std::abs(x1 - x0) + 300;
}

NEXTPNR_NAMESPACE_END
