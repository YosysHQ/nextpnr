/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@symbioticeda.com>
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
#include <cctype>
#include <vector>
#include "cells.h"
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

inline TileType tile_at(const Context *ctx, int x, int y)
{
    return ctx->chip_info->tile_grid[y * ctx->chip_info->width + x];
}

const ConfigEntryPOD &find_config(const TileInfoPOD &tile, const std::string &name)
{
    for (int i = 0; i < tile.num_config_entries; i++) {
        if (std::string(tile.entries[i].name.get()) == name) {
            return tile.entries[i];
        }
    }
    NPNR_ASSERT_FALSE_STR("unable to find config bit " + name);
}

std::tuple<int8_t, int8_t, int8_t> get_ieren(const BitstreamInfoPOD &bi, int8_t x, int8_t y, int8_t z)
{
    for (int i = 0; i < bi.num_ierens; i++) {
        auto ie = bi.ierens[i];
        if (ie.iox == x && ie.ioy == y && ie.ioz == z) {
            return std::make_tuple(ie.ierx, ie.iery, ie.ierz);
        }
    }
    // No pin at this location
    return std::make_tuple(-1, -1, -1);
};

bool get_config(const TileInfoPOD &ti, std::vector<std::vector<int8_t>> &tile_cfg, const std::string &name,
                int index = -1)
{
    const ConfigEntryPOD &cfg = find_config(ti, name);
    if (index == -1) {
        for (int i = 0; i < cfg.num_bits; i++) {
            return tile_cfg.at(cfg.bits[i].row).at(cfg.bits[i].col);
        }
    } else {
        return tile_cfg.at(cfg.bits[index].row).at(cfg.bits[index].col);
    }
    return false;
}

void set_config(const TileInfoPOD &ti, std::vector<std::vector<int8_t>> &tile_cfg, const std::string &name, bool value,
                int index = -1)
{
    const ConfigEntryPOD &cfg = find_config(ti, name);
    if (index == -1) {
        for (int i = 0; i < cfg.num_bits; i++) {
            int8_t &cbit = tile_cfg.at(cfg.bits[i].row).at(cfg.bits[i].col);
            if (cbit && !value)
                log_error("clearing already set config bit %s\n", name.c_str());
            cbit = value;
        }
    } else {
        int8_t &cbit = tile_cfg.at(cfg.bits[index].row).at(cfg.bits[index].col);
        cbit = value;
        if (cbit && !value)
            log_error("clearing already set config bit %s[%d]\n", name.c_str(), index);
    }
}

// Set an IE_{EN,REN} logical bit in a tile config. Logical means enabled.
// On {HX,LP}1K devices these bits are active low, so we need to invert them.
void set_ie_bit_logical(const Context *ctx, const TileInfoPOD &ti, std::vector<std::vector<int8_t>> &tile_cfg,
                        const std::string &name, bool value)
{
    if (ctx->args.type == ArchArgs::LP1K || ctx->args.type == ArchArgs::HX1K) {
        set_config(ti, tile_cfg, name, !value);
    } else {
        set_config(ti, tile_cfg, name, value);
    }
}

int get_param_or_def(const CellInfo *cell, const IdString param, int defval = 0)
{
    auto found = cell->params.find(param);
    if (found != cell->params.end())
        return std::stoi(found->second);
    else
        return defval;
}

std::string get_param_str_or_def(const CellInfo *cell, const IdString param, std::string defval = "")
{
    auto found = cell->params.find(param);
    if (found != cell->params.end())
        return found->second;
    else
        return defval;
}

char get_hexdigit(int i) { return std::string("0123456789ABCDEF").at(i); }

static const BelConfigPOD &get_ec_config(const ChipInfoPOD *chip, BelId bel)
{
    for (int i = 0; i < chip->num_belcfgs; i++) {
        if (chip->bel_config[i].bel_index == bel.index)
            return chip->bel_config[i];
    }
    NPNR_ASSERT_FALSE("failed to find bel config");
}

typedef std::vector<std::vector<std::vector<std::vector<int8_t>>>> chipconfig_t;

static void set_ec_cbit(chipconfig_t &config, const Context *ctx, const BelConfigPOD &cell_cbits, std::string name,
                        bool value, std::string prefix)
{
    const ChipInfoPOD *chip = ctx->chip_info;

    for (int i = 0; i < cell_cbits.num_entries; i++) {
        const auto &cbit = cell_cbits.entries[i];
        if (cbit.entry_name.get() == name) {
            const auto &ti = chip->bits_info->tiles_nonrouting[tile_at(ctx, cbit.x, cbit.y)];
            set_config(ti, config.at(cbit.y).at(cbit.x), prefix + cbit.cbit_name.get(), value);
            return;
        }
    }
    NPNR_ASSERT_FALSE_STR("failed to config extra cell config bit " + name);
}

void configure_extra_cell(chipconfig_t &config, const Context *ctx, CellInfo *cell,
                          const std::vector<std::pair<std::string, int>> &params, bool string_style, std::string prefix)
{
    const ChipInfoPOD *chip = ctx->chip_info;
    const auto &bc = get_ec_config(chip, cell->bel);
    for (auto p : params) {
        std::vector<bool> value;
        if (string_style) {
            // Lattice's weird string style params, not sure if
            // prefixes other than 0b should be supported, only 0b features in docs
            std::string raw = get_param_str_or_def(cell, ctx->id(p.first), "0b0");
            if (raw.substr(0, 2) != "0b")
                log_error("expected configuration string starting with '0b' for parameter '%s' on cell '%s'\n",
                          p.first.c_str(), cell->name.c_str(ctx));
            raw = raw.substr(2);
            value.resize(raw.length());
            for (int i = 0; i < (int)raw.length(); i++) {
                if (raw[i] == '1') {
                    value[(raw.length() - 1) - i] = 1;
                } else {
                    assert(raw[i] == '0');
                    value[(raw.length() - 1) - i] = 0;
                }
            }
        } else {
            int ival;
            try {
                ival = get_param_or_def(cell, ctx->id(p.first), 0);
            } catch (std::invalid_argument &e) {
                log_error("expected numeric value for parameter '%s' on cell '%s'\n", p.first.c_str(),
                          cell->name.c_str(ctx));
            }

            for (int i = 0; i < p.second; i++)
                value.push_back((ival >> i) & 0x1);
        }

        value.resize(p.second);
        if (p.second == 1) {
            set_ec_cbit(config, ctx, bc, p.first, value.at(0), prefix);
        } else {
            for (int i = 0; i < p.second; i++) {
                set_ec_cbit(config, ctx, bc, p.first + "_" + std::to_string(i), value.at(i), prefix);
            }
        }
    }
}

