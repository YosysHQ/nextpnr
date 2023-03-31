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

#include "bitstream.h"

#include <boost/algorithm/string/predicate.hpp>
#include <fstream>
#include <iomanip>
#include <queue>
#include <regex>
#include <streambuf>
#include "config.h"
#include "log.h"
#include "util.h"

#define fmt_str(x) (static_cast<const std::ostringstream &>(std::ostringstream() << x).str())

NEXTPNR_NAMESPACE_BEGIN

// These seem simple enough to do inline for now.
namespace BaseConfigs {
void config_empty_lcmxo2_256(ChipConfig &cc);
void config_empty_lcmxo2_640(ChipConfig &cc);
void config_empty_lcmxo2_1200(ChipConfig &cc);
void config_empty_lcmxo2_2000(ChipConfig &cc);
void config_empty_lcmxo2_4000(ChipConfig &cc);
void config_empty_lcmxo2_7000(ChipConfig &cc);
void config_empty_lcmxo3_1300(ChipConfig &cc);
void config_empty_lcmxo3_2100(ChipConfig &cc);
void config_empty_lcmxo3_4300(ChipConfig &cc);
void config_empty_lcmxo3_6900(ChipConfig &cc);
void config_empty_lcmxo3_9400(ChipConfig &cc);
} // namespace BaseConfigs

namespace {
struct MachXO2Bitgen
{
    explicit MachXO2Bitgen(Context *ctx) : ctx(ctx){};
    Context *ctx;
    ChipConfig cc;
    // Convert an absolute wire name to a relative Trellis one
    std::string get_trellis_wirename(Location loc, WireId wire)
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
    std::vector<bool> int_to_bitvector(int val, int size)
    {
        std::vector<bool> bv;
        for (int i = 0; i < size; i++) {
            bv.push_back((val & (1 << i)) != 0);
        }
        return bv;
    }
    std::vector<bool> str_to_bitvector(std::string str, int size)
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

