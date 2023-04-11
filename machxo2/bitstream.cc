/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
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
#include <iostream>

#include "bitstream.h"
#include "config.h"
#include "nextpnr.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

// These seem simple enough to do inline for now.
namespace BaseConfigs {
void config_empty_lcmxo2_1200(ChipConfig &cc)
{
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
    std::string basename = ctx->tile_info(wire)->wire_data[wire.index].name.get();
    std::string prefix2 = basename.substr(0, 2);
    std::string prefix7 = basename.substr(0, 7);
    int max_col = ctx->chip_info->width - 1;

    // Handle MachXO2's wonderful naming quirks for wires in left/right tiles, whose
    // relative coords push them outside the bounds of the chip.
    // Indents are based on wires proximity/purpose.
    auto is_pio_wire = [](std::string name) {
        // clang-format off
        return (name.find("DI") != std::string::npos || name.find("JDI") != std::string::npos ||
                    name.find("PADD") != std::string::npos || name.find("INDD") != std::string::npos ||
                    name.find("IOLDO") != std::string::npos || name.find("IOLTO") != std::string::npos ||
                // JCE0-3, JCLK0-3, JLSR0-3 connect to PIO wires named JCEA-D, JCLKA-D, JLSRA-D.
                name.find("JCEA") != std::string::npos || name.find("JCEB") != std::string::npos ||
                    name.find("JCEC") != std::string::npos || name.find("JCED") != std::string::npos ||
                    name.find("JCLKA") != std::string::npos || name.find("JCLKB") != std::string::npos ||
                    name.find("JCLKC") != std::string::npos || name.find("JCLKD") != std::string::npos ||
                    name.find("JLSRA") != std::string::npos || name.find("JLSRB") != std::string::npos ||
                    name.find("JLSRC") != std::string::npos || name.find("JLSRD") != std::string::npos ||
                name.find("JONEG") != std::string::npos || name.find("JOPOS") != std::string::npos ||
                    name.find("JTS") != std::string::npos ||
                name.find("JIN") != std::string::npos || name.find("JIP") != std::string::npos ||
                // Connections to global mux
                name.find("JINCK") != std::string::npos);
        // clang-format on
    };

    if (prefix2 == "G_" || prefix2 == "L_" || prefix2 == "R_" || prefix7 == "BRANCH_")
        return basename;

    if (prefix2 == "U_" || prefix2 == "D_") {
        // We needded to keep U_ and D_ prefixes to generate the routing
        // graph connections properly, but in truth they are not relevant
        // outside of the center row of tiles as far as the database is
        // concerned. So convert U_/D_ prefixes back to G_ if not in the
        // center row.

        // FIXME: This is hardcoded to 1200HC coordinates for now. Perhaps
        // add a center row/col field to chipdb?
        if (loc.y == 6)
            return basename;
        else
            return "G_" + basename.substr(2);
    }

    if (loc == wire.location) {
        // TODO: JINCK is not currently handled by this.
        if (is_pio_wire(basename)) {
            if (wire.location.x == 0) {
                std::string pio_name = "W1_" + basename;
                if (ctx->verbose)
                    log_info("PIO wire %s was adjusted by W1 to form Trellis name %s.\n", ctx->nameOfWire(wire),
                             pio_name.c_str());
                return pio_name;
            } else if (wire.location.x == max_col) {
                std::string pio_name = "E1_" + basename;
                if (ctx->verbose)
                    log_info("PIO wire %s was adjusted by E1 to form Trellis name %s.\n", ctx->nameOfWire(wire),
                             pio_name.c_str());
                return pio_name;
            }
        }
        return basename;
    }

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
    std::string tile = ctx->get_pip_tilename(pip);
    std::string tile_type = ctx->chip_info->tiletype_names[ctx->tile_info(pip)->pip_data[pip.index].tile_type].get();
    std::string source = get_trellis_wirename(ctx, pip.location, ctx->getPipSrcWire(pip));
    std::string sink = get_trellis_wirename(ctx, pip.location, ctx->getPipDstWire(pip));
    cc.tiles[tile].add_arc(sink, source);

    // Special case pips whose config bits are spread across tiles.
    if (source == "G_PCLKCIBVIQT0" && sink == "G_VPRXCLKI0") {
        if (tile_type == "CENTER7") {
            cc.tiles[ctx->get_tile_by_type("CENTER8")].add_arc(sink, source);
        } else if (tile_type == "CENTER8") {
            cc.tiles[ctx->get_tile_by_type("CENTER7")].add_arc(sink, source);
        } else {
            NPNR_ASSERT_FALSE("Tile does not contain special-cased pip");
        }
    }
}

static std::vector<bool> int_to_bitvector(int val, int size)
{
    std::vector<bool> bv;
    for (int i = 0; i < size; i++) {
        bv.push_back((val & (1 << i)) != 0);
    }
    return bv;
}

std::string intstr_or_default(const dict<IdString, Property> &ct, const IdString &key, std::string def = "0")
{
    auto found = ct.find(key);
    if (found == ct.end())
        return def;
    else {
        if (found->second.is_string)
            return found->second.as_string();
        else
            return std::to_string(found->second.as_int64());
    }
};

// Get the PIC tile corresponding to a PIO bel
static std::string get_pic_tile(Context *ctx, BelId bel)
{
    static const std::set<std::string> pio_t = {"PIC_T0", "PIC_T0_256", "PIC_TS0"};
    static const std::set<std::string> pio_b = {"PIC_B0", "PIC_B0_256", "PIC_BS0_256"};
    static const std::set<std::string> pio_l = {"PIC_L0",       "PIC_L1",       "PIC_L2",       "PIC_L3",
                                                "PIC_LS0",      "PIC_L0_VREF3", "PIC_L0_VREF4", "PIC_L0_VREF5",
                                                "PIC_L1_VREF3", "PIC_L1_VREF4", "PIC_L1_VREF5", "PIC_L2_VREF4",
                                                "PIC_L2_VREF5", "PIC_L3_VREF4", "PIC_L3_VREF5"};
    static const std::set<std::string> pio_r = {"PIC_R0",     "PIC_R1",     "PIC_RS0",
                                                "PIC_R0_256", "PIC_R1_640", "PIC_RS0_256"};

    std::string pio_name = ctx->tile_info(bel)->bel_data[bel.index].name.get();
    if (bel.location.y == 0) {
        return ctx->get_tile_by_type_loc(0, bel.location.x, pio_t);
    } else if (bel.location.y == ctx->chip_info->height - 1) {
        return ctx->get_tile_by_type_loc(bel.location.y, bel.location.x, pio_b);
    } else if (bel.location.x == 0) {
        return ctx->get_tile_by_type_loc(bel.location.y, 0, pio_l);
    } else if (bel.location.x == ctx->chip_info->width - 1) {
        return ctx->get_tile_by_type_loc(bel.location.y, bel.location.x, pio_r);
    } else {
        NPNR_ASSERT_FALSE("bad PIO location");
    }
}

void write_bitstream(Context *ctx, std::string text_config_file)
{
    ChipConfig cc;
    IdString base_id = ctx->id(ctx->chip_info->device_name.get());
    // IdString device_id = ctx->id(ctx->device_name);
    if (base_id == ctx->id("LCMXO2-1200"))
        BaseConfigs::config_empty_lcmxo2_1200(cc);
    else
        NPNR_ASSERT_FALSE("Unsupported device type");
    cc.chip_name = ctx->chip_info->device_name.get();
    cc.chip_variant = ctx->device_name;

    cc.metadata.push_back("Part: " + ctx->getChipName());

    // Add all set, configurable pips to the config
    for (auto pip : ctx->getPips()) {
        if (ctx->getBoundPipNet(pip) != nullptr) {
            if (ctx->get_pip_class(pip) == 0) { // ignore fixed pips
                set_pip(ctx, cc, pip);
            }
        }
    }

    // TODO: Bank Voltages

    // Configure slices
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->bel == BelId()) {
            log_warning("found unplaced cell '%s' during bitstream gen. Not writing to bitstream.\n",
                        ci->name.c_str(ctx));
            continue;
        }
        BelId bel = ci->bel;
        if (ci->type == id_TRELLIS_SLICE) {
            std::string tname = ctx->get_tile_by_type_loc(bel.location.y, bel.location.x, "PLC");
            std::string slice = ctx->tile_info(bel)->bel_data[bel.index].name.get();

            NPNR_ASSERT(slice.substr(0, 5) == "SLICE");
            int int_index = slice[5] - 'A';
            NPNR_ASSERT(int_index >= 0 && int_index < 4);

            int lut0_init = int_or_default(ci->params, id_LUT0_INITVAL);
            int lut1_init = int_or_default(ci->params, id_LUT1_INITVAL);
            cc.tiles[tname].add_word(slice + ".K0.INIT", int_to_bitvector(lut0_init, 16));
            cc.tiles[tname].add_word(slice + ".K1.INIT", int_to_bitvector(lut1_init, 16));
            cc.tiles[tname].add_enum(slice + ".MODE", str_or_default(ci->params, id_MODE, "LOGIC"));
            cc.tiles[tname].add_enum(slice + ".GSR", str_or_default(ci->params, id_GSR, "ENABLED"));
            cc.tiles[tname].add_enum("LSR" + std::to_string(int_index) + ".SRMODE",
                                     str_or_default(ci->params, id_SRMODE, "LSR_OVER_CE"));
            cc.tiles[tname].add_enum(slice + ".CEMUX", intstr_or_default(ci->params, id_CEMUX, "1"));
            cc.tiles[tname].add_enum("CLK" + std::to_string(int_index) + ".CLKMUX",
                                     intstr_or_default(ci->params, id_CLKMUX, "0"));
            cc.tiles[tname].add_enum("LSR" + std::to_string(int_index) + ".LSRMUX",
                                     str_or_default(ci->params, id_LSRMUX, "LSR"));
            cc.tiles[tname].add_enum("LSR" + std::to_string(int_index) + ".LSRONMUX",
                                     intstr_or_default(ci->params, id_LSRONMUX, "LSRMUX"));
            cc.tiles[tname].add_enum(slice + ".REGMODE", str_or_default(ci->params, id_REGMODE, "FF"));
            cc.tiles[tname].add_enum(slice + ".REG0.SD", intstr_or_default(ci->params, id_REG0_SD, "0"));
            cc.tiles[tname].add_enum(slice + ".REG1.SD", intstr_or_default(ci->params, id_REG1_SD, "0"));
            cc.tiles[tname].add_enum(slice + ".REG0.REGSET", str_or_default(ci->params, id_REG0_REGSET, "RESET"));
            cc.tiles[tname].add_enum(slice + ".REG1.REGSET", str_or_default(ci->params, id_REG1_REGSET, "RESET"));
        } else if (ci->type == id_TRELLIS_IO) {
            std::string pio = ctx->tile_info(bel)->bel_data[bel.index].name.get();
            std::string iotype = str_or_default(ci->attrs, id_IO_TYPE, "LVCMOS33");
            std::string dir = str_or_default(ci->params, id_DIR, "INPUT");
            std::string pic_tile = get_pic_tile(ctx, bel);
            cc.tiles[pic_tile].add_enum(pio + ".BASE_TYPE", dir + "_" + iotype);
        } else if (ci->type == id_OSCH) {
            std::string freq = str_or_default(ci->params, id_NOM_FREQ, "2.08");
            cc.tiles[ctx->get_tile_by_type("CFG1")].add_enum("OSCH.MODE", "OSCH");
            cc.tiles[ctx->get_tile_by_type("CFG1")].add_enum("OSCH.NOM_FREQ", freq);
        }
    }

    // Configure chip
    if (!text_config_file.empty()) {
        std::ofstream out_config(text_config_file);
        out_config << cc;
    }
}

NEXTPNR_NAMESPACE_END