std::string tagTileType(TileType &tile)
{
    if (tile == TILE_NONE)
        return "";
    switch (tile) {
    case TILE_LOGIC:
        return ".logic_tile";
        break;
    case TILE_IO:
        return ".io_tile";
        break;
    case TILE_RAMB:
        return ".ramb_tile";
        break;
    case TILE_RAMT:
        return ".ramt_tile";
        break;
    case TILE_DSP0:
        return ".dsp0_tile";
        break;
    case TILE_DSP1:
        return ".dsp1_tile";
        break;
    case TILE_DSP2:
        return ".dsp2_tile";
        break;
    case TILE_DSP3:
        return ".dsp3_tile";
        break;
    case TILE_IPCON:
        return ".ipcon_tile";
        break;
    default:
        NPNR_ASSERT(false);
    }
}

static BelPin get_one_bel_pin(const Context *ctx, WireId wire)
{
    auto pins = ctx->getWireBelPins(wire);
    NPNR_ASSERT(pins.begin() != pins.end());
    return *pins.begin();
}

// Permute LUT init value given map (LUT input -> ext input)
unsigned permute_lut(unsigned orig_init, const std::unordered_map<int, int> &input_permute)
{
    unsigned new_init = 0;

    for (int i = 0; i < 16; i++) {
        int permute_address = 0;
        for (int j = 0; j < 4; j++) {
            if ((i >> j) & 0x1)
                permute_address |= (1 << input_permute.at(j));
        }
        if ((orig_init >> i) & 0x1) {
            new_init |= (1 << permute_address);
        }
    }

    return new_init;
}

