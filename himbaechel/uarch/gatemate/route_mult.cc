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

#include "gatemate.h"
#include "log.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

namespace {
USING_NEXTPNR_NAMESPACE;

bool is_fourgroup_a_within_tile(const GateMateTileExtraDataPOD *ti, int &x_within_fourgroup, int &y_within_fourgroup)
{
    auto x_fourgroup = ti->tile_x % 4;
    auto y_fourgroup = ti->tile_y % 4;
    x_within_fourgroup = ti->tile_x % 2;
    y_within_fourgroup = ti->tile_y % 2;
    return (x_fourgroup < 2 && y_fourgroup < 2) || (x_fourgroup >= 2 && y_fourgroup >= 2);
}

void find_and_bind_downhill_pip(Context *ctx, WireId from, WireId to, NetInfo *net)
{
    NPNR_ASSERT(from != WireId());
    NPNR_ASSERT(to != WireId());
    for (auto pip : ctx->getPipsDownhill(from)) {
        if (ctx->getPipDstWire(pip) == to) {
            if (ctx->debug)
                log_info("    pip %s: %s -> %s\n", ctx->nameOfPip(pip), ctx->nameOfWire(from), ctx->nameOfWire(to));

            ctx->bindPip(pip, net, STRENGTH_LOCKED);
            return;
        }
    }
    log_error("Couldn't find pip from %s to %s\n", ctx->nameOfWire(from), ctx->nameOfWire(to));
}

void route_mult_diag(Context *ctx, NetInfo *net, Loc loc, WireId last_wire, int plane)
{
    auto hops = 0;
    auto in_port = ctx->idf("IN%d", plane);
    for (auto user : net->users) {
        if (user.port == in_port)
            hops++;
    }

    if (ctx->debug)
        log_info("  routing diagonal: %d hops\n", hops);

    for (int i = 0; i < hops; i++) {
        auto in_mux_y = ctx->getWireByName(
                IdStringList::concat(ctx->idf("X%dY%d", loc.x + i, loc.y + i), ctx->idf("IM.P%02d.Y", plane)));
        auto d4 = ctx->getWireByName(
                IdStringList::concat(ctx->idf("X%dY%d", loc.x + i + 1, loc.y + i + 1), ctx->idf("IM.P%02d.D4", plane)));
        auto cpe_in = ctx->getWireByName(
                IdStringList::concat(ctx->idf("X%dY%d", loc.x + i, loc.y + i), ctx->idf("CPE.IN%d", plane)));
        auto cpe_in_int = ctx->getWireByName(
                IdStringList::concat(ctx->idf("X%dY%d", loc.x + i, loc.y + i), ctx->idf("CPE.IN%d_int", plane)));

        find_and_bind_downhill_pip(ctx, last_wire, in_mux_y, net);
        find_and_bind_downhill_pip(ctx, in_mux_y, cpe_in, net);
        find_and_bind_downhill_pip(ctx, cpe_in, cpe_in_int, net);

        find_and_bind_downhill_pip(ctx, in_mux_y, d4, net);

        last_wire = d4;
    }
}

void route_mult_x1y1_lower(Context *ctx, NetInfo *net, CellInfo *lower, Loc loc, bool is_fourgroup_a)
{
    if (ctx->debug)
        log_info("  routing net '%s' -> IN5 using x1y1\n", net->name.c_str(ctx));

    auto x1y1 = ctx->idf("X%dY%d", loc.x, loc.y);
    auto x2y1 = ctx->idf("X%dY%d", loc.x + 1, loc.y);

    auto cpe_combout1 = ctx->getBelPinWire(lower->bel, id_OUT);
    auto cpe_out1_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT1_int")));
    auto cpe_out1 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT1")));
    auto in_mux = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("IM.P05.D0")));

    ctx->bindWire(cpe_combout1, net, STRENGTH_LOCKED);
    find_and_bind_downhill_pip(ctx, cpe_combout1, cpe_out1_int, net);
    find_and_bind_downhill_pip(ctx, cpe_out1_int, cpe_out1, net);

    if (is_fourgroup_a) {
        auto sb_big_d0 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P05.D0")));
        auto sb_big_y1 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P05.Y1")));

        find_and_bind_downhill_pip(ctx, cpe_out1, sb_big_d0, net);
        find_and_bind_downhill_pip(ctx, sb_big_d0, sb_big_y1, net);
        find_and_bind_downhill_pip(ctx, sb_big_y1, in_mux, net);
    } else {
        auto sb_sml_d0 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P05.D0")));
        auto sb_sml_y1_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P05.Y1_int")));
        auto sb_sml_y1 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P05.Y1")));

        find_and_bind_downhill_pip(ctx, cpe_out1, sb_sml_d0, net);
        find_and_bind_downhill_pip(ctx, sb_sml_d0, sb_sml_y1_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_y1_int, sb_sml_y1, net);
        find_and_bind_downhill_pip(ctx, sb_sml_y1, in_mux, net); // inverting
    }

    route_mult_diag(ctx, net, Loc{loc.x + 1, loc.y, 0}, in_mux, 5);
}

void route_mult_x1y1_upper_in1(Context *ctx, NetInfo *net, CellInfo *upper, Loc loc, bool is_fourgroup_a)
{
    if (ctx->debug)
        log_info("  routing net '%s' -> IN1 using x1y1\n", net->name.c_str(ctx));

    auto x1y1 = ctx->idf("X%dY%d", loc.x, loc.y);
    auto x2y1 = ctx->idf("X%dY%d", loc.x + 1, loc.y);

    auto cpe_combout2 = ctx->getBelPinWire(upper->bel, id_OUT);
    auto cpe_out2_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2_int")));
    auto cpe_out2 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2")));
    auto in_mux = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("IM.P01.D0")));

    ctx->bindWire(cpe_combout2, net, STRENGTH_LOCKED);
    find_and_bind_downhill_pip(ctx, cpe_combout2, cpe_out2_int, net);
    find_and_bind_downhill_pip(ctx, cpe_out2_int, cpe_out2, net);

    if (is_fourgroup_a) {
        auto sb_big = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P01.D0")));

        find_and_bind_downhill_pip(ctx, cpe_out2, sb_big, net);
        find_and_bind_downhill_pip(ctx, sb_big, in_mux, net); // inverting
    } else {
        auto sb_sml_d0 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P01.D0")));
        auto sb_sml_y1_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P01.Y1_int")));

        find_and_bind_downhill_pip(ctx, cpe_out2, sb_sml_d0, net);
        find_and_bind_downhill_pip(ctx, sb_sml_d0, sb_sml_y1_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_y1_int, in_mux, net); // inverting
    }

    route_mult_diag(ctx, net, Loc{loc.x + 1, loc.y, 0}, in_mux, 1);
}

