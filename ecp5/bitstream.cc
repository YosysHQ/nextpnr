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

#define fmt_str(x) (static_cast<const std::ostringstream &>(std::ostringstream() << x).str())

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

// Get the PIO tile corresponding to a PIO bel
static std::string get_pio_tile(Context *ctx, Trellis::Chip &chip, BelId bel)
{
    static const std::set<std::string> pioabcd_l = {"PICL1", "PICL1_DQS0", "PICL1_DQS3"};
    static const std::set<std::string> pioabcd_r = {"PICR1", "PICR1_DQS0", "PICR1_DQS3"};
    static const std::set<std::string> pioa_b = {"PICB0", "EFB0_PICB0", "EFB2_PICB0"};
    static const std::set<std::string> piob_b = {"PICB1", "EFB1_PICB1", "EFB3_PICB1"};

    std::string pio_name = ctx->locInfo(bel)->bel_data[bel.index].name.get();
    if (bel.location.y == 0) {
        if (pio_name == "PIOA") {
            return chip.get_tile_by_position_and_type(0, bel.location.x, "PIOT0");
        } else if (pio_name == "PIOB") {
            return chip.get_tile_by_position_and_type(0, bel.location.x + 1, "PIOT1");
        } else {
            NPNR_ASSERT_FALSE("bad PIO location");
        }
    } else if (bel.location.y == ctx->chip_info->height - 1) {
        if (pio_name == "PIOA") {
            return chip.get_tile_by_position_and_type(bel.location.y, bel.location.x, pioa_b);
        } else if (pio_name == "PIOB") {
            return chip.get_tile_by_position_and_type(bel.location.y, bel.location.x + 1, piob_b);
        } else {
            NPNR_ASSERT_FALSE("bad PIO location");
        }
    } else if (bel.location.x == 0) {
        return chip.get_tile_by_position_and_type(bel.location.y + 1, bel.location.x, pioabcd_l);
    } else if (bel.location.x == ctx->chip_info->width - 1) {
        return chip.get_tile_by_position_and_type(bel.location.y + 1, bel.location.x, pioabcd_r);
    } else {
        NPNR_ASSERT_FALSE("bad PIO location");
    }
}

// Get the PIC tile corresponding to a PIO bel
static std::string get_pic_tile(Context *ctx, Trellis::Chip &chip, BelId bel)
{
    static const std::set<std::string> picab_l = {"PICL0", "PICL0_DQS2"};
    static const std::set<std::string> piccd_l = {"PICL2", "PICL2_DQS1", "MIB_CIB_LR"};
    static const std::set<std::string> picab_r = {"PICR0", "PICR0_DQS2"};
    static const std::set<std::string> piccd_r = {"PICR2", "PICR2_DQS1", "MIB_CIB_LR_A"};

    static const std::set<std::string> pica_b = {"PICB0", "EFB0_PICB0", "EFB2_PICB0"};
    static const std::set<std::string> picb_b = {"PICB1", "EFB1_PICB1", "EFB3_PICB1"};

    std::string pio_name = ctx->locInfo(bel)->bel_data[bel.index].name.get();
    if (bel.location.y == 0) {
        if (pio_name == "PIOA") {
            return chip.get_tile_by_position_and_type(1, bel.location.x, "PICT0");
        } else if (pio_name == "PIOB") {
            return chip.get_tile_by_position_and_type(1, bel.location.x + 1, "PICT1");
        } else {
            NPNR_ASSERT_FALSE("bad PIO location");
        }
    } else if (bel.location.y == ctx->chip_info->height - 1) {
        if (pio_name == "PIOA") {
            return chip.get_tile_by_position_and_type(bel.location.y, bel.location.x, pica_b);
        } else if (pio_name == "PIOB") {
            return chip.get_tile_by_position_and_type(bel.location.y, bel.location.x + 1, picb_b);
        } else {
            NPNR_ASSERT_FALSE("bad PIO location");
        }
    } else if (bel.location.x == 0) {
        if (pio_name == "PIOA" || pio_name == "PIOB") {
            return chip.get_tile_by_position_and_type(bel.location.y, bel.location.x, picab_l);
        } else if (pio_name == "PIOC" || pio_name == "PIOD") {
            return chip.get_tile_by_position_and_type(bel.location.y + 2, bel.location.x, piccd_l);
        } else {
            NPNR_ASSERT_FALSE("bad PIO location");
        }
    } else if (bel.location.x == ctx->chip_info->width - 1) {
        if (pio_name == "PIOA" || pio_name == "PIOB") {
            return chip.get_tile_by_position_and_type(bel.location.y, bel.location.x, picab_r);
        } else if (pio_name == "PIOC" || pio_name == "PIOD") {
            return chip.get_tile_by_position_and_type(bel.location.y + 2, bel.location.x, piccd_r);
        } else {
            NPNR_ASSERT_FALSE("bad PIO location");
        }
    } else {
        NPNR_ASSERT_FALSE("bad PIO location");
    }
}

