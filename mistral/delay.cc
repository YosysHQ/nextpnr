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

    if (src.is_nextpnr_created() || dst.is_nextpnr_created())
        return DelayQuad{20};

    // This is guesswork based on average of (interconnect delay / number of pips)
    auto src_type = CycloneV::rn2t(src.node);

    switch (src_type) {
    case CycloneV::rnode_type_t::SCLK:
        return DelayQuad{136, 136, 139, 139};
    case CycloneV::rnode_type_t::SCLKB1:
        return DelayQuad{296, 296, 370, 370};
    case CycloneV::rnode_type_t::SCLKB2:
        return DelayQuad{71, 71, 83, 83};
    case CycloneV::rnode_type_t::HCLK:
        return DelayQuad{183, 183, 239, 239};
    case CycloneV::rnode_type_t::HCLKB:
        return DelayQuad{165, 165, 244, 244};
    case CycloneV::rnode_type_t::XCLKB1:
        return DelayQuad{97, 97, 125, 125};
    case CycloneV::rnode_type_t::GIN:
        return DelayQuad{100};
    case CycloneV::rnode_type_t::H14:
        return DelayQuad{273, 286, 288, 291};
    case CycloneV::rnode_type_t::H3:
        return DelayQuad{196, 226, 163, 173};
    case CycloneV::rnode_type_t::H6:
        return DelayQuad{220, 275, 199, 217};
    case CycloneV::rnode_type_t::V12:
        return DelayQuad{361, 374, 337, 340};
    case CycloneV::rnode_type_t::V2:
        return DelayQuad{214, 231, 163, 175};
    case CycloneV::rnode_type_t::V4:
        return DelayQuad{290, 294, 243, 245};
    case CycloneV::rnode_type_t::WM:
        // WM explicitly has zero delay.
        return DelayQuad{0};
    case CycloneV::rnode_type_t::TD:
        return DelayQuad{208, 208, 177, 177};
    default:
        return DelayQuad{0};
    }
}