void route_mult_x1y1_upper_in8(Context *ctx, NetInfo *net, CellInfo *upper, Loc loc, bool is_fourgroup_a,
                               bool bind_route_start = false)
{
    if (ctx->debug)
        log_info("  routing net '%s' -> IN8 using x1y1\n", net->name.c_str(ctx));

    auto x1y1 = ctx->idf("X%dY%d", loc.x, loc.y);
    auto x2y2 = ctx->idf("X%dY%d", loc.x + 1, loc.y + 1);
    auto x4y2 = ctx->idf("X%dY%d", loc.x + 3, loc.y + 1);

    auto cpe_combout2 = ctx->getBelPinWire(upper->bel, id_OUT);
    auto cpe_out2_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2_int")));
    auto cpe_out2 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2")));
    auto out_mux_d0 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("OM.P12.D0")));
    auto out_mux_y = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("OM.P12.Y")));
    auto in_mux_p12 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P12.D2")));

    if (bind_route_start) {
        ctx->bindWire(cpe_combout2, net, STRENGTH_LOCKED);
        find_and_bind_downhill_pip(ctx, cpe_combout2, cpe_out2_int, net);
        find_and_bind_downhill_pip(ctx, cpe_out2_int, cpe_out2, net);
        find_and_bind_downhill_pip(ctx, cpe_out2, out_mux_d0, net);
    }

    find_and_bind_downhill_pip(ctx, out_mux_d0, out_mux_y, net); // inverting

    if (is_fourgroup_a) {
        auto sb_sml = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("SB_SML.P12.Y1_int")));
        auto sb_big_d2_1 = ctx->getWireByName(IdStringList::concat(x4y2, ctx->idf("SB_BIG.P12.D2_1")));
        auto sb_big_y1 = ctx->getWireByName(IdStringList::concat(x4y2, ctx->idf("SB_BIG.P12.Y1")));
        auto sb_big_ydiag = ctx->getWireByName(IdStringList::concat(x4y2, ctx->idf("SB_BIG.P12.YDIAG")));

        find_and_bind_downhill_pip(ctx, out_mux_y, sb_sml, net);
        find_and_bind_downhill_pip(ctx, sb_sml, sb_big_d2_1, net);      // inverting
        find_and_bind_downhill_pip(ctx, sb_big_d2_1, sb_big_y1, net);   // inverting
        find_and_bind_downhill_pip(ctx, sb_big_y1, sb_big_ydiag, net);  // inverting
        find_and_bind_downhill_pip(ctx, sb_big_ydiag, in_mux_p12, net); // inverting
    } else {
        auto sb_big =
                ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("SB_BIG.P12.Y1"))); // aka x4y2/SB_SML.P12.D2_1
        auto sb_sml_y1_int = ctx->getWireByName(IdStringList::concat(x4y2, ctx->idf("SB_SML.P12.Y1_int")));
        auto sb_sml_ydiag_int = ctx->getWireByName(IdStringList::concat(x4y2, ctx->idf("SB_SML.P12.YDIAG_int")));
        auto sb_sml_y3_int = ctx->getWireByName(IdStringList::concat(x4y2, ctx->idf("SB_SML.P12.Y3_int")));

        find_and_bind_downhill_pip(ctx, out_mux_y, sb_big, net); // inverting
        find_and_bind_downhill_pip(ctx, sb_big, sb_sml_y1_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_y1_int, sb_sml_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_ydiag_int, sb_sml_y3_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_y3_int, in_mux_p12, net); // inverting
    }

    auto in_mux_p04 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P04.D7"))); // aka IM.P12.Y
    auto in_mux_p08 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P08.D6"))); // aka IM.P04.Y

    find_and_bind_downhill_pip(ctx, in_mux_p12, in_mux_p04, net); // inverting
    find_and_bind_downhill_pip(ctx, in_mux_p04, in_mux_p08, net); // inverting

    route_mult_diag(ctx, net, Loc{loc.x + 1, loc.y + 1, 0}, in_mux_p08, 8);
}

