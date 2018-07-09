/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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

#include "bitstream.h"

// From Project Trellis
#include "BitDatabase.hpp"
#include "Bitstream.hpp"
#include "Chip.hpp"
#include "ChipConfig.hpp"
#include "Tile.hpp"
#include "TileConfig.hpp"

#include <fstream>
#include <streambuf>

#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

// Convert an absolute wire name to a relative Trellis one
static std::string get_trellis_wirename(Context *ctx, Location loc, WireId wire)
{
    std::string basename = ctx->locInfo(wire)->wire_data[wire.index].name.get();
    std::string prefix2 = basename.substr(0, 2);
    if (prefix2 == "G_" || prefix2 == "L_" || prefix2 == "R_")
        return basename;
    if (loc == wire.location)
        return basename;
    std::string rel_prefix;
    if (wire.location.y < loc.y)
        rel_prefix += "N" + to_string(loc.y - wire.location.y);
    if (wire.location.y > loc.y)
        rel_prefix += "S" + to_string(wire.location.y - loc.y);
    if (wire.location.x > loc.x)
        rel_prefix += "E" + to_string(wire.location.x - loc.x);
    if (wire.location.x < loc.x)
        rel_prefix += "W" + to_string(loc.x - wire.location.x);
    return rel_prefix + "_" + basename;
}

static std::vector<bool> int_to_bitvector(int val, int size)
{
    std::vector<bool> bv;
    for (int i = 0; i < size; i++) {
        bv.push_back((val & (1 << i)) != 0);
    }
    return bv;
}

void write_bitstream(Context *ctx, std::string base_config_file, std::string text_config_file,
                     std::string bitstream_file)
{
    Trellis::Chip empty_chip(ctx->getChipName());
    Trellis::ChipConfig cc;
    if (!base_config_file.empty()) {
        std::ifstream config_file(base_config_file);
        if (!config_file) {
            log_error("failed to open base config file '%s'\n", base_config_file.c_str());
        }
        std::string str((std::istreambuf_iterator<char>(config_file)), std::istreambuf_iterator<char>());
        cc = Trellis::ChipConfig::from_string(str);
    } else {
        cc.chip_name = ctx->getChipName();
        // TODO: .bit metadata
    }

    // Add all set, configurable pips to the config
    for (auto pip : ctx->getPips()) {
        if (ctx->getBoundPipNet(pip) != IdString()) {
            if (ctx->getPipType(pip) == 0) { // ignore fixed pips
                auto tile = empty_chip.get_tile_by_position_and_type(pip.location.y, pip.location.x,
                                                                     ctx->getPipTiletype(pip));
                std::string source = get_trellis_wirename(ctx, pip.location, ctx->getPipSrcWire(pip));
                std::string sink = get_trellis_wirename(ctx, pip.location, ctx->getPipDstWire(pip));
                cc.tiles[tile->info.name].add_arc(sink, source);
            }
        }
    }

    // Set all bankref tiles to 3.3V (TODO)
    for (const auto &tile : empty_chip.tiles) {
        std::string type = tile.second->info.type;
        if (type.find("BANKREF") != std::string::npos && type != "BANKREF8") {
            cc.tiles[type].add_enum("BANK.VCCIO", "3V3");
        }
    }

    // Configure slices
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->bel == BelId()) {
            log_warning("found unplaced cell '%s' during bitstream gen\n", ci->name.c_str(ctx));
        }
        BelId bel = ci->bel;
        if (ci->type == ctx->id("TRELLIS_SLICE")) {
            auto tile = empty_chip.get_tile_by_position_and_type(bel.location.y, bel.location.x, "PLC2");
            std::string tname = tile->info.name;
            std::string slice = ctx->locInfo(bel)->bel_data[bel.index].name.get();
            int lut0_init = int_or_default(ci->params, ctx->id("LUT0_INITVAL"));
            int lut1_init = int_or_default(ci->params, ctx->id("LUT1_INITVAL"));
            cc.tiles[tname].add_word(slice + ".K0.INIT", int_to_bitvector(lut0_init, 16));
            cc.tiles[tname].add_word(slice + ".K1.INIT", int_to_bitvector(lut1_init, 16));
            cc.tiles[tname].add_enum(slice + ".MODE", str_or_default(ci->params, ctx->id("MODE"), "LOGIC"));
            cc.tiles[tname].add_enum(slice + ".GSR", str_or_default(ci->params, ctx->id("GSR"), "ENABLED"));
            cc.tiles[tname].add_enum(slice + ".REG0.SD", str_or_default(ci->params, ctx->id("REG0_SD"), "0"));
            cc.tiles[tname].add_enum(slice + ".REG1.SD", str_or_default(ci->params, ctx->id("REG1_SD"), "0"));
            cc.tiles[tname].add_enum(slice + ".REG0.REGSET",
                                     str_or_default(ci->params, ctx->id("REG0_REGSET"), "RESET"));
            cc.tiles[tname].add_enum(slice + ".REG1.REGSET",
                                     str_or_default(ci->params, ctx->id("REG1_REGSET"), "RESET"));
            cc.tiles[tname].add_enum(slice + ".CEMUX", str_or_default(ci->params, ctx->id("CEMUX"), "1"));
            // TODO: CLKMUX, CEMUX, carry
        } else if (ci->type == ctx->id("TRELLIS_IO")) {
            // TODO: IO config
        } else {
            NPNR_ASSERT_FALSE("unsupported cell type");
        }
    }

    // Configure chip
    Trellis::Chip cfg_chip = cc.to_chip();
    if (!bitstream_file.empty()) {
        Trellis::Bitstream::serialise_chip(cfg_chip).write_bit_py(bitstream_file);
    }
    if (!text_config_file.empty()) {
        std::ofstream out_config(text_config_file);
        out_config << cc.to_string();
    }
}

NEXTPNR_NAMESPACE_END
