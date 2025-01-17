/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
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

#include <boost/algorithm/string.hpp>
#include "config.h"
#include "gatemate.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {

struct BitstreamBackend
{
    Context *ctx;
    GateMateImpl *uarch;
    const std::string &device;
    std::ostream &out;

    BitstreamBackend(Context *ctx, GateMateImpl *uarch, const std::string &device, std::ostream &out)
            : ctx(ctx), uarch(uarch), device(device), out(out) {};

    void get_bitstream_tile(int x, int y, int &b_x, int &b_y)
    {
        // Edge blocks are bit bigger
        if (x == -2)
            x++;
        if (x == 163)
            x--;
        if (y == -2)
            y++;
        if (y == 131)
            y--;
        b_x = (x + 1) / 2;
        b_y = (y + 1) / 2;
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
        for (int i = 0; i < int(str.size()); i++) {
            char c = str.at((str.size() - i) - 1);
            NPNR_ASSERT(c == '0' || c == '1');
            bv.at(i) = (c == '1');
        }
        return bv;
    }

    CfgLoc getConfigLoc(Context *ctx, int tile)
    {
        int x0, y0;
        int bx, by;
        tile_xy(ctx->chip_info, tile, x0, y0);
        get_bitstream_tile(x0 - 2, y0 - 2, bx, by);
        CfgLoc loc;
        loc.die = 0;
        loc.x = bx;
        loc.y = by;
        return loc;
    }

    int getInTileIndex(Context *ctx, int tile)
    {
        int x0, y0;
        tile_xy(ctx->chip_info, tile, x0, y0);
        x0 -= 2 - 1;
        y0 -= 2 - 1;
        return (x0 % 2) * 2 + (y0 % 2) + 1;
    }

    void write_bitstream()
    {
        ChipConfig cc;
        cc.chip_name = device;
        cc.configs[0].add_word("GPIO.BANK_E1", int_to_bitvector(1, 1));
        cc.configs[0].add_word("GPIO.BANK_E2", int_to_bitvector(1, 1));
        cc.configs[0].add_word("GPIO.BANK_N1", int_to_bitvector(1, 1));
        cc.configs[0].add_word("GPIO.BANK_N2", int_to_bitvector(1, 1));
        cc.configs[0].add_word("GPIO.BANK_S1", int_to_bitvector(1, 1));
        cc.configs[0].add_word("GPIO.BANK_S2", int_to_bitvector(1, 1));
        cc.configs[0].add_word("GPIO.BANK_W1", int_to_bitvector(1, 1));
        cc.configs[0].add_word("GPIO.BANK_W2", int_to_bitvector(1, 1));
        for (auto &cell : ctx->cells) {
            CfgLoc loc = getConfigLoc(ctx, cell.second.get()->bel.tile);
            auto &params = cell.second.get()->params;
            switch (cell.second->type.index) {
            case id_CC_IBUF.index:
            case id_CC_TOBUF.index:
            case id_CC_OBUF.index:
            case id_CC_IOBUF.index:
            case id_CC_LVDS_IBUF.index:
            case id_CC_LVDS_TOBUF.index:
            case id_CC_LVDS_OBUF.index:
            case id_CC_LVDS_IOBUF.index:
                for (auto &p : params) {
                    cc.tiles[loc].add_word(stringf("GPIO.%s", p.first.c_str(ctx)), p.second.as_bits());
                }
                break;
            case id_CPE.index: {
                int x = getInTileIndex(ctx, cell.second.get()->bel.tile);
                for (auto &p : params) {
                    cc.tiles[loc].add_word(stringf("CPE%d.%s", x, p.first.c_str(ctx)), p.second.as_bits());
                }
            } break;
            case id_BUFG.index:
                {
                    Loc l = ctx->getBelLocation(cell.second->bel);
                    cc.configs[0].add_word(stringf("GLBOUT.GLB%d_EN",l.z), int_to_bitvector(1,1));
                }
            break;
            case id_PLL.index:
                {
                    Loc l = ctx->getBelLocation(cell.second->bel);
                    cc.configs[0].add_word(stringf("PLL%d.CLK_OUT_EN",l.z-4), int_to_bitvector(1,1));
                    cc.configs[0].add_word(stringf("PLL%d.PLL_EN",l.z-4), int_to_bitvector(1,1));
                    cc.configs[0].add_word(stringf("PLL%d.PLL_RST",l.z-4), int_to_bitvector(1,1));
                    for (auto &p : params) {
                        cc.configs[0].add_word(stringf("PLL%d.%s", l.z-4, p.first.c_str(ctx)), p.second.as_bits());
                    }
                }
            break;
            default:
                log_error("Unhandled cell %s of type %s\n", cell.second.get()->name.c_str(ctx),
                          cell.second->type.c_str(ctx));
            }
        }

        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            if (ni->wires.empty())
                continue;
            std::set<std::string> nets;
            for (auto &w : ni->wires) {
                if (w.second.pip != PipId()) {
                    PipId pip = w.second.pip;
                    const auto extra_data = *reinterpret_cast<const GateMatePipExtraDataPOD *>(
                            chip_pip_info(ctx->chip_info, pip).extra_data.get());
                    if (extra_data.type == PipExtra::PIP_EXTRA_MUX && (extra_data.flags & MUX_VISIBLE)) {
                        IdString name = IdString(extra_data.name);
                        CfgLoc loc = getConfigLoc(ctx, pip.tile);
                        std::string word = name.c_str(ctx);
                        if (extra_data.flags & MUX_CONFIG) {
                            cc.configs[loc.die].add_word(word, int_to_bitvector(extra_data.value, extra_data.bits));    
                        } else {
                            int x = getInTileIndex(ctx, pip.tile);
                            if (boost::starts_with(word, "IM."))
                                boost::replace_all(word, "IM.", stringf("IM%d.", x));
                            if (boost::starts_with(word, "OM."))
                                boost::replace_all(word, "OM.", stringf("OM%d.", x));
                            if (boost::starts_with(word, "IOES."))
                                boost::replace_all(word, "IOES.", "IOES1.");
                            if (boost::starts_with(word, "CPE."))
                                boost::replace_all(word, "CPE.", stringf("CPE%d.", x));
                            cc.tiles[loc].add_word(word, int_to_bitvector(extra_data.value, extra_data.bits));
                        }
                    }
                }
            }
        }
        out << cc;
    }
};

} // namespace

void GateMateImpl::write_bitstream(const std::string &device, const std::string &filename)
{
    std::ofstream out(filename);
    if (!out)
        log_error("failed to open file %s for writing (%s)\n", filename.c_str(), strerror(errno));

    BitstreamBackend be(ctx, this, device, out);
    be.write_bitstream();
}

bool GateMateImpl::read_bitstream(const std::string &device, const std::string &filename)
{
    std::ifstream in(filename);
    if (!in)
        log_error("failed to open file %s for reading (%s)\n", filename.c_str(), strerror(errno));
    return false;
}

NEXTPNR_NAMESPACE_END