void route_mult_x1y2_lower(Context *ctx, NetInfo *net, CellInfo *lower, Loc loc, bool is_fourgroup_a)
{
    if (ctx->debug)
        log_info("  routing net '%s' -> IN5 using x1y2\n", net->name.c_str(ctx));

    auto x1y1 = ctx->idf("X%dY%d", loc.x, loc.y);
    auto x2y1 = ctx->idf("X%dY%d", loc.x + 1, loc.y);
    auto x4y1 = ctx->idf("X%dY%d", loc.x + 3, loc.y);

    auto cpe_combout1 = ctx->getBelPinWire(lower->bel, id_OUT);
    auto cpe_out1_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT1_int")));
    auto cpe_out1 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT1")));
    auto in_mux = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("IM.P05.D2")));

    ctx->bindWire(cpe_combout1, net, STRENGTH_LOCKED);
    find_and_bind_downhill_pip(ctx, cpe_combout1, cpe_out1_int, net);
    find_and_bind_downhill_pip(ctx, cpe_out1_int, cpe_out1, net);

    if (is_fourgroup_a) {
        auto sb_sml_p06_d0 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_SML.P06.D0")));
        auto sb_sml_p06_y1_int = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_SML.P06.Y1_int")));
        auto sb_sml_p06_ydiag_int = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_SML.P06.YDIAG_int")));
        auto sb_sml_p06_ydiag = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_SML.P06.YDIAG")));
        auto sb_sml_p05_x23 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_SML.P05.X23")));
        auto sb_sml_p05_ydiag_int = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_SML.P05.YDIAG_int")));
        auto sb_sml_p05_y1_int = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_SML.P05.Y1_int")));
        auto sb_sml_p05_y1 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_SML.P05.Y1")));
        auto sb_big_d2_1 = ctx->getWireByName(IdStringList::concat(x4y1, ctx->idf("SB_BIG.P05.D2_1")));
        auto sb_big_y1 = ctx->getWireByName(IdStringList::concat(x4y1, ctx->idf("SB_BIG.P05.Y1")));
        auto sb_big_ydiag = ctx->getWireByName(IdStringList::concat(x4y1, ctx->idf("SB_BIG.P05.YDIAG")));
        auto sb_big_y3 = ctx->getWireByName(IdStringList::concat(x4y1, ctx->idf("SB_BIG.P05.Y3")));

        find_and_bind_downhill_pip(ctx, cpe_out1, sb_sml_p06_d0, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p06_d0, sb_sml_p06_y1_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p06_y1_int, sb_sml_p06_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p06_ydiag_int, sb_sml_p06_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p06_ydiag, sb_sml_p05_x23, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p05_x23, sb_sml_p05_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p05_ydiag_int, sb_sml_p05_y1_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p05_y1_int, sb_sml_p05_y1, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p05_y1, sb_big_d2_1, net);
        find_and_bind_downhill_pip(ctx, sb_big_d2_1, sb_big_y1, net);
        find_and_bind_downhill_pip(ctx, sb_big_y1, sb_big_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_ydiag, sb_big_y3, net);
        find_and_bind_downhill_pip(ctx, sb_big_y3, in_mux, net);
    } else {
        auto sb_big_p06_d0 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_BIG.P06.D0")));
        auto sb_big_p06_y1 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_BIG.P06.Y1")));
        auto sb_big_p06_ydiag =
                ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_BIG.P06.YDIAG")));
        auto sb_big_p05_x23 =
                ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_BIG.P05.X23")));
        auto sb_big_p05_ydiag = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_BIG.P05.YDIAG")));
        auto sb_big_p05_y1 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_BIG.P05.Y1")));
        auto sb_sml_d2_1 = ctx->getWireByName(IdStringList::concat(x4y1, ctx->idf("SB_SML.P05.D2_1")));        
        auto sb_sml_y1_int = ctx->getWireByName(IdStringList::concat(x4y1, ctx->idf("SB_SML.P05.Y1_int")));
        auto sb_sml_ydiag_int = ctx->getWireByName(IdStringList::concat(x4y1, ctx->idf("SB_SML.P05.YDIAG_int")));
        auto sb_sml_y3_int = ctx->getWireByName(IdStringList::concat(x4y1, ctx->idf("SB_SML.P05.Y3_int")));
        auto sb_sml_y3 = ctx->getWireByName(IdStringList::concat(x4y1, ctx->idf("SB_SML.P05.Y3")));

        find_and_bind_downhill_pip(ctx, cpe_out1, sb_big_p06_d0, net);
        find_and_bind_downhill_pip(ctx, sb_big_p06_d0, sb_big_p06_y1, net);
        find_and_bind_downhill_pip(ctx, sb_big_p06_y1, sb_big_p06_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p06_ydiag, sb_big_p05_x23, net);
        find_and_bind_downhill_pip(ctx, sb_big_p05_x23, sb_big_p05_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p05_ydiag, sb_big_p05_y1, net);
        find_and_bind_downhill_pip(ctx, sb_big_p05_y1, sb_sml_d2_1, net);
        find_and_bind_downhill_pip(ctx, sb_sml_d2_1, sb_sml_y1_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_y1_int, sb_sml_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_ydiag_int, sb_sml_y3_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_y3_int, sb_sml_y3, net);
        find_and_bind_downhill_pip(ctx, sb_sml_y3, in_mux, net);
    }

    route_mult_diag(ctx, net, Loc{loc.x + 1, loc.y, 0}, in_mux, 5);
}

void route_mult_x1y2_upper_in1(Context *ctx, NetInfo *net, CellInfo *upper, Loc loc, bool is_fourgroup_a)
{
    if (ctx->debug)
        log_info("  routing net '%s' -> IN1 using x1y2\n", net->name.c_str(ctx));

    auto x1y1 = ctx->idf("X%dY%d", loc.x, loc.y);
    auto x2y1 = ctx->idf("X%dY%d", loc.x + 1, loc.y);
    auto x4y1 = ctx->idf("X%dY%d", loc.x + 3, loc.y);

    auto cpe_combout2 = ctx->getBelPinWire(upper->bel, id_OUT);
    auto cpe_out2_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2_int")));
    auto cpe_out2 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2")));

    ctx->bindWire(cpe_combout2, net, STRENGTH_LOCKED);
    find_and_bind_downhill_pip(ctx, cpe_combout2, cpe_out2_int, net);
    find_and_bind_downhill_pip(ctx, cpe_out2_int, cpe_out2, net);

    auto in_mux = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("IM.P01.D2")));

    if (is_fourgroup_a) {
        auto sb_sml_p02_d0 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_SML.P02.D0")));
        auto sb_sml_p02_y1 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_SML.P02.Y1_int")));
        auto sb_sml_p02_ydiag_int = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_SML.P02.YDIAG_int")));
        auto sb_sml_p02_ydiag =
                ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_SML.P02.YDIAG"))); // AKA SB_SML.P01.X23
        auto sb_sml_p01_ydiag = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_SML.P01.YDIAG_int")));
        auto sb_sml_p01_y1 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_SML.P01.Y1_int")));
        auto sb_big_d2_1 = ctx->getWireByName(IdStringList::concat(x4y1, ctx->idf("SB_BIG.P01.D2_1")));
        auto sb_big_y1 = ctx->getWireByName(IdStringList::concat(x4y1, ctx->idf("SB_BIG.P01.Y1")));
        auto sb_big_ydiag = ctx->getWireByName(IdStringList::concat(x4y1, ctx->idf("SB_BIG.P01.YDIAG")));
        // x2y1/IM.P01.D2 is x4y1/SB_BIG.P01.Y3

        find_and_bind_downhill_pip(ctx, cpe_out2, sb_sml_p02_d0, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p02_d0, sb_sml_p02_y1, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p02_y1, sb_sml_p02_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p02_ydiag_int, sb_sml_p02_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p02_ydiag, sb_sml_p01_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p01_ydiag, sb_sml_p01_y1, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p01_y1, sb_big_d2_1, net);
        find_and_bind_downhill_pip(ctx, sb_big_d2_1, sb_big_y1, net);
        find_and_bind_downhill_pip(ctx, sb_big_y1, sb_big_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_ydiag, in_mux, net);
    } else {
        auto sb_big_p02_d0 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_BIG.P02.D0")));
        auto sb_big_p02_y1 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_BIG.P02.Y1")));
        auto sb_big_p02_ydiag =
                ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_BIG.P02.YDIAG"))); // AKA SB_BIG.P01.X23
        auto sb_big_p01_ydiag = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_BIG.P01.YDIAG")));
        auto sb_big_p01_y1 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_BIG.P01.Y1")));
        auto sb_sml_y1 = ctx->getWireByName(IdStringList::concat(x4y1, ctx->idf("SB_SML.P01.Y1_int")));
        auto sb_sml_ydiag = ctx->getWireByName(IdStringList::concat(x4y1, ctx->idf("SB_SML.P01.YDIAG_int")));
        auto sb_sml_y3 = ctx->getWireByName(IdStringList::concat(x4y1, ctx->idf("SB_SML.P01.Y3_int")));

        find_and_bind_downhill_pip(ctx, cpe_out2, sb_big_p02_d0, net);
        find_and_bind_downhill_pip(ctx, sb_big_p02_d0, sb_big_p02_y1, net);
        find_and_bind_downhill_pip(ctx, sb_big_p02_y1, sb_big_p02_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p02_ydiag, sb_big_p01_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p01_ydiag, sb_big_p01_y1, net);
        find_and_bind_downhill_pip(ctx, sb_big_p01_y1, sb_sml_y1, net);
        find_and_bind_downhill_pip(ctx, sb_sml_y1, sb_sml_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_sml_ydiag, sb_sml_y3, net);
        find_and_bind_downhill_pip(ctx, sb_sml_y3, in_mux, net);
    }

    route_mult_diag(ctx, net, Loc{loc.x + 1, loc.y, 0}, in_mux, 1);
}

