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
    const auto &src_info = torc_info->tiles.getTileInfo(src_tw.getTileIndex());
    const auto &dst_tw = torc_info->wire_to_tilewire[dst.index];
    const auto &dst_info = torc_info->tiles.getTileInfo(dst_tw.getTileIndex());

    return 100 * (abs(src_info.getCol() - dst_info.getCol()) + abs(src_info.getRow() - dst_info.getRow()));
}

delay_t Arch::predictDelay(const NetInfo *net_info, const PortRef &sink) const
{
    const auto &driver = net_info->driver;
    auto driver_loc = getBelLocation(driver.cell->bel);
    auto sink_loc = getBelLocation(sink.cell->bel);

    return 100 * (abs(driver_loc.x - sink_loc.x) + abs(driver_loc.y - sink_loc.y));
}

NEXTPNR_NAMESPACE_END
