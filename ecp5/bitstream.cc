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

#include <boost/algorithm/string/predicate.hpp>
#include <fstream>
#include <iomanip>
#include <queue>
#include <regex>
#include <streambuf>
#include "config.h"
#include "log.h"
#include "pio.h"
#include "util.h"

#define fmt_str(x) (static_cast<const std::ostringstream &>(std::ostringstream() << x).str())

NEXTPNR_NAMESPACE_BEGIN

namespace BaseConfigs {
void config_empty_lfe5u_25f(ChipConfig &cc);
void config_empty_lfe5u_45f(ChipConfig &cc);
void config_empty_lfe5u_85f(ChipConfig &cc);
void config_empty_lfe5um_25f(ChipConfig &cc);
void config_empty_lfe5um_45f(ChipConfig &cc);
void config_empty_lfe5um_85f(ChipConfig &cc);
void config_empty_lfe5um5g_25f(ChipConfig &cc);
void config_empty_lfe5um5g_45f(ChipConfig &cc);
void config_empty_lfe5um5g_85f(ChipConfig &cc);
} // namespace BaseConfigs

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
        rel_prefix += "N" + std::to_string(loc.y - wire.location.y);
    if (wire.location.y > loc.y)
        rel_prefix += "S" + std::to_string(wire.location.y - loc.y);
    if (wire.location.x > loc.x)
        rel_prefix += "E" + std::to_string(wire.location.x - loc.x);
    if (wire.location.x < loc.x)
        rel_prefix += "W" + std::to_string(loc.x - wire.location.x);
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

static std::vector<bool> str_to_bitvector(std::string str, int size)
{
    std::vector<bool> bv;
    bv.resize(size, 0);
    if (str.substr(0, 2) != "0b")
        log_error("error parsing value '%s', expected 0b prefix\n", str.c_str());
    for (int i = 0; i < int(str.size()) - 2; i++) {
        char c = str.at((str.size() - i) - 1);
        NPNR_ASSERT(c == '0' || c == '1');
        bv.at(i) = (c == '1');
    }
    return bv;
}

// Tie a wire using the CIB ties
static void tie_cib_signal(Context *ctx, ChipConfig &cc, WireId wire, bool value)
{
    static const std::regex cib_re("J([A-D]|CE|LSR|CLK)[0-7]");
    std::queue<WireId> signals;
    signals.push(wire);
    WireId cibsig;
    std::string basename;
    while (true) {
        NPNR_ASSERT(!signals.empty());
        NPNR_ASSERT(signals.size() < 100);
        cibsig = signals.front();
        basename = ctx->getWireBasename(cibsig).str(ctx);
        signals.pop();
        if (std::regex_match(basename, cib_re))
            break;
        for (auto pip : ctx->getPipsUphill(cibsig))
            signals.push(ctx->getPipSrcWire(pip));
    }

    bool out_value = value;
    if (basename.substr(0, 3) == "JCE")
        NPNR_ASSERT(value);
    if (basename.substr(0, 4) == "JCLK" || basename.substr(0, 4) == "JLSR") {
        NPNR_ASSERT(value);
        out_value = 0;
    }

    for (const auto &tile : ctx->getTilesAtLocation(cibsig.location.y, cibsig.location.x)) {
        if (tile.second.substr(0, 3) == "CIB" || tile.second.substr(0, 4) == "VCIB") {

            cc.tiles[tile.first].add_enum("CIB." + basename + "MUX", out_value ? "1" : "0");
            return;
        }
    }
    NPNR_ASSERT_FALSE("CIB tile not found at location");
}

inline int chtohex(char c)
{
    static const std::string hex = "0123456789ABCDEF";
    return hex.find(c);
}

std::vector<bool> parse_init_str(const std::string &str, int length)
{
    // Parse a string that may be binary or hex
    std::vector<bool> result;
    result.resize(length, false);
    if (str.substr(0, 2) == "0x") {
        // Lattice style hex string
        if (int(str.length()) > (2 + ((length + 3) / 4)))
            log_error("hex string value too long, expected up to %d chars and found %d.\n", (2 + ((length + 3) / 4)),
                      int(str.length()));
        for (int i = 0; i < int(str.length()) - 2; i++) {
            char c = str.at((str.size() - i) - 1);
            int nibble = chtohex(c);
            result.at(i * 4) = nibble & 0x1;
            if (i * 4 + 1 < length)
                result.at(i * 4 + 1) = nibble & 0x2;
            if (i * 4 + 2 < length)
                result.at(i * 4 + 2) = nibble & 0x4;
            if (i * 4 + 3 < length)
                result.at(i * 4 + 3) = nibble & 0x8;
        }
    } else {
        // Yosys style binary string
        if (int(str.length()) > length)
            log_error("hex string value too long, expected up to %d bits and found %d.\n", length, int(str.length()));
        for (int i = 0; i < int(str.length()); i++) {
            char c = str.at((str.size() - i) - 1);
            NPNR_ASSERT(c == '0' || c == '1' || c == 'X' || c == 'x');
            result.at(i) = (c == '1');
        }
    }
    return result;
}

inline uint16_t bit_reverse(uint16_t x, int size)
{
    uint16_t y = 0;
    for (int i = 0; i < size; i++)
        if (x & (1 << i))
            y |= (1 << ((size - 1) - i));
    return y;
}

// Get the PIO tile corresponding to a PIO bel
static std::string get_pio_tile(Context *ctx, BelId bel)
{
    static const std::set<std::string> pioabcd_l = {"PICL1", "PICL1_DQS0", "PICL1_DQS3"};
    static const std::set<std::string> pioabcd_r = {"PICR1", "PICR1_DQS0", "PICR1_DQS3"};
    static const std::set<std::string> pioa_b = {"PICB0", "EFB0_PICB0", "EFB2_PICB0"};
    static const std::set<std::string> piob_b = {"PICB1", "EFB1_PICB1", "EFB3_PICB1"};

    std::string pio_name = ctx->locInfo(bel)->bel_data[bel.index].name.get();
    if (bel.location.y == 0) {
        if (pio_name == "PIOA") {
            return ctx->getTileByTypeAndLocation(0, bel.location.x, "PIOT0");
        } else if (pio_name == "PIOB") {
            return ctx->getTileByTypeAndLocation(0, bel.location.x + 1, "PIOT1");
        } else {
            NPNR_ASSERT_FALSE("bad PIO location");
        }
    } else if (bel.location.y == ctx->chip_info->height - 1) {
        if (pio_name == "PIOA") {
            return ctx->getTileByTypeAndLocation(bel.location.y, bel.location.x, pioa_b);
        } else if (pio_name == "PIOB") {
            return ctx->getTileByTypeAndLocation(bel.location.y, bel.location.x + 1, piob_b);
        } else {
            NPNR_ASSERT_FALSE("bad PIO location");
        }
    } else if (bel.location.x == 0) {
        return ctx->getTileByTypeAndLocation(bel.location.y + 1, bel.location.x, pioabcd_l);
    } else if (bel.location.x == ctx->chip_info->width - 1) {
        return ctx->getTileByTypeAndLocation(bel.location.y + 1, bel.location.x, pioabcd_r);
    } else {
        NPNR_ASSERT_FALSE("bad PIO location");
    }
}

// Get the PIC tile corresponding to a PIO bel
static std::string get_pic_tile(Context *ctx, BelId bel)
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
            return ctx->getTileByTypeAndLocation(1, bel.location.x, "PICT0");
        } else if (pio_name == "PIOB") {
            return ctx->getTileByTypeAndLocation(1, bel.location.x + 1, "PICT1");
        } else {
            NPNR_ASSERT_FALSE("bad PIO location");
        }
    } else if (bel.location.y == ctx->chip_info->height - 1) {
        if (pio_name == "PIOA") {
            return ctx->getTileByTypeAndLocation(bel.location.y, bel.location.x, pica_b);
        } else if (pio_name == "PIOB") {
            return ctx->getTileByTypeAndLocation(bel.location.y, bel.location.x + 1, picb_b);
        } else {
            NPNR_ASSERT_FALSE("bad PIO location");
        }
    } else if (bel.location.x == 0) {
        if (pio_name == "PIOA" || pio_name == "PIOB") {
            return ctx->getTileByTypeAndLocation(bel.location.y, bel.location.x, picab_l);
        } else if (pio_name == "PIOC" || pio_name == "PIOD") {
            return ctx->getTileByTypeAndLocation(bel.location.y + 2, bel.location.x, piccd_l);
        } else {
            NPNR_ASSERT_FALSE("bad PIO location");
        }
    } else if (bel.location.x == ctx->chip_info->width - 1) {
        if (pio_name == "PIOA" || pio_name == "PIOB") {
            return ctx->getTileByTypeAndLocation(bel.location.y, bel.location.x, picab_r);
        } else if (pio_name == "PIOC" || pio_name == "PIOD") {
            return ctx->getTileByTypeAndLocation(bel.location.y + 2, bel.location.x, piccd_r);
        } else {
            NPNR_ASSERT_FALSE("bad PIO location");
        }
    } else {
        NPNR_ASSERT_FALSE("bad PIO location");
    }
}