void route_mult_x1y2_upper_in8(Context *ctx, NetInfo *net, CellInfo *upper, Loc loc, bool is_fourgroup_a,
                               bool bind_route_start = false)
{
    if (ctx->debug)
        log_info("  routing net '%s' -> IN8 using x1y2\n", net->name.c_str(ctx));

    auto x1y1 = ctx->idf("X%dY%d", loc.x, loc.y);
    auto x2y1 = ctx->idf("X%dY%d", loc.x + 1, loc.y);
    auto x2y2 = ctx->idf("X%dY%d", loc.x + 1, loc.y + 1);

    auto cpe_combout2 = ctx->getBelPinWire(upper->bel, id_OUT);
    auto cpe_out2_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2_int")));
    auto cpe_out2 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2")));
    auto out_mux_d1 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("OM.P10.D1")));
    auto out_mux_y = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("OM.P10.Y")));
    auto in_mux_p10 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P10.D1")));

    if (bind_route_start) {
        ctx->bindWire(cpe_combout2, net, STRENGTH_LOCKED);
        find_and_bind_downhill_pip(ctx, cpe_combout2, cpe_out2_int, net);
        find_and_bind_downhill_pip(ctx, cpe_out2_int,  cpe_out2, net);
        find_and_bind_downhill_pip(ctx, cpe_out2, out_mux_d1, net);
    }

    find_and_bind_downhill_pip(ctx, out_mux_d1, out_mux_y, net); // inverting

    if (is_fourgroup_a) {
        auto sb_sml = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_SML.P10.Y2_int")));

        find_and_bind_downhill_pip(ctx, out_mux_y, sb_sml, net);
        find_and_bind_downhill_pip(ctx, sb_sml, in_mux_p10, net); // inverting
    } else {
        // x2y1/OM.P10.Y is x2y1/SB_BIG.P10.D0
        // x2y2/IM.P10.D1 is x2y1/SB_BIG.P10.Y2

        find_and_bind_downhill_pip(ctx, out_mux_y, in_mux_p10, net); // inverting
    }

    auto in_mux_p12 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P12.D6"))); // aka IM.P10.Y
    auto in_mux_p04 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P04.D7"))); // aka IM.P12.Y
    auto in_mux_p08 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P08.D6"))); // aka IM.P04.Y

    find_and_bind_downhill_pip(ctx, in_mux_p10, in_mux_p12, net); // inverting
    find_and_bind_downhill_pip(ctx, in_mux_p12, in_mux_p04, net); // inverting
    find_and_bind_downhill_pip(ctx, in_mux_p04, in_mux_p08, net); // inverting

    route_mult_diag(ctx, net, Loc{loc.x + 1, loc.y + 1, 0}, in_mux_p08, 8);
}

void route_mult_x2y1_lower(Context *ctx, NetInfo *net, CellInfo *lower, Loc loc, bool is_fourgroup_a)
{
    if (ctx->debug)
        log_info("  routing net '%s' -> IN5 using x2y1\n", net->name.c_str(ctx));

    auto x0y1 = ctx->idf("X%dY%d", loc.x - 1, loc.y);
    auto x1y1 = ctx->idf("X%dY%d", loc.x, loc.y);
    auto x2y1 = ctx->idf("X%dY%d", loc.x + 1, loc.y);

    auto cpe_combout1 = ctx->getBelPinWire(lower->bel, id_OUT);
    auto cpe_out1_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT1_int")));
    auto cpe_out1 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT1")));
    auto in_mux = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("IM.P05.D0")));

    ctx->bindWire(cpe_combout1, net, STRENGTH_LOCKED);
    find_and_bind_downhill_pip(ctx, cpe_combout1, cpe_out1_int, net);
    find_and_bind_downhill_pip(ctx, cpe_out1_int, cpe_out1, net);

    if (is_fourgroup_a) {
        auto sb_big_p07_d0 = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_BIG.P07.D0")));
        auto sb_big_p07_y1 = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_BIG.P07.Y1")));
        auto sb_big_p07_ydiag =
                ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_BIG.P07.YDIAG")));
        auto sb_big_p06_x23 =
                ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_BIG.P06.X23")));
        auto sb_big_p06_ydiag =
                ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_BIG.P06.YDIAG")));
        auto sb_big_p05_x23 =
                ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_BIG.P05.X23")));
        auto sb_big_p05_ydiag = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_BIG.P05.YDIAG")));
        auto sb_big_p05_y1 = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_BIG.P05.Y1")));

        find_and_bind_downhill_pip(ctx, cpe_out1, sb_big_p07_d0, net);
        find_and_bind_downhill_pip(ctx, sb_big_p07_d0, sb_big_p07_y1, net);
        find_and_bind_downhill_pip(ctx, sb_big_p07_y1, sb_big_p07_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p07_ydiag, sb_big_p06_x23, net);
        find_and_bind_downhill_pip(ctx, sb_big_p06_x23, sb_big_p06_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p06_ydiag, sb_big_p05_x23, net);
        find_and_bind_downhill_pip(ctx, sb_big_p05_x23, sb_big_p05_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p05_ydiag, sb_big_p05_y1, net);
        find_and_bind_downhill_pip(ctx, sb_big_p05_y1, in_mux, net);
    } else {
        auto sb_sml_p07_d0 = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P07.D0")));
        auto sb_sml_p07_y1_int = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P07.Y1_int")));
        auto sb_sml_p07_ydiag_int = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P07.YDIAG_int")));
        auto sb_sml_p07_ydiag =
                ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P07.YDIAG")));
        auto sb_sml_p06_x23 =
                ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P06.X23")));
        auto sb_sml_p06_ydiag_int = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P06.YDIAG_int")));
        auto sb_sml_p06_ydiag =
                ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P06.YDIAG")));
        auto sb_sml_p05_x23 =
                ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P05.X23")));
        auto sb_sml_p05_ydiag_int = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P05.YDIAG_int")));
        auto sb_sml_p05_y1_int = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P05.Y1_int")));
        auto sb_sml_p05_y1 = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P05.Y1")));

        find_and_bind_downhill_pip(ctx, cpe_out1, sb_sml_p07_d0, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p07_d0, sb_sml_p07_y1_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p07_y1_int, sb_sml_p07_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p07_ydiag_int, sb_sml_p07_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p07_ydiag, sb_sml_p06_x23, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p06_x23, sb_sml_p06_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p06_ydiag_int, sb_sml_p06_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p06_ydiag, sb_sml_p05_x23, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p05_x23, sb_sml_p05_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p05_ydiag_int, sb_sml_p05_y1_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p05_y1_int, sb_sml_p05_y1, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p05_y1, in_mux, net);
    }

    route_mult_diag(ctx, net, Loc{loc.x + 1, loc.y, 0}, in_mux, 5);
}