void write_asc(const Context *ctx, std::ostream &out)
{

    static const std::vector<int> lut_perm = {
            4, 14, 15, 5, 6, 16, 17, 7, 3, 13, 12, 2, 1, 11, 10, 0,
    };

    // [y][x][row][col]
    const ChipInfoPOD &ci = *ctx->chip_info;
    const BitstreamInfoPOD &bi = *ci.bits_info;
    chipconfig_t config;
    config.resize(ci.height);
    for (int y = 0; y < ci.height; y++) {
        config.at(y).resize(ci.width);
        for (int x = 0; x < ci.width; x++) {
            TileType tile = tile_at(ctx, x, y);
            int rows = bi.tiles_nonrouting[tile].rows;
            int cols = bi.tiles_nonrouting[tile].cols;
            config.at(y).at(x).resize(rows, std::vector<int8_t>(cols));
        }
    }

    std::vector<std::tuple<int, int, int>> extra_bits;

    out << ".comment from next-pnr" << std::endl;

    switch (ctx->args.type) {
    case ArchArgs::LP384:
        out << ".device 384" << std::endl;
        break;
    case ArchArgs::HX1K:
    case ArchArgs::LP1K:
        out << ".device 1k" << std::endl;
        break;
    case ArchArgs::HX8K:
    case ArchArgs::LP8K:
        out << ".device 8k" << std::endl;
        break;
    case ArchArgs::UP5K:
        out << ".device 5k" << std::endl;
        break;
    default:
        NPNR_ASSERT_FALSE("unsupported device type\n");
    }
    // Set pips
    for (auto pip : ctx->getPips()) {
        if (ctx->pip_to_net[pip.index] != nullptr) {
            const PipInfoPOD &pi = ci.pip_data[pip.index];
            const SwitchInfoPOD &swi = bi.switches[pi.switch_index];
            int sw_bel_idx = swi.bel;
            if (sw_bel_idx >= 0) {
                const BelInfoPOD &beli = ci.bel_data[sw_bel_idx];
                const TileInfoPOD &ti = bi.tiles_nonrouting[TILE_LOGIC];
                BelId sw_bel;
                sw_bel.index = sw_bel_idx;
                NPNR_ASSERT(ctx->getBelType(sw_bel) == id_ICESTORM_LC);

                if (ci.wire_data[ctx->getPipDstWire(pip).index].type == WireInfoPOD::WIRE_TYPE_LUTFF_IN_LUT)
                    continue; // Permutation pips
                BelPin output = get_one_bel_pin(ctx, ctx->getPipDstWire(pip));
                NPNR_ASSERT(output.bel == sw_bel && output.pin == id_O);
                unsigned lut_init;

                WireId permWire;
                for (auto permPip : ctx->getPipsUphill(ctx->getPipSrcWire(pip))) {
                    if (ctx->getBoundPipNet(permPip) != nullptr) {
                        permWire = ctx->getPipSrcWire(permPip);
                    }
                }
                NPNR_ASSERT(permWire != WireId());
                std::string dName = ci.wire_data[permWire.index].name.get();

                switch (dName.back()) {
                case '0':
                    lut_init = 2;
                    break;
                case '1':
                    lut_init = 4;
                    break;
                case '2':
                    lut_init = 16;
                    break;
                case '3':
                    lut_init = 256;
                    break;
                default:
                    NPNR_ASSERT_FALSE("bad feedthru LUT input");
                }
                std::vector<bool> lc(20, false);
                for (int i = 0; i < 16; i++) {
                    if ((lut_init >> i) & 0x1)
                        lc.at(lut_perm.at(i)) = true;
                }

                for (int i = 0; i < 20; i++)
                    set_config(ti, config.at(beli.y).at(beli.x), "LC_" + std::to_string(beli.z), lc.at(i), i);
            } else {
                for (int i = 0; i < swi.num_bits; i++) {
                    bool val = (pi.switch_mask & (1 << ((swi.num_bits - 1) - i))) != 0;
                    int8_t &cbit = config.at(swi.y).at(swi.x).at(swi.cbits[i].row).at(swi.cbits[i].col);
                    if (bool(cbit) != 0)
                        NPNR_ASSERT(false);
                    cbit = val;
                }
            }
        }
    }

    // Scan for PLL and collects the affected SB_IOs
    std::unordered_set<Loc> sb_io_used_by_pll_out;
    std::unordered_set<Loc> sb_io_used_by_pll_pad;

    for (auto &cell : ctx->cells) {
        if (cell.second->type != ctx->id("ICESTORM_PLL"))
            continue;

        // Collect all locations matching an PLL output port
        //  note: It doesn't matter if the port is connected or not, or if fabric/global
        //        is used. As long as it's a PLL type for which the port exists, the SB_IO
        //        is not available and must be configured for PLL mode
        const std::vector<IdString> ports = {id_PLLOUT_A, id_PLLOUT_B};
        for (auto &port : ports) {
            // If the output is not enabled in this mode, ignore it
            if (port == id_PLLOUT_B && !is_sb_pll40_dual(ctx, cell.second.get()))
                continue;

            // Get IO Bel that this PLL port goes through by finding sibling
            // Bel driving the same wire via PIN_D_IN_0.
            auto wire = ctx->getBelPinWire(cell.second->bel, port);
            BelId io_bel;
            for (auto pin : ctx->getWireBelPins(wire)) {
                if (pin.pin == id_D_IN_0) {
                    io_bel = pin.bel;
                    break;
                }
            }
            NPNR_ASSERT(io_bel.index != -1);
            auto io_bel_loc = ctx->getBelLocation(io_bel);

            // Mark this SB_IO as being used by a PLL output path
            sb_io_used_by_pll_out.insert(io_bel_loc);

            // If this is a PAD PLL, and this is the 'PLLOUT_A' port, then the same SB_IO is also PAD
            if (port == id_PLLOUT_A && is_sb_pll40_pad(ctx, cell.second.get()))
                sb_io_used_by_pll_pad.insert(io_bel_loc);
        }
    }

    // Set logic cell config
    for (auto &cell : ctx->cells) {

        BelId bel = cell.second.get()->bel;
        if (bel == BelId()) {
            std::cout << "Found unplaced cell " << cell.first.str(ctx) << " while generating bitstream!" << std::endl;
            continue;
        }
        if (cell.second->type == ctx->id("ICESTORM_LC")) {
            const BelInfoPOD &beli = ci.bel_data[bel.index];
            int x = beli.x, y = beli.y, z = beli.z;
            const TileInfoPOD &ti = bi.tiles_nonrouting[TILE_LOGIC];
            unsigned lut_init = get_param_or_def(cell.second.get(), ctx->id("LUT_INIT"));
            bool neg_clk = get_param_or_def(cell.second.get(), ctx->id("NEG_CLK"));
            bool dff_enable = get_param_or_def(cell.second.get(), ctx->id("DFF_ENABLE"));
            bool async_sr = get_param_or_def(cell.second.get(), ctx->id("ASYNC_SR"));
            bool set_noreset = get_param_or_def(cell.second.get(), ctx->id("SET_NORESET"));
            bool carry_enable = get_param_or_def(cell.second.get(), ctx->id("CARRY_ENABLE"));
            std::vector<bool> lc(20, false);

            // Discover permutation
            std::unordered_map<int, int> input_perm;
            std::set<int> unused;
            for (int i = 0; i < 4; i++)
                unused.insert(i);
            for (int i = 0; i < 4; i++) {
                WireId lut_wire = ctx->getBelPinWire(bel, IdString(ID_I0 + i));
                for (auto pip : ctx->getPipsUphill(lut_wire)) {
                    if (ctx->getBoundPipNet(pip) != nullptr) {
                        std::string name = ci.wire_data[ctx->getPipSrcWire(pip).index].name.get();
                        switch (name.back()) {
                        case '0':
                            input_perm[i] = 0;
                            unused.erase(0);
                            break;
                        case '1':
                            input_perm[i] = 1;
                            unused.erase(1);
                            break;
                        case '2':
                            input_perm[i] = 2;
                            unused.erase(2);
                            break;
                        case '3':
                            input_perm[i] = 3;
                            unused.erase(3);
                            break;
                        default:
                            NPNR_ASSERT_FALSE("failed to determine LUT permutation");
                        }
                        break;
                    }
                }
            }
            for (int i = 0; i < 4; i++) {
                if (!input_perm.count(i)) {
                    NPNR_ASSERT(!unused.empty());
                    input_perm[i] = *(unused.begin());
                    unused.erase(input_perm[i]);
                }
            }
            lut_init = permute_lut(lut_init, input_perm);
            for (int i = 0; i < 16; i++) {
                if ((lut_init >> i) & 0x1)
                    lc.at(lut_perm.at(i)) = true;
            }
            lc.at(8) = carry_enable;
            lc.at(9) = dff_enable;
            lc.at(18) = set_noreset;
            lc.at(19) = async_sr;

            for (int i = 0; i < 20; i++)
                set_config(ti, config.at(y).at(x), "LC_" + std::to_string(z), lc.at(i), i);
            if (dff_enable)
                set_config(ti, config.at(y).at(x), "NegClk", neg_clk);

            bool carry_const = get_param_or_def(cell.second.get(), ctx->id("CIN_CONST"));
            bool carry_set = get_param_or_def(cell.second.get(), ctx->id("CIN_SET"));
            if (carry_const) {
                if (!ctx->force)
                    NPNR_ASSERT(z == 0);
                set_config(ti, config.at(y).at(x), "CarryInSet", carry_set);
            }
        } else if (cell.second->type == ctx->id("SB_IO")) {
            const BelInfoPOD &beli = ci.bel_data[bel.index];
            int x = beli.x, y = beli.y, z = beli.z;
            const TileInfoPOD &ti = bi.tiles_nonrouting[TILE_IO];
            unsigned pin_type = get_param_or_def(cell.second.get(), ctx->id("PIN_TYPE"));
            bool neg_trigger = get_param_or_def(cell.second.get(), ctx->id("NEG_TRIGGER"));
            bool pullup = get_param_or_def(cell.second.get(), ctx->id("PULLUP"));
            bool lvds = cell.second->ioInfo.lvds;
            bool used_by_pll_out = sb_io_used_by_pll_out.count(Loc(x, y, z)) > 0;
            bool used_by_pll_pad = sb_io_used_by_pll_pad.count(Loc(x, y, z)) > 0;

            for (int i = used_by_pll_out ? 2 : 0; i < 6; i++) {
                bool val = (pin_type >> i) & 0x01;
                set_config(ti, config.at(y).at(x), "IOB_" + std::to_string(z) + ".PINTYPE_" + std::to_string(i), val);
            }
            set_config(ti, config.at(y).at(x), "NegClk", neg_trigger);

            bool input_en = false;
            if ((ctx->wire_to_net[ctx->getBelPinWire(bel, id_D_IN_0).index] != nullptr) ||
                (ctx->wire_to_net[ctx->getBelPinWire(bel, id_D_IN_1).index] != nullptr)) {
                input_en = true;
            }
            input_en = (input_en & !used_by_pll_out) | used_by_pll_pad;
            input_en |= cell.second->ioInfo.global;

            if (!lvds) {
                auto ieren = get_ieren(bi, x, y, z);
                int iex, iey, iez;
                std::tie(iex, iey, iez) = ieren;
                NPNR_ASSERT(iez != -1);

                if (ctx->args.type == ArchArgs::LP1K || ctx->args.type == ArchArgs::HX1K) {
                    set_config(ti, config.at(iey).at(iex), "IoCtrl.IE_" + std::to_string(iez), !input_en);
                    set_config(ti, config.at(iey).at(iex), "IoCtrl.REN_" + std::to_string(iez), !pullup);
                } else {
                    set_config(ti, config.at(iey).at(iex), "IoCtrl.IE_" + std::to_string(iez), input_en);
                    set_config(ti, config.at(iey).at(iex), "IoCtrl.REN_" + std::to_string(iez), !pullup);
                }

                if (ctx->args.type == ArchArgs::UP5K) {
                    std::string pullup_resistor = "100K";
                    if (cell.second->attrs.count(ctx->id("PULLUP_RESISTOR")))
                        pullup_resistor = cell.second->attrs.at(ctx->id("PULLUP_RESISTOR"));
                    NPNR_ASSERT(pullup_resistor == "100K" || pullup_resistor == "10K" || pullup_resistor == "6P8K" ||
                                pullup_resistor == "3P3K");
                    if (iez == 0) {
                        set_config(ti, config.at(iey).at(iex), "IoCtrl.cf_bit_39",
                                   (!pullup) || (pullup_resistor != "100K"));
                        set_config(ti, config.at(iey).at(iex), "IoCtrl.cf_bit_36", pullup && pullup_resistor == "3P3K");
                        set_config(ti, config.at(iey).at(iex), "IoCtrl.cf_bit_37", pullup && pullup_resistor == "6P8K");
                        set_config(ti, config.at(iey).at(iex), "IoCtrl.cf_bit_38", pullup && pullup_resistor == "10K");
                    } else if (iez == 1) {
                        set_config(ti, config.at(iey).at(iex), "IoCtrl.cf_bit_35",
                                   (!pullup) || (pullup_resistor != "100K"));
                        set_config(ti, config.at(iey).at(iex), "IoCtrl.cf_bit_32", pullup && pullup_resistor == "3P3K");
                        set_config(ti, config.at(iey).at(iex), "IoCtrl.cf_bit_33", pullup && pullup_resistor == "6P8K");
                        set_config(ti, config.at(iey).at(iex), "IoCtrl.cf_bit_34", pullup && pullup_resistor == "10K");
                    }
                }
            } else {
                NPNR_ASSERT(z == 0);
                // Only enable the actual LVDS buffer if input is used for something
                set_config(ti, config.at(y).at(x), "IoCtrl.LVDS", input_en);

                // Set both IO config
                for (int cz = 0; cz < 2; cz++) {
                    auto ieren = get_ieren(bi, x, y, cz);
                    int iex, iey, iez;
                    std::tie(iex, iey, iez) = ieren;
                    NPNR_ASSERT(iez != -1);

                    pullup &= !input_en; /* If input is used, force disable pullups */

                    if (ctx->args.type == ArchArgs::LP1K || ctx->args.type == ArchArgs::HX1K) {
                        set_config(ti, config.at(iey).at(iex), "IoCtrl.IE_" + std::to_string(iez), true);
                        set_config(ti, config.at(iey).at(iex), "IoCtrl.REN_" + std::to_string(iez), !pullup);
                    } else {
                        set_config(ti, config.at(iey).at(iex), "IoCtrl.IE_" + std::to_string(iez), false);
                        set_config(ti, config.at(iey).at(iex), "IoCtrl.REN_" + std::to_string(iez), !pullup);
                    }

                    if (ctx->args.type == ArchArgs::UP5K) {
                        if (iez == 0) {
                            set_config(ti, config.at(iey).at(iex), "IoCtrl.cf_bit_39", !pullup);
                        } else if (iez == 1) {
                            set_config(ti, config.at(iey).at(iex), "IoCtrl.cf_bit_35", !pullup);
                        }
                    }
                }
            }
        } else if (cell.second->type == ctx->id("SB_GB")) {
            if (cell.second->gbInfo.forPadIn) {
                Loc gb_loc = ctx->getBelLocation(bel);
                for (int i = 0; i < ci.num_global_networks; i++) {
                    if ((gb_loc.x == ci.global_network_info[i].gb_x) && (gb_loc.y == ci.global_network_info[i].gb_y)) {
                        extra_bits.push_back(std::make_tuple(ci.global_network_info[i].pi_eb_bank,
                                                             ci.global_network_info[i].pi_eb_x,
                                                             ci.global_network_info[i].pi_eb_y));
                    }
                }
            }
        } else if (cell.second->type == ctx->id("ICESTORM_RAM")) {
            const BelInfoPOD &beli = ci.bel_data[bel.index];
            int x = beli.x, y = beli.y;
            const TileInfoPOD &ti_ramt = bi.tiles_nonrouting[TILE_RAMT];
            const TileInfoPOD &ti_ramb = bi.tiles_nonrouting[TILE_RAMB];
            if (!(ctx->args.type == ArchArgs::LP1K || ctx->args.type == ArchArgs::HX1K)) {
                set_config(ti_ramb, config.at(y).at(x), "RamConfig.PowerUp", true);
            }
            bool negclk_r = get_param_or_def(cell.second.get(), ctx->id("NEG_CLK_R"));
            bool negclk_w = get_param_or_def(cell.second.get(), ctx->id("NEG_CLK_W"));
            int write_mode = get_param_or_def(cell.second.get(), ctx->id("WRITE_MODE"));
            int read_mode = get_param_or_def(cell.second.get(), ctx->id("READ_MODE"));
            set_config(ti_ramb, config.at(y).at(x), "NegClk", negclk_w);
            set_config(ti_ramt, config.at(y + 1).at(x), "NegClk", negclk_r);

            set_config(ti_ramt, config.at(y + 1).at(x), "RamConfig.CBIT_0", write_mode & 0x1);
            set_config(ti_ramt, config.at(y + 1).at(x), "RamConfig.CBIT_1", write_mode & 0x2);
            set_config(ti_ramt, config.at(y + 1).at(x), "RamConfig.CBIT_2", read_mode & 0x1);
            set_config(ti_ramt, config.at(y + 1).at(x), "RamConfig.CBIT_3", read_mode & 0x2);
        } else if (cell.second->type == ctx->id("SB_RGBA_DRV")) {
            const std::vector<std::pair<std::string, int>> rgba_params = {
                    {"CURRENT_MODE", 1}, {"RGB0_CURRENT", 6}, {"RGB1_CURRENT", 6}, {"RGB2_CURRENT", 6}};
            configure_extra_cell(config, ctx, cell.second.get(), rgba_params, true, std::string("IpConfig."));
            set_ec_cbit(config, ctx, get_ec_config(ctx->chip_info, cell.second->bel), "RGBA_DRV_EN", true, "IpConfig.");
        } else if (cell.second->type == ctx->id("SB_WARMBOOT") || cell.second->type == ctx->id("ICESTORM_LFOSC") ||
                   cell.second->type == ctx->id("SB_LEDDA_IP")) {
            // No config needed
        } else if (cell.second->type == ctx->id("ICESTORM_SPRAM")) {
            const BelInfoPOD &beli = ci.bel_data[bel.index];
            int x = beli.x, y = beli.y, z = beli.z;
            NPNR_ASSERT(ctx->args.type == ArchArgs::UP5K);
            if (x == 0 && y == 0) {
                const TileInfoPOD &ti_ipcon = bi.tiles_nonrouting[TILE_IPCON];
                if (z == 1) {
                    set_config(ti_ipcon, config.at(1).at(0), "IpConfig.CBIT_0", true);
                } else if (z == 2) {
                    set_config(ti_ipcon, config.at(1).at(0), "IpConfig.CBIT_1", true);
                } else {
                    NPNR_ASSERT(false);
                }
            } else if (x == 25 && y == 0) {
                const TileInfoPOD &ti_ipcon = bi.tiles_nonrouting[TILE_IPCON];
                if (z == 3) {
                    set_config(ti_ipcon, config.at(1).at(25), "IpConfig.CBIT_0", true);
                } else if (z == 4) {
                    set_config(ti_ipcon, config.at(1).at(25), "IpConfig.CBIT_1", true);
                } else {
                    NPNR_ASSERT(false);
                }
            }
        } else if (cell.second->type == ctx->id("ICESTORM_DSP")) {
            const std::vector<std::pair<std::string, int>> mac16_params = {{"C_REG", 1},
                                                                           {"A_REG", 1},
                                                                           {"B_REG", 1},
                                                                           {"D_REG", 1},
                                                                           {"TOP_8x8_MULT_REG", 1},
                                                                           {"BOT_8x8_MULT_REG", 1},
                                                                           {"PIPELINE_16x16_MULT_REG1", 1},
                                                                           {"PIPELINE_16x16_MULT_REG2", 1},
                                                                           {"TOPOUTPUT_SELECT", 2},
                                                                           {"TOPADDSUB_LOWERINPUT", 2},
                                                                           {"TOPADDSUB_UPPERINPUT", 1},
                                                                           {"TOPADDSUB_CARRYSELECT", 2},
                                                                           {"BOTOUTPUT_SELECT", 2},
                                                                           {"BOTADDSUB_LOWERINPUT", 2},
                                                                           {"BOTADDSUB_UPPERINPUT", 1},
                                                                           {"BOTADDSUB_CARRYSELECT", 2},
                                                                           {"MODE_8x8", 1},
                                                                           {"A_SIGNED", 1},
                                                                           {"B_SIGNED", 1}};
            configure_extra_cell(config, ctx, cell.second.get(), mac16_params, false, std::string("IpConfig."));
        } else if (cell.second->type == ctx->id("ICESTORM_HFOSC")) {
            const std::vector<std::pair<std::string, int>> hfosc_params = {{"CLKHF_DIV", 2}, {"TRIM_EN", 1}};
            configure_extra_cell(config, ctx, cell.second.get(), hfosc_params, true, std::string("IpConfig."));

        } else if (cell.second->type == ctx->id("ICESTORM_PLL")) {
            const std::vector<std::pair<std::string, int>> pll_params = {{"DELAY_ADJMODE_FB", 1},
                                                                         {"DELAY_ADJMODE_REL", 1},
                                                                         {"DIVF", 7},
                                                                         {"DIVQ", 3},
                                                                         {"DIVR", 4},
                                                                         {"FDA_FEEDBACK", 4},
                                                                         {"FDA_RELATIVE", 4},
                                                                         {"FEEDBACK_PATH", 3},
                                                                         {"FILTER_RANGE", 3},
                                                                         {"PLLOUT_SELECT_A", 2},
                                                                         {"PLLOUT_SELECT_B", 2},
                                                                         {"PLLTYPE", 3},
                                                                         {"SHIFTREG_DIV_MODE", 1},
                                                                         {"TEST_MODE", 1}};
            configure_extra_cell(config, ctx, cell.second.get(), pll_params, false, std::string("PLL."));

            // Configure the SB_IOs that the clock outputs are going through.
            for (auto &io_bel_loc : sb_io_used_by_pll_out) {
                // Write config.
                const TileInfoPOD &ti = bi.tiles_nonrouting[TILE_IO];

                // PINTYPE[1:0] == "01" passes the PLL through to the fabric.
                set_config(ti, config.at(io_bel_loc.y).at(io_bel_loc.x),
                           "IOB_" + std::to_string(io_bel_loc.z) + ".PINTYPE_1", false);
                set_config(ti, config.at(io_bel_loc.y).at(io_bel_loc.x),
                           "IOB_" + std::to_string(io_bel_loc.z) + ".PINTYPE_0", true);
            }

        } else {
            NPNR_ASSERT(false);
        }
    }
    // Set config bits in unused IO and RAM
    for (auto bel : ctx->getBels()) {
        if (ctx->bel_to_cell[bel.index] == nullptr && ctx->getBelType(bel) == id_SB_IO) {
            const TileInfoPOD &ti = bi.tiles_nonrouting[TILE_IO];
            const BelInfoPOD &beli = ci.bel_data[bel.index];
            int x = beli.x, y = beli.y, z = beli.z;
            if (sb_io_used_by_pll_out.count(Loc(x, y, z))) {
                continue;
            }

            auto ieren = get_ieren(bi, x, y, z);
            int iex, iey, iez;
            std::tie(iex, iey, iez) = ieren;
            if (iez != -1) {
                // If IO is in LVDS pair, it will be configured by the other pair
                if (z == 1) {
                    BelId lvds0 = ctx->getBelByLocation(Loc{x, y, 0});
                    const CellInfo *lvds0cell = ctx->getBoundBelCell(lvds0);
                    if (lvds0cell != nullptr && lvds0cell->ioInfo.lvds)
                        continue;
                }
                if (ctx->args.type == ArchArgs::LP1K || ctx->args.type == ArchArgs::HX1K) {
                    set_config(ti, config.at(iey).at(iex), "IoCtrl.IE_" + std::to_string(iez), true);
                    set_config(ti, config.at(iey).at(iex), "IoCtrl.REN_" + std::to_string(iez), false);
                } else {
                    set_config(ti, config.at(iey).at(iex), "IoCtrl.IE_" + std::to_string(iez), false);
                    set_config(ti, config.at(iey).at(iex), "IoCtrl.REN_" + std::to_string(iez), false);
                }
            }
        } else if (ctx->bel_to_cell[bel.index] == nullptr && ctx->getBelType(bel) == id_ICESTORM_RAM) {
            const BelInfoPOD &beli = ci.bel_data[bel.index];
            int x = beli.x, y = beli.y;
            const TileInfoPOD &ti = bi.tiles_nonrouting[TILE_RAMB];
            if ((ctx->args.type == ArchArgs::LP1K || ctx->args.type == ArchArgs::HX1K)) {
                set_config(ti, config.at(y).at(x), "RamConfig.PowerUp", true);
            }
        }
    }

    // Set other config bits
    for (int y = 0; y < ci.height; y++) {
        for (int x = 0; x < ci.width; x++) {
            TileType tile = tile_at(ctx, x, y);
            const TileInfoPOD &ti = bi.tiles_nonrouting[tile];

            // set all ColBufCtrl bits (FIXME)
            bool setColBufCtrl = true;
            if (ctx->args.type == ArchArgs::LP1K || ctx->args.type == ArchArgs::HX1K) {
                if (tile == TILE_RAMB || tile == TILE_RAMT) {
                    setColBufCtrl = (y == 3 || y == 5 || y == 11 || y == 13);
                } else {
                    setColBufCtrl = (y == 4 || y == 5 || y == 12 || y == 13);
                }
            } else if (ctx->args.type == ArchArgs::LP8K || ctx->args.type == ArchArgs::HX8K) {
                setColBufCtrl = (y == 8 || y == 9 || y == 24 || y == 25);
            } else if (ctx->args.type == ArchArgs::UP5K) {
                setColBufCtrl = (y == 4 || y == 5 || y == 14 || y == 15 || y == 26 || y == 27);
            } else if (ctx->args.type == ArchArgs::LP384) {
                setColBufCtrl = false;
            }
            if (setColBufCtrl) {
                set_config(ti, config.at(y).at(x), "ColBufCtrl.glb_netwk_0", true);
                set_config(ti, config.at(y).at(x), "ColBufCtrl.glb_netwk_1", true);
                set_config(ti, config.at(y).at(x), "ColBufCtrl.glb_netwk_2", true);
                set_config(ti, config.at(y).at(x), "ColBufCtrl.glb_netwk_3", true);
                set_config(ti, config.at(y).at(x), "ColBufCtrl.glb_netwk_4", true);
                set_config(ti, config.at(y).at(x), "ColBufCtrl.glb_netwk_5", true);
                set_config(ti, config.at(y).at(x), "ColBufCtrl.glb_netwk_6", true);
                set_config(ti, config.at(y).at(x), "ColBufCtrl.glb_netwk_7", true);
            }

            // Weird UltraPlus bits
            if (tile == TILE_DSP0 || tile == TILE_DSP1 || tile == TILE_DSP2 || tile == TILE_DSP3 ||
                tile == TILE_IPCON) {
                if (ctx->args.type == ArchArgs::UP5K && x == 25 && y == 14) {
                    // Mystery bits not set in this one tile
                } else {
                    for (int lc_idx = 0; lc_idx < 8; lc_idx++) {
                        static const std::vector<int> ip_dsp_lut_perm = {
                                4, 14, 15, 5, 6, 16, 17, 7, 3, 13, 12, 2, 1, 11, 10, 0,
                        };
                        for (int i = 0; i < 16; i++)
                            set_config(ti, config.at(y).at(x), "LC_" + std::to_string(lc_idx), ((i % 8) >= 4),
                                       ip_dsp_lut_perm.at(i));
                        if (tile == TILE_IPCON)
                            set_config(ti, config.at(y).at(x),
                                       "Cascade.IPCON_LC0" + std::to_string(lc_idx) + "_inmux02_5", true);
                        else
                            set_config(ti, config.at(y).at(x),
                                       "Cascade.MULT" + std::to_string(int(tile - TILE_DSP0)) + "_LC0" +
                                               std::to_string(lc_idx) + "_inmux02_5",
                                       true);
                    }
                }
            }
        }
    }

    // Write config out
    for (int y = 0; y < ci.height; y++) {
        for (int x = 0; x < ci.width; x++) {
            TileType tile = tile_at(ctx, x, y);
            if (tile == TILE_NONE)
                continue;
            out << tagTileType(tile);
            out << " " << x << " " << y << std::endl;
            for (auto row : config.at(y).at(x)) {
                for (auto col : row) {
                    if (col == 1)
                        out << "1";
                    else
                        out << "0";
                }
                out << std::endl;
            }
            out << std::endl;
        }
    }

    // Write RAM init data
    for (auto &cell : ctx->cells) {
        if (cell.second->bel != BelId()) {
            if (cell.second->type == ctx->id("ICESTORM_RAM")) {
                const BelInfoPOD &beli = ci.bel_data[cell.second->bel.index];
                int x = beli.x, y = beli.y;
                out << ".ram_data " << x << " " << y << std::endl;
                for (int w = 0; w < 16; w++) {
                    std::vector<bool> bits(256);
                    std::string init =
                            get_param_str_or_def(cell.second.get(), ctx->id(std::string("INIT_") + get_hexdigit(w)));
                    for (size_t i = 0; i < init.size(); i++) {
                        bool val = (init.at((init.size() - 1) - i) == '1');
                        bits.at(i) = val;
                    }
                    for (int i = bits.size() - 4; i >= 0; i -= 4) {
                        int c = bits.at(i) + (bits.at(i + 1) << 1) + (bits.at(i + 2) << 2) + (bits.at(i + 3) << 3);
                        out << char(std::tolower(get_hexdigit(c)));
                    }
                    out << std::endl;
                }
                out << std::endl;
            }
        }
    }

    // Write extra-bits
    for (auto eb : extra_bits)
        out << ".extra_bit " << std::get<0>(eb) << " " << std::get<1>(eb) << " " << std::get<2>(eb) << std::endl;

    // Write symbols
    // const bool write_symbols = 1;
    for (auto wire : ctx->getWires()) {
        NetInfo *net = ctx->getBoundWireNet(wire);
        if (net != nullptr)
            out << ".sym " << wire.index << " " << net->name.str(ctx) << std::endl;
    }
}

