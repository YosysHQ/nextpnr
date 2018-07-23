/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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
                log_error("clearing already set config bit %s", name.c_str());
            cbit = value;
        }
    } else {
        int8_t &cbit = tile_cfg.at(cfg.bits[index].row).at(cfg.bits[index].col);
        cbit = value;
        if (cbit && !value)
            log_error("clearing already set config bit %s[%d]", name.c_str(), index);
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
                        bool value)
{
    const ChipInfoPOD *chip = ctx->chip_info;

    for (int i = 0; i < cell_cbits.num_entries; i++) {
        const auto &cbit = cell_cbits.entries[i];
        if (cbit.entry_name.get() == name) {
            const auto &ti = chip->bits_info->tiles_nonrouting[tile_at(ctx, cbit.x, cbit.y)];
            set_config(ti, config.at(cbit.y).at(cbit.x), std::string("IpConfig.") + cbit.cbit_name.get(), value);
            return;
        }
    }
    NPNR_ASSERT_FALSE_STR("failed to config extra cell config bit " + name);
}

void configure_extra_cell(chipconfig_t &config, const Context *ctx, CellInfo *cell,
                          const std::vector<std::pair<std::string, int>> &params, bool string_style)
{
    const ChipInfoPOD *chip = ctx->chip_info;
    const auto &bc = get_ec_config(chip, cell->bel);
    for (auto p : params) {
        std::vector<bool> value;
        if (string_style) {
            // Lattice's weird string style params, not sure if
            // prefixes other than 0b should be supported, only 0b features in docs
            std::string raw = get_param_str_or_def(cell, ctx->id(p.first), "0b0");
            assert(raw.substr(0, 2) == "0b");
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
            int ival = get_param_or_def(cell, ctx->id(p.first), 0);

            for (int i = 0; i < p.second; i++)
                value.push_back((ival >> i) & 0x1);
        }

        value.resize(p.second);
        if (p.second == 1) {
            set_ec_cbit(config, ctx, bc, p.first, value.at(0));
        } else {
            for (int i = 0; i < p.second; i++) {
                set_ec_cbit(config, ctx, bc, p.first + "_" + std::to_string(i), value.at(i));
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
void write_asc(const Context *ctx, std::ostream &out)
{
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
        if (ctx->pip_to_net[pip.index] != IdString()) {
            const PipInfoPOD &pi = ci.pip_data[pip.index];
            const SwitchInfoPOD &swi = bi.switches[pi.switch_index];
            for (int i = 0; i < swi.num_bits; i++) {
                bool val = (pi.switch_mask & (1 << ((swi.num_bits - 1) - i))) != 0;
                int8_t &cbit = config.at(swi.y).at(swi.x).at(swi.cbits[i].row).at(swi.cbits[i].col);
                if (bool(cbit) != 0)
                    NPNR_ASSERT(false);
                cbit = val;
            }
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
            // From arachne-pnr
            static std::vector<int> lut_perm = {
                    4, 14, 15, 5, 6, 16, 17, 7, 3, 13, 12, 2, 1, 11, 10, 0,
            };
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
            for (int i = 0; i < 6; i++) {
                bool val = (pin_type >> i) & 0x01;
                set_config(ti, config.at(y).at(x), "IOB_" + std::to_string(z) + ".PINTYPE_" + std::to_string(i), val);
            }
            set_config(ti, config.at(y).at(x), "NegClk", neg_trigger);
            auto ieren = get_ieren(bi, x, y, z);
            int iex, iey, iez;
            std::tie(iex, iey, iez) = ieren;
            NPNR_ASSERT(iez != -1);

            bool input_en = false;
            if ((ctx->wire_to_net[ctx->getBelPinWire(bel, PIN_D_IN_0).index] != IdString()) ||
                (ctx->wire_to_net[ctx->getBelPinWire(bel, PIN_D_IN_1).index] != IdString())) {
                input_en = true;
            }

            if (ctx->args.type == ArchArgs::LP1K || ctx->args.type == ArchArgs::HX1K) {
                set_config(ti, config.at(iey).at(iex), "IoCtrl.IE_" + std::to_string(iez), !input_en);
                set_config(ti, config.at(iey).at(iex), "IoCtrl.REN_" + std::to_string(iez), !pullup);
            } else {
                set_config(ti, config.at(iey).at(iex), "IoCtrl.IE_" + std::to_string(iez), input_en);
                set_config(ti, config.at(iey).at(iex), "IoCtrl.REN_" + std::to_string(iez), !pullup);
            }

            if (ctx->args.type == ArchArgs::UP5K) {
                if (iez == 0) {
                    set_config(ti, config.at(iey).at(iex), "IoCtrl.cf_bit_39", !pullup);
                } else if (iez == 1) {
                    set_config(ti, config.at(iey).at(iex), "IoCtrl.cf_bit_35", !pullup);
                }
            }
        } else if (cell.second->type == ctx->id("SB_GB")) {
            // no cell config bits
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
        } else if (cell.second->type == ctx->id("SB_WARMBOOT") || cell.second->type == ctx->id("ICESTORM_LFOSC")) {
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
            configure_extra_cell(config, ctx, cell.second.get(), mac16_params, false);
        } else {
            NPNR_ASSERT(false);
        }
    }
    // Set config bits in unused IO and RAM
    for (auto bel : ctx->getBels()) {
        if (ctx->bel_to_cell[bel.index] == IdString() && ctx->getBelType(bel) == TYPE_SB_IO) {
            const TileInfoPOD &ti = bi.tiles_nonrouting[TILE_IO];
            const BelInfoPOD &beli = ci.bel_data[bel.index];
            int x = beli.x, y = beli.y, z = beli.z;
            auto ieren = get_ieren(bi, x, y, z);
            int iex, iey, iez;
            std::tie(iex, iey, iez) = ieren;
            if (iez != -1) {
                if (ctx->args.type == ArchArgs::LP1K || ctx->args.type == ArchArgs::HX1K) {
                    set_config(ti, config.at(iey).at(iex), "IoCtrl.IE_" + std::to_string(iez), true);
                    set_config(ti, config.at(iey).at(iex), "IoCtrl.REN_" + std::to_string(iez), false);
                }
            }
        } else if (ctx->bel_to_cell[bel.index] == IdString() && ctx->getBelType(bel) == TYPE_ICESTORM_RAM) {
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
                    NPNR_ASSERT(init != "");
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

    // Write symbols
    // const bool write_symbols = 1;
    for (auto wire : ctx->getWires()) {
        IdString net = ctx->getBoundWireNet(wire);
        if (net != IdString())
            out << ".sym " << wire.index << " " << net.str(ctx) << std::endl;
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
                ctx->bindWire(wire, netName, STRENGTH_WEAK);
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
                IdString net = ctx->wire_to_net[pi.dst];
                WireId wire;
                wire.index = pi.dst;
                ctx->unbindWire(wire);
                ctx->bindPip(pip, net, STRENGTH_WEAK);
            }
        }
        for (auto bel : ctx->getBels()) {
            if (ctx->getBelType(bel) == TYPE_ICESTORM_LC) {
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
                    ctx->bindBel(bel, name, STRENGTH_WEAK);
                    // TODO: Add port mapping to nets and assign values of properties
                }
            }
            if (ctx->getBelType(bel) == TYPE_SB_IO) {
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
                    ctx->bindBel(bel, name, STRENGTH_WEAK);
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
                            if (ctx->getBelType(belpin.bel) == TYPE_ICESTORM_LC) {
                                std::unique_ptr<CellInfo> created = create_ice_cell(ctx, ctx->id("ICESTORM_LC"));
                                IdString name = created->name;
                                ctx->cells[name] = std::move(created);
                                ctx->bindBel(belpin.bel, name, STRENGTH_WEAK);
                                // TODO: Add port mapping to nets
                            }
                            if (ctx->getBelType(belpin.bel) == TYPE_SB_IO) {
                                std::unique_ptr<CellInfo> created = create_ice_cell(ctx, ctx->id("SB_IO"));
                                IdString name = created->name;
                                ctx->cells[name] = std::move(created);
                                ctx->bindBel(belpin.bel, name, STRENGTH_WEAK);
                                // TODO: Add port mapping to nets
                            }
                            if (ctx->getBelType(belpin.bel) == TYPE_SB_GB) {
                                std::unique_ptr<CellInfo> created = create_ice_cell(ctx, ctx->id("SB_GB"));
                                IdString name = created->name;
                                ctx->cells[name] = std::move(created);
                                ctx->bindBel(belpin.bel, name, STRENGTH_WEAK);
                                // TODO: Add port mapping to nets
                            }
                            if (ctx->getBelType(belpin.bel) == TYPE_SB_WARMBOOT) {
                                std::unique_ptr<CellInfo> created = create_ice_cell(ctx, ctx->id("SB_WARMBOOT"));
                                IdString name = created->name;
                                ctx->cells[name] = std::move(created);
                                ctx->bindBel(belpin.bel, name, STRENGTH_WEAK);
                                // TODO: Add port mapping to nets
                            }
                            if (ctx->getBelType(belpin.bel) == TYPE_ICESTORM_LFOSC) {
                                std::unique_ptr<CellInfo> created = create_ice_cell(ctx, ctx->id("ICESTORM_LFOSC"));
                                IdString name = created->name;
                                ctx->cells[name] = std::move(created);
                                ctx->bindBel(belpin.bel, name, STRENGTH_WEAK);
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
                    PortPin pin = ctx->portPinFromId(port.first);
                    WireId wire = ctx->getBelPinWire(cell.second->bel, pin);
                    if (wire != WireId()) {
                        IdString name = ctx->getBoundWireNet(wire);
                        if (name != IdString()) {
                            port.second.net = ctx->nets[name].get();
                            PortRef ref;
                            ref.cell = cell.second.get();
                            ref.port = port.second.name;

                            if (port.second.type == PORT_OUT)
                                ctx->nets[name]->driver = ref;
                            else
                                ctx->nets[name]->users.push_back(ref);
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