void route_mult_x2y1_upper_in1(Context *ctx, NetInfo *net, CellInfo *upper, Loc loc, bool is_fourgroup_a)
{
    if (ctx->debug)
        log_info("  routing net '%s' -> IN1 using x2y1\n", net->name.c_str(ctx));

    auto x0y1 = ctx->idf("X%dY%d", loc.x - 1, loc.y);
    auto x1y1 = ctx->idf("X%dY%d", loc.x, loc.y);
    auto x2y1 = ctx->idf("X%dY%d", loc.x + 1, loc.y);

    auto cpe_combout2 = ctx->getBelPinWire(upper->bel, id_OUT);
    auto cpe_out2_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2_int")));
    auto cpe_out2 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2")));
    auto in_mux = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("IM.P01.D0")));

    ctx->bindWire(cpe_combout2, net, STRENGTH_LOCKED);
    find_and_bind_downhill_pip(ctx, cpe_combout2, cpe_out2_int, net);
    find_and_bind_downhill_pip(ctx, cpe_out2_int, cpe_out2, net);

    if (is_fourgroup_a) {
        auto sb_big_p03_d0 = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_BIG.P03.D0")));
        auto sb_big_p03_y1 = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_BIG.P03.Y1")));
        auto sb_big_p03_ydiag =
                ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_BIG.P03.YDIAG"))); // AKA SB_BIG.P02.X23
        auto sb_big_p02_ydiag =
                ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_BIG.P02.YDIAG"))); // AKA SB_BIG.P01.X23
        auto sb_big_p01_ydiag = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_BIG.P01.YDIAG")));
        // x2y1/IM.P01.D0 is x0y1/SB_BIG.P01.Y1

        find_and_bind_downhill_pip(ctx, cpe_out2, sb_big_p03_d0, net);
        find_and_bind_downhill_pip(ctx, sb_big_p03_d0, sb_big_p03_y1, net);
        find_and_bind_downhill_pip(ctx, sb_big_p03_y1, sb_big_p03_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p03_ydiag, sb_big_p02_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p02_ydiag, sb_big_p01_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p01_ydiag, in_mux, net);
    } else {
        auto sb_sml_p03_d0 = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P03.D0")));
        auto sb_sml_p03_y1_int = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P03.Y1_int")));
        auto sb_sml_p03_ydiag_int = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P03.YDIAG_int")));
        auto sb_sml_p03_ydiag =
                ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P03.YDIAG"))); // AKA SB_SML.P02.X23
        auto sb_sml_p02_ydiag_int = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P02.YDIAG_int")));
        auto sb_sml_p02_ydiag =
                ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P02.YDIAG"))); // AKA SB_SML.P01.X23
        auto sb_sml_p01_ydiag_int = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P01.YDIAG_int")));
        auto sb_sml_p01_y1_int = ctx->getWireByName(IdStringList::concat(x0y1, ctx->idf("SB_SML.P01.Y1_int")));

        find_and_bind_downhill_pip(ctx, cpe_out2, sb_sml_p03_d0, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p03_d0, sb_sml_p03_y1_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p03_y1_int, sb_sml_p03_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p03_ydiag_int, sb_sml_p03_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p03_ydiag, sb_sml_p02_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p02_ydiag_int, sb_sml_p02_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p02_ydiag, sb_sml_p01_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p01_ydiag_int, sb_sml_p01_y1_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p01_y1_int, in_mux, net);
    }

    route_mult_diag(ctx, net, Loc{loc.x + 1, loc.y, 0}, in_mux, 1);
}

void route_mult_x2y1_upper_in8(Context *ctx, NetInfo *net, CellInfo *upper, Loc loc, bool is_fourgroup_a,
                               bool bind_route_start = false)
{
    if (ctx->debug)
        log_info("  routing net '%s' -> IN8 using x2y1\n", net->name.c_str(ctx));

    auto x1y1 = ctx->idf("X%dY%d", loc.x, loc.y);
    auto x1y2 = ctx->idf("X%dY%d", loc.x, loc.y + 1);
    auto x2y2 = ctx->idf("X%dY%d", loc.x + 1, loc.y + 1);

    auto cpe_combout2 = ctx->getBelPinWire(upper->bel, id_OUT);
    auto cpe_out2_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2_int")));
    auto cpe_out2 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2")));
    auto out_mux_d2 = ctx->getWireByName(IdStringList::concat(x1y2, ctx->idf("OM.P09.D2")));
    auto out_mux_y = ctx->getWireByName(IdStringList::concat(x1y2, ctx->idf("OM.P09.Y")));
    auto in_mux_p09 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P09.D0")));

    if (bind_route_start) {
        ctx->bindWire(cpe_combout2, net, STRENGTH_LOCKED);
        find_and_bind_downhill_pip(ctx, cpe_combout2, cpe_out2_int, net);
        find_and_bind_downhill_pip(ctx, cpe_out2_int, cpe_out2, net);
        find_and_bind_downhill_pip(ctx, cpe_out2, out_mux_d2, net);
    }

    find_and_bind_downhill_pip(ctx, out_mux_d2, out_mux_y, net); // inverting

    if (is_fourgroup_a) {
        auto sb_sml = ctx->getWireByName(IdStringList::concat(x1y2, ctx->idf("SB_SML.P09.Y1_int")));

        find_and_bind_downhill_pip(ctx, out_mux_y, sb_sml, net);
        find_and_bind_downhill_pip(ctx, sb_sml, in_mux_p09, net); // inverting
    } else {
        // x1y2/OM.P09.Y is x1y2/SB_BIG.P09.D0
        // x2y2/IM.P09.D0 is x2y1/SB_BIG.P09.Y1

        find_and_bind_downhill_pip(ctx, out_mux_y, in_mux_p09, net); // inverting
    }

    auto in_mux_p12 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P12.D7"))); // aka IM.P09.Y
    auto in_mux_p04 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P04.D7"))); // aka IM.P12.Y
    auto in_mux_p08 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P08.D6"))); // aka IM.P04.Y

    find_and_bind_downhill_pip(ctx, in_mux_p09, in_mux_p12, net); // inverting
    find_and_bind_downhill_pip(ctx, in_mux_p12, in_mux_p04, net); // inverting
    find_and_bind_downhill_pip(ctx, in_mux_p04, in_mux_p08, net); // inverting

    route_mult_diag(ctx, net, Loc{loc.x + 1, loc.y + 1, 0}, in_mux_p08, 8);
}