void read_config(Context *ctx, std::istream &in, chipconfig_t &config)
{
    constexpr size_t line_buf_size = 65536;
    char buffer[line_buf_size];
    int tile_x = -1, tile_y = -1, line_nr = -1;

    while (1) {
        in.getline(buffer, line_buf_size);
        if (buffer[0] == '.') {
            line_nr = -1;
            const char *tok = strtok(buffer, " \t\r\n");

            if (!strcmp(tok, ".device")) {
                std::string config_device = strtok(nullptr, " \t\r\n");
                std::string expected;
                switch (ctx->args.type) {
                case ArchArgs::LP384:
                    expected = "384";
                    break;
                case ArchArgs::HX1K:
                case ArchArgs::LP1K:
                    expected = "1k";
                    break;
                case ArchArgs::HX8K:
                case ArchArgs::LP8K:
                    expected = "8k";
                    break;
                case ArchArgs::UP5K:
                    expected = "5k";
                    break;
                default:
                    log_error("unsupported device type\n");
                }
                if (expected != config_device)
                    log_error("device type does not match\n");
            } else if (!strcmp(tok, ".io_tile") || !strcmp(tok, ".logic_tile") || !strcmp(tok, ".ramb_tile") ||
                       !strcmp(tok, ".ramt_tile") || !strcmp(tok, ".ipcon_tile") || !strcmp(tok, ".dsp0_tile") ||
                       !strcmp(tok, ".dsp1_tile") || !strcmp(tok, ".dsp2_tile") || !strcmp(tok, ".dsp3_tile")) {
                line_nr = 0;
                tile_x = atoi(strtok(nullptr, " \t\r\n"));
                tile_y = atoi(strtok(nullptr, " \t\r\n"));

                TileType tile = tile_at(ctx, tile_x, tile_y);
                if (tok != tagTileType(tile))
                    log_error("Wrong tile type for specified position\n");

            } else if (!strcmp(tok, ".extra_bit")) {
                /*
                int b = atoi(strtok(nullptr, " \t\r\n"));
                int x = atoi(strtok(nullptr, " \t\r\n"));
                int y = atoi(strtok(nullptr, " \t\r\n"));
                std::tuple<int, int, int> key(b, x, y);
                extra_bits.insert(key);
                */
            } else if (!strcmp(tok, ".sym")) {
                int wireIndex = atoi(strtok(nullptr, " \t\r\n"));
                const char *name = strtok(nullptr, " \t\r\n");

                IdString netName = ctx->id(name);

                if (ctx->nets.find(netName) == ctx->nets.end()) {
                    std::unique_ptr<NetInfo> created_net = std::unique_ptr<NetInfo>(new NetInfo);
                    created_net->name = netName;
                    ctx->nets[netName] = std::move(created_net);
                }

                WireId wire;
                wire.index = wireIndex;
                ctx->bindWire(wire, ctx->nets.at(netName).get(), STRENGTH_WEAK);
            }
        } else if (line_nr >= 0 && strlen(buffer) > 0) {
            if (line_nr > int(config.at(tile_y).at(tile_x).size() - 1))
                log_error("Invalid data in input asc file");
            for (int i = 0; buffer[i] == '0' || buffer[i] == '1'; i++)
                config.at(tile_y).at(tile_x).at(line_nr).at(i) = (buffer[i] == '1') ? 1 : 0;
            line_nr++;
        }
        if (in.eof())
            break;
    }
}