// Get the list of tiles corresponding to a blockram
std::vector<std::string> get_bram_tiles(Context *ctx, BelId bel)
{
    std::vector<std::string> tiles;
    Loc loc = ctx->getBelLocation(bel);

    static const std::set<std::string> ebr0 = {"MIB_EBR0", "EBR_CMUX_UR", "EBR_CMUX_LR", "EBR_CMUX_LR_25K"};
    static const std::set<std::string> ebr8 = {"MIB_EBR8",      "EBR_SPINE_UL1",   "EBR_SPINE_UR1", "EBR_SPINE_LL1",
                                               "EBR_CMUX_UL",   "EBR_SPINE_LL0",   "EBR_CMUX_LL",   "EBR_SPINE_LR0",
                                               "EBR_SPINE_LR1", "EBR_CMUX_LL_25K", "EBR_SPINE_UL2", "EBR_SPINE_UL0",
                                               "EBR_SPINE_UR2", "EBR_SPINE_LL2",   "EBR_SPINE_LR2", "EBR_SPINE_UR0"};

    switch (loc.z) {
    case 0:
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x, ebr0));
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 1, "MIB_EBR1"));
        break;
    case 1:
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x, "MIB_EBR2"));
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 1, "MIB_EBR3"));
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 2, "MIB_EBR4"));
        break;
    case 2:
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x, "MIB_EBR4"));
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 1, "MIB_EBR5"));
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 2, "MIB_EBR6"));
        break;
    case 3:
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x, "MIB_EBR6"));
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 1, "MIB_EBR7"));
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 2, ebr8));
        break;
    default:
        NPNR_ASSERT_FALSE("bad EBR z loc");
    }
    return tiles;
}

// Get the list of tiles corresponding to a DSP
std::vector<std::string> get_dsp_tiles(Context *ctx, BelId bel)
{
    std::vector<std::string> tiles;
    Loc loc = ctx->getBelLocation(bel);

    static const std::set<std::string> dsp8 = {"MIB_DSP8", "DSP_SPINE_UL0", "DSP_SPINE_UR0", "DSP_SPINE_UR1"};
    if (ctx->getBelType(bel) == id_MULT18X18D) {
        switch (loc.z) {
        case 0:
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x, "MIB_DSP0"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x, "MIB2_DSP0"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 1, "MIB_DSP1"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 1, "MIB2_DSP1"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 2, "MIB_DSP2"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 2, "MIB2_DSP2"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 3, "MIB_DSP3"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 3, "MIB2_DSP3"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 4, "MIB_DSP4"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 4, "MIB2_DSP4"));
            break;
        case 1:
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x - 1, "MIB_DSP0"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x - 1, "MIB2_DSP0"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x, "MIB_DSP1"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x, "MIB2_DSP1"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 1, "MIB_DSP2"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 1, "MIB2_DSP2"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 2, "MIB_DSP3"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 2, "MIB2_DSP3"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 3, "MIB_DSP4"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 3, "MIB2_DSP4"));
            break;
        case 4:
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x, "MIB_DSP4"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x, "MIB2_DSP4"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 1, "MIB_DSP5"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 1, "MIB2_DSP5"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 2, "MIB_DSP6"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 2, "MIB2_DSP6"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 3, "MIB_DSP7"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 3, "MIB2_DSP7"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 4, dsp8));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 4, "MIB2_DSP8"));
            break;
        case 5:
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x - 1, "MIB_DSP4"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x - 1, "MIB2_DSP4"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x, "MIB_DSP5"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x, "MIB2_DSP5"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 1, "MIB_DSP6"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 1, "MIB2_DSP6"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 2, "MIB_DSP7"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 2, "MIB2_DSP7"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 3, dsp8));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 3, "MIB2_DSP8"));
            break;
        default:
            NPNR_ASSERT_FALSE("bad MULT z loc");
        }
    } else if (ctx->getBelType(bel) == id_ALU54B) {
        switch (loc.z) {
        case 3:
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x - 3, "MIB_DSP0"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x - 3, "MIB2_DSP0"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x - 2, "MIB_DSP1"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x - 2, "MIB2_DSP1"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x - 1, "MIB_DSP2"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x - 1, "MIB2_DSP2"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x, "MIB_DSP3"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x, "MIB2_DSP3"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 1, "MIB_DSP4"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 1, "MIB2_DSP4"));
            break;
        case 7:
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x - 3, "MIB_DSP4"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x - 3, "MIB2_DSP4"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x - 2, "MIB_DSP5"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x - 2, "MIB2_DSP5"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x - 1, "MIB_DSP6"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x - 1, "MIB2_DSP6"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x, "MIB_DSP7"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x, "MIB2_DSP7"));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 1, dsp8));
            tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 1, "MIB2_DSP8"));
            break;
        default:
            NPNR_ASSERT_FALSE("bad ALU z loc");
        }
    }
    return tiles;
}

// Get the list of tiles corresponding to a DCU
std::vector<std::string> get_dcu_tiles(Context *ctx, BelId bel)
{
    std::vector<std::string> tiles;
    Loc loc = ctx->getBelLocation(bel);
    for (int i = 0; i < 9; i++)
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + i, "DCU" + std::to_string(i)));
    return tiles;
}

// Get the list of tiles corresponding to a PLL
std::vector<std::string> get_pll_tiles(Context *ctx, BelId bel)
{
    std::string name = ctx->locInfo(bel)->bel_data[bel.index].name.get();
    std::vector<std::string> tiles;
    Loc loc = ctx->getBelLocation(bel);
    static const std::set<std::string> pll1_lr = {"PLL1_LR", "BANKREF4"};

    if (name == "EHXPLL_UL") {
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x - 1, "PLL0_UL"));
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y + 1, loc.x - 1, "PLL1_UL"));
    } else if (name == "EHXPLL_LL") {
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y + 1, loc.x, "PLL0_LL"));
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y + 1, loc.x + 1, "BANKREF8"));
    } else if (name == "EHXPLL_LR") {
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y + 1, loc.x, "PLL0_LR"));
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y + 1, loc.x - 1, pll1_lr));
    } else if (name == "EHXPLL_UR") {
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x + 1, "PLL0_UR"));
        tiles.push_back(ctx->getTileByTypeAndLocation(loc.y + 1, loc.x + 1, "PLL1_UR"));
    } else {
        NPNR_ASSERT_FALSE_STR("bad PLL loc " + name);
    }
    return tiles;
}

void fix_tile_names(Context *ctx, ChipConfig &cc)
{
    // Remove the V prefix/suffix on certain tiles if device is a SERDES variant
    if (ctx->args.type == ArchArgs::LFE5U_25F || ctx->args.type == ArchArgs::LFE5U_45F ||
        ctx->args.type == ArchArgs::LFE5U_85F) {
        std::map<std::string, std::string> tiletype_xform;
        for (const auto &tile : cc.tiles) {
            std::string newname = tile.first;
            auto cibdcu = tile.first.find("CIB_DCU");
            if (cibdcu != std::string::npos) {
                // Add the V
                if (newname.at(cibdcu - 1) != 'V')
                    newname.insert(cibdcu, 1, 'V');
                tiletype_xform[tile.first] = newname;
            } else if (boost::ends_with(tile.first, "BMID_0H")) {
                newname.back() = 'V';
                tiletype_xform[tile.first] = newname;
            } else if (boost::ends_with(tile.first, "BMID_2")) {
                newname.push_back('V');
                tiletype_xform[tile.first] = newname;
            }
        }
        // Apply the name changes
        for (auto xform : tiletype_xform) {
            cc.tiles[xform.second] = cc.tiles.at(xform.first);
            cc.tiles.erase(xform.first);
        }
    }
}

void tieoff_dsp_ports(Context *ctx, ChipConfig &cc, CellInfo *ci)
{
    for (auto port : ci->ports) {
        if (port.second.net == nullptr && port.second.type == PORT_IN) {
            if (port.first.str(ctx).substr(0, 3) == "CLK" || port.first.str(ctx).substr(0, 2) == "CE" ||
                port.first.str(ctx).substr(0, 3) == "RST" || port.first.str(ctx).substr(0, 3) == "SRO" ||
                port.first.str(ctx).substr(0, 3) == "SRI" || port.first.str(ctx).substr(0, 2) == "RO" ||
                port.first.str(ctx).substr(0, 2) == "MA" || port.first.str(ctx).substr(0, 2) == "MB" ||
                port.first.str(ctx).substr(0, 3) == "CFB" || port.first.str(ctx).substr(0, 3) == "CIN" ||
                port.first.str(ctx).substr(0, 6) == "SOURCE" || port.first.str(ctx).substr(0, 6) == "SIGNED" ||
                port.first.str(ctx).substr(0, 2) == "OP")
                continue;
            bool value = bool_or_default(ci->params, ctx->id(port.first.str(ctx) + "MUX"), false);
            tie_cib_signal(ctx, cc, ctx->getBelPinWire(ci->bel, port.first), value);
        }
    }
}

void tieoff_dcu_ports(Context *ctx, ChipConfig &cc, CellInfo *ci)
{
    for (auto port : ci->ports) {
        if (port.second.net == nullptr && port.second.type == PORT_IN) {
            if (port.first.str(ctx).find("CLK") != std::string::npos ||
                port.first.str(ctx).find("HDIN") != std::string::npos ||
                port.first.str(ctx).find("HDOUT") != std::string::npos)
                continue;
            bool value = bool_or_default(ci->params, ctx->id(port.first.str(ctx) + "MUX"), false);
            tie_cib_signal(ctx, cc, ctx->getBelPinWire(ci->bel, port.first), value);
        }
    }
}

static void set_pip(Context *ctx, ChipConfig &cc, PipId pip)
{
    std::string tile = ctx->getPipTilename(pip);
    std::string source = get_trellis_wirename(ctx, pip.location, ctx->getPipSrcWire(pip));
    std::string sink = get_trellis_wirename(ctx, pip.location, ctx->getPipDstWire(pip));
    cc.tiles[tile].add_arc(sink, source);
}