bool Arch::getArcDelayOverride(const NetInfo *net_info, const PortRef &sink, DelayQuad &delay) const
{
    if (!this->bitstream_configured)
        return false;

    WireId src_wire = getCtx()->getNetinfoSourceWire(net_info);
    WireId dst_wire = getCtx()->getNetinfoSinkWire(net_info, sink, 0);
    NPNR_ASSERT(src_wire != WireId());

    bool inverted = false;
    mistral::AnalogSim::wave input_wave[2], output_wave[2];
    mistral::AnalogSim::time_interval output_delays[2];
    mistral::AnalogSim::time_interval output_delay_sum[2];
    std::vector<std::pair<mistral::CycloneV::rnode_t, int>> outputs;
    auto temp = mistral::CycloneV::T_100;
    auto est = mistral::CycloneV::EST_SLOW;

    output_delay_sum[0].mi = 0;
    output_delay_sum[0].mx = 0;
    output_delay_sum[1].mi = 0;
    output_delay_sum[1].mx = 0;

    // Mistral's analogue simulator propagates from source to destination,
    // but nextpnr finds paths from destination to source, so some slight
    // contortions are necessary.

    std::vector<PipId> pips;

    WireId cursor = dst_wire;
    while (cursor != WireId() && cursor != src_wire) {
        auto it = net_info->wires.find(cursor);

        if (it == net_info->wires.end())
            break;

        PipId pip = it->second.pip;
        if (pip == PipId())
            break;

        pips.push_back(pip);
        cursor = getPipSrcWire(pip);
    }

    for (auto it = pips.rbegin(); it != pips.rend(); it++) {
        PipId pip = *it;
        auto src = getPipSrcWire(pip);
        auto dst = getPipDstWire(pip);

        if (src.is_nextpnr_created())
            continue;

        if (dst.is_nextpnr_created())
            dst.node = 0;

        auto mode = cyclonev->rnode_timing_get_mode(src.node);
        NPNR_ASSERT(mode != mistral::CycloneV::RTM_UNSUPPORTED);

        auto inverting = cyclonev->rnode_is_inverting(src.node);

        if (mode == mistral::CycloneV::RTM_P2P) {
            if (inverting == mistral::CycloneV::INV_YES || inverting == mistral::CycloneV::INV_PROGRAMMABLE)
                inverted = !inverted;
            continue;
        }

        if (mode == mistral::CycloneV::RTM_NO_DELAY) {
            if (inverting)
                inverted = !inverted;
            continue;
        }

        if (input_wave[0].empty()) {
            cyclonev->rnode_timing_build_input_wave(src.node, temp, CycloneV::DELAY_MAX,
                                                    inverted ? mistral::CycloneV::RF_FALL : mistral::CycloneV::RF_RISE,
                                                    est, input_wave[0]);
            cyclonev->rnode_timing_build_input_wave(src.node, temp, CycloneV::DELAY_MAX,
                                                    inverted ? mistral::CycloneV::RF_RISE : mistral::CycloneV::RF_FALL,
                                                    est, input_wave[1]);
            if (input_wave[mistral::CycloneV::RF_RISE].empty() || input_wave[mistral::CycloneV::RF_FALL].empty())
                return false;
        }

        for (int edge = 0; edge != 2; edge++) {
            auto actual_edge = edge       ? inverted ? mistral::CycloneV::RF_RISE : mistral::CycloneV::RF_FALL
                               : inverted ? mistral::CycloneV::RF_FALL
                                          : mistral::CycloneV::RF_RISE;
            mistral::AnalogSim sim;
            int input = -1;
            std::vector<std::pair<mistral::CycloneV::rnode_t, int>> outputs;
            cyclonev->rnode_timing_build_circuit(src.node, temp, CycloneV::DELAY_MAX, actual_edge, sim, input, outputs);

            sim.set_input_wave(input, input_wave[edge]);
            auto o = std::find_if(
                    outputs.begin(), outputs.end(),
                    [&](std::pair<mistral::CycloneV::rnode_t, int> output) { return output.first == dst.node; });
            NPNR_ASSERT(o != outputs.end());

            output_wave[edge].clear();
            sim.set_output_wave(o->second, output_wave[edge], output_delays[edge]);
            sim.run();
            cyclonev->rnode_timing_trim_wave(temp, CycloneV::DELAY_MAX, output_wave[edge], input_wave[edge]);

            output_delay_sum[edge].mi += output_delays[edge].mi;
            output_delay_sum[edge].mx += output_delays[edge].mx;
        }

        if (inverting == mistral::CycloneV::INV_YES || inverting == mistral::CycloneV::INV_PROGRAMMABLE)
            inverted = !inverted;
    }

    delay = DelayQuad{delay_t(output_delay_sum[0].mi * 1e12), delay_t(output_delay_sum[0].mx * 1e12),
                      delay_t(output_delay_sum[1].mi * 1e12), delay_t(output_delay_sum[1].mx * 1e12)};

    return true;
}

delay_t Arch::predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const
{
    NPNR_UNUSED(src_pin);
    NPNR_UNUSED(dst_pin);
    Loc src_loc = getBelLocation(src_bel);
    Loc dst_loc = getBelLocation(dst_bel);
    int x_diff = std::abs(dst_loc.x - src_loc.x);
    int y_diff = std::abs(dst_loc.y - src_loc.y);
    return 75 * x_diff + 200 * y_diff;
}

delay_t Arch::estimateDelay(WireId src, WireId dst) const
{
    int x0 = CycloneV::rn2x(src.node);
    int y0 = CycloneV::rn2y(src.node);
    int x1 = CycloneV::rn2x(dst.node);
    int y1 = CycloneV::rn2y(dst.node);
    int x_diff = std::abs(x1 - x0);
    int y_diff = std::abs(y1 - y0);
    return 75 * x_diff + 200 * y_diff;
}

NEXTPNR_NAMESPACE_END
