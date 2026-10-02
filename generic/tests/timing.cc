/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2026  Deano Calver <deano@geometric.so>
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

#include "gtest/gtest.h"
#include "nextpnr.h"
#include "timing.h"

USING_NEXTPNR_NAMESPACE

// Exercise the common analyser without a device database or routing randomness.
TEST(Timing, ClockWaveform)
{
    for (int duty : {25, 50, 75}) {
        for (ClockEdge launch : {RISING_EDGE, FALLING_EDGE}) {
            for (ClockEdge capture : {RISING_EDGE, FALLING_EDGE}) {
                for (bool skew : {false, true}) {
                    SCOPED_TRACE(::testing::Message() << "duty=" << duty << " launch=" << launch
                                                    << " capture=" << capture << " skew=" << skew);
                    Context ctx(ArchArgs{});
                    ctx.settings[ctx.id("target_freq")] = 25e6;
                    auto clk = ctx.createNet(ctx.id("clk"));
                    clk->clkconstr = std::make_unique<ClockConstraint>();
                    clk->clkconstr->period = DelayPair(40);
                    clk->clkconstr->high = DelayPair(40 * duty / 100);
                    clk->clkconstr->low = DelayPair(40 * (100 - duty) / 100);
                    auto data = ctx.createNet(ctx.id("data"));
                    auto source = ctx.createCell(ctx.id("source"), ctx.id("FF"));
                    auto sink = ctx.createCell(ctx.id("sink"), ctx.id("FF"));
                    auto c = ctx.id("CLK"), q = ctx.id("Q"), d = ctx.id("D");
                    source->addInput(c);
                    source->addOutput(q);
                    sink->addInput(c);
                    sink->addInput(d);
                    source->connectPort(c, clk);
                    sink->connectPort(c, clk);
                    source->connectPort(q, data);
                    sink->connectPort(d, data);
                    ctx.addCellTimingClock(source->name, c);
                    ctx.addCellTimingClock(sink->name, c);
                    ctx.addCellTimingClockToOut(source->name, q, c, 2);
                    ctx.addCellTimingSetupHold(sink->name, d, c, 1, 0);
                    ctx.cellTiming[source->name].clockingInfo[q][0].edge = launch;
                    ctx.cellTiming[sink->name].clockingInfo[d][0].edge = capture;
                    TimingAnalyser timing(&ctx);
                    timing.with_clock_skew = skew;
                    timing.setup();
                    timing.set_route_delay(CellPortKey(sink->name, d), DelayPair(5));
                    timing.set_route_delay(CellPortKey(source->name, c), DelayPair(3));
                    timing.set_route_delay(CellPortKey(sink->name, c), DelayPair(4));
                    timing.run(false, false, true, true);
                    float interval = launch == capture ? 40
                            : launch == RISING_EDGE ? 40 * duty / 100 : 40 * (100 - duty) / 100;
                    // 2 ns clock-to-Q + 5 ns data route + 1 ns setup,
                    // reduced by the optional 1 ns positive capture-clock skew.
                    float delay = skew ? 7 : 8;
                    EXPECT_FLOAT_EQ(timing.get_setup_slack(CellPortKey(sink->name, d)), interval - delay);
                    auto &result = timing.get_timing_result();
                    EXPECT_NEAR(result.clock_fmax.at(clk->name).achieved, 1000 * interval / 40 / delay, 1e-4);
                    EXPECT_FLOAT_EQ(result.clock_paths.at(clk->name).max_delay, interval);
                    EXPECT_EQ(result.slack_histogram.count(int((interval - delay) * 1000)), 1u);
                }
            }
        }
    }
}