static std::vector<bool> parse_config_str(std::string str, int length)
{
    // For DCU config which might be bin, hex or dec using prefices accordingly
    std::string base = str.substr(0, 2);
    std::vector<bool> word;
    word.resize(length, false);
    if (base == "0b") {
        for (int i = 0; i < int(str.length()) - 2; i++) {
            char c = str.at((str.size() - 1) - i);
            NPNR_ASSERT(c == '0' || c == '1');
            word.at(i) = (c == '1');
        }
    } else if (base == "0x") {
        for (int i = 0; i < int(str.length()) - 2; i++) {
            char c = str.at((str.size() - i) - 1);
            int nibble = chtohex(c);
            word.at(i * 4) = nibble & 0x1;
            if (i * 4 + 1 < length)
                word.at(i * 4 + 1) = nibble & 0x2;
            if (i * 4 + 2 < length)
                word.at(i * 4 + 2) = nibble & 0x4;
            if (i * 4 + 3 < length)
                word.at(i * 4 + 3) = nibble & 0x8;
        }
    } else if (base == "0d") {
        NPNR_ASSERT(length < 64);
        unsigned long long value = std::stoull(str.substr(2));
        for (int i = 0; i < length; i++)
            if (value & (1 << i))
                word.at(i) = true;
    } else {
        NPNR_ASSERT(length < 64);
        unsigned long long value = std::stoull(str);
        for (int i = 0; i < length; i++)
            if (value & (1 << i))
                word.at(i) = true;
    }
    return word;
}