void route_mult_x2y2_lower(Context *ctx, NetInfo *net, CellInfo *lower, Loc loc, bool is_fourgroup_a)
{
    if (ctx->debug)
        log_info("  routing net '%s' -> IN5 using x2y2\n", net->name.c_str(ctx));

    auto x1y1 = ctx->idf("X%dY%d", loc.x, loc.y);
    auto x2y1 = ctx->idf("X%dY%d", loc.x + 1, loc.y);

    auto cpe_combout1 = ctx->getBelPinWire(lower->bel, id_OUT);
    auto cpe_out1_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT1_int")));
    auto cpe_out1 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT1")));
    auto in_mux = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("IM.P05.D0")));

    ctx->bindWire(cpe_combout1, net, STRENGTH_LOCKED);
    find_and_bind_downhill_pip(ctx, cpe_combout1, cpe_out1_int, net);
    find_and_bind_downhill_pip(ctx, cpe_out1_int, cpe_out1, net);

    if (is_fourgroup_a) {
        auto sb_sml_p08_d0 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P08.D0")));
        auto sb_sml_p08_y1_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P08.Y1_int")));
        auto sb_sml_p08_ydiag_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P08.YDIAG_int")));
        auto sb_sml_p08_ydiag =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P08.YDIAG")));
        auto sb_sml_p07_x23 =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P07.X23")));
        auto sb_sml_p07_ydiag_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P07.YDIAG_int")));
        auto sb_sml_p07_ydiag =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P07.YDIAG")));
        auto sb_sml_p06_x23 =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P06.X23")));
        auto sb_sml_p06_ydiag_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P06.YDIAG_int")));
        auto sb_sml_p06_ydiag =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P06.YDIAG")));
        auto sb_sml_p05_x23 =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P05.X23")));
        auto sb_sml_p05_ydiag_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P05.YDIAG_int")));
        auto sb_sml_p05_y1_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P05.Y1_int")));
        auto sb_sml_p05_y1 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P05.Y1")));

        find_and_bind_downhill_pip(ctx, cpe_out1, sb_sml_p08_d0, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p08_d0, sb_sml_p08_y1_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p08_y1_int, sb_sml_p08_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p08_ydiag_int, sb_sml_p08_ydiag, net); // inverting
        find_and_bind_downhill_pip(ctx, sb_sml_p08_ydiag, sb_sml_p07_x23, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p07_x23, sb_sml_p07_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p07_ydiag_int, sb_sml_p07_ydiag, net); // inverting
        find_and_bind_downhill_pip(ctx, sb_sml_p07_ydiag, sb_sml_p06_x23, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p06_x23, sb_sml_p06_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p06_ydiag_int, sb_sml_p06_ydiag, net); // inverting
        find_and_bind_downhill_pip(ctx, sb_sml_p06_ydiag, sb_sml_p05_x23, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p05_x23, sb_sml_p05_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p05_ydiag_int, sb_sml_p05_y1_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p05_y1_int, sb_sml_p05_y1, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p05_y1, in_mux, net);
    } else {
        auto sb_big_p08_d0 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P08.D0")));
        auto sb_big_p08_y1 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P08.Y1")));
        auto sb_big_p08_ydiag =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P08.YDIAG")));
        auto sb_big_p07_x23 =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P07.X23")));
        auto sb_big_p07_ydiag =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P07.YDIAG")));
        auto sb_big_p06_x23 =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P06.X23")));
        auto sb_big_p06_ydiag =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P06.YDIAG")));
        auto sb_big_p05_x23 =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P05.X23")));
        auto sb_big_p05_ydiag = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P05.YDIAG")));
        auto sb_big_p05_y1 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P05.Y1")));

        find_and_bind_downhill_pip(ctx, cpe_out1, sb_big_p08_d0, net);
        find_and_bind_downhill_pip(ctx, sb_big_p08_d0, sb_big_p08_y1, net);
        find_and_bind_downhill_pip(ctx, sb_big_p08_y1, sb_big_p08_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p08_ydiag, sb_big_p07_x23, net);
        find_and_bind_downhill_pip(ctx, sb_big_p07_x23, sb_big_p07_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p07_ydiag, sb_big_p06_x23, net);
        find_and_bind_downhill_pip(ctx, sb_big_p06_x23, sb_big_p06_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p06_ydiag, sb_big_p05_x23, net);
        find_and_bind_downhill_pip(ctx, sb_big_p05_x23, sb_big_p05_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p05_ydiag, sb_big_p05_y1, net);
        find_and_bind_downhill_pip(ctx, sb_big_p05_y1, in_mux, net);
    }

    route_mult_diag(ctx, net, Loc{loc.x + 1, loc.y, 0}, in_mux, 5);
}

