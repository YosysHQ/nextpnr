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

        if (prefix2 == "G_" || prefix7 == "BRANCH_")
            return basename;

        if (prefix2 == "L_" || prefix2 == "R_") {
            if (loc.x == 0 || loc.x == (ctx->getGridDimX() - 1))
                return "G_" + basename.substr(2);
            return basename;
        }
        if (prefix2 == "U_" || prefix2 == "D_") {
            // We needded to keep U_ and D_ prefixes to generate the routing
            // graph connections properly, but in truth they are not relevant
            // outside of the center row of tiles as far as the database is
            // concerned. So convert U_/D_ prefixes back to G_ if not in the
            // center row.
            if (ctx->is_spine_row(loc.y))
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

    inline int chtohex(char c)
    {
        static const std::string hex = "0123456789ABCDEF";
        return hex.find(std::toupper(c));
    }

    std::vector<bool> parse_init_str(const Property &p, int length, const char *cellname)
    {
        // Parse a string that may be binary or hex
        std::vector<bool> result;
        result.resize(length, false);
        if (p.is_string) {
            std::string str = p.as_string();
            NPNR_ASSERT(str.substr(0, 2) == "0x");
            // Lattice style hex string
            if (int(str.length()) > (2 + ((length + 3) / 4)))
                log_error("hex string value too long, expected up to %d chars and found %d.\n",
                          (2 + ((length + 3) / 4)), int(str.length()));
            for (int i = 0; i < int(str.length()) - 2; i++) {
                char c = str.at((str.size() - i) - 1);
                int nibble = chtohex(c);
                if (nibble == (int)std::string::npos)
                    log_error("hex string has invalid char '%c' at position %d.\n", c, i);
                result.at(i * 4) = nibble & 0x1;
                if (i * 4 + 1 < length)
                    result.at(i * 4 + 1) = nibble & 0x2;
                if (i * 4 + 2 < length)
                    result.at(i * 4 + 2) = nibble & 0x4;
                if (i * 4 + 3 < length)
                    result.at(i * 4 + 3) = nibble & 0x8;
            }
        } else {
            result = p.as_bits();
            result.resize(length, false);
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

    // Get the PIC tile corresponding to a PIO bel
    std::string get_pic_tile(BelId bel)
    {
        static const std::set<std::string> pio_t = {"PIC_T0", "PIC_T0_256", "PIC_TS0"};
        static const std::set<std::string> pio_b = {"PIC_B0", "PIC_B0_256", "PIC_BS0_256"};
        static const std::set<std::string> pio_l = {"PIC_L0",       "PIC_L1",        "PIC_L2",        "PIC_L3",
                                                    "PIC_LS0",      "PIC_L0_VREF3",  "PIC_L0_VREF4",  "PIC_L0_VREF5",
                                                    "PIC_L1_VREF3", "PIC_L1_VREF4",  "PIC_L1_VREF5",  "PIC_L2_VREF4",
                                                    "PIC_L2_VREF5", "PIC_L3_VREF4",  "PIC_L3_VREF5",  "LLC0PIC",
                                                    "LLC1PIC",      "LLC0PIC_VREF3", "LLC3PIC_VREF3", "ULC3PIC"};
        static const std::set<std::string> pio_r = {"PIC_R0",      "PIC_R1",   "PIC_RS0",  "PIC_R0_256", "PIC_R1_640",
                                                    "PIC_RS0_256", "LRC1PIC1", "LRC1PIC2", "URC1PIC"};

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

    // Get the list of tiles corresponding to a blockram
    std::vector<std::string> get_bram_tiles(BelId bel)
    {
        std::vector<std::string> tiles;
        Loc loc = ctx->getBelLocation(bel);

        static const std::set<std::string> ebr0 = {"EBR0", "EBR0_END", "EBR0_10K", "EBR0_END_10K"};
        static const std::set<std::string> ebr1 = {"EBR1", "EBR1_10K"};
        static const std::set<std::string> ebr2 = {"EBR2", "EBR2_END", "EBR2_10K", "EBR2_END_10K"};
        tiles.push_back(ctx->get_tile_by_type_loc(loc.y, loc.x, ebr0));
        tiles.push_back(ctx->get_tile_by_type_loc(loc.y, loc.x + 1, ebr1));
        tiles.push_back(ctx->get_tile_by_type_loc(loc.y, loc.x + 2, ebr2));
        static const std::set<std::string> cib_ebr0 = {
                "CIB_EBR0",           "CIB_EBR0_10K",  "CIB_EBR0_END0",      "CIB_EBR0_END0_10K",  "CIB_EBR0_END0_DLL3",
                "CIB_EBR0_END0_DLL5", "CIB_EBR0_END1", "CIB_EBR0_END2_DLL3", "CIB_EBR0_END2_DLL45"};
        static const std::set<std::string> cib_ebr1 = {"CIB_EBR1", "CIB_EBR1_10K"};
        static const std::set<std::string> cib_ebr2 = {"CIB_EBR2",      "CIB_EBR2_10K",      "CIB_EBR2_END0",
                                                       "CIB_EBR2_END1", "CIB_EBR2_END1_10K", "CIB_EBR2_END1_SP"};
        tiles.push_back(ctx->get_tile_by_type_loc(loc.y, loc.x, cib_ebr0));
        tiles.push_back(ctx->get_tile_by_type_loc(loc.y, loc.x + 1, cib_ebr1));
        tiles.push_back(ctx->get_tile_by_type_loc(loc.y, loc.x + 2, cib_ebr2));
        return tiles;
    }

    // Get the list of tiles corresponding to a PLL
    std::vector<std::string> get_pll_tiles(BelId bel)
    {
        std::string name = ctx->tile_info(bel)->bel_data[bel.index].name.get();
        std::vector<std::string> tiles;
        Loc loc = ctx->getBelLocation(bel);

        if (name == "LPLL") {
            tiles.push_back(ctx->get_tile_by_type_loc(loc.y - 1, loc.x - 1, "GPLL_L0"));
        } else if (name == "RPLL") {
            tiles.push_back(ctx->get_tile_by_type_loc(loc.y + 1, loc.x - 1, "GPLL_R0"));
        } else {
            NPNR_ASSERT_FALSE_STR("bad PLL loc " + name);
        }
        return tiles;
    }

    void set_pip(ChipConfig &cc, PipId pip)
    {
        std::string tile = ctx->get_pip_tilename(pip);
        std::string tile_type =
                ctx->chip_info->tiletype_names[ctx->tile_info(pip)->pip_data[pip.index].tile_type].get();
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
            cc.tiles[tname].add_enum("CLK2.CLKMUX", wckmux);
        }
    }

    void write_ff(CellInfo *ci)
    {
        BelId bel = ci->bel;
        std::string tname = ctx->get_tile_by_type_loc(bel.location.y, bel.location.x, "PLC");
        int z = ctx->tile_info(bel)->bel_data[bel.index].z >> Arch::lc_idx_shift;
        std::string slice = std::string("SLICE") + "ABCD"[z / 2];
        std::string lc = std::to_string(z % 2);

        cc.tiles[tname].add_enum(slice + ".GSR", str_or_default(ci->params, id_GSR, "ENABLED"));
        cc.tiles[tname].add_enum(slice + ".REGMODE", str_or_default(ci->params, id_REGMODE, "FF"));
        cc.tiles[tname].add_enum(slice + ".REG" + lc + ".SD", intstr_or_default(ci->params, id_SD, "0"));
        cc.tiles[tname].add_enum(slice + ".REG" + lc + ".REGSET", str_or_default(ci->params, id_REGSET, "RESET"));

        cc.tiles[tname].add_enum(slice + ".CEMUX", str_or_default(ci->params, id_CEMUX, "1"));

        std::string lsr = std::string("LSR") + "0123"[z / 2];
        if (ci->getPort(id_LSR)) {
            cc.tiles[tname].add_enum(lsr + ".LSRMUX", str_or_default(ci->params, id_LSRMUX, "LSR"));
            cc.tiles[tname].add_enum(lsr + ".SRMODE", str_or_default(ci->params, id_SRMODE, "LSR_OVER_CE"));
            cc.tiles[tname].add_enum(lsr + ".LSRONMUX", str_or_default(ci->params, id_LSRONMUX, "LSRMUX"));
        } else {
            cc.tiles[tname].add_enum(lsr + ".LSRONMUX", "0");
        }
        if (ci->getPort(id_CLK)) {
            std::string clk = std::string("CLK") + "0123"[z / 2];
            cc.tiles[tname].add_enum(clk + ".CLKMUX", str_or_default(ci->params, id_CLKMUX, "0"));
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

    void write_dcc(CellInfo *ci)
    {
        static const std::set<std::string> dcc = {"CENTERB", "CENTER4", "CENTER9"};
        const NetInfo *cen = ci->getPort(id_CE);
        if (cen != nullptr) {
            std::string belname = ctx->tile_info(ci->bel)->bel_data[ci->bel.index].name.get();
            std::string dcc_tile = ctx->get_tile_by_type_loc(ci->bel.location.y - 2, ci->bel.location.x, dcc);
            cc.tiles[dcc_tile].add_enum(belname + ".MODE", "DCCA");
        }
    }

    void write_bram(CellInfo *ci)
    {
        TileGroup tg;
        tg.tiles = get_bram_tiles(ci->bel);
        std::string ebr = "EBR";

        if (ci->ramInfo.is_pdp) {
            tg.config.add_enum(ebr + ".MODE", "PDPW8KC");
            tg.config.add_enum(ebr + ".PDPW8KC.DATA_WIDTH_R", intstr_or_default(ci->params, id_DATA_WIDTH_B, "18"));
            tg.config.add_enum(ebr + ".FIFO8KB.DATA_WIDTH_W", "18"); // default for PDPW8KC
        } else {
            tg.config.add_enum(ebr + ".MODE", "DP8KC");
            tg.config.add_enum(ebr + ".DP8KC.DATA_WIDTH_A", intstr_or_default(ci->params, id_DATA_WIDTH_A, "18"));
            tg.config.add_enum(ebr + ".DP8KC.DATA_WIDTH_B", intstr_or_default(ci->params, id_DATA_WIDTH_B, "18"));
            tg.config.add_enum(ebr + ".DP8KC.WRITEMODE_A", str_or_default(ci->params, id_WRITEMODE_A, "NORMAL"));
            tg.config.add_enum(ebr + ".DP8KC.WRITEMODE_B", str_or_default(ci->params, id_WRITEMODE_B, "NORMAL"));
        }

        auto csd_a = str_to_bitvector(str_or_default(ci->params, id_CSDECODE_A, "0b000"), 3),
             csd_b = str_to_bitvector(str_or_default(ci->params, id_CSDECODE_B, "0b000"), 3);

        tg.config.add_enum(ebr + ".REGMODE_A", str_or_default(ci->params, id_REGMODE_A, "NOREG"));
        tg.config.add_enum(ebr + ".REGMODE_B", str_or_default(ci->params, id_REGMODE_B, "NOREG"));

        tg.config.add_enum(ebr + ".RESETMODE", str_or_default(ci->params, id_RESETMODE, "SYNC"));
        tg.config.add_enum(ebr + ".ASYNC_RESET_RELEASE", str_or_default(ci->params, id_ASYNC_RESET_RELEASE, "SYNC"));
        tg.config.add_enum(ebr + ".GSR", str_or_default(ci->params, id_GSR, "DISABLED"));

        tg.config.add_word(ebr + ".WID", int_to_bitvector(int_or_default(ci->attrs, id_WID, 0), 9));

        // Invert CSDECODE bits to emulate inversion muxes on CSA/CSB signals
        for (auto &port : {std::make_pair("CSA", std::ref(csd_a)), std::make_pair("CSB", std::ref(csd_b))}) {
            for (int bit = 0; bit < 3; bit++) {
                std::string sig = port.first + std::to_string(bit);
                if (str_or_default(ci->params, ctx->id(sig + "MUX"), sig) == "INV")
                    port.second.at(bit) = !port.second.at(bit);
            }
        }
        tg.config.add_enum(ebr + ".CLKAMUX", str_or_default(ci->params, id_CLKAMUX, "CLKA"));
        tg.config.add_enum(ebr + ".CLKBMUX", str_or_default(ci->params, id_CLKBMUX, "CLKB"));

        tg.config.add_enum(ebr + ".RSTAMUX", str_or_default(ci->params, id_RSTAMUX, "RSTA"));
        tg.config.add_enum(ebr + ".RSTBMUX", str_or_default(ci->params, id_RSTBMUX, "RSTB"));
        if (!ci->ramInfo.is_pdp) {
            tg.config.add_enum(ebr + ".WEAMUX", str_or_default(ci->params, id_WEAMUX, "WEA"));
            tg.config.add_enum(ebr + ".WEBMUX", str_or_default(ci->params, id_WEBMUX, "WEB"));
        }
        tg.config.add_enum(ebr + ".CEAMUX", str_or_default(ci->params, id_CEAMUX, "CEA"));
        tg.config.add_enum(ebr + ".CEBMUX", str_or_default(ci->params, id_CEBMUX, "CEB"));
        tg.config.add_enum(ebr + ".OCEAMUX", str_or_default(ci->params, id_OCEAMUX, "OCEA"));
        tg.config.add_enum(ebr + ".OCEBMUX", str_or_default(ci->params, id_OCEBMUX, "OCEB"));

        std::reverse(csd_a.begin(), csd_a.end());
        std::reverse(csd_b.begin(), csd_b.end());

        tg.config.add_word(ebr + ".CSDECODE_A", csd_a);
        tg.config.add_word(ebr + ".CSDECODE_B", csd_b);

        std::vector<uint16_t> init_data;
        init_data.resize(1024, 0x0);
        // INIT_00 .. INIT_1F
        for (int i = 0; i <= 0x1F; i++) {
            IdString param = ctx->idf("INITVAL_%02X", i);
            auto value = parse_init_str(get_or_default(ci->params, param, Property(0)), 320, ci->name.c_str(ctx));
            for (int j = 0; j < 16; j++) {
                // INIT parameter consists of 16 18-bit words with 2-bit padding
                int ofs = 20 * j;
                for (int k = 0; k < 18; k++) {
                    if (value.at(ofs + k))
                        init_data.at(i * 32 + j * 2 + (k / 9)) |= (1 << (k % 9));
                }
            }
        }
        int wid = int_or_default(ci->attrs, id_WID, 0);
        NPNR_ASSERT(!cc.bram_data.count(wid));
        cc.bram_data[wid] = init_data;
        cc.tilegroups.push_back(tg);
    }

    void write_pll(CellInfo *ci)
    {
        TileGroup tg;
        tg.tiles = get_pll_tiles(ci->bel);

        tg.config.add_enum("MODE", "EHXPLLJ");

        tg.config.add_word("CLKI_DIV", int_to_bitvector(int_or_default(ci->params, id_CLKI_DIV, 1) - 1, 7));
        tg.config.add_word("CLKFB_DIV", int_to_bitvector(int_or_default(ci->params, id_CLKFB_DIV, 1) - 1, 7));

        tg.config.add_enum("CLKOP_ENABLE", str_or_default(ci->params, id_CLKOP_ENABLE, "ENABLED"));
        tg.config.add_enum("CLKOS_ENABLE", str_or_default(ci->params, id_CLKOS_ENABLE, "ENABLED"));
        tg.config.add_enum("CLKOS2_ENABLE", str_or_default(ci->params, id_CLKOS2_ENABLE, "ENABLED"));
        tg.config.add_enum("CLKOS3_ENABLE", str_or_default(ci->params, id_CLKOS3_ENABLE, "ENABLED"));

        for (std::string out : {"CLKOP", "CLKOS", "CLKOS2", "CLKOS3"}) {
            tg.config.add_word(out + "_DIV",
                               int_to_bitvector(int_or_default(ci->params, ctx->id(out + "_DIV"), 8) - 1, 7));
            tg.config.add_word(out + "_CPHASE",
                               int_to_bitvector(int_or_default(ci->params, ctx->id(out + "_CPHASE"), 0), 7));
            tg.config.add_word(out + "_FPHASE",
                               int_to_bitvector(int_or_default(ci->params, ctx->id(out + "_FPHASE"), 0), 3));
        }

        tg.config.add_enum("FEEDBK_PATH", str_or_default(ci->params, id_FEEDBK_PATH, "CLKOP"));
        tg.config.add_enum("CLKOP_TRIM_POL", str_or_default(ci->params, id_CLKOP_TRIM_POL, "RISING"));

        tg.config.add_enum("CLKOP_TRIM_DELAY", intstr_or_default(ci->params, id_CLKOP_TRIM_DELAY, "0"));

        tg.config.add_enum("CLKOS_TRIM_POL", str_or_default(ci->params, id_CLKOS_TRIM_POL, "RISING"));

        tg.config.add_enum("CLKOS_TRIM_DELAY", intstr_or_default(ci->params, id_CLKOS_TRIM_DELAY, "0"));

        tg.config.add_enum("VCO_BYPASS_A0", str_or_default(ci->params, id_VCO_BYPASS_A0, "DISABLED"));
        tg.config.add_enum("VCO_BYPASS_B0", str_or_default(ci->params, id_VCO_BYPASS_B0, "DISABLED"));
        tg.config.add_enum("VCO_BYPASS_C0", str_or_default(ci->params, id_VCO_BYPASS_C0, "DISABLED"));
        tg.config.add_enum("VCO_BYPASS_D0", str_or_default(ci->params, id_VCO_BYPASS_D0, "DISABLED"));

        tg.config.add_word("PREDIVIDER_MUXA1", int_to_bitvector(int_or_default(ci->attrs, id_PREDIVIDER_MUXA1, 0), 2));
        tg.config.add_word("PREDIVIDER_MUXB1", int_to_bitvector(int_or_default(ci->attrs, id_PREDIVIDER_MUXB1, 0), 2));
        tg.config.add_word("PREDIVIDER_MUXC1", int_to_bitvector(int_or_default(ci->attrs, id_PREDIVIDER_MUXC1, 0), 2));
        tg.config.add_word("PREDIVIDER_MUXD1", int_to_bitvector(int_or_default(ci->attrs, id_PREDIVIDER_MUXD1, 0), 2));
        tg.config.add_enum("OUTDIVIDER_MUXA2",
                           str_or_default(ci->params, id_OUTDIVIDER_MUXA2, ci->getPort(id_CLKOP) ? "DIVA" : "REFCLK"));
        tg.config.add_enum("OUTDIVIDER_MUXB2",
                           str_or_default(ci->params, id_OUTDIVIDER_MUXB2, ci->getPort(id_CLKOP) ? "DIVB" : "REFCLK"));
        tg.config.add_enum("OUTDIVIDER_MUXC2",
                           str_or_default(ci->params, id_OUTDIVIDER_MUXC2, ci->getPort(id_CLKOP) ? "DIVC" : "REFCLK"));
        tg.config.add_enum("OUTDIVIDER_MUXD2",
                           str_or_default(ci->params, id_OUTDIVIDER_MUXD2, ci->getPort(id_CLKOP) ? "DIVD" : "REFCLK"));

        tg.config.add_word("PLL_LOCK_MODE", int_to_bitvector(int_or_default(ci->params, id_PLL_LOCK_MODE, 0), 3));

        tg.config.add_enum("STDBY_ENABLE", str_or_default(ci->params, id_STDBY_ENABLE, "DISABLED"));
        tg.config.add_enum("REFIN_RESET", str_or_default(ci->params, id_REFIN_RESET, "DISABLED"));
        tg.config.add_enum("SYNC_ENABLE", str_or_default(ci->params, id_SYNC_ENABLE, "DISABLED"));
        tg.config.add_enum("INT_LOCK_STICKY", str_or_default(ci->params, id_INT_LOCK_STICKY, "ENABLED"));
        tg.config.add_enum("DPHASE_SOURCE", str_or_default(ci->params, id_DPHASE_SOURCE, "DISABLED"));
        tg.config.add_enum("PLLRST_ENA", str_or_default(ci->params, id_PLLRST_ENA, "DISABLED"));
        tg.config.add_enum("INTFB_WAKE", str_or_default(ci->params, id_INTFB_WAKE, "DISABLED"));
        tg.config.add_enum("PLLRST_ENA", str_or_default(ci->params, id_PLLRST_ENA, "DISABLED"));
        tg.config.add_enum("MRST_ENA", str_or_default(ci->params, id_MRST_ENA, "DISABLED"));
        tg.config.add_enum("DCRST_ENA", str_or_default(ci->params, id_DCRST_ENA, "DISABLED"));
        tg.config.add_enum("DDRST_ENA", str_or_default(ci->params, id_DDRST_ENA, "DISABLED"));

        tg.config.add_word("KVCO", int_to_bitvector(int_or_default(ci->attrs, id_KVCO, 0), 3));
        tg.config.add_word("LPF_CAPACITOR", int_to_bitvector(int_or_default(ci->attrs, id_LPF_CAPACITOR, 0), 2));
        tg.config.add_word("LPF_RESISTOR", int_to_bitvector(int_or_default(ci->attrs, id_LPF_RESISTOR, 0), 7));
        tg.config.add_word("ICP_CURRENT", int_to_bitvector(int_or_default(ci->attrs, id_ICP_CURRENT, 0), 5));
        tg.config.add_word("FREQ_LOCK_ACCURACY",
                           int_to_bitvector(int_or_default(ci->attrs, id_FREQ_LOCK_ACCURACY, 0), 2));

        tg.config.add_word("GMC_GAIN", int_to_bitvector(int_or_default(ci->attrs, id_GMC_GAIN, 0), 3));
        tg.config.add_word("GMC_TEST", int_to_bitvector(int_or_default(ci->attrs, id_GMC_TEST, 14), 4));
        tg.config.add_word("MFG1_TEST", int_to_bitvector(int_or_default(ci->attrs, id_MFG1_TEST, 0), 3));
        tg.config.add_word("MFG2_TEST", int_to_bitvector(int_or_default(ci->attrs, id_MFG2_TEST, 0), 3));

        tg.config.add_word("MFG_FORCE_VFILTER",
                           int_to_bitvector(int_or_default(ci->attrs, id_MFG_FORCE_VFILTER, 0), 1));
        tg.config.add_word("MFG_ICP_TEST", int_to_bitvector(int_or_default(ci->attrs, id_MFG_ICP_TEST, 0), 1));
        tg.config.add_word("MFG_EN_UP", int_to_bitvector(int_or_default(ci->attrs, id_MFG_EN_UP, 0), 1));
        tg.config.add_word("MFG_FLOAT_ICP", int_to_bitvector(int_or_default(ci->attrs, id_MFG_FLOAT_ICP, 0), 1));
        tg.config.add_word("MFG_GMC_PRESET", int_to_bitvector(int_or_default(ci->attrs, id_MFG_GMC_PRESET, 0), 1));
        tg.config.add_word("MFG_LF_PRESET", int_to_bitvector(int_or_default(ci->attrs, id_MFG_LF_PRESET, 0), 1));
        tg.config.add_word("MFG_GMC_RESET", int_to_bitvector(int_or_default(ci->attrs, id_MFG_GMC_RESET, 0), 1));
        tg.config.add_word("MFG_LF_RESET", int_to_bitvector(int_or_default(ci->attrs, id_MFG_LF_RESET, 0), 1));
        tg.config.add_word("MFG_LF_RESGRND", int_to_bitvector(int_or_default(ci->attrs, id_MFG_LF_RESGRND, 0), 1));
        tg.config.add_word("MFG_GMCREF_SEL", int_to_bitvector(int_or_default(ci->attrs, id_MFG_GMCREF_SEL, 0), 2));
        tg.config.add_word("MFG_ENABLE_FILTEROPAMP",
                           int_to_bitvector(int_or_default(ci->attrs, id_MFG_ENABLE_FILTEROPAMP, 0), 1));

        tg.config.add_enum("CLOCK_ENABLE_PORTS", str_or_default(ci->params, id_DDRST_ENA, "DISABLED"));
        tg.config.add_enum("PLL_EXPERT", str_or_default(ci->params, id_PLL_EXPERT, "DISABLED"));
        tg.config.add_enum("PLL_USE_WB", str_or_default(ci->params, id_PLL_USE_WB, "DISABLED"));

        tg.config.add_enum("FRACN_ENABLE", str_or_default(ci->params, id_FRACN_ENABLE, "DISABLED"));
        tg.config.add_word("FRACN_DIV", int_to_bitvector(int_or_default(ci->attrs, id_FRACN_DIV, 0), 16));
        tg.config.add_word("FRACN_ORDER", int_to_bitvector(int_or_default(ci->attrs, id_FRACN_ORDER, 0), 2));
        cc.tilegroups.push_back(tg);
    }

    void run()
    {
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
            } else if (ci->type == id_OSCJ) {
                std::string freq = str_or_default(ci->params, id_NOM_FREQ, "2.08");
                cc.tiles[ctx->get_tile_by_type("CFG1")].add_enum("OSCJ.MODE", "OSCJ");
                cc.tiles[ctx->get_tile_by_type("CFG1")].add_enum("OSCJ.NOM_FREQ", freq);
            } else if (ci->type == id_DCCA) {
                write_dcc(ci);
            } else if (ci->type == id_DP8KC) {
                write_bram(ci);
            } else if (ci->type == id_EHXPLLJ) {
                write_pll(ci);
            } else if (ci->type == id_GSR) {
                cc.tiles[ctx->get_tile_by_type("CFG0")].add_enum("GSR.GSRMODE",
                                                                 str_or_default(ci->params, id_MODE, "ACTIVE_LOW"));
                cc.tiles[ctx->get_tile_by_type("CFG0")].add_enum("GSR.SYNCMODE",
                                                                 str_or_default(ci->params, id_SYNCMODE, "ASYNC"));
            } else if (ci->type == id_JTAGF) {
                cc.tiles[ctx->get_tile_by_type("CFG0")].add_enum("JTAG.ER1",
                                                                 str_or_default(ci->params, id_ER1, "ENABLED"));
                cc.tiles[ctx->get_tile_by_type("CFG0")].add_enum("JTAG.ER2",
                                                                 str_or_default(ci->params, id_ER2, "ENABLED"));
            } else if (ci->type == id_TSALL) {
                cc.tiles[ctx->get_tile_by_type("CFG0")].add_enum("TSALL.MODE",
                                                                 str_or_default(ci->params, id_MODE, "TSALL"));
                cc.tiles[ctx->get_tile_by_type("CFG0")].add_enum("TSALL.TSALL",
                                                                 str_or_default(ci->params, id_TSALL, "TSALL"));
            } else if (ci->type == id_START) {
                cc.tiles[ctx->get_tile_by_type("CIB_CFG0")].add_enum(
                        "START.STARTCLK", str_or_default(ci->params, id_STARTCLK, "STARTCLK"));
            } else if (ci->type == id_CLKDIVC) {
                Loc loc = ctx->getBelLocation(ci->bel);
                bool t = loc.y < 2;
                std::string clkdiv = (t ? "T" : "B") + std::string("CLKDIV") + std::to_string(loc.z);
                std::string tile = ctx->get_tile_by_type(t ? "PIC_T_DUMMY_VIQ" : "PIC_B_DUMMY_VIQ_VREF");
                cc.tiles[tile].add_enum(clkdiv + ".DIV", str_or_default(ci->params, id_DIV, "2.0"));
                cc.tiles[tile].add_enum(clkdiv + ".GSR", str_or_default(ci->params, id_GSR, "DISABLED"));
            } else {
                NPNR_ASSERT_FALSE("unsupported cell type");
            }
        }

        // Add some SYSCONFIG settings
        const std::string prefix = "arch.sysconfig.";
        for (auto &setting : ctx->settings) {
            std::string key = setting.first.str(ctx);
            if (key.substr(0, prefix.length()) != prefix)
                continue;
            key = key.substr(prefix.length());
            std::string value = setting.second.as_string();
            if (key == "BACKGROUND_RECONFIG" || key == "ENABLE_TRANSFR" || key == "SDM_PORT") {
                cc.tiles[ctx->get_tile_by_type("CFG0")].add_enum("SYSCONFIG." + key, value);
            } else if (key == "I2C_PORT" || key == "MASTER_SPI_PORT" || key == "SLAVE_SPI_PORT") {
                cc.tiles[ctx->get_tile_by_type("CFG1")].add_enum("SYSCONFIG." + key, value);
            } else {
                cc.sysconfig[key] = value;
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