bool read_asc(Context *ctx, std::istream &in)
{
    try {
        // [y][x][row][col]
        const ChipInfoPOD &ci = *ctx->chip_info;
        const BitstreamInfoPOD &bi = *ci.bits_info;
        chipconfig_t config;
        config.resize(ci.height);
        for (int y = 0; y < ci.height; y++) {
            config.at(y).resize(ci.width);
            for (int x = 0; x < ci.width; x++) {
                TileType tile = tile_at(ctx, x, y);
                int rows = bi.tiles_nonrouting[tile].rows;
                int cols = bi.tiles_nonrouting[tile].cols;
                config.at(y).at(x).resize(rows, std::vector<int8_t>(cols));
            }
        }
        read_config(ctx, in, config);

        // Set pips
        for (auto pip : ctx->getPips()) {
            const PipInfoPOD &pi = ci.pip_data[pip.index];
            const SwitchInfoPOD &swi = bi.switches[pi.switch_index];
            bool isUsed = true;
            for (int i = 0; i < swi.num_bits; i++) {
                bool val = (pi.switch_mask & (1 << ((swi.num_bits - 1) - i))) != 0;
                int8_t cbit = config.at(swi.y).at(swi.x).at(swi.cbits[i].row).at(swi.cbits[i].col);
                isUsed &= !(bool(cbit) ^ val);
            }
            if (isUsed) {
                NetInfo *net = ctx->wire_to_net[pi.dst];
                if (net != nullptr) {
                    WireId wire;
                    wire.index = pi.dst;
                    ctx->unbindWire(wire);
                    ctx->bindPip(pip, net, STRENGTH_WEAK);
                }
            }
        }
        for (auto bel : ctx->getBels()) {
            if (ctx->getBelType(bel) == id_ICESTORM_LC) {
                const TileInfoPOD &ti = bi.tiles_nonrouting[TILE_LOGIC];
                const BelInfoPOD &beli = ci.bel_data[bel.index];
                int x = beli.x, y = beli.y, z = beli.z;
                std::vector<bool> lc(20, false);
                bool isUsed = false;
                for (int i = 0; i < 20; i++) {
                    lc.at(i) = get_config(ti, config.at(y).at(x), "LC_" + std::to_string(z), i);
                    isUsed |= lc.at(i);
                }
                bool neg_clk = get_config(ti, config.at(y).at(x), "NegClk");
                isUsed |= neg_clk;
                bool carry_set = get_config(ti, config.at(y).at(x), "CarryInSet");
                isUsed |= carry_set;

                if (isUsed) {
                    std::unique_ptr<CellInfo> created = create_ice_cell(ctx, ctx->id("ICESTORM_LC"));
                    IdString name = created->name;
                    ctx->cells[name] = std::move(created);
                    ctx->bindBel(bel, ctx->cells[name].get(), STRENGTH_WEAK);
                    // TODO: Add port mapping to nets and assign values of properties
                }
            }
            if (ctx->getBelType(bel) == id_SB_IO) {
                const TileInfoPOD &ti = bi.tiles_nonrouting[TILE_IO];
                const BelInfoPOD &beli = ci.bel_data[bel.index];
                int x = beli.x, y = beli.y, z = beli.z;
                bool isUsed = false;
                for (int i = 0; i < 6; i++) {
                    isUsed |= get_config(ti, config.at(y).at(x),
                                         "IOB_" + std::to_string(z) + ".PINTYPE_" + std::to_string(i));
                }
                bool neg_trigger = get_config(ti, config.at(y).at(x), "NegClk");
                isUsed |= neg_trigger;

                if (isUsed) {
                    std::unique_ptr<CellInfo> created = create_ice_cell(ctx, ctx->id("SB_IO"));
                    IdString name = created->name;
                    ctx->cells[name] = std::move(created);
                    ctx->bindBel(bel, ctx->cells[name].get(), STRENGTH_WEAK);
                    // TODO: Add port mapping to nets and assign values of properties
                }
            }
        }
        // Add cells that are without change in initial state of configuration
        for (auto &net : ctx->nets) {
            for (auto w : net.second->wires) {
                if (w.second.pip == PipId()) {
                    WireId wire = w.first;
                    for (auto belpin : ctx->getWireBelPins(wire)) {

                        if (ctx->checkBelAvail(belpin.bel)) {
                            if (ctx->getBelType(belpin.bel) == id_ICESTORM_LC) {
                                std::unique_ptr<CellInfo> created = create_ice_cell(ctx, ctx->id("ICESTORM_LC"));
                                IdString name = created->name;
                                ctx->cells[name] = std::move(created);
                                ctx->bindBel(belpin.bel, ctx->cells[name].get(), STRENGTH_WEAK);
                                // TODO: Add port mapping to nets
                            }
                            if (ctx->getBelType(belpin.bel) == id_SB_IO) {
                                std::unique_ptr<CellInfo> created = create_ice_cell(ctx, ctx->id("SB_IO"));
                                IdString name = created->name;
                                ctx->cells[name] = std::move(created);
                                ctx->bindBel(belpin.bel, ctx->cells[name].get(), STRENGTH_WEAK);
                                // TODO: Add port mapping to nets
                            }
                            if (ctx->getBelType(belpin.bel) == id_SB_GB) {
                                std::unique_ptr<CellInfo> created = create_ice_cell(ctx, ctx->id("SB_GB"));
                                IdString name = created->name;
                                ctx->cells[name] = std::move(created);
                                ctx->bindBel(belpin.bel, ctx->cells[name].get(), STRENGTH_WEAK);
                                // TODO: Add port mapping to nets
                            }
                            if (ctx->getBelType(belpin.bel) == id_SB_WARMBOOT) {
                                std::unique_ptr<CellInfo> created = create_ice_cell(ctx, ctx->id("SB_WARMBOOT"));
                                IdString name = created->name;
                                ctx->cells[name] = std::move(created);
                                ctx->bindBel(belpin.bel, ctx->cells[name].get(), STRENGTH_WEAK);
                                // TODO: Add port mapping to nets
                            }
                            if (ctx->getBelType(belpin.bel) == id_ICESTORM_LFOSC) {
                                std::unique_ptr<CellInfo> created = create_ice_cell(ctx, ctx->id("ICESTORM_LFOSC"));
                                IdString name = created->name;
                                ctx->cells[name] = std::move(created);
                                ctx->bindBel(belpin.bel, ctx->cells[name].get(), STRENGTH_WEAK);
                                // TODO: Add port mapping to nets
                            }
                        }
                    }
                }
            }
        }
        for (auto &cell : ctx->cells) {
            if (cell.second->bel != BelId()) {
                for (auto &port : cell.second->ports) {
                    IdString pin = port.first;
                    WireId wire = ctx->getBelPinWire(cell.second->bel, pin);
                    if (wire != WireId()) {
                        NetInfo *net = ctx->getBoundWireNet(wire);
                        if (net != nullptr) {
                            port.second.net = net;
                            PortRef ref;
                            ref.cell = cell.second.get();
                            ref.port = port.second.name;

                            if (port.second.type == PORT_OUT)
                                net->driver = ref;
                            else
                                net->users.push_back(ref);
                        }
                    }
                }
            }
        }
        return true;
    } catch (log_execution_error_exception) {
        return false;
    }
}
NEXTPNR_NAMESPACE_END