void route_mult_x2y2_upper_in1(Context *ctx, NetInfo *net, CellInfo *upper, Loc loc, bool is_fourgroup_a)
{
    if (ctx->debug)
        log_info("  routing net '%s' -> IN1 using x2y2\n", net->name.c_str(ctx));

    auto x1y1 = ctx->idf("X%dY%d", loc.x, loc.y);
    auto x2y1 = ctx->idf("X%dY%d", loc.x + 1, loc.y);

    auto cpe_combout2 = ctx->getBelPinWire(upper->bel, id_OUT);
    auto cpe_out2_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2_int")));
    auto cpe_out2 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2")));
    auto in_mux = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("IM.P01.D0")));

    ctx->bindWire(cpe_combout2, net, STRENGTH_LOCKED);
    find_and_bind_downhill_pip(ctx, cpe_combout2, cpe_out2_int, net);
    find_and_bind_downhill_pip(ctx, cpe_out2_int, cpe_out2, net);

    if (is_fourgroup_a) {
        auto sb_sml_p04_d0 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P04.D0")));
        auto sb_sml_p04_y1_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P04.Y1_int")));
        auto sb_sml_p04_ydiag_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P04.YDIAG_int")));
        auto sb_sml_p04_ydiag =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P04.YDIAG"))); // AKA SB_SML.P03.X23
        auto sb_sml_p03_ydiag_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P03.YDIAG_int")));
        auto sb_sml_p03_ydiag =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P03.YDIAG"))); // AKA SB_SML.P02.X23
        auto sb_sml_p02_ydiag_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P02.YDIAG_int")));
        auto sb_sml_p02_ydiag =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P02.YDIAG"))); // AKA SB_SML.P01.X23
        auto sb_sml_p01_ydiag_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P01.YDIAG_int")));
        auto sb_sml_p01_y1_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P01.Y1_int")));

        find_and_bind_downhill_pip(ctx, cpe_out2_int, sb_sml_p04_d0, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p04_d0, sb_sml_p04_y1_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p04_y1_int, sb_sml_p04_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p04_ydiag_int, sb_sml_p04_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p04_ydiag, sb_sml_p03_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p03_ydiag_int, sb_sml_p03_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p03_ydiag, sb_sml_p02_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p02_ydiag_int, sb_sml_p02_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p02_ydiag, sb_sml_p01_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p01_ydiag_int, sb_sml_p01_y1_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_p01_y1_int, in_mux, net);
    } else {
        auto sb_big_p04_d0 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P04.D0")));
        auto sb_big_p04_y1 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P04.Y1")));
        auto sb_big_p04_ydiag =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P04.YDIAG"))); // AKA SB_BIG.P07.X23
        auto sb_big_p03_ydiag =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P03.YDIAG"))); // AKA SB_BIG.P05.X23
        auto sb_big_p02_ydiag =
                ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P02.YDIAG"))); // AKA SB_BIG.P05.X23
        auto sb_big_p01_ydiag = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P01.YDIAG")));
        // x2y1/IM.P05.D0 is x1y1/SB_BIG.P05.Y1

        find_and_bind_downhill_pip(ctx, cpe_out2_int, sb_big_p04_d0, net);
        find_and_bind_downhill_pip(ctx, sb_big_p04_d0, sb_big_p04_y1, net);
        find_and_bind_downhill_pip(ctx, sb_big_p04_y1, sb_big_p04_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p04_ydiag, sb_big_p03_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p03_ydiag, sb_big_p02_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p02_ydiag, sb_big_p01_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_p01_ydiag, in_mux, net);
    }

    route_mult_diag(ctx, net, Loc{loc.x + 1, loc.y, 0}, in_mux, 1);
}

void route_mult_x2y2_upper_in8(Context *ctx, NetInfo *net, CellInfo *upper, Loc loc, bool is_fourgroup_a,
                               bool bind_route_start = false)
{
    if (ctx->debug)
        log_info("  routing net '%s' -> IN8 using x2y2\n", net->name.c_str(ctx));

    auto x0y0 = ctx->idf("X%dY%d", loc.x - 1, loc.y - 1);
    auto x1y1 = ctx->idf("X%dY%d", loc.x, loc.y);
    auto x2y0 = ctx->idf("X%dY%d", loc.x + 1, loc.y - 1);
    auto x2y2 = ctx->idf("X%dY%d", loc.x + 1, loc.y + 1);

    auto cpe_combout2 = ctx->getBelPinWire(upper->bel, id_OUT);
    auto cpe_out2_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2_int")));
    auto cpe_out2 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2")));
    auto in_mux = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P08.D1")));

    if (bind_route_start) {
        ctx->bindWire(cpe_combout2, net, STRENGTH_LOCKED);
        find_and_bind_downhill_pip(ctx, cpe_combout2, cpe_out2_int, net);
        find_and_bind_downhill_pip(ctx, cpe_out2_int, cpe_out2, net);
        if (is_fourgroup_a) {
            auto sb_big_d0 = ctx->getWireByName(IdStringList::concat(x0y0, ctx->idf("SB_BIG.P08.D0")));
            find_and_bind_downhill_pip(ctx, cpe_out2, sb_big_d0, net);
        } else {
            auto sb_sml_d0 = ctx->getWireByName(IdStringList::concat(x0y0, ctx->idf("SB_SML.P08.D0")));
            find_and_bind_downhill_pip(ctx, cpe_out2, sb_sml_d0, net);
        }
    }

    if (is_fourgroup_a) {
        auto sb_big_d0 = ctx->getWireByName(IdStringList::concat(x0y0, ctx->idf("SB_BIG.P08.D0")));
        auto sb_big_y1 = ctx->getWireByName(IdStringList::concat(x0y0, ctx->idf("SB_BIG.P08.Y1")));
        auto sb_sml_y1_int = ctx->getWireByName(IdStringList::concat(x2y0, ctx->idf("SB_SML.P08.Y1_int")));
        auto sb_sml_ydiag_int = ctx->getWireByName(IdStringList::concat(x2y0, ctx->idf("SB_SML.P08.YDIAG_int")));
        auto sb_sml_y2_int = ctx->getWireByName(IdStringList::concat(x2y0, ctx->idf("SB_SML.P08.Y2_int")));

        find_and_bind_downhill_pip(ctx, sb_big_d0, sb_big_y1, net);
        find_and_bind_downhill_pip(ctx, sb_big_y1, sb_sml_y1_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_y1_int, sb_sml_ydiag_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_ydiag_int, sb_sml_y2_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_y2_int, in_mux, net);
    } else {
        auto sb_sml_d0 = ctx->getWireByName(IdStringList::concat(x0y0, ctx->idf("SB_SML.P08.D0")));
        auto sb_sml_y1_int = ctx->getWireByName(IdStringList::concat(x0y0, ctx->idf("SB_SML.P08.Y1_int")));
        auto sb_sml_y1 = ctx->getWireByName(IdStringList::concat(x0y0, ctx->idf("SB_SML.P08.Y1")));
        auto sb_big_y1 = ctx->getWireByName(IdStringList::concat(x2y0, ctx->idf("SB_BIG.P08.Y1")));
        auto sb_big_ydiag = ctx->getWireByName(IdStringList::concat(x2y0, ctx->idf("SB_BIG.P08.YDIAG")));
        // x2y2/IM.P08.D0 is x2y0/SB_BIG.P08.Y2

        find_and_bind_downhill_pip(ctx, sb_sml_d0, sb_sml_y1_int, net);
        find_and_bind_downhill_pip(ctx, sb_sml_y1_int, sb_sml_y1, net);
        find_and_bind_downhill_pip(ctx, sb_sml_y1, sb_big_y1, net);
        find_and_bind_downhill_pip(ctx, sb_big_y1, sb_big_ydiag, net);
        find_and_bind_downhill_pip(ctx, sb_big_ydiag, in_mux, net);
    }

    route_mult_diag(ctx, net, Loc{loc.x + 1, loc.y + 1, 0}, in_mux, 8);
}
} // namespace

NEXTPNR_NAMESPACE_BEGIN