    // Get the PIC tile corresponding to a PIO bel
    std::string get_pic_tile(BelId bel)
    {
        static const std::set<std::string> pio_t = {"PIC_T0", "PIC_T0_256", "PIC_TS0"};
        static const std::set<std::string> pio_b = {"PIC_B0", "PIC_B0_256", "PIC_BS0_256"};
        static const std::set<std::string> pio_l = {"PIC_L0", "PIC_L1", "PIC_L2", "PIC_L3", "PIC_LS0",
                                                    "PIC_L0_VREF3", "PIC_L0_VREF4", "PIC_L0_VREF5",
                                                    "PIC_L1_VREF3", "PIC_L1_VREF4", "PIC_L1_VREF5",
                                                    "PIC_L2_VREF4", "PIC_L2_VREF5",
                                                    "PIC_L3_VREF4", "PIC_L3_VREF5"};
        static const std::set<std::string> pio_r = {"PIC_R0", "PIC_R1", "PIC_RS0",
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

    void set_pip(ChipConfig &cc, PipId pip)
    {
        std::string tile = ctx->get_pip_tilename(pip);
        std::string tile_type = ctx->chip_info->tiletype_names[ctx->tile_info(pip)->pip_data[pip.index].tile_type].get();
        std::string source = get_trellis_wirename(pip.location, ctx->getPipSrcWire(pip));
        std::string sink = get_trellis_wirename(pip.location, ctx->getPipDstWire(pip));
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

    unsigned permute_lut(CellInfo *cell, pool<IdString> &used_phys_pins, unsigned orig_init)
    {
        std::array<std::vector<unsigned>, 4> phys_to_log;
        const std::array<IdString, 4> ports{id_A, id_B, id_C, id_D};
        for (unsigned i = 0; i < 4; i++) {
            WireId pin_wire = ctx->getBelPinWire(cell->bel, ports[i]);
            for (PipId pip : ctx->getPipsUphill(pin_wire)) {
                if (!ctx->getBoundPipNet(pip))
                    continue;
                unsigned lp = ctx->tile_info(pip)->pip_data[pip.index].lutperm_flags;
                if (!is_lutperm_pip(lp)) { // non-permuting
                    phys_to_log[i].push_back(i);
                } else { // permuting
                    unsigned from_pin = lutperm_in(lp);
                    unsigned to_pin = lutperm_out(lp);
                    NPNR_ASSERT(to_pin == i);
                    phys_to_log[from_pin].push_back(i);
                }
            }
        }
        for (unsigned i = 0; i < 4; i++)
            if (!phys_to_log.at(i).empty())
                used_phys_pins.insert(ports.at(i));
        if (cell->combInfo.flags & ArchCellInfo::COMB_CARRY) {
            // Insert dummy entries to ensure we keep the split between the two halves of a CCU2
            for (unsigned i = 0; i < 4; i++) {
                if (!phys_to_log.at(i).empty())
                    continue;
                for (unsigned j = 2 * (i / 2); j < 2 * ((i / 2) + 1); j++) {
                    if (!ctx->getBoundWireNet(ctx->getBelPinWire(cell->bel, ports[j])))
                        phys_to_log.at(i).push_back(j);
                }
            }
        }
        unsigned permuted_init = 0;
        for (unsigned i = 0; i < 16; i++) {
            unsigned log_idx = 0;
            for (unsigned j = 0; j < 4; j++) {
                if ((i >> j) & 0x1) {
                    for (auto log_pin : phys_to_log[j])
                        log_idx |= (1 << log_pin);
                }
            }
            if ((orig_init >> log_idx) & 0x1)
                permuted_init |= (1 << i);
        }
        return permuted_init;
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

    void write_comb(CellInfo *ci)
    {
        pool<IdString> used_phys_pins;
        BelId bel = ci->bel;
        std::string tname = ctx->get_tile_by_type_loc(bel.location.y, bel.location.x, "PLC");
        int z = ctx->tile_info(bel)->bel_data[bel.index].z >> Arch::lc_idx_shift;
        std::string slice = std::string("SLICE") + "ABCD"[z / 2];
        std::string lc = std::to_string(z % 2);
        std::string mode = str_or_default(ci->params, id_MODE, "LOGIC");
        if (mode == "RAMW_BLOCK")
            return;
        int lut_init = int_or_default(ci->params, id_INITVAL);
        cc.tiles[tname].add_enum(slice + ".MODE", mode);
        cc.tiles[tname].add_word(slice + ".K" + lc + ".INIT",
                                int_to_bitvector(permute_lut(ci, used_phys_pins, lut_init), 16));
        if (mode == "CCU2") {
            cc.tiles[tname].add_enum(slice + ".CCU2.INJECT1_" + lc, str_or_default(ci->params, id_CCU2_INJECT1, "YES"));
        } else {
            // Don't interfere with cascade mux wiring
            cc.tiles[tname].add_enum(slice + ".CCU2.INJECT1_" + lc, "_NONE_");
        }
        if (mode == "DPRAM" && slice == "SLICEA" && lc == "0") {
            cc.tiles[tname].add_enum(slice + ".WREMUX", str_or_default(ci->params, id_WREMUX, "WRE"));
            std::string wckmux = str_or_default(ci->params, id_WCKMUX, "WCK");
            wckmux = (wckmux == "WCK") ? "CLK" : wckmux;
            cc.tiles[tname].add_enum("CLK1.CLKMUX", wckmux);
        }
    }

    void write_ff(CellInfo *ci)
    {
        BelId bel = ci->bel;
        std::string tname = ctx->get_tile_by_type_loc(bel.location.y, bel.location.x, "PLC");
        int z = ctx->tile_info(bel)->bel_data[bel.index].z >> Arch::lc_idx_shift;
        std::string slice = std::string("SLICE") + "ABCD"[z / 2];
        std::string lc = std::to_string(z % 2);

        cc.tiles[tname].add_enum(slice + ".MODE", str_or_default(ci->params, id_MODE, "LOGIC"));
        cc.tiles[tname].add_enum(slice + ".GSR", str_or_default(ci->params, id_GSR, "ENABLED"));
        cc.tiles[tname].add_enum(slice + ".REGMODE", str_or_default(ci->params, id_REGMODE, "FF"));
        cc.tiles[tname].add_enum(slice + ".REG" + lc + ".SD", intstr_or_default(ci->params, id_SD, "0"));
        cc.tiles[tname].add_enum(slice + ".REG" + lc + ".REGSET", str_or_default(ci->params, id_REGSET, "RESET"));

        cc.tiles[tname].add_enum(slice + ".CEMUX", str_or_default(ci->params, id_CEMUX, "1"));

        NetInfo *lsrnet = ci->getPort(id_LSR);
        if (ctx->getBoundWireNet(ctx->get_wire_by_loc_basename(bel.location, "LSR0")) == lsrnet) {
            cc.tiles[tname].add_enum("LSR0.LSRMUX", str_or_default(ci->params, id_LSRMUX, "LSR"));
            cc.tiles[tname].add_enum("LSR0.LSRONMUX", str_or_default(ci->params, id_LSRONMUX, "LSRMUX"));
        }
        if (ctx->getBoundWireNet(ctx->get_wire_by_loc_basename(bel.location, "LSR1")) == lsrnet) {
            cc.tiles[tname].add_enum("LSR1.LSRMUX", str_or_default(ci->params, id_LSRMUX, "LSR"));
            cc.tiles[tname].add_enum("LSR1.LSRONMUX", str_or_default(ci->params, id_LSRONMUX, "LSRMUX"));
        }

        NetInfo *clknet = ci->getPort(id_CLK);
        if (ctx->getBoundWireNet(ctx->get_wire_by_loc_basename(bel.location, "CLK0")) == clknet) {
            cc.tiles[tname].add_enum("CLK0.CLKMUX", str_or_default(ci->params, id_CLKMUX, "0"));
        }
        if (ctx->getBoundWireNet(ctx->get_wire_by_loc_basename(bel.location, "CLK1")) == clknet) {
            cc.tiles[tname].add_enum("CLK1.CLKMUX", str_or_default(ci->params, id_CLKMUX, "0"));
        }
    }

    void write_io(CellInfo *ci)
    {
        BelId bel = ci->bel;
        std::string pio = ctx->tile_info(bel)->bel_data[bel.index].name.get();
        std::string iotype = str_or_default(ci->attrs, id_IO_TYPE, "LVCMOS33");
        std::string dir = str_or_default(ci->params, id_DIR, "INPUT");
        std::string pic_tile = get_pic_tile(bel);
        cc.tiles[pic_tile].add_enum(pio + ".BASE_TYPE", dir + "_" + iotype);
    }

    void run()
    {
        ChipConfig cc;
        IdString base_id = ctx->id(ctx->chip_info->device_name.get());
        if (base_id == ctx->id("LCMXO2-256"))
            BaseConfigs::config_empty_lcmxo2_256(cc);
        else if (base_id == ctx->id("LCMXO2-640"))
            BaseConfigs::config_empty_lcmxo2_640(cc);
        else if (base_id == ctx->id("LCMXO2-1200"))
            BaseConfigs::config_empty_lcmxo2_1200(cc);
        else if (base_id == ctx->id("LCMXO2-2000"))
            BaseConfigs::config_empty_lcmxo2_2000(cc);
        else if (base_id == ctx->id("LCMXO2-4000"))
            BaseConfigs::config_empty_lcmxo2_4000(cc);
        else if (base_id == ctx->id("LCMXO2-7000"))
            BaseConfigs::config_empty_lcmxo2_7000(cc);
        else if (base_id == ctx->id("LCMXO3-1300"))
            BaseConfigs::config_empty_lcmxo3_1300(cc);
        else if (base_id == ctx->id("LCMXO3-2100"))
            BaseConfigs::config_empty_lcmxo3_2100(cc);
        else if (base_id == ctx->id("LCMXO3-4300") || base_id == ctx->id("LCMXO3D-4300"))
            BaseConfigs::config_empty_lcmxo3_4300(cc);
        else if (base_id == ctx->id("LCMXO3-6900"))
            BaseConfigs::config_empty_lcmxo3_6900(cc);
        else if (base_id == ctx->id("LCMXO3-9400") || base_id == ctx->id("LCMXO3D-9400"))
            BaseConfigs::config_empty_lcmxo3_9400(cc);
        else
            NPNR_ASSERT_FALSE("Unsupported device type");
        cc.chip_name = ctx->chip_info->device_name.get();
        cc.chip_variant = ctx->device_name;

        cc.metadata.push_back("Part: " + ctx->getChipName());

        if (cc.chip_variant.find("LCMXO3L-") != std::string::npos) {
            // XO3L have this set but not XO3LF
            cc.tiles["PT5:CFG1"].add_unknown(5, 36);
        }
        if (cc.chip_variant.find("LCMXO3D-") != std::string::npos) {
            cc.tiles["PT5:CFG1"].add_unknown(5, 36);
            cc.tiles["PT6:CFG2"].add_unknown(5, 37);
        }

        // Add all set, configurable pips to the config
        for (auto pip : ctx->getPips()) {
            if (ctx->getBoundPipNet(pip) != nullptr) {
                if (ctx->get_pip_class(pip) == 0) { // ignore fixed pips
                    set_pip(cc, pip);
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

            if (ci->type == id_TRELLIS_COMB) {
                write_comb(ci);
            } else if (ci->type == id_TRELLIS_FF) {
                write_ff(ci);
            } else if (ci->type == id_TRELLIS_RAMW) {
                std::string tname = ctx->get_tile_by_type_loc(bel.location.y, bel.location.x, "PLC");
                cc.tiles[tname].add_enum("SLICEC.MODE", "RAMW");
                cc.tiles[tname].add_word("SLICEC.K0.INIT", std::vector<bool>(16, false));
                cc.tiles[tname].add_word("SLICEC.K1.INIT", std::vector<bool>(16, false));
            } else if (ci->type == id_TRELLIS_IO) {
                write_io(ci);
            } else if (ci->type == id_OSCH) {
                std::string freq = str_or_default(ci->params, id_NOM_FREQ, "2.08");
                cc.tiles[ctx->get_tile_by_type("CFG1")].add_enum("OSCH.MODE", "OSCH");
                cc.tiles[ctx->get_tile_by_type("CFG1")].add_enum("OSCH.NOM_FREQ", freq);
            }
        }
    }
};
} // namespace

void write_bitstream(Context *ctx, std::string text_config_file)
{
    MachXO2Bitgen bitgen(ctx);
    bitgen.run();

    // Configure chip
    if (!text_config_file.empty()) {
        std::ofstream out_config(text_config_file);
        out_config << bitgen.cc;
    }
}

NEXTPNR_NAMESPACE_END