void write_bitstream(Context *ctx, std::string base_config_file, std::string text_config_file)
{
    ChipConfig cc;

    std::set<std::string> cib_tiles = {"CIB", "CIB_LR", "CIB_LR_S", "CIB_EFB0", "CIB_EFB1"};

    if (!base_config_file.empty()) {
        std::ifstream config_file(base_config_file);
        if (!config_file) {
            log_error("failed to open base config file '%s'\n", base_config_file.c_str());
        }
        config_file >> cc;
    } else {
        switch (ctx->args.type) {
        case ArchArgs::LFE5U_25F:
            BaseConfigs::config_empty_lfe5u_25f(cc);
            break;
        case ArchArgs::LFE5U_45F:
            BaseConfigs::config_empty_lfe5u_45f(cc);
            break;
        case ArchArgs::LFE5U_85F:
            BaseConfigs::config_empty_lfe5u_85f(cc);
            break;
        case ArchArgs::LFE5UM_25F:
            BaseConfigs::config_empty_lfe5um_25f(cc);
            break;
        case ArchArgs::LFE5UM_45F:
            BaseConfigs::config_empty_lfe5um_45f(cc);
            break;
        case ArchArgs::LFE5UM_85F:
            BaseConfigs::config_empty_lfe5um_85f(cc);
            break;
        case ArchArgs::LFE5UM5G_25F:
            BaseConfigs::config_empty_lfe5um5g_25f(cc);
            break;
        case ArchArgs::LFE5UM5G_45F:
            BaseConfigs::config_empty_lfe5um5g_45f(cc);
            break;
        case ArchArgs::LFE5UM5G_85F:
            BaseConfigs::config_empty_lfe5um5g_85f(cc);
            break;
        default:
            NPNR_ASSERT_FALSE("Unsupported device type");
        }
    }

    // Clear out DCU tieoffs in base config if DCU used
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type == id_DCUA) {
            Loc loc = ctx->getBelLocation(ci->bel);
            for (int i = 0; i < 12; i++) {
                auto tiles = ctx->getTilesAtLocation(loc.y - 1, loc.x + i);
                for (const auto &tile : tiles) {
                    auto cc_tile = cc.tiles.find(tile.first);
                    if (cc_tile != cc.tiles.end()) {
                        cc_tile->second.cenums.clear();
                        cc_tile->second.cunknowns.clear();
                    }
                }
            }
        }
    }
    // Add all set, configurable pips to the config
    for (auto pip : ctx->getPips()) {
        if (ctx->getBoundPipNet(pip) != nullptr) {
            if (ctx->getPipClass(pip) == 0) { // ignore fixed pips
                std::string source = get_trellis_wirename(ctx, pip.location, ctx->getPipSrcWire(pip));
                if (source.find("CLKI_PLL") != std::string::npos) {
                    // Special case - must set pip in all relevant tiles
                    for (auto equiv_pip : ctx->getPipsUphill(ctx->getPipDstWire(pip))) {
                        if (ctx->getPipSrcWire(equiv_pip) == ctx->getPipSrcWire(pip))
                            set_pip(ctx, cc, equiv_pip);
                    }
                } else {
                    set_pip(ctx, cc, pip);
                }
            }
        }
    }
    // Find bank voltages
    std::unordered_map<int, IOVoltage> bankVcc;
    std::unordered_map<int, bool> bankLvds, bankVref;

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->bel != BelId() && ci->type == ctx->id("TRELLIS_IO")) {
            int bank = ctx->getPioBelBank(ci->bel);
            std::string dir = str_or_default(ci->params, ctx->id("DIR"), "INPUT");
            std::string iotype = str_or_default(ci->attrs, ctx->id("IO_TYPE"), "LVCMOS33");

            if (dir != "INPUT" || is_referenced(ioType_from_str(iotype))) {
                IOVoltage vcc = get_vccio(ioType_from_str(iotype));
                if (bankVcc.find(bank) != bankVcc.end()) {
                    // TODO: strong and weak constraints
                    if (bankVcc[bank] != vcc) {
                        log_error("Error processing '%s': incompatible IO voltages %s and %s on bank %d.",
                                  cell.first.c_str(ctx), iovoltage_to_str(bankVcc[bank]).c_str(),
                                  iovoltage_to_str(vcc).c_str(), bank);
                    }
                } else {
                    bankVcc[bank] = vcc;
                }
            }

            if (iotype == "LVDS")
                bankLvds[bank] = true;
            if ((dir == "INPUT" || dir == "BIDIR") && is_referenced(ioType_from_str(iotype)))
                bankVref[bank] = true;
        }
    }

    // Set all bankref tiles to appropriate VccIO
    for (int y = 0; y < ctx->getGridDimY(); y++) {
        for (int x = 0; x < ctx->getGridDimX(); x++) {
            auto tiles = ctx->getTilesAtLocation(y, x);
            for (auto tile : tiles) {
                std::string type = tile.second;
                if (type.find("BANKREF") != std::string::npos && type != "BANKREF8") {
                    int bank = std::stoi(type.substr(7));
                    if (bankVcc.find(bank) != bankVcc.end()) {
                        if (bankVcc[bank] == IOVoltage::VCC_1V35)
                            cc.tiles[tile.first].add_enum("BANK.VCCIO", "1V2");
                        else
                            cc.tiles[tile.first].add_enum("BANK.VCCIO", iovoltage_to_str(bankVcc[bank]));
                    }
                    if (bankLvds[bank]) {
                        cc.tiles[tile.first].add_enum("BANK.DIFF_REF", "ON");
                        cc.tiles[tile.first].add_enum("BANK.LVDSO", "ON");
                    }
                    if (bankVref[bank]) {
                        cc.tiles[tile.first].add_enum("BANK.DIFF_REF", "ON");
                        cc.tiles[tile.first].add_enum("BANK.VREF", "ON");
                    }
                }
            }
        }
    }

    // Create dummy outputs used as Vref input buffer for banks where Vref is used
    for (auto bv : bankVref) {
        if (!bv.second)
            continue;
        BelId vrefIO = ctx->getPioByFunctionName(fmt_str("VREF1_" << bv.first));
        if (vrefIO == BelId())
            log_error("unable to find VREF input for bank %d\n", bv.first);
        if (!ctx->checkBelAvail(vrefIO)) {
            CellInfo *bound = ctx->getBoundBelCell(vrefIO);
            if (bound != nullptr)
                log_error("VREF pin %s of bank %d is occupied by IO '%s'\n", ctx->getBelPackagePin(vrefIO).c_str(),
                          bv.first, bound->name.c_str(ctx));
            else
                log_error("VREF pin %s of bank %d is unavailable\n", ctx->getBelPackagePin(vrefIO).c_str(), bv.first);
        }
        log_info("Using pin %s as VREF for bank %d\n", ctx->getBelPackagePin(vrefIO).c_str(), bv.first);
        std::string pio_tile = get_pio_tile(ctx, vrefIO);

        std::string iotype;
        switch (bankVcc[bv.first]) {
        case IOVoltage::VCC_1V2:
            iotype = "HSUL12";
            break;
        case IOVoltage::VCC_1V35:
            iotype = "SSTL18_I";
            break;
        case IOVoltage::VCC_1V5:
            iotype = "SSTL18_I";
            break;
        case IOVoltage::VCC_1V8:
            iotype = "SSTL18_I";
            break;
        default:
            log_error("Referenced inputs are not supported with bank VccIO of %s.\n",
                      iovoltage_to_str(bankVcc[bv.first]).c_str());
        }

        std::string pio = ctx->locInfo(vrefIO)->bel_data[vrefIO.index].name.get();
        cc.tiles[pio_tile].add_enum(pio + ".BASE_TYPE", "OUTPUT_" + iotype);
        cc.tiles[pio_tile].add_enum(pio + ".PULLMODE", "NONE");
    }

    // Configure slices
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->bel == BelId()) {
            log_warning("found unplaced cell '%s' during bitstream gen\n", ci->name.c_str(ctx));
        }
        BelId bel = ci->bel;
        if (ci->type == ctx->id("TRELLIS_SLICE")) {
            std::string tname = ctx->getTileByTypeAndLocation(bel.location.y, bel.location.x, "PLC2");
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

            if (ci->sliceInfo.using_dff) {
                NetInfo *lsrnet = nullptr;
                if (ci->ports.find(ctx->id("LSR")) != ci->ports.end() && ci->ports.at(ctx->id("LSR")).net != nullptr)
                    lsrnet = ci->ports.at(ctx->id("LSR")).net;
                if (ctx->getBoundWireNet(ctx->getWireByName(
                            ctx->id(fmt_str("X" << bel.location.x << "/Y" << bel.location.y << "/LSR0")))) == lsrnet) {
                    cc.tiles[tname].add_enum("LSR0.SRMODE",
                                             str_or_default(ci->params, ctx->id("SRMODE"), "LSR_OVER_CE"));
                    cc.tiles[tname].add_enum("LSR0.LSRMUX", str_or_default(ci->params, ctx->id("LSRMUX"), "LSR"));
                } else if (ctx->getBoundWireNet(ctx->getWireByName(ctx->id(
                                   fmt_str("X" << bel.location.x << "/Y" << bel.location.y << "/LSR1")))) == lsrnet) {
                    cc.tiles[tname].add_enum("LSR1.SRMODE",
                                             str_or_default(ci->params, ctx->id("SRMODE"), "LSR_OVER_CE"));
                    cc.tiles[tname].add_enum("LSR1.LSRMUX", str_or_default(ci->params, ctx->id("LSRMUX"), "LSR"));
                }

                NetInfo *clknet = nullptr;
                if (ci->ports.find(ctx->id("CLK")) != ci->ports.end() && ci->ports.at(ctx->id("CLK")).net != nullptr)
                    clknet = ci->ports.at(ctx->id("CLK")).net;
                if (ctx->getBoundWireNet(ctx->getWireByName(
                            ctx->id(fmt_str("X" << bel.location.x << "/Y" << bel.location.y << "/CLK0")))) == clknet) {
                    cc.tiles[tname].add_enum("CLK0.CLKMUX", str_or_default(ci->params, ctx->id("CLKMUX"), "CLK"));
                } else if (ctx->getBoundWireNet(ctx->getWireByName(ctx->id(
                                   fmt_str("X" << bel.location.x << "/Y" << bel.location.y << "/CLK1")))) == clknet) {
                    cc.tiles[tname].add_enum("CLK1.CLKMUX", str_or_default(ci->params, ctx->id("CLKMUX"), "CLK"));
                }
            }

            if (str_or_default(ci->params, ctx->id("MODE"), "LOGIC") == "CCU2") {
                cc.tiles[tname].add_enum(slice + ".CCU2.INJECT1_0",
                                         str_or_default(ci->params, ctx->id("INJECT1_0"), "YES"));
                cc.tiles[tname].add_enum(slice + ".CCU2.INJECT1_1",
                                         str_or_default(ci->params, ctx->id("INJECT1_1"), "YES"));
            } else {
                // Don't interfere with cascade mux wiring
                cc.tiles[tname].add_enum(slice + ".CCU2.INJECT1_0",
                                         str_or_default(ci->params, ctx->id("INJECT1_0"), "_NONE_"));
                cc.tiles[tname].add_enum(slice + ".CCU2.INJECT1_1",
                                         str_or_default(ci->params, ctx->id("INJECT1_1"), "_NONE_"));
            }

            if (str_or_default(ci->params, ctx->id("MODE"), "LOGIC") == "DPRAM" && slice == "SLICEA") {
                cc.tiles[tname].add_enum(slice + ".WREMUX", str_or_default(ci->params, ctx->id("WREMUX"), "WRE"));

                std::string wckmux = str_or_default(ci->params, ctx->id("WCKMUX"), "WCK");
                wckmux = (wckmux == "WCK") ? "CLK" : wckmux;
                cc.tiles[tname].add_enum("CLK1.CLKMUX", wckmux);
            }

            // Tie unused inputs high
            for (auto input : {id_A0, id_B0, id_C0, id_D0, id_A1, id_B1, id_C1, id_D1}) {
                if (ci->ports.find(input) == ci->ports.end() || ci->ports.at(input).net == nullptr) {
                    cc.tiles[tname].add_enum(slice + "." + input.str(ctx) + "MUX", "1");
                }
            }

            // TODO: CLKMUX
        } else if (ci->type == ctx->id("TRELLIS_IO")) {
            std::string pio = ctx->locInfo(bel)->bel_data[bel.index].name.get();
            std::string iotype = str_or_default(ci->attrs, ctx->id("IO_TYPE"), "LVCMOS33");
            std::string dir = str_or_default(ci->params, ctx->id("DIR"), "INPUT");
            std::string pio_tile = get_pio_tile(ctx, bel);
            std::string pic_tile = get_pic_tile(ctx, bel);
            cc.tiles[pio_tile].add_enum(pio + ".BASE_TYPE", dir + "_" + iotype);
            cc.tiles[pic_tile].add_enum(pio + ".BASE_TYPE", dir + "_" + iotype);
            if (is_differential(ioType_from_str(iotype))) {
                // Explicitly disable other pair
                std::string other;
                if (pio == "PIOA")
                    other = "PIOB";
                else if (pio == "PIOC")
                    other = "PIOD";
                else
                    log_error("cannot place differential IO at location %s\n", pio.c_str());
                // cc.tiles[pio_tile].add_enum(other + ".BASE_TYPE", "_NONE_");
                // cc.tiles[pic_tile].add_enum(other + ".BASE_TYPE", "_NONE_");
                cc.tiles[pio_tile].add_enum(other + ".PULLMODE", "NONE");
                cc.tiles[pio_tile].add_enum(pio + ".PULLMODE", "NONE");
            } else if (is_referenced(ioType_from_str(iotype))) {
                cc.tiles[pio_tile].add_enum(pio + ".PULLMODE", "NONE");
            }
            if (dir != "INPUT" &&
                (ci->ports.find(ctx->id("T")) == ci->ports.end() || ci->ports.at(ctx->id("T")).net == nullptr) &&
                (ci->ports.find(ctx->id("IOLTO")) == ci->ports.end() ||
                 ci->ports.at(ctx->id("IOLTO")).net == nullptr)) {
                // Tie tristate low if unconnected for outputs or bidir
                std::string jpt = fmt_str("X" << bel.location.x << "/Y" << bel.location.y << "/JPADDT" << pio.back());
                WireId jpt_wire = ctx->getWireByName(ctx->id(jpt));
                PipId jpt_pip = *ctx->getPipsUphill(jpt_wire).begin();
                WireId cib_wire = ctx->getPipSrcWire(jpt_pip);
                std::string cib_tile =
                        ctx->getTileByTypeAndLocation(cib_wire.location.y, cib_wire.location.x, cib_tiles);
                std::string cib_wirename = ctx->locInfo(cib_wire)->wire_data[cib_wire.index].name.get();
                cc.tiles[cib_tile].add_enum("CIB." + cib_wirename + "MUX", "0");
            }
            if (dir == "INPUT" && !is_differential(ioType_from_str(iotype)) &&
                !is_referenced(ioType_from_str(iotype))) {
                cc.tiles[pio_tile].add_enum(pio + ".HYSTERESIS", "ON");
            }
            if (ci->attrs.count(ctx->id("SLEWRATE")) && !is_referenced(ioType_from_str(iotype)))
                cc.tiles[pio_tile].add_enum(pio + ".SLEWRATE", str_or_default(ci->attrs, ctx->id("SLEWRATE"), "SLOW"));
            if (ci->attrs.count(ctx->id("PULLMODE")))
                cc.tiles[pio_tile].add_enum(pio + ".PULLMODE", str_or_default(ci->attrs, ctx->id("PULLMODE"), "NONE"));
            if (ci->attrs.count(ctx->id("DIFFRESISTOR")))
                cc.tiles[pio_tile].add_enum(pio + ".DIFFRESISTOR",
                                            str_or_default(ci->attrs, ctx->id("DIFFRESISTOR"), "OFF"));
            if (ci->attrs.count(ctx->id("TERMINATION"))) {
                auto vccio = get_vccio(ioType_from_str(iotype));
                switch (vccio) {
                case IOVoltage::VCC_1V8:
                    cc.tiles[pio_tile].add_enum(pio + ".TERMINATION_1V8",
                                                str_or_default(ci->attrs, ctx->id("TERMINATION"), "OFF"));
                    break;
                case IOVoltage::VCC_1V5:
                    cc.tiles[pio_tile].add_enum(pio + ".TERMINATION_1V5",
                                                str_or_default(ci->attrs, ctx->id("TERMINATION"), "OFF"));
                    break;
                case IOVoltage::VCC_1V35:
                    cc.tiles[pio_tile].add_enum(pio + ".TERMINATION_1V35",
                                                str_or_default(ci->attrs, ctx->id("TERMINATION"), "OFF"));
                    break;
                default:
                    log_error("TERMINATION is not supported with Vcc = %s (on PIO %s)\n",
                              iovoltage_to_str(vccio).c_str(), ci->name.c_str(ctx));
                }
            }
            std::string datamux_oddr = str_or_default(ci->params, ctx->id("DATAMUX_ODDR"), "PADDO");
            if (datamux_oddr != "PADDO")
                cc.tiles[pic_tile].add_enum(pio + ".DATAMUX_ODDR", datamux_oddr);
            std::string datamux_mddr = str_or_default(ci->params, ctx->id("DATAMUX_MDDR"), "PADDO");
            if (datamux_mddr != "PADDO")
                cc.tiles[pic_tile].add_enum(pio + ".DATAMUX_MDDR", datamux_mddr);
        } else if (ci->type == ctx->id("DCCA")) {
            // Nothing to do
        } else if (ci->type == ctx->id("DP16KD")) {
            TileGroup tg;
            Loc loc = ctx->getBelLocation(ci->bel);
            tg.tiles = get_bram_tiles(ctx, ci->bel);
            std::string ebr = "EBR" + std::to_string(loc.z);

            tg.config.add_enum(ebr + ".MODE", "DP16KD");

            auto csd_a = str_to_bitvector(str_or_default(ci->params, ctx->id("CSDECODE_A"), "0b000"), 3),
                 csd_b = str_to_bitvector(str_or_default(ci->params, ctx->id("CSDECODE_B"), "0b000"), 3);

            tg.config.add_enum(ebr + ".DP16KD.DATA_WIDTH_A", str_or_default(ci->params, ctx->id("DATA_WIDTH_A"), "18"));
            tg.config.add_enum(ebr + ".DP16KD.DATA_WIDTH_B", str_or_default(ci->params, ctx->id("DATA_WIDTH_B"), "18"));

            tg.config.add_enum(ebr + ".DP16KD.WRITEMODE_A",
                               str_or_default(ci->params, ctx->id("WRITEMODE_A"), "NORMAL"));
            tg.config.add_enum(ebr + ".DP16KD.WRITEMODE_B",
                               str_or_default(ci->params, ctx->id("WRITEMODE_B"), "NORMAL"));

            tg.config.add_enum(ebr + ".REGMODE_A", str_or_default(ci->params, ctx->id("REGMODE_A"), "NOREG"));
            tg.config.add_enum(ebr + ".REGMODE_B", str_or_default(ci->params, ctx->id("REGMODE_B"), "NOREG"));

            tg.config.add_enum(ebr + ".RESETMODE", str_or_default(ci->params, ctx->id("RESETMODE"), "SYNC"));
            tg.config.add_enum(ebr + ".ASYNC_RESET_RELEASE",
                               str_or_default(ci->params, ctx->id("ASYNC_RESET_RELEASE"), "SYNC"));
            tg.config.add_enum(ebr + ".GSR", str_or_default(ci->params, ctx->id("GSR"), "DISABLED"));

            tg.config.add_word(ebr + ".WID",
                               int_to_bitvector(bit_reverse(int_or_default(ci->attrs, ctx->id("WID"), 0), 9), 9));

            // Tie signals as appropriate
            for (auto port : ci->ports) {
                if (port.second.net == nullptr && port.second.type == PORT_IN) {
                    if (port.first == id_CLKA || port.first == id_CLKB || port.first == id_WEA ||
                        port.first == id_WEB || port.first == id_RSTA || port.first == id_RSTB) {
                        // CIB clock or LSR. Tie to "1" (also 0 in prjtrellis db?) in CIB
                        // If MUX doesn't exist, set to INV to emulate default 0
                        tie_cib_signal(ctx, cc, ctx->getBelPinWire(ci->bel, port.first), true);
                        if (!ci->params.count(ctx->id(port.first.str(ctx) + "MUX")))
                            ci->params[ctx->id(port.first.str(ctx) + "MUX")] = "INV";
                    } else if (port.first == id_CEA || port.first == id_CEB || port.first == id_OCEA ||
                               port.first == id_OCEB) {
                        // CIB CE. Tie to "1" in CIB
                        // If MUX doesn't exist, set to passthru to emulate default 1
                        tie_cib_signal(ctx, cc, ctx->getBelPinWire(ci->bel, port.first), true);
                        if (!ci->params.count(ctx->id(port.first.str(ctx) + "MUX")))
                            ci->params[ctx->id(port.first.str(ctx) + "MUX")] = port.first.str(ctx);
                    } else if (port.first == id_CSA0 || port.first == id_CSA1 || port.first == id_CSA2 ||
                               port.first == id_CSB0 || port.first == id_CSB1 || port.first == id_CSB2) {
                        // CIB CE. Tie to "1" in CIB.
                        // If MUX doesn't exist, set to INV to emulate default 0
                        tie_cib_signal(ctx, cc, ctx->getBelPinWire(ci->bel, port.first), true);
                        if (!ci->params.count(ctx->id(port.first.str(ctx) + "MUX")))
                            ci->params[ctx->id(port.first.str(ctx) + "MUX")] = "INV";
                    } else {
                        // CIB ABCD signal
                        // Tie signals low unless explicit MUX param specified
                        bool value = bool_or_default(ci->params, ctx->id(port.first.str(ctx) + "MUX"), false);
                        tie_cib_signal(ctx, cc, ctx->getBelPinWire(ci->bel, port.first), value);
                    }
                }
            }

            // Invert CSDECODE bits to emulate inversion muxes on CSA/CSB signals
            for (auto port : {std::make_pair("CSA", std::ref(csd_a)), std::make_pair("CSB", std::ref(csd_b))}) {
                for (int bit = 0; bit < 3; bit++) {
                    std::string sig = port.first + std::to_string(bit);
                    if (str_or_default(ci->params, ctx->id(sig + "MUX"), sig) == "INV")
                        port.second.at(bit) = !port.second.at(bit);
                }
            }

            tg.config.add_enum(ebr + ".CLKAMUX", str_or_default(ci->params, ctx->id("CLKAMUX"), "CLKA"));
            tg.config.add_enum(ebr + ".CLKBMUX", str_or_default(ci->params, ctx->id("CLKBMUX"), "CLKB"));

            tg.config.add_enum(ebr + ".RSTAMUX", str_or_default(ci->params, ctx->id("RSTAMUX"), "RSTA"));
            tg.config.add_enum(ebr + ".RSTBMUX", str_or_default(ci->params, ctx->id("RSTBMUX"), "RSTB"));
            tg.config.add_enum(ebr + ".WEAMUX", str_or_default(ci->params, ctx->id("WEAMUX"), "WEA"));
            tg.config.add_enum(ebr + ".WEBMUX", str_or_default(ci->params, ctx->id("WEBMUX"), "WEB"));

            tg.config.add_enum(ebr + ".CEAMUX", str_or_default(ci->params, ctx->id("CEAMUX"), "CEA"));
            tg.config.add_enum(ebr + ".CEBMUX", str_or_default(ci->params, ctx->id("CEBMUX"), "CEB"));
            tg.config.add_enum(ebr + ".OCEAMUX", str_or_default(ci->params, ctx->id("OCEAMUX"), "OCEA"));
            tg.config.add_enum(ebr + ".OCEBMUX", str_or_default(ci->params, ctx->id("OCEBMUX"), "OCEB"));

            tg.config.add_word(ebr + ".CSDECODE_A", csd_a);
            tg.config.add_word(ebr + ".CSDECODE_B", csd_b);

            std::vector<uint16_t> init_data;
            init_data.resize(2048, 0x0);
            // INIT_00 .. INIT_3F
            for (int i = 0; i <= 0x3F; i++) {
                IdString param = ctx->id("INITVAL_" +
                                         fmt_str(std::hex << std::uppercase << std::setw(2) << std::setfill('0') << i));
                auto value = parse_init_str(str_or_default(ci->params, param, "0"), 320);
                for (int j = 0; j < 16; j++) {
                    // INIT parameter consists of 16 18-bit words with 2-bit padding
                    int ofs = 20 * j;
                    for (int k = 0; k < 18; k++) {
                        if (value.at(ofs + k))
                            init_data.at(i * 32 + j * 2 + (k / 9)) |= (1 << (k % 9));
                    }
                }
            }
            int wid = int_or_default(ci->attrs, ctx->id("WID"), 0);
            NPNR_ASSERT(!cc.bram_data.count(wid));
            cc.bram_data[wid] = init_data;
            cc.tilegroups.push_back(tg);
        } else if (ci->type == id_MULT18X18D) {
            TileGroup tg;
            Loc loc = ctx->getBelLocation(ci->bel);
            tg.tiles = get_dsp_tiles(ctx, ci->bel);
            std::string dsp = "MULT18_" + std::to_string(loc.z);
            tg.config.add_enum(dsp + ".REG_INPUTA_CLK", str_or_default(ci->params, ctx->id("REG_INPUTA_CLK"), "NONE"));
            tg.config.add_enum(dsp + ".REG_INPUTA_CE", str_or_default(ci->params, ctx->id("REG_INPUTA_CE"), "CE0"));
            tg.config.add_enum(dsp + ".REG_INPUTA_RST", str_or_default(ci->params, ctx->id("REG_INPUTA_RST"), "RST0"));
            tg.config.add_enum(dsp + ".REG_INPUTB_CLK", str_or_default(ci->params, ctx->id("REG_INPUTB_CLK"), "NONE"));
            tg.config.add_enum(dsp + ".REG_INPUTB_CE", str_or_default(ci->params, ctx->id("REG_INPUTB_CE"), "CE0"));
            tg.config.add_enum(dsp + ".REG_INPUTB_RST", str_or_default(ci->params, ctx->id("REG_INPUTB_RST"), "RST0"));
            tg.config.add_enum(dsp + ".REG_INPUTC_CLK", str_or_default(ci->params, ctx->id("REG_INPUTC_CLK"), "NONE"));
            tg.config.add_enum(dsp + ".REG_PIPELINE_CLK",
                               str_or_default(ci->params, ctx->id("REG_PIPELINE_CLK"), "NONE"));
            tg.config.add_enum(dsp + ".REG_PIPELINE_CE", str_or_default(ci->params, ctx->id("REG_PIPELINE_CE"), "CE0"));
            tg.config.add_enum(dsp + ".REG_PIPELINE_RST",
                               str_or_default(ci->params, ctx->id("REG_PIPELINE_RST"), "RST0"));
            tg.config.add_enum(dsp + ".REG_OUTPUT_CLK", str_or_default(ci->params, ctx->id("REG_OUTPUT_CLK"), "NONE"));
            if (dsp == "MULT18_0" || dsp == "MULT18_4")
                tg.config.add_enum(dsp + ".REG_OUTPUT_RST",
                                   str_or_default(ci->params, ctx->id("REG_OUTPUT_RST"), "RST0"));

            tg.config.add_enum(dsp + ".CLK0_DIV", str_or_default(ci->params, ctx->id("CLK0_DIV"), "ENABLED"));
            tg.config.add_enum(dsp + ".CLK1_DIV", str_or_default(ci->params, ctx->id("CLK1_DIV"), "ENABLED"));
            tg.config.add_enum(dsp + ".CLK2_DIV", str_or_default(ci->params, ctx->id("CLK2_DIV"), "ENABLED"));
            tg.config.add_enum(dsp + ".CLK3_DIV", str_or_default(ci->params, ctx->id("CLK3_DIV"), "ENABLED"));
            tg.config.add_enum(dsp + ".GSR", str_or_default(ci->params, ctx->id("GSR"), "ENABLED"));
            tg.config.add_enum(dsp + ".SOURCEB_MODE", str_or_default(ci->params, ctx->id("SOURCEB_MODE"), "B_SHIFT"));
            tg.config.add_enum(dsp + ".RESETMODE", str_or_default(ci->params, ctx->id("RESETMODE"), "SYNC"));

            tg.config.add_enum(dsp + ".MODE", "MULT18X18D");
            if (str_or_default(ci->params, ctx->id("REG_OUTPUT_CLK"), "NONE") == "NONE")
                tg.config.add_enum(dsp + ".CIBOUT_BYP", "ON");

            if (loc.z < 4)
                tg.config.add_enum("DSP_LEFT.CIBOUT", "ON");
            else
                tg.config.add_enum("DSP_RIGHT.CIBOUT", "ON");

            // Some muxes default to INV, make all pass-thru
            for (auto port : {"CLK", "CE", "RST"}) {
                for (int i = 0; i < 4; i++) {
                    std::string sig = port + std::to_string(i);
                    tg.config.add_enum(dsp + "." + sig + "MUX", sig);
                }
            }

            tieoff_dsp_ports(ctx, cc, ci);
            cc.tilegroups.push_back(tg);

        } else if (ci->type == id_ALU54B) {
            TileGroup tg;
            Loc loc = ctx->getBelLocation(ci->bel);
            tg.tiles = get_dsp_tiles(ctx, ci->bel);
            std::string dsp = "ALU54_" + std::to_string(loc.z);
            tg.config.add_enum(dsp + ".REG_INPUTC0_CLK",
                               str_or_default(ci->params, ctx->id("REG_INPUTC0_CLK"), "NONE"));
            tg.config.add_enum(dsp + ".REG_INPUTC1_CLK",
                               str_or_default(ci->params, ctx->id("REG_INPUTC1_CLK"), "NONE"));
            tg.config.add_enum(dsp + ".REG_OPCODEOP0_0_CLK",
                               str_or_default(ci->params, ctx->id("REG_OPCODEOP0_0_CLK"), "NONE"));
            tg.config.add_enum(dsp + ".REG_OPCODEOP0_0_CE",
                               str_or_default(ci->params, ctx->id("REG_OPCODEOP0_0_CE"), "CE0"));
            tg.config.add_enum(dsp + ".REG_OPCODEOP0_0_RST",
                               str_or_default(ci->params, ctx->id("REG_OPCODEOP0_0_RST"), "RST0"));
            tg.config.add_enum(dsp + ".REG_OPCODEOP1_0_CLK",
                               str_or_default(ci->params, ctx->id("REG_OPCODEOP1_0_CLK"), "NONE"));
            tg.config.add_enum(dsp + ".REG_OPCODEOP0_1_CLK",
                               str_or_default(ci->params, ctx->id("REG_OPCODEOP0_1_CLK"), "NONE"));
            tg.config.add_enum(dsp + ".REG_OPCODEOP0_1_CE",
                               str_or_default(ci->params, ctx->id("REG_OPCODEOP0_1_CE"), "CE0"));
            tg.config.add_enum(dsp + ".REG_OPCODEOP0_1_RST",
                               str_or_default(ci->params, ctx->id("REG_OPCODEOP0_1_RST"), "RST0"));
            tg.config.add_enum(dsp + ".REG_OPCODEIN_0_CLK",
                               str_or_default(ci->params, ctx->id("REG_OPCODEIN_0_CLK"), "NONE"));
            tg.config.add_enum(dsp + ".REG_OPCODEIN_0_CE",
                               str_or_default(ci->params, ctx->id("REG_OPCODEIN_0_CE"), "CE0"));
            tg.config.add_enum(dsp + ".REG_OPCODEIN_0_RST",
                               str_or_default(ci->params, ctx->id("REG_OPCODEIN_0_RST"), "RST0"));
            tg.config.add_enum(dsp + ".REG_OPCODEIN_1_CLK",
                               str_or_default(ci->params, ctx->id("REG_OPCODEIN_1_CLK"), "NONE"));
            tg.config.add_enum(dsp + ".REG_OPCODEIN_1_CE",
                               str_or_default(ci->params, ctx->id("REG_OPCODEIN_1_CE"), "CE0"));
            tg.config.add_enum(dsp + ".REG_OPCODEIN_1_RST",
                               str_or_default(ci->params, ctx->id("REG_OPCODEIN_1_RST"), "RST0"));
            tg.config.add_enum(dsp + ".REG_OUTPUT0_CLK",
                               str_or_default(ci->params, ctx->id("REG_OUTPUT0_CLK"), "NONE"));
            tg.config.add_enum(dsp + ".REG_OUTPUT1_CLK",
                               str_or_default(ci->params, ctx->id("REG_OUTPUT1_CLK"), "NONE"));
            tg.config.add_enum(dsp + ".REG_FLAG_CLK", str_or_default(ci->params, ctx->id("REG_FLAG_CLK"), "NONE"));
            tg.config.add_enum(dsp + ".MCPAT_SOURCE", str_or_default(ci->params, ctx->id("MCPAT_SOURCE"), "STATIC"));
            tg.config.add_enum(dsp + ".MASKPAT_SOURCE",
                               str_or_default(ci->params, ctx->id("MASKPAT_SOURCE"), "STATIC"));
            tg.config.add_word(dsp + ".MASK01",
                               parse_init_str(str_or_default(ci->params, ctx->id("MASK01"), "0x00000000000000"), 56));
            tg.config.add_enum(dsp + ".CLK0_DIV", str_or_default(ci->params, ctx->id("CLK0_DIV"), "ENABLED"));
            tg.config.add_enum(dsp + ".CLK1_DIV", str_or_default(ci->params, ctx->id("CLK1_DIV"), "ENABLED"));
            tg.config.add_enum(dsp + ".CLK2_DIV", str_or_default(ci->params, ctx->id("CLK2_DIV"), "ENABLED"));
            tg.config.add_enum(dsp + ".CLK3_DIV", str_or_default(ci->params, ctx->id("CLK3_DIV"), "ENABLED"));
            tg.config.add_word(dsp + ".MCPAT",
                               parse_init_str(str_or_default(ci->params, ctx->id("MCPAT"), "0x00000000000000"), 56));
            tg.config.add_word(dsp + ".MASKPAT",
                               parse_init_str(str_or_default(ci->params, ctx->id("MASKPAT"), "0x00000000000000"), 56));
            tg.config.add_word(dsp + ".RNDPAT",
                               parse_init_str(str_or_default(ci->params, ctx->id("RNDPAT"), "0x00000000000000"), 56));
            tg.config.add_enum(dsp + ".GSR", str_or_default(ci->params, ctx->id("GSR"), "ENABLED"));
            tg.config.add_enum(dsp + ".RESETMODE", str_or_default(ci->params, ctx->id("RESETMODE"), "SYNC"));
            tg.config.add_enum(dsp + ".FORCE_ZERO_BARREL_SHIFT",
                               str_or_default(ci->params, ctx->id("FORCE_ZERO_BARREL_SHIFT"), "DISABLED"));
            tg.config.add_enum(dsp + ".LEGACY", str_or_default(ci->params, ctx->id("LEGACY"), "DISABLED"));

            tg.config.add_enum(dsp + ".MODE", "ALU54B");

            if (loc.z < 4)
                tg.config.add_enum("DSP_LEFT.CIBOUT", "ON");
            else
                tg.config.add_enum("DSP_RIGHT.CIBOUT", "ON");
            if (str_or_default(ci->params, ctx->id("REG_FLAG_CLK"), "NONE") == "NONE") {
                if (dsp == "ALU54_7") {
                    tg.config.add_enum("MULT18_5.CIBOUT_BYP", "ON");
                } else if (dsp == "ALU54_3") {
                    tg.config.add_enum("MULT18_5.CIBOUT_BYP", "ON");
                }
            }
            if (str_or_default(ci->params, ctx->id("REG_OUTPUT0_CLK"), "NONE") == "NONE") {
                if (dsp == "ALU54_7") {
                    tg.config.add_enum("MULT18_4.CIBOUT_BYP", "ON");
                } else if (dsp == "ALU54_3") {
                    tg.config.add_enum("MULT18_0.CIBOUT_BYP", "ON");
                }
            }
            tieoff_dsp_ports(ctx, cc, ci);
            cc.tilegroups.push_back(tg);
        } else if (ci->type == id_EHXPLLL) {
            TileGroup tg;
            tg.tiles = get_pll_tiles(ctx, ci->bel);

            tg.config.add_enum("MODE", "EHXPLLL");

            tg.config.add_word("CLKI_DIV", int_to_bitvector(int_or_default(ci->params, ctx->id("CLKI_DIV"), 1) - 1, 7));
            tg.config.add_word("CLKFB_DIV",
                               int_to_bitvector(int_or_default(ci->params, ctx->id("CLKFB_DIV"), 1) - 1, 7));

            tg.config.add_enum("CLKOP_ENABLE", str_or_default(ci->params, ctx->id("CLKOP_ENABLE"), "ENABLED"));
            tg.config.add_enum("CLKOS_ENABLE", str_or_default(ci->params, ctx->id("CLKOS_ENABLE"), "ENABLED"));
            tg.config.add_enum("CLKOS2_ENABLE", str_or_default(ci->params, ctx->id("CLKOS2_ENABLE"), "ENABLED"));
            tg.config.add_enum("CLKOS3_ENABLE", str_or_default(ci->params, ctx->id("CLKOS3_ENABLE"), "ENABLED"));

            for (std::string out : {"CLKOP", "CLKOS", "CLKOS2", "CLKOS3"}) {
                tg.config.add_word(out + "_DIV",
                                   int_to_bitvector(int_or_default(ci->params, ctx->id(out + "_DIV"), 8) - 1, 7));
                tg.config.add_word(out + "_CPHASE",
                                   int_to_bitvector(int_or_default(ci->params, ctx->id(out + "_CPHASE"), 0), 7));
                tg.config.add_word(out + "_FPHASE",
                                   int_to_bitvector(int_or_default(ci->params, ctx->id(out + "_FPHASE"), 0), 3));
            }

            tg.config.add_enum("FEEDBK_PATH", str_or_default(ci->params, ctx->id("FEEDBK_PATH"), "CLKOP"));
            tg.config.add_enum("CLKOP_TRIM_POL", str_or_default(ci->params, ctx->id("CLKOP_TRIM_POL"), "RISING"));
            tg.config.add_enum("CLKOP_TRIM_DELAY", str_or_default(ci->params, ctx->id("CLKOP_TRIM_DELAY"), "0"));
            tg.config.add_enum("CLKOS_TRIM_POL", str_or_default(ci->params, ctx->id("CLKOS_TRIM_POL"), "RISING"));
            tg.config.add_enum("CLKOS_TRIM_DELAY", str_or_default(ci->params, ctx->id("CLKOS_TRIM_DELAY"), "0"));

            tg.config.add_enum("OUTDIVIDER_MUXA", str_or_default(ci->params, ctx->id("OUTDIVIDER_MUXA"),
                                                                 get_net_or_empty(ci, id_CLKOP) ? "DIVA" : "REFCLK"));
            tg.config.add_enum("OUTDIVIDER_MUXB", str_or_default(ci->params, ctx->id("OUTDIVIDER_MUXB"),
                                                                 get_net_or_empty(ci, id_CLKOP) ? "DIVB" : "REFCLK"));
            tg.config.add_enum("OUTDIVIDER_MUXC", str_or_default(ci->params, ctx->id("OUTDIVIDER_MUXC"),
                                                                 get_net_or_empty(ci, id_CLKOP) ? "DIVC" : "REFCLK"));
            tg.config.add_enum("OUTDIVIDER_MUXD", str_or_default(ci->params, ctx->id("OUTDIVIDER_MUXD"),
                                                                 get_net_or_empty(ci, id_CLKOP) ? "DIVD" : "REFCLK"));

            tg.config.add_word("PLL_LOCK_MODE",
                               int_to_bitvector(int_or_default(ci->params, ctx->id("PLL_LOCK_MODE"), 0), 3));

            tg.config.add_enum("STDBY_ENABLE", str_or_default(ci->params, ctx->id("STDBY_ENABLE"), "DISABLED"));
            tg.config.add_enum("REFIN_RESET", str_or_default(ci->params, ctx->id("REFIN_RESET"), "DISABLED"));
            tg.config.add_enum("SYNC_ENABLE", str_or_default(ci->params, ctx->id("SYNC_ENABLE"), "DISABLED"));
            tg.config.add_enum("INT_LOCK_STICKY", str_or_default(ci->params, ctx->id("INT_LOCK_STICKY"), "ENABLED"));
            tg.config.add_enum("DPHASE_SOURCE", str_or_default(ci->params, ctx->id("DPHASE_SOURCE"), "DISABLED"));
            tg.config.add_enum("PLLRST_ENA", str_or_default(ci->params, ctx->id("PLLRST_ENA"), "DISABLED"));
            tg.config.add_enum("INTFB_WAKE", str_or_default(ci->params, ctx->id("INTFB_WAKE"), "DISABLED"));

            tg.config.add_word("KVCO", int_to_bitvector(int_or_default(ci->attrs, ctx->id("KVCO"), 0), 3));
            tg.config.add_word("LPF_CAPACITOR",
                               int_to_bitvector(int_or_default(ci->attrs, ctx->id("LPF_CAPACITOR"), 0), 2));
            tg.config.add_word("LPF_RESISTOR",
                               int_to_bitvector(int_or_default(ci->attrs, ctx->id("LPF_RESISTOR"), 0), 7));
            tg.config.add_word("ICP_CURRENT",
                               int_to_bitvector(int_or_default(ci->attrs, ctx->id("ICP_CURRENT"), 0), 5));
            tg.config.add_word("FREQ_LOCK_ACCURACY",
                               int_to_bitvector(int_or_default(ci->attrs, ctx->id("FREQ_LOCK_ACCURACY"), 0), 2));

            tg.config.add_word("MFG_GMC_GAIN",
                               int_to_bitvector(int_or_default(ci->attrs, ctx->id("MFG_GMC_GAIN"), 0), 3));
            tg.config.add_word("MFG_GMC_TEST",
                               int_to_bitvector(int_or_default(ci->attrs, ctx->id("MFG_GMC_TEST"), 14), 4));
            tg.config.add_word("MFG1_TEST", int_to_bitvector(int_or_default(ci->attrs, ctx->id("MFG1_TEST"), 0), 3));
            tg.config.add_word("MFG2_TEST", int_to_bitvector(int_or_default(ci->attrs, ctx->id("MFG2_TEST"), 0), 3));

            tg.config.add_word("MFG_FORCE_VFILTER",
                               int_to_bitvector(int_or_default(ci->attrs, ctx->id("MFG_FORCE_VFILTER"), 0), 1));
            tg.config.add_word("MFG_ICP_TEST",
                               int_to_bitvector(int_or_default(ci->attrs, ctx->id("MFG_ICP_TEST"), 0), 1));
            tg.config.add_word("MFG_EN_UP", int_to_bitvector(int_or_default(ci->attrs, ctx->id("MFG_EN_UP"), 0), 1));
            tg.config.add_word("MFG_FLOAT_ICP",
                               int_to_bitvector(int_or_default(ci->attrs, ctx->id("MFG_FLOAT_ICP"), 0), 1));
            tg.config.add_word("MFG_GMC_PRESET",
                               int_to_bitvector(int_or_default(ci->attrs, ctx->id("MFG_GMC_PRESET"), 0), 1));
            tg.config.add_word("MFG_LF_PRESET",
                               int_to_bitvector(int_or_default(ci->attrs, ctx->id("MFG_LF_PRESET"), 0), 1));
            tg.config.add_word("MFG_GMC_RESET",
                               int_to_bitvector(int_or_default(ci->attrs, ctx->id("MFG_GMC_RESET"), 0), 1));
            tg.config.add_word("MFG_LF_RESET",
                               int_to_bitvector(int_or_default(ci->attrs, ctx->id("MFG_LF_RESET"), 0), 1));
            tg.config.add_word("MFG_LF_RESGRND",
                               int_to_bitvector(int_or_default(ci->attrs, ctx->id("MFG_LF_RESGRND"), 0), 1));
            tg.config.add_word("MFG_GMCREF_SEL",
                               int_to_bitvector(int_or_default(ci->attrs, ctx->id("MFG_GMCREF_SEL"), 0), 2));
            tg.config.add_word("MFG_ENABLE_FILTEROPAMP",
                               int_to_bitvector(int_or_default(ci->attrs, ctx->id("MFG_ENABLE_FILTEROPAMP"), 0), 1));

            cc.tilegroups.push_back(tg);
        } else if (ci->type == id_IOLOGIC || ci->type == id_SIOLOGIC) {
            Loc pio_loc = ctx->getBelLocation(ci->bel);
            pio_loc.z -= ci->type == id_SIOLOGIC ? 2 : 4;
            std::string pic_tile = get_pic_tile(ctx, ctx->getBelByLocation(pio_loc));
            std::string prim = std::string("IOLOGIC") + "ABCD"[pio_loc.z];
            for (auto &param : ci->params) {
                if (param.first == ctx->id("DELAY.DEL_VALUE"))
                    cc.tiles[pic_tile].add_word(prim + "." + param.first.str(ctx),
                                                int_to_bitvector(std::stoi(param.second), 7));
                else
                    cc.tiles[pic_tile].add_enum(prim + "." + param.first.str(ctx), param.second);
            }
        } else if (ci->type == id_DCUA) {
            TileGroup tg;
            tg.tiles = get_dcu_tiles(ctx, ci->bel);
            tg.config.add_enum("DCU.MODE", "DCUA");
#include "dcu_bitstream.h"
            cc.tilegroups.push_back(tg);
            tieoff_dcu_ports(ctx, cc, ci);
        } else if (ci->type == id_EXTREFB) {
            TileGroup tg;
            tg.tiles = get_dcu_tiles(ctx, ci->bel);
            tg.config.add_word("EXTREF.REFCK_DCBIAS_EN",
                               parse_config_str(str_or_default(ci->params, ctx->id("REFCK_DCBIAS_EN"), "0"), 1));
            tg.config.add_word("EXTREF.REFCK_RTERM",
                               parse_config_str(str_or_default(ci->params, ctx->id("REFCK_RTERM"), "0"), 1));
            tg.config.add_word("EXTREF.REFCK_PWDNB",
                               parse_config_str(str_or_default(ci->params, ctx->id("REFCK_PWDNB"), "0"), 1));
            cc.tilegroups.push_back(tg);
        } else if (ci->type == id_PCSCLKDIV) {
            Loc loc = ctx->getBelLocation(ci->bel);
            std::string tname = ctx->getTileByTypeAndLocation(loc.y + 1, loc.x, "BMID_0H");
            cc.tiles[tname].add_enum("PCSCLKDIV" + std::to_string(loc.z),
                                     str_or_default(ci->params, ctx->id("GSR"), "ENABLED"));
        } else if (ci->type == id_DTR) {
            cc.tiles[ctx->getTileByType("DTR")].add_enum("DTR.MODE", "DTR");
        } else if (ci->type == id_OSCG) {
            int div = int_or_default(ci->params, ctx->id("DIV"), 128);
            if (div == 128)
                div = 127;
            cc.tiles[ctx->getTileByType("EFB0_PICB0")].add_enum("OSC.DIV", std::to_string(div));
            cc.tiles[ctx->getTileByType("EFB1_PICB1")].add_enum("OSC.DIV", std::to_string(div));
            cc.tiles[ctx->getTileByType("EFB1_PICB1")].add_enum("OSC.MODE", "OSCG");
            cc.tiles[ctx->getTileByType("EFB1_PICB1")].add_enum("CCLK.MODE", "_NONE_");
        } else if (ci->type == id_USRMCLK) {
            cc.tiles[ctx->getTileByType("EFB3_PICB1")].add_enum("CCLK.MODE", "USRMCLK");
        } else if (ci->type == id_GSR) {
            cc.tiles[ctx->getTileByType("EFB0_PICB0")].add_enum(
                    "GSR.GSRMODE", str_or_default(ci->params, ctx->id("MODE"), "ACTIVE_HIGH"));
            cc.tiles[ctx->getTileByType("VIQ_BUF")].add_enum("GSR.SYNCMODE",
                                                             str_or_default(ci->params, ctx->id("SYNCMODE"), "ASYNC"));
        } else if (ci->type == id_JTAGG) {
            cc.tiles[ctx->getTileByType("EFB0_PICB0")].add_enum("JTAG.ER1",
                                                                str_or_default(ci->params, ctx->id("ER1"), "ENABLED"));
            cc.tiles[ctx->getTileByType("EFB0_PICB0")].add_enum("JTAG.ER2",
                                                                str_or_default(ci->params, ctx->id("ER2"), "ENABLED"));
        } else if (ci->type == id_CLKDIVF) {
            Loc loc = ctx->getBelLocation(ci->bel);
            bool r = loc.x > 5;
            std::string clkdiv = std::string("CLKDIV_") + (r ? "R" : "L") + std::to_string(loc.z);
            std::string tile = ctx->getTileByType(std::string("ECLK_") + (r ? "R" : "L"));
            cc.tiles[tile].add_enum(clkdiv + ".DIV", str_or_default(ci->params, ctx->id("DIV"), "2.0"));
            cc.tiles[tile].add_enum(clkdiv + ".GSR", str_or_default(ci->params, ctx->id("GSR"), "DISABLED"));
        } else if (ci->type == id_TRELLIS_ECLKBUF) {
        } else if (ci->type == id_DQSBUFM) {
            Loc loc = ctx->getBelLocation(ci->bel);
            bool l = loc.x < 10;
            std::string pic = l ? "PICL" : "PICR";
            TileGroup tg;
            tg.tiles.push_back(ctx->getTileByTypeAndLocation(loc.y - 2, loc.x, pic + "1_DQS0"));
            tg.tiles.push_back(ctx->getTileByTypeAndLocation(loc.y - 1, loc.x, pic + "2_DQS1"));
            tg.tiles.push_back(ctx->getTileByTypeAndLocation(loc.y, loc.x, pic + "0_DQS2"));
            tg.tiles.push_back(ctx->getTileByTypeAndLocation(loc.y + 1, loc.x, pic + "1_DQS3"));
            tg.config.add_enum("DQS.MODE", "DQSBUFM");
            tg.config.add_enum("DQS.DQS_LI_DEL_ADJ", str_or_default(ci->params, ctx->id("DQS_LI_DEL_ADJ"), "PLUS"));
            tg.config.add_enum("DQS.DQS_LO_DEL_ADJ", str_or_default(ci->params, ctx->id("DQS_LO_DEL_ADJ"), "PLUS"));
            int li_del_value = int_or_default(ci->params, ctx->id("DQS_LI_DEL_VAL"), 0);
            if (str_or_default(ci->params, ctx->id("DQS_LI_DEL_ADJ"), "PLUS") == "MINUS")
                li_del_value = (256 - li_del_value) & 0xFF;
            int lo_del_value = int_or_default(ci->params, ctx->id("DQS_LO_DEL_VAL"), 0);
            if (str_or_default(ci->params, ctx->id("DQS_LO_DEL_ADJ"), "PLUS") == "MINUS")
                lo_del_value = (256 - lo_del_value) & 0xFF;
            tg.config.add_word("DQS.DQS_LI_DEL_VAL", int_to_bitvector(li_del_value, 8));
            tg.config.add_word("DQS.DQS_LO_DEL_VAL", int_to_bitvector(lo_del_value, 8));
            tg.config.add_enum("DQS.WRLOADN_USED", get_net_or_empty(ci, id_WRLOADN) != nullptr ? "YES" : "NO");
            tg.config.add_enum("DQS.RDLOADN_USED", get_net_or_empty(ci, id_RDLOADN) != nullptr ? "YES" : "NO");
            tg.config.add_enum("DQS.PAUSE_USED", get_net_or_empty(ci, id_PAUSE) != nullptr ? "YES" : "NO");
            tg.config.add_enum("DQS.READ_USED",
                               (get_net_or_empty(ci, id_READ0) != nullptr || get_net_or_empty(ci, id_READ1) != nullptr)
                                       ? "YES"
                                       : "NO");
            tg.config.add_enum("DQS.DDRDEL", get_net_or_empty(ci, id_DDRDEL) != nullptr ? "DDRDEL" : "0");
            tg.config.add_enum("DQS.GSR", str_or_default(ci->params, ctx->id("GSR"), "DISABLED"));
            cc.tilegroups.push_back(tg);
        } else if (ci->type == id_ECLKSYNCB) {
            Loc loc = ctx->getBelLocation(ci->bel);
            bool r = loc.x > 5;
            std::string eclksync = ctx->locInfo(bel)->bel_data[bel.index].name.get();
            std::string tile = ctx->getTileByType(std::string("ECLK_") + (r ? "R" : "L"));
            if (get_net_or_empty(ci, id_STOP) != nullptr)
                cc.tiles[tile].add_enum(eclksync + ".MODE", "ECLKSYNCB");
        } else if (ci->type == id_DDRDLL) {
            Loc loc = ctx->getBelLocation(ci->bel);
            bool u = loc.y<15, r = loc.x> 15;
            std::string tiletype = fmt_str("DDRDLL_" << (u ? 'U' : 'L') << (r ? 'R' : 'L'));
            if (ctx->args.type == ArchArgs::LFE5U_25F || ctx->args.type == ArchArgs::LFE5UM_25F ||
                ctx->args.type == ArchArgs::LFE5UM5G_25F)
                tiletype += "A";
            std::string tile = ctx->getTileByType(tiletype);
            cc.tiles[tile].add_enum("DDRDLL.MODE", "DDRDLLA");
            cc.tiles[tile].add_enum("DDRDLL.GSR", str_or_default(ci->params, ctx->id("GSR"), "DISABLED"));
            cc.tiles[tile].add_enum("DDRDLL.FORCE_MAX_DELAY",
                                    str_or_default(ci->params, ctx->id("FORCE_MAX_DELAY"), "NO"));
        } else {
            NPNR_ASSERT_FALSE("unsupported cell type");
        }
    }

    // Fixup tile names
    fix_tile_names(ctx, cc);
    // Configure chip
    if (!text_config_file.empty()) {
        std::ofstream out_config(text_config_file);
        out_config << cc;
    }
}

NEXTPNR_NAMESPACE_END
