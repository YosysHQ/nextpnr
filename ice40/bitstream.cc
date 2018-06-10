/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
 *  Copyright (C) 2018  David Shah <dave@ds0.me>
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
#include <vector>

inline TileType tile_at(const Chip &chip, int x, int y)
{
    return chip.chip_info.tile_grid[y * chip.chip_info.width + x];
}

void write_asc(const Chip &chip, std::ostream &out)
{
    // [y][x][row][col]
    const ChipInfoPOD &ci = chip.chip_info;
    const BitstreamInfoPOD &bi = *ci.bits_info;
    std::vector<std::vector<std::vector<std::vector<int8_t>>>> config;
    config.resize(ci.height);
    for (int y = 0; y < ci.height; y++) {
        config.at(y).resize(ci.width);
        for (int x = 0; x < ci.width; x++) {
            TileType tile = tile_at(chip, x, y);
            int rows = bi.tiles_nonrouting[tile].rows;
            int cols = bi.tiles_nonrouting[tile].cols;
            config.at(y).at(x).resize(rows, vector<int8_t>(cols));
        }
    }
    out << ".comment from next-pnr" << std::endl;

    switch (chip.args.type) {
    case ChipArgs::LP384:
        out << ".device 384" << std::endl;
        break;
    case ChipArgs::HX1K:
    case ChipArgs::LP1K:
        out << ".device 1k" << std::endl;
        break;
    case ChipArgs::HX8K:
    case ChipArgs::LP8K:
        out << ".device 8k" << std::endl;
        break;
    case ChipArgs::UP5K:
        out << ".device 5k" << std::endl;
        break;
    default:
        assert(false);
    }
    // Write config out
    for (int y = 0; y < ci.height; y++) {
        for (int x = 0; x < ci.width; x++) {
            TileType tile = tile_at(chip, x, y);
            if (tile == TILE_NONE)
                continue;
            switch (tile) {
            case TILE_LOGIC:
                out << ".logic_tile";
                break;
            case TILE_IO:
                out << ".io_tile";
                break;
            case TILE_RAMB:
                out << ".ramb_tile";
                break;
            case TILE_RAMT:
                out << ".ramt_tile";
                break;
            default:
                assert(false);
            }
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
}