void write_bitstream(Context *ctx, std::string base_config_file, std::string text_config_file,
                     std::string bitstream_file)
{
    Trellis::Chip empty_chip(ctx->getChipName());
    Trellis::ChipConfig cc;

    std::set<std::string> cib_tiles = {"CIB", "CIB_LR", "CIB_LR_S", "CIB_EFB0", "CIB_EFB1"};

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
                std::string tile = empty_chip.get_tile_by_position_and_type(pip.location.y, pip.location.x,
                                                                            ctx->getPipTiletype(pip));
                std::string source = get_trellis_wirename(ctx, pip.location, ctx->getPipSrcWire(pip));
                std::string sink = get_trellis_wirename(ctx, pip.location, ctx->getPipDstWire(pip));
                cc.tiles[tile].add_arc(sink, source);
            }
        }
    }

    // Set all bankref tiles to 3.3V (TODO)
    for (const auto &tile : empty_chip.tiles) {
        std::string type = tile.second->info.type;
        if (type.find("BANKREF") != std::string::npos && type != "BANKREF8") {
            cc.tiles[tile.first].add_enum("BANK.VCCIO", "3V3");
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
            std::string tname = empty_chip.get_tile_by_position_and_type(bel.location.y, bel.location.x, "PLC2");
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
            IdString lsrnet;
            if (ci->ports.find(ctx->id("LSR")) != ci->ports.end() && ci->ports.at(ctx->id("LSR")).net != nullptr)
                lsrnet = ci->ports.at(ctx->id("LSR")).net->name;
            if (ctx->getBoundWireNet(ctx->getWireByName(
                        ctx->id(fmt_str("X" << bel.location.x << "/Y" << bel.location.y << "/LSR0")))) == lsrnet) {
                cc.tiles[tname].add_enum("LSR0.SRMODE", str_or_default(ci->params, ctx->id("SRMODE"), "LSR_OVER_CE"));
                cc.tiles[tname].add_enum("LSR0.LSRMUX", str_or_default(ci->params, ctx->id("LSRMUX"), "LSR"));
            } else if (ctx->getBoundWireNet(ctx->getWireByName(ctx->id(
                               fmt_str("X" << bel.location.x << "/Y" << bel.location.y << "/LSR1")))) == lsrnet) {
                cc.tiles[tname].add_enum("LSR1.SRMODE", str_or_default(ci->params, ctx->id("SRMODE"), "LSR_OVER_CE"));
                cc.tiles[tname].add_enum("LSR1.LSRMUX", str_or_default(ci->params, ctx->id("LSRMUX"), "LSR"));
            }
            // TODO: CLKMUX, CEMUX, carry
        } else if (ci->type == ctx->id("TRELLIS_IO")) {
            std::string pio = ctx->locInfo(bel)->bel_data[bel.index].name.get();
            std::string iotype = str_or_default(ci->attrs, ctx->id("IO_TYPE"), "LVCMOS33");
            std::string dir = str_or_default(ci->params, ctx->id("DIR"), "INPUT");
            std::string pio_tile = get_pio_tile(ctx, empty_chip, bel);
            std::string pic_tile = get_pic_tile(ctx, empty_chip, bel);
            cc.tiles[pio_tile].add_enum(pio + ".BASE_TYPE", dir + "_" + iotype);
            cc.tiles[pic_tile].add_enum(pio + ".BASE_TYPE", dir + "_" + iotype);
            if (dir != "INPUT" &&
                (ci->ports.find(ctx->id("T")) == ci->ports.end() || ci->ports.at(ctx->id("T")).net == nullptr)) {
                // Tie tristate low if unconnected for outputs or bidir
                std::string jpt = fmt_str("X" << bel.location.x << "/Y" << bel.location.y << "/JPADDT" << pio.back());
                WireId jpt_wire = ctx->getWireByName(ctx->id(jpt));
                PipId jpt_pip = *ctx->getPipsUphill(jpt_wire).begin();
                WireId cib_wire = ctx->getPipSrcWire(jpt_pip);
                std::string cib_tile =
                        empty_chip.get_tile_by_position_and_type(cib_wire.location.y, cib_wire.location.x, cib_tiles);
                std::string cib_wirename = ctx->locInfo(cib_wire)->wire_data[cib_wire.index].name.get();
                cc.tiles[cib_tile].add_enum("CIB." + cib_wirename + "MUX", "0");
            }
            if (dir == "INPUT") {
                cc.tiles[pio_tile].add_enum(pio + ".HYSTERESIS", "ON");
            }
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
