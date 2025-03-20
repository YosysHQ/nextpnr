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
#include "gatemate_util.h"

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

    CfgLoc getConfigLoc(int tile)
    {
        auto ti = *uarch->tile_extra_data(tile);
        CfgLoc loc;
        loc.die = ti.die;
        loc.x = ti.bit_x;
        loc.y = ti.bit_y;
        return loc;
    }

    CfgLoc getRAMConfigLoc(int tile)
    {
        auto ti = *uarch->tile_extra_data(tile);
        CfgLoc loc;
        loc.die = ti.die;
        loc.x = (ti.bit_x - 17) / 16;
        loc.y = (ti.bit_y - 1) / 8;
        return loc;
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
            CfgLoc loc = getConfigLoc(cell.second.get()->bel.tile);
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
            case id_CPE_HALF_U.index:
            case id_CPE_HALF_L.index: {
                int id = uarch->tile_extra_data(cell.second.get()->bel.tile)->prim_id;
                for (auto &p : params) {
                    cc.tiles[loc].add_word(stringf("CPE%d.%s", id, p.first.c_str(ctx)), p.second.as_bits());
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
                    for (auto &p : params) {
                        cc.configs[0].add_word(stringf("PLL%d.%s", l.z-4, p.first.c_str(ctx)), p.second.as_bits());
                    }
                }
            break;
            case id_RAM.index:
                {
                    CfgLoc loc = getRAMConfigLoc(cell.second.get()->bel.tile);
                    auto &bram = cc.brams[loc];
                    for (auto &p : params) {
                        std::string name = p.first.c_str(ctx);
                        if (boost::starts_with(name, "RAM_cfg"))
                            bram.add_word(name, p.second.as_bits());
                    }
                    auto &bram_data = cc.bram_data[loc];
                    bram_data = std::vector<uint8_t>(5120);
                    for(int i=0;i<128;i++) {
                        for(int j=0;j<40;j++) {
                            bram_data[i*40+j] = extract_bits(params, ctx->idf("INIT_%02X",i), j*8, 8);
                        }
                    }
                }
            break;
            case id_USR_RSTN.index:
            case id_CFG_CTRL.index:
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
                        CfgLoc loc = getConfigLoc(pip.tile);
                        std::string word = name.c_str(ctx);
                        if (extra_data.flags & MUX_CONFIG) {
                            cc.configs[loc.die].add_word(word, int_to_bitvector(extra_data.value, extra_data.bits));
                        } else {
                            int id = uarch->tile_extra_data(pip.tile)->prim_id;
                            if (boost::starts_with(word, "IM."))
                                boost::replace_all(word, "IM.", stringf("IM%d.", id));
                            else if (boost::starts_with(word, "OM."))
                                boost::replace_all(word, "OM.", stringf("OM%d.", id));
                            else if (boost::starts_with(word, "CPE."))
                                boost::replace_all(word, "CPE.", stringf("CPE%d.", id));
                            else if (boost::starts_with(word, "IOES."))
                                boost::replace_all(word, "IOES.", stringf("IOES%d.", id));
                            else if (boost::starts_with(word, "LES."))
                                boost::replace_all(word, "LES.", stringf("LES%d.", id));
                            else if (boost::starts_with(word, "BES."))
                                boost::replace_all(word, "BES.", stringf("BES%d.", id));
                            else if (boost::starts_with(word, "RES."))
                                boost::replace_all(word, "RES.", stringf("RES%d.", id));
                            else if (boost::starts_with(word, "TES."))
                                boost::replace_all(word, "TES.", stringf("TES%d.", id));
                            if (boost::starts_with(word, "SB_DRIVE.")) {
                                Loc l;
                                tile_xy(ctx->chip_info, pip.tile, l.x, l.y);
                                l.z = 0;
                                BelId cpe_bel = ctx->getBelByLocation(l);
                                // Only if switchbox is inside core (same as sharing location with CPE)
                                if (cpe_bel != BelId() && ctx->getBelType(cpe_bel).in(id_CPE_HALF_L,id_CPE_HALF_U)) {
                                    // Convert coordinates into in-tile coordinates
                                    int xt = ((l.x-2-1)+16) % 8;
                                    int yt = ((l.y-2-1)+16) % 8;
                                    // Bitstream data for certain SB_DRIVES is located in other tiles
                                    switch(word[14]) {
                                        case '3' : if (xt >= 4) { loc.x -= 2; word[14] = '1'; }; break;
                                        case '4' : if (yt >= 4) { loc.y -= 2; word[14] = '2'; }; break;
                                        case '1' : if (xt <= 3) { loc.x += 2; word[14] = '3'; }; break;
                                        case '2' : if (yt <= 3) { loc.y += 2; word[14] = '4'; }; break;
                                        default:
                                            break;
                                    }
                                }
                            }

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
