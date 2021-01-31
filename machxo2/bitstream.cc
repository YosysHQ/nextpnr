/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
 *  Copyright (C) 2021  William D. Jones <wjones@wdj-consulting.com>
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

#include <fstream>

#include "bitstream.h"
#include "config.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

// These seem simple enough to do inline for now.
namespace BaseConfigs {
    void config_empty_lcmxo2_1200hc(ChipConfig &cc)
    {
        cc.chip_name = "LCMXO2-1200HC";

        cc.tiles["EBR_R6C11:EBR1"].add_unknown(0, 12);
        cc.tiles["EBR_R6C15:EBR1"].add_unknown(0, 12);
        cc.tiles["EBR_R6C18:EBR1"].add_unknown(0, 12);
        cc.tiles["EBR_R6C21:EBR1"].add_unknown(0, 12);
        cc.tiles["EBR_R6C2:EBR1"].add_unknown(0, 12);
        cc.tiles["EBR_R6C5:EBR1"].add_unknown(0, 12);
        cc.tiles["EBR_R6C8:EBR1"].add_unknown(0, 12);

        cc.tiles["PT4:CFG0"].add_unknown(5, 30);
        cc.tiles["PT4:CFG0"].add_unknown(5, 32);
        cc.tiles["PT4:CFG0"].add_unknown(5, 36);

        cc.tiles["PT7:CFG3"].add_unknown(5, 18);
    }
} // namespace BaseConfigs

// Convert an absolute wire name to a relative Trellis one
static std::string get_trellis_wirename(Context *ctx, Location loc, WireId wire)
{
    std::string basename = ctx->tileInfo(wire)->wire_data[wire.index].name.get();
    std::string prefix2 = basename.substr(0, 2);
    if (prefix2 == "G_" || prefix2 == "L_" || prefix2 == "R_")
        return basename;
    if (loc == wire.location)
        return basename;
    std::string rel_prefix;
    if (wire.location.y < loc.y)
        rel_prefix += "N" + std::to_string(loc.y - wire.location.y);
    if (wire.location.y > loc.y)
        rel_prefix += "S" + std::to_string(wire.location.y - loc.y);
    if (wire.location.x > loc.x)
        rel_prefix += "E" + std::to_string(wire.location.x - loc.x);
    if (wire.location.x < loc.x)
        rel_prefix += "W" + std::to_string(loc.x - wire.location.x);
    return rel_prefix + "_" + basename;
}

static void set_pip(Context *ctx, ChipConfig &cc, PipId pip)
{
    std::string tile = ctx->getPipTilename(pip);
    std::string source = get_trellis_wirename(ctx, pip.location, ctx->getPipSrcWire(pip));
    std::string sink = get_trellis_wirename(ctx, pip.location, ctx->getPipDstWire(pip));
    cc.tiles[tile].add_arc(sink, source);
}

void write_bitstream(Context *ctx, std::string text_config_file)
{
    ChipConfig cc;

    switch (ctx->args.type) {
    case ArchArgs::LCMXO2_1200HC:
        BaseConfigs::config_empty_lcmxo2_1200hc(cc);
        break;
    default:
        NPNR_ASSERT_FALSE("Unsupported device type");
    }

    cc.metadata.push_back("Part: " + ctx->getFullChipName());

    // Add all set, configurable pips to the config
    for (auto pip : ctx->getPips()) {
        if (ctx->getBoundPipNet(pip) != nullptr) {
            if (ctx->getPipClass(pip) == 0) { // ignore fixed pips
                set_pip(ctx, cc, pip);
            }
        }
    }

    // Configure chip
    if (!text_config_file.empty()) {
        std::ofstream out_config(text_config_file);
        out_config << cc;
    }
}

NEXTPNR_NAMESPACE_END
