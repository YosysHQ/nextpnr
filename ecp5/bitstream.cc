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

#include <fstream>
#include <iomanip>
#include <regex>
#include <streambuf>

#include "config.h"
#include "io.h"
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
    WireId cibsig = wire;
    std::string basename = ctx->getWireBasename(wire).str(ctx);

    while (!std::regex_match(basename, cib_re)) {
        auto uphill = ctx->getPipsUphill(cibsig);
        NPNR_ASSERT(uphill.begin() != uphill.end()); // At least one uphill pip
        auto iter = uphill.begin();
        cibsig = ctx->getPipSrcWire(*iter);
        basename = ctx->getWireBasename(cibsig).str(ctx);
        ++iter;
        NPNR_ASSERT(!(iter != uphill.end())); // Exactly one uphill pip
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
            result.at(i * 4 + 1) = nibble & 0x2;
            result.at(i * 4 + 2) = nibble & 0x4;
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

void fix_tile_names(Context *ctx, ChipConfig &cc)
{
    // Remove the V prefix/suffix on certain tiles if device is a SERDES variant
    if (ctx->args.type == ArchArgs::LFE5UM_25F || ctx->args.type == ArchArgs::LFE5UM_45F ||
        ctx->args.type == ArchArgs::LFE5UM_85F || ctx->args.type == ArchArgs::LFE5UM5G_25F ||
        ctx->args.type == ArchArgs::LFE5UM5G_45F || ctx->args.type == ArchArgs::LFE5UM5G_85F) {
        std::map<std::string, std::string> tiletype_xform;
        for (const auto &tile : cc.tiles) {
            std::string newname = tile.first;
            auto vcib = tile.first.find("VCIB");
            if (vcib != std::string::npos) {
                // Remove the V
                newname.erase(vcib);
                tiletype_xform[tile.first] = newname;
            } else if (tile.first.back() == 'V') {
                // BMID_0V or BMID_2V
                if (tile.first.at(tile.first.size() - 2) == '0') {
                    newname.at(tile.first.size() - 1) = 'H';
                    tiletype_xform[tile.first] = newname;
                } else if (tile.first.at(tile.first.size() - 2) == '2') {
                    newname.pop_back();
                    tiletype_xform[tile.first] = newname;
                }
            }
        }
        // Apply the name changes
        for (auto xform : tiletype_xform) {
            cc.tiles[xform.second] = cc.tiles.at(xform.first);
            cc.tiles.erase(xform.first);
        }
    }
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
        cc.chip_name = ctx->getChipName();
        // TODO: .bit metadata
    }

    // Add all set, configurable pips to the config
    for (auto pip : ctx->getPips()) {
        if (ctx->getBoundPipNet(pip) != nullptr) {
            if (ctx->getPipClass(pip) == 0) { // ignore fixed pips
                std::string tile = ctx->getPipTilename(pip);
                std::string source = get_trellis_wirename(ctx, pip.location, ctx->getPipSrcWire(pip));
                std::string sink = get_trellis_wirename(ctx, pip.location, ctx->getPipDstWire(pip));
                cc.tiles[tile].add_arc(sink, source);
            }
        }
    }
    // Find bank voltages
    std::unordered_map<int, IOVoltage> bankVcc;
    std::unordered_map<int, bool> bankLvds;

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->bel != BelId() && ci->type == ctx->id("TRELLIS_IO")) {
            int bank = ctx->getPioBelBank(ci->bel);
            std::string dir = str_or_default(ci->params, ctx->id("DIR"), "INPUT");
            std::string iotype = str_or_default(ci->attrs, ctx->id("IO_TYPE"), "LVCMOS33");

            if (dir != "INPUT") {
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
                    if (bankVcc.find(bank) != bankVcc.end())
                        cc.tiles[tile.first].add_enum("BANK.VCCIO", iovoltage_to_str(bankVcc[bank]));
                    if (bankLvds[bank]) {
                        cc.tiles[tile.first].add_enum("BANK.DIFF_REF", "ON");
                        cc.tiles[tile.first].add_enum("BANK.LVDSO", "ON");
                    }
                }
            }
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
            }
            if (dir != "INPUT" &&
                (ci->ports.find(ctx->id("T")) == ci->ports.end() || ci->ports.at(ctx->id("T")).net == nullptr)) {
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
            if (dir == "INPUT" && !is_differential(ioType_from_str(iotype))) {
                cc.tiles[pio_tile].add_enum(pio + ".HYSTERESIS", "ON");
            }
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
