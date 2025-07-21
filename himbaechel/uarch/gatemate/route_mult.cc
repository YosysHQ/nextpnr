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
#include "nextpnr_base_types.h"
#include "nextpnr_namespaces.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

void GateMateImpl::route_mult() {
    log_info("Routing multipliers...\n");

    // I am fully aware the nextpnr API is absolutely not designed around naming specific pips.
    // Unfortunately, this is the easiest way to describe the specific routing required.
    // Myrtle, please forgive me.
    for (auto &a_passthru : this->multiplier_a_passthrus) {
        auto *lower = a_passthru.first;
        auto *upper = a_passthru.second;

        auto *lower_out = lower->ports.at(id_OUT).net;
        auto *upper_out = upper->ports.at(id_OUT).net;

        auto loc = ctx->getBelLocation(lower->bel);

        auto x_fourgroup = (loc.x - 3) % 4;
        auto y_fourgroup = (loc.y - 3) % 4;
        bool is_fourgroup_a = (x_fourgroup < 2 && y_fourgroup < 2) || (x_fourgroup >= 2 && y_fourgroup >= 2);
        auto x_within_fourgroup = (loc.x - 3) % 2;
        auto y_within_fourgroup = (loc.y - 3) % 2;

        bool needs_in8_route = false;

        log_info("  A passthrough at (%d, %d) has 4-group %c\n", loc.x, loc.y, is_fourgroup_a ? 'A' : 'B');

        log_info("    lower.OUT [OUT1] = %s\n", ctx->nameOfWire(ctx->getBelPinWire(lower->bel, id_OUT)));
        for (auto sink_port : lower->ports.at(id_OUT).net->users)
            log_info("      -> %s.%s\n", sink_port.cell->name.c_str(ctx), sink_port.port.c_str(ctx));

        log_info("    upper.OUT [OUT2] = %s\n", ctx->nameOfWire(ctx->getBelPinWire(upper->bel, id_OUT)));
        for (auto sink_port : upper->ports.at(id_OUT).net->users) {
            if (sink_port.port == id_IN8)
                needs_in8_route = true;
            log_info("      -> %s.%s\n", sink_port.cell->name.c_str(ctx), sink_port.port.c_str(ctx));
        }

        auto x1y1 = ctx->idf("X%dY%d", loc.x, loc.y);
        auto x2y1 = ctx->idf("X%dY%d", loc.x + 1, loc.y);
        auto x2y2 = ctx->idf("X%dY%d", loc.x + 1, loc.y + 1);
        auto x4y2 = ctx->idf("X%dY%d", loc.x + 3, loc.y + 1);

        auto find_downhill_pip = [&](WireId from, WireId to) {
            NPNR_ASSERT(from != WireId());
            NPNR_ASSERT(to != WireId());
            for (auto pip : ctx->getPipsDownhill(from)) {
                if (ctx->getPipDstWire(pip) == to) {
                    log_info("    pip %s: %s -> %s\n", ctx->nameOfPip(pip), ctx->nameOfWire(from), ctx->nameOfWire(to));

                    return pip;
                }
            }
            log_error("Couldn't find pip from %s to %s\n", ctx->nameOfWire(from), ctx->nameOfWire(to));
        };

        if (is_fourgroup_a) {
            if (x_within_fourgroup == 0 && y_within_fourgroup == 0) {
                auto cpe_combout1 = ctx->getBelPinWire(lower->bel, id_OUT);
                auto cpe_out1_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT1_int")));

                ctx->bindWire(cpe_combout1, lower_out, STRENGTH_LOCKED);
                ctx->bindPip(find_downhill_pip(cpe_combout1, cpe_out1_int), lower_out, STRENGTH_LOCKED);

                auto cpe_combout2 = ctx->getBelPinWire(upper->bel, id_OUT);
                auto cpe_out2_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2_int")));

                ctx->bindWire(cpe_combout2, upper_out, STRENGTH_LOCKED);
                ctx->bindPip(find_downhill_pip(cpe_combout2, cpe_out2_int), upper_out, STRENGTH_LOCKED);

                {
                    log_info("  routing net '%s' -> IN5\n", lower_out->name.c_str(ctx));

                    auto sb_big = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P05.D0")));
                    auto in_mux = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("IM.P05.D0")));
                    auto cpe_in5 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("CPE.IN5")));
                    auto cpe_in5_int = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("CPE.IN5_int")));

                    ctx->bindPip(find_downhill_pip(cpe_out1_int, sb_big), lower_out, STRENGTH_LOCKED);
                    ctx->bindPip(find_downhill_pip(sb_big, in_mux), lower_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(in_mux, cpe_in5), lower_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(cpe_in5, cpe_in5_int), lower_out, STRENGTH_LOCKED);
                }

                {
                    log_info("  routing net '%s' -> IN1\n", upper_out->name.c_str(ctx));

                    auto sb_big = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_BIG.P01.D0")));
                    auto in_mux = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("IM.P01.D0")));
                    auto cpe_in1 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("CPE.IN1")));
                    auto cpe_in1_int = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("CPE.IN1_int")));

                    ctx->bindPip(find_downhill_pip(cpe_out2_int, sb_big), upper_out, STRENGTH_LOCKED);
                    ctx->bindPip(find_downhill_pip(sb_big, in_mux), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(in_mux, cpe_in1), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(cpe_in1, cpe_in1_int), upper_out, STRENGTH_LOCKED);
                }

                if (needs_in8_route) {
                    log_info("  routing net '%s' -> IN8\n", upper_out->name.c_str(ctx));

                    auto out_mux_d0 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("OM.P12.D0")));
                    auto out_mux_y = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("OM.P12.Y")));
                    auto sb_sml = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("SB_SML.P12.Y1_int")));
                    auto sb_big_d2_1 = ctx->getWireByName(IdStringList::concat(x4y2, ctx->idf("SB_BIG.P12.D2_1")));
                    auto sb_big_y1 = ctx->getWireByName(IdStringList::concat(x4y2, ctx->idf("SB_BIG.P12.Y1")));
                    auto sb_big_ydiag = ctx->getWireByName(IdStringList::concat(x4y2, ctx->idf("SB_BIG.P12.YDIAG")));
                    auto in_mux_p12 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P12.D2")));
                    auto in_mux_p04 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P04.D7"))); // aka IM.P12.Y
                    auto in_mux_p08 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P08.D6"))); // aka IM.P04.Y
                    auto in_mux_p08_y = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P08.Y")));
                    auto cpe_in8_int = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("CPE.IN8_int")));

                    ctx->bindPip(find_downhill_pip(out_mux_d0, out_mux_y), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(out_mux_y, sb_sml), upper_out, STRENGTH_LOCKED);
                    ctx->bindPip(find_downhill_pip(sb_sml, sb_big_d2_1), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(sb_big_d2_1, sb_big_y1), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(sb_big_y1, sb_big_ydiag), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(sb_big_ydiag, in_mux_p12), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(in_mux_p12, in_mux_p04), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(in_mux_p04, in_mux_p08), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(in_mux_p08, in_mux_p08_y), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(in_mux_p08_y, cpe_in8_int), upper_out, STRENGTH_LOCKED);
                }
            } else if (x_within_fourgroup == 0 && y_within_fourgroup == 1) {
                /* auto cpe_combout1 = ctx->getBelPinWire(lower->bel, id_OUT);
                auto cpe_out1_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT1_int")));

                ctx->bindWire(cpe_combout1, lower_out, STRENGTH_LOCKED);
                ctx->bindPip(find_downhill_pip(cpe_combout1, cpe_out1_int), lower_out, STRENGTH_LOCKED);*/

                auto cpe_combout2 = ctx->getBelPinWire(upper->bel, id_OUT);
                auto cpe_out2_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2_int")));

                ctx->bindWire(cpe_combout2, upper_out, STRENGTH_LOCKED);
                ctx->bindPip(find_downhill_pip(cpe_combout2, cpe_out2_int), upper_out, STRENGTH_LOCKED);

                {
                    //log_info("  routing net '%s' -> IN5\n", lower_out->name.c_str(ctx));

                    auto sb_sml = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("SB_SML.P06.D0")));
                    // the cologne chip guide for this magically teleports from plane 6 to plane 5. I do not know how.
                }

                if (needs_in8_route) {
                    log_info("  routing net '%s' -> IN8\n", upper_out->name.c_str(ctx));

                    auto out_mux_d1 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("OM.P10.D1")));
                    auto out_mux_y = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("OM.P10.Y")));
                    auto sb_sml = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("SB_SML.P10.Y2_int")));
                    auto in_mux_p10 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P10.D1")));
                    auto in_mux_p12 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P12.D6"))); // aka IM.P10.Y
                    auto in_mux_p04 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P04.D7"))); // aka IM.P12.Y
                    auto in_mux_p08 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P08.D6"))); // aka IM.P04.Y
                    auto in_mux_p08_y = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P08.Y")));
                    auto cpe_in8_int = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("CPE.IN8_int")));

                    ctx->bindPip(find_downhill_pip(cpe_out2_int, out_mux_d1), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(out_mux_d1, out_mux_y), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(out_mux_y, sb_sml), upper_out, STRENGTH_LOCKED);
                    ctx->bindPip(find_downhill_pip(sb_sml, in_mux_p10), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(in_mux_p10, in_mux_p12), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(in_mux_p12, in_mux_p04), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(in_mux_p04, in_mux_p08), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(in_mux_p08, in_mux_p08_y), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(in_mux_p08_y, cpe_in8_int), upper_out, STRENGTH_LOCKED);
                }
            } else if (x_within_fourgroup == 0 && y_within_fourgroup == 1) {
                /* auto cpe_combout1 = ctx->getBelPinWire(lower->bel, id_OUT);
                auto cpe_out1_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT1_int")));

                ctx->bindWire(cpe_combout1, lower_out, STRENGTH_LOCKED);
                ctx->bindPip(find_downhill_pip(cpe_combout1, cpe_out1_int), lower_out, STRENGTH_LOCKED);*/

                auto cpe_combout2 = ctx->getBelPinWire(upper->bel, id_OUT);
                auto cpe_out2_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2_int")));

                ctx->bindWire(cpe_combout2, upper_out, STRENGTH_LOCKED);
                ctx->bindPip(find_downhill_pip(cpe_combout2, cpe_out2_int), upper_out, STRENGTH_LOCKED);

                if (needs_in8_route) {
                    log_info("  routing net '%s' -> IN8\n", upper_out->name.c_str(ctx));


                }
            } else {
                log_info("  don't know how to route net '%s' (it's four-group A, (%d, %d))\n", lower_out->name.c_str(ctx), x_within_fourgroup, y_within_fourgroup);
                log_info("  don't know how to route net '%s' (it's four-group A, (%d, %d))\n", upper_out->name.c_str(ctx), x_within_fourgroup, y_within_fourgroup);
            }
        } else {
            if (x_within_fourgroup == 0 && y_within_fourgroup == 0) {
                auto cpe_combout1 = ctx->getBelPinWire(lower->bel, id_OUT);
                auto cpe_out1_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT1_int")));

                ctx->bindWire(cpe_combout1, lower_out, STRENGTH_LOCKED);
                ctx->bindPip(find_downhill_pip(cpe_combout1, cpe_out1_int), lower_out, STRENGTH_LOCKED);

                auto cpe_combout2 = ctx->getBelPinWire(upper->bel, id_OUT);
                auto cpe_out2_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("CPE.OUT2_int")));

                ctx->bindWire(cpe_combout2, upper_out, STRENGTH_LOCKED);
                ctx->bindPip(find_downhill_pip(cpe_combout2, cpe_out2_int), upper_out, STRENGTH_LOCKED);

                {
                    log_info("  routing net '%s' -> IN5\n", lower_out->name.c_str(ctx));

                    auto sb_sml_d0 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P05.D0")));
                    auto sb_sml_y1_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P05.Y1_int")));
                    auto in_mux = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("IM.P05.D0")));
                    auto cpe_in5 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("CPE.IN5")));
                    auto cpe_in5_int = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("CPE.IN5_int")));

                    ctx->bindPip(find_downhill_pip(cpe_out1_int, sb_sml_d0), lower_out, STRENGTH_LOCKED);
                    ctx->bindPip(find_downhill_pip(sb_sml_d0, sb_sml_y1_int), lower_out, STRENGTH_LOCKED);
                    ctx->bindPip(find_downhill_pip(sb_sml_y1_int, in_mux), lower_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(in_mux, cpe_in5), lower_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(cpe_in5, cpe_in5_int), lower_out, STRENGTH_LOCKED);
                }

                {
                    log_info("  routing net '%s' -> IN1\n", upper_out->name.c_str(ctx));

                    auto sb_sml_d0 = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P01.D0")));
                    auto sb_sml_y1_int = ctx->getWireByName(IdStringList::concat(x1y1, ctx->idf("SB_SML.P01.Y1_int")));
                    auto in_mux = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("IM.P01.D0")));
                    auto cpe_in1 = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("CPE.IN1")));
                    auto cpe_in1_int = ctx->getWireByName(IdStringList::concat(x2y1, ctx->idf("CPE.IN1_int")));

                    ctx->bindPip(find_downhill_pip(cpe_out2_int, sb_sml_d0), upper_out, STRENGTH_LOCKED);
                    ctx->bindPip(find_downhill_pip(sb_sml_d0, sb_sml_y1_int), upper_out, STRENGTH_LOCKED);
                    ctx->bindPip(find_downhill_pip(sb_sml_y1_int, in_mux), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(in_mux, cpe_in1), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(cpe_in1, cpe_in1_int), upper_out, STRENGTH_LOCKED);
                }

                if (needs_in8_route) {
                    log_info("  routing net '%s' -> IN8\n", upper_out->name.c_str(ctx));

                    auto out_mux_d0 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("OM.P12.D0")));
                    auto out_mux_y = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("OM.P12.Y")));
                    auto sb_big = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("SB_BIG.P12.Y1"))); // aka x4y2/SB_SML.P12.D2_1
                    auto sb_sml_y1_int = ctx->getWireByName(IdStringList::concat(x4y2, ctx->idf("SB_SML.P12.Y1_int")));
                    auto sb_sml_ydiag_int = ctx->getWireByName(IdStringList::concat(x4y2, ctx->idf("SB_SML.P12.YDIAG_int")));
                    auto sb_sml_y3_int = ctx->getWireByName(IdStringList::concat(x4y2, ctx->idf("SB_SML.P12.Y3_int")));
                    auto in_mux_p12 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P12.D2")));
                    auto in_mux_p04 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P04.D7"))); // aka IM.P12.Y
                    auto in_mux_p08 = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P08.D6"))); // aka IM.P04.Y
                    auto in_mux_p08_y = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("IM.P08.Y")));
                    auto cpe_in8_int = ctx->getWireByName(IdStringList::concat(x2y2, ctx->idf("CPE.IN8_int")));

                    ctx->bindPip(find_downhill_pip(out_mux_d0, out_mux_y), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(out_mux_y, sb_big), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(sb_big, sb_sml_y1_int), upper_out, STRENGTH_LOCKED);
                    ctx->bindPip(find_downhill_pip(sb_sml_y1_int, sb_sml_ydiag_int), upper_out, STRENGTH_LOCKED);
                    ctx->bindPip(find_downhill_pip(sb_sml_ydiag_int, sb_sml_y3_int), upper_out, STRENGTH_LOCKED);
                    ctx->bindPip(find_downhill_pip(sb_sml_y3_int, in_mux_p12), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(in_mux_p12, in_mux_p04), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(in_mux_p04, in_mux_p08), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(in_mux_p08, in_mux_p08_y), upper_out, STRENGTH_LOCKED); // inverting
                    ctx->bindPip(find_downhill_pip(in_mux_p08_y, cpe_in8_int), upper_out, STRENGTH_LOCKED);
                }
            } else {
                log_info("  don't know how to route net '%s' (it's four-group B, (%d, %d))\n", lower_out->name.c_str(ctx), x_within_fourgroup, y_within_fourgroup);
                log_info("  don't know how to route net '%s' (it's four-group B, (%d, %d))\n", upper_out->name.c_str(ctx), x_within_fourgroup, y_within_fourgroup);
            }
        }
    }
}

NEXTPNR_NAMESPACE_END
