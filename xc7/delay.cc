/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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

#include "nextpnr.h"
#include "router1.h"

NEXTPNR_NAMESPACE_BEGIN

#define NUM_FUZZ_ROUTES 100000

void ice40DelayFuzzerMain(Context *ctx)
{
    //    std::vector<WireId> srcWires, dstWires;
    //
    //    for (int i = 0; i < ctx->chip_info->num_wires; i++) {
    //        WireId wire;
    //        wire.index = i;
    //
    //        switch (ctx->chip_info->wire_data[i].type) {
    //        case WireInfoPOD::WIRE_TYPE_LUTFF_OUT:
    //            srcWires.push_back(wire);
    //            break;
    //
    //        case WireInfoPOD::WIRE_TYPE_LUTFF_IN_LUT:
    //            dstWires.push_back(wire);
    //            break;
    //
    //        default:
    //            break;
    //        }
    //    }
    //
    //    ctx->shuffle(srcWires);
    //    ctx->shuffle(dstWires);
    //
    //    int index = 0;
    //    int cnt = 0;
    //
    //    while (cnt < NUM_FUZZ_ROUTES) {
    //        if (index >= int(srcWires.size()) || index >= int(dstWires.size())) {
    //            index = 0;
    //            ctx->shuffle(srcWires);
    //            ctx->shuffle(dstWires);
    //        }
    //
    //        WireId src = srcWires[index];
    //        WireId dst = dstWires[index++];
    //        std::unordered_map<WireId, PipId> route;
    //
    //#if NUM_FUZZ_ROUTES <= 1000
    //        if (!ctx->getActualRouteDelay(src, dst, nullptr, &route, false))
    //            continue;
    //#else
    //        if (!ctx->getActualRouteDelay(src, dst, nullptr, &route, true))
    //            continue;
    //#endif
    //
    //        WireId cursor = dst;
    //        delay_t delay = 0;
    //
    //        while (1) {
    //            delay += ctx->getWireDelay(cursor).maxDelay();
    //
    //            printf("%s %d %d %s %s %d %d\n", cursor == dst ? "dst" : "src",
    //                   int(ctx->chip_info->wire_data[cursor.index].x), int(ctx->chip_info->wire_data[cursor.index].y),
    //                   ctx->getWireType(cursor).c_str(ctx), ctx->getWireName(cursor).c_str(ctx), int(delay),
    //                   int(ctx->estimateDelay(cursor, dst)));
    //
    //            if (cursor == src)
    //                break;
    //
    //            PipId pip = route.at(cursor);
    //            delay += ctx->getPipDelay(pip).maxDelay();
    //            cursor = ctx->getPipSrcWire(pip);
    //        }
    //
    //        cnt++;
    //
    //        if (cnt % 100 == 0)
    //            fprintf(stderr, "Fuzzed %d arcs.\n", cnt);
    //    }
}

delay_t Arch::estimateDelay(WireId src, WireId dst) const
{
    const auto &src_tw = torc_info->wire_to_tilewire[src.index];
    const auto &src_loc = torc_info->tile_to_xy[src_tw.getTileIndex()];
    const auto &dst_tw = torc_info->wire_to_tilewire[dst.index];
    const auto &dst_loc = torc_info->tile_to_xy[dst_tw.getTileIndex()];

    if (!torc_info->wire_is_global[src.index]) {
        auto abs_delta_x = abs(dst_loc.first - src_loc.first);
        auto abs_delta_y = abs(dst_loc.second - src_loc.second);
        auto div_LH = std::div(abs_delta_x, 12);
        auto div_LV = std::div(abs_delta_y, 18);
        auto div_LVB = std::div(div_LV.rem, 12);
        auto div_H6 = std::div(div_LH.rem, 6);
        auto div_V6 = std::div(div_LVB.rem, 6);
        auto div_H4 = std::div(div_H6.rem, 4);
        auto div_V4 = std::div(div_V6.rem, 4);
        auto div_H2 = std::div(div_H4.rem, 2);
        auto div_V2 = std::div(div_V4.rem, 2);
        auto num_H1 = div_H2.rem;
        auto num_V1 = div_V2.rem;
        return div_LH.quot * 360 + div_LVB.quot * 300 + div_LV.quot * 350 +
               (div_H6.quot + div_H4.quot + div_V6.quot + div_V4.quot) * 210 + (div_H2.quot + div_V2.quot) * 170 +
               (num_H1 + num_V1) * 150;
    }
    else {
        auto src_y = src_loc.second;
        auto dst_y = dst_loc.second;
        auto div_src_y = std::div(src_y, 52);
        auto div_dst_y = std::div(dst_y, 52);
        return abs(div_dst_y.quot - div_src_y.quot) * 52 + abs(div_dst_y.rem - div_src_y.rem);
    }
}

delay_t Arch::predictDelay(const NetInfo *net_info, const PortRef &sink) const
{
    const auto &driver = net_info->driver;
    auto driver_loc = getBelLocation(driver.cell->bel);
    auto sink_loc = getBelLocation(sink.cell->bel);
    auto abs_delta_x = abs(driver_loc.x - sink_loc.x);
    auto abs_delta_y = abs(driver_loc.y - sink_loc.y);
    auto div_LH = std::div(abs_delta_x, 12);
    auto div_LV = std::div(abs_delta_y, 18);
    auto div_LVB = std::div(div_LV.rem, 12);
    auto div_H6 = std::div(div_LH.rem, 6);
    auto div_V6 = std::div(div_LVB.rem, 6);
    auto div_H4 = std::div(div_H6.rem, 4);
    auto div_V4 = std::div(div_V6.rem, 4);
    auto div_H2 = std::div(div_H4.rem, 2);
    auto div_V2 = std::div(div_V4.rem, 2);
    auto num_H1 = div_H2.rem;
    auto num_V1 = div_V2.rem;
    return div_LH.quot * 360 + div_LVB.quot * 300 + div_LV.quot * 350 +
           (div_H6.quot + div_H4.quot + div_V6.quot + div_V4.quot) * 210 + (div_H2.quot + div_V2.quot) * 170 +
           (num_H1 + num_V1) * 150;
}

NEXTPNR_NAMESPACE_END
