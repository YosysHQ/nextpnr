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
    std::vector<WireId> srcWires, dstWires;

    for (int i = 0; i < ctx->chip_info->num_wires; i++) {
        WireId wire;
        wire.index = i;

        switch (ctx->chip_info->wire_data[i].type) {
        case WireInfoPOD::WIRE_TYPE_LUTFF_OUT:
            srcWires.push_back(wire);
            break;

        case WireInfoPOD::WIRE_TYPE_LUTFF_IN_LUT:
            dstWires.push_back(wire);
            break;

        default:
            break;
        }
    }

    ctx->shuffle(srcWires);
    ctx->shuffle(dstWires);

    int index = 0;
    int cnt = 0;

    while (cnt < NUM_FUZZ_ROUTES) {
        if (index >= int(srcWires.size()) || index >= int(dstWires.size())) {
            index = 0;
            ctx->shuffle(srcWires);
            ctx->shuffle(dstWires);
        }

        WireId src = srcWires[index];
        WireId dst = dstWires[index++];
        std::unordered_map<WireId, PipId> route;

#if NUM_FUZZ_ROUTES <= 1000
        if (!ctx->getActualRouteDelay(src, dst, nullptr, &route, false))
            continue;
#else
        if (!ctx->getActualRouteDelay(src, dst, nullptr, &route, true))
            continue;
#endif

        WireId cursor = dst;
        delay_t delay = 0;

        while (1) {
            delay += ctx->getWireDelay(cursor).maxDelay();

            printf("%s %d %d %s %s %d %d\n", cursor == dst ? "dst" : "src",
                   int(ctx->chip_info->wire_data[cursor.index].x), int(ctx->chip_info->wire_data[cursor.index].y),
                   ctx->getWireType(cursor).c_str(ctx), ctx->getWireName(cursor).c_str(ctx), int(delay),
                   int(ctx->estimateDelay(cursor, dst)));

            if (cursor == src)
                break;

            PipId pip = route.at(cursor);
            delay += ctx->getPipDelay(pip).maxDelay();
            cursor = ctx->getPipSrcWire(pip);
        }

        cnt++;

        if (cnt % 100 == 0)
            fprintf(stderr, "Fuzzed %d arcs.\n", cnt);
    }
}

namespace {

struct model_params_t
{
    int neighbourhood;

    int model0_offset;
    int model0_norm1;

    int model1_offset;
    int model1_norm1;
    int model1_norm2;
    int model1_norm3;

    int model2_offset;
    int model2_linear;
    int model2_sqrt;

    int delta_local;
    int delta_lutffin;
    int delta_sp4;
    int delta_sp12;

    static const model_params_t &get(const ArchArgs &args)
    {
        static const model_params_t model_hx8k = {588,    129253, 8658, 118333, 23915, -73105, 57696,
                                                  -86797, 89,     3706, -316,   -575,  -158,   -296};

        static const model_params_t model_lp8k = {867,     206236, 11043, 191910, 31074, -95972, 75739,
                                                  -309793, 30,     11056, -474,   -856,  -363,   -536};

        static const model_params_t model_up5k = {1761,    305798, 16705, 296830, 24430, -40369, 33038,
                                                  -162662, 94,     4705,  -1099,  -1761, -418,   -838};

        if (args.type == ArchArgs::HX1K || args.type == ArchArgs::HX8K)
            return model_hx8k;

        if (args.type == ArchArgs::LP384 || args.type == ArchArgs::LP1K || args.type == ArchArgs::LP8K)
            return model_lp8k;

        if (args.type == ArchArgs::UP5K)
            return model_up5k;

        NPNR_ASSERT(0);
    }
};

} // namespace

delay_t Arch::estimateDelay(WireId src, WireId dst) const
{
    NPNR_ASSERT(src != WireId());
    int x1 = chip_info->wire_data[src.index].x;
    int y1 = chip_info->wire_data[src.index].y;
    int z1 = chip_info->wire_data[src.index].z;
    int type = chip_info->wire_data[src.index].type;

    NPNR_ASSERT(dst != WireId());
    int x2 = chip_info->wire_data[dst.index].x;
    int y2 = chip_info->wire_data[dst.index].y;
    int z2 = chip_info->wire_data[dst.index].z;

    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);

    const model_params_t &p = model_params_t::get(args);
    delay_t v = p.neighbourhood;

    if (dx > 1 || dy > 1)
        v = (p.model0_offset + p.model0_norm1 * (dx + dy)) / 128;

    if (dx == 0 && dy == 0) {
        if (type == WireInfoPOD::WIRE_TYPE_LOCAL)
            v += p.delta_local;

        if (type == WireInfoPOD::WIRE_TYPE_LUTFF_IN || type == WireInfoPOD::WIRE_TYPE_LUTFF_IN_LUT)
            v += (z1 == z2) ? p.delta_lutffin : 0;
    }

    if (type == WireInfoPOD::WIRE_TYPE_SP4_V || type == WireInfoPOD::WIRE_TYPE_SP4_H)
        v += p.delta_sp4;

    if (type == WireInfoPOD::WIRE_TYPE_SP12_V || type == WireInfoPOD::WIRE_TYPE_SP12_H)
        v += p.delta_sp12;

    return v;
}

delay_t Arch::predictDelay(const NetInfo *net_info, const PortRef &sink) const
{
    const auto &driver = net_info->driver;
    auto driver_loc = getBelLocation(driver.cell->bel);
    auto sink_loc = getBelLocation(sink.cell->bel);

    if (driver.port == id_COUT) {
        if (driver_loc.y == sink_loc.y)
            return 0;
        return 250;
    }

    int dx = abs(sink_loc.x - driver_loc.x);
    int dy = abs(sink_loc.y - driver_loc.y);

    const model_params_t &p = model_params_t::get(args);

    if (dx <= 1 && dy <= 1)
        return p.neighbourhood;

#if 1
    // Model #0
    return (p.model0_offset + p.model0_norm1 * (dx + dy)) / 128;
#else
    float norm1 = dx + dy;

    float dx2 = dx * dx;
    float dy2 = dy * dy;
    float norm2 = sqrtf(dx2 + dy2);

    float dx3 = dx2 * dx;
    float dy3 = dy2 * dy;
    float norm3 = powf(dx3 + dy3, 1.0 / 3.0);

    // Model #1
    float v = p.model1_offset;
    v += p.model1_norm1 * norm1;
    v += p.model1_norm2 * norm2;
    v += p.model1_norm3 * norm3;
    v /= 128;

    // Model #2
    v = p.model2_offset + p.model2_linear * v + p.model2_sqrt * sqrtf(v);
    v /= 128;

    return v;
#endif
}

NEXTPNR_NAMESPACE_END