void GateMateImpl::route_mult()
{
    int x_within_fourgroup, y_within_fourgroup;
    log_info("Routing multipliers...\n");

    // I am fully aware the nextpnr API is absolutely not designed around naming specific pips.
    // Unfortunately, this is the easiest way to describe the specific routing required.
    // Myrtle, please forgive me.
    for (auto a_passthru_lower : this->multiplier_a_passthru_lowers) {
        auto *lower = ctx->cells.at(a_passthru_lower).get();
        auto *lower_out = lower->ports.at(id_OUT).net;

        auto loc = ctx->getBelLocation(lower->bel);
        bool is_fourgroup_a =
                is_fourgroup_a_within_tile(tile_extra_data(lower->bel.tile), x_within_fourgroup, y_within_fourgroup);

        if (ctx->debug) {
            log_info("  A passthrough at (%d, %d) has 4-group %c\n", loc.x, loc.y, is_fourgroup_a ? 'A' : 'B');

            log_info("    lower.OUT [OUT1] = %s\n", ctx->nameOfWire(ctx->getBelPinWire(lower->bel, id_OUT)));
            for (auto sink_port : lower->ports.at(id_OUT).net->users) {
                auto sink_loc = ctx->getBelLocation(sink_port.cell->bel);
                log_info("      -> %s.%s at (%d, %d)\n", sink_port.cell->name.c_str(ctx), sink_port.port.c_str(ctx),
                         sink_loc.x, sink_loc.y);
            }
        }

        if (x_within_fourgroup == 0 && y_within_fourgroup == 0) {
            route_mult_x1y1_lower(ctx, lower_out, lower, loc, is_fourgroup_a);
        } else if (x_within_fourgroup == 0 && y_within_fourgroup == 1) {
            route_mult_x1y2_lower(ctx, lower_out, lower, loc, is_fourgroup_a);
        } else if (x_within_fourgroup == 1 && y_within_fourgroup == 0) {
            route_mult_x2y1_lower(ctx, lower_out, lower, loc, is_fourgroup_a);
        } else /* if (x_within_fourgroup == 1 && y_within_fourgroup == 1) */ {
            route_mult_x2y2_lower(ctx, lower_out, lower, loc, is_fourgroup_a);
        }
    }

    for (auto a_passthru_upper : this->multiplier_a_passthru_uppers) {
        auto *upper = ctx->cells.at(a_passthru_upper).get();
        auto *upper_out = upper->ports.at(id_OUT).net;

        auto loc = ctx->getBelLocation(upper->bel);
        bool is_fourgroup_a =
                is_fourgroup_a_within_tile(tile_extra_data(upper->bel.tile), x_within_fourgroup, y_within_fourgroup);

        bool needs_in8_route = false;

        if (ctx->debug) {
            log_info("  A passthrough at (%d, %d) has 4-group %c\n", loc.x, loc.y, is_fourgroup_a ? 'A' : 'B');

            log_info("    upper.OUT [OUT2] = %s\n", ctx->nameOfWire(ctx->getBelPinWire(upper->bel, id_OUT)));
        }
        for (auto sink_port : upper->ports.at(id_OUT).net->users) {
            if (sink_port.port == id_IN8)
                needs_in8_route = true;
            auto sink_loc = ctx->getBelLocation(sink_port.cell->bel);
            if (ctx->debug)
                log_info("      -> %s.%s at (%d, %d)\n", sink_port.cell->name.c_str(ctx), sink_port.port.c_str(ctx),
                         sink_loc.x, sink_loc.y);
        }

        if (x_within_fourgroup == 0 && y_within_fourgroup == 0) {
            route_mult_x1y1_upper_in1(ctx, upper_out, upper, loc, is_fourgroup_a);
            if (needs_in8_route)
                route_mult_x1y1_upper_in8(ctx, upper_out, upper, loc, is_fourgroup_a);
        } else if (x_within_fourgroup == 0 && y_within_fourgroup == 1) {
            route_mult_x1y2_upper_in1(ctx, upper_out, upper, loc, is_fourgroup_a);
            if (needs_in8_route)
                route_mult_x1y2_upper_in8(ctx, upper_out, upper, loc, is_fourgroup_a);
        } else if (x_within_fourgroup == 1 && y_within_fourgroup == 0) {
            route_mult_x2y1_upper_in1(ctx, upper_out, upper, loc, is_fourgroup_a);
            if (needs_in8_route)
                route_mult_x2y1_upper_in8(ctx, upper_out, upper, loc, is_fourgroup_a);
        } else /* if (x_within_fourgroup == 1 && y_within_fourgroup == 1) */ {
            route_mult_x2y2_upper_in1(ctx, upper_out, upper, loc, is_fourgroup_a);
            if (needs_in8_route)
                route_mult_x2y2_upper_in8(ctx, upper_out, upper, loc, is_fourgroup_a);
        }
    }

    for (auto zero_driver_name : this->multiplier_zero_drivers) {
        auto *zero_driver = ctx->cells.at(zero_driver_name).get();
        auto *out = zero_driver->ports.at(id_OUT).net;

        auto loc = ctx->getBelLocation(zero_driver->bel);
        bool is_fourgroup_a = is_fourgroup_a_within_tile(tile_extra_data(zero_driver->bel.tile), x_within_fourgroup,
                                                         y_within_fourgroup);

        if (ctx->debug) {
            log_info("  Zero driver at (%d, %d) has 4-group %c\n", loc.x, loc.y, is_fourgroup_a ? 'A' : 'B');

            log_info("    zero_driver.OUT [OUT2] = %s\n",
                     ctx->nameOfWire(ctx->getBelPinWire(zero_driver->bel, id_OUT)));
            for (auto sink_port : zero_driver->ports.at(id_OUT).net->users) {
                auto sink_loc = ctx->getBelLocation(sink_port.cell->bel);
                log_info("      -> %s.%s at (%d, %d)\n", sink_port.cell->name.c_str(ctx), sink_port.port.c_str(ctx),
                         sink_loc.x, sink_loc.y);
            }
        }

        if (x_within_fourgroup == 0 && y_within_fourgroup == 0) {
            route_mult_x1y1_upper_in8(ctx, out, zero_driver, loc, is_fourgroup_a, /*bind_route_start=*/true);
        } else if (x_within_fourgroup == 0 && y_within_fourgroup == 1) {
            route_mult_x1y2_upper_in8(ctx, out, zero_driver, loc, is_fourgroup_a, /*bind_route_start=*/true);
        } else if (x_within_fourgroup == 1 && y_within_fourgroup == 0) {
            route_mult_x2y1_upper_in8(ctx, out, zero_driver, loc, is_fourgroup_a, /*bind_route_start=*/true);
        } else /* if (x_within_fourgroup == 1 && y_within_fourgroup == 1) */ {
            route_mult_x2y2_upper_in8(ctx, out, zero_driver, loc, is_fourgroup_a, /*bind_route_start=*/true);
        }
    }
}

NEXTPNR_NAMESPACE_END
