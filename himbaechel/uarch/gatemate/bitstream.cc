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

    const GateMateTileExtraDataPOD *tile_extra_data(int tile) const
    {
        return reinterpret_cast<const GateMateTileExtraDataPOD *>(ctx->chip_info->tile_insts[tile].extra_data.get());
    }

    bool need_inversion(CellInfo *cell, IdString port)
    {
        PortRef sink;
        sink.cell = cell;
        sink.port = port;

        NetInfo *net_info = cell->getPort(port);
        if (!net_info)
            return false;

        WireId src_wire = ctx->getNetinfoSourceWire(net_info);
        WireId dst_wire = ctx->getNetinfoSinkWire(net_info, sink, 0);

        if (src_wire == WireId())
            return false;

        WireId cursor = dst_wire;
        bool invert = false;
        while (cursor != WireId() && cursor != src_wire) {
            auto it = net_info->wires.find(cursor);

            if (it == net_info->wires.end())
                break;

            PipId pip = it->second.pip;
            if (pip == PipId())
                break;

            invert ^= ctx->isPipInverting(pip);
            cursor = ctx->getPipSrcWire(pip);
        }

        return invert;
    }

    void update_cpe_lt(CellInfo *cell, IdString port, IdString init, dict<IdString, Property> &params, bool even)
    {
        unsigned init_val = int_or_default(params, init);
        bool invert = need_inversion(cell, port);
        if (invert) {
            if (even)
                init_val = (init_val & 0b1010) >> 1 | (init_val & 0b0101) << 1;
            else
                init_val = (init_val & 0b0011) << 2 | (init_val & 0b1100) >> 2;
            params[init] = Property(init_val, 4);
        }
    }

    void update_cpe_inv(CellInfo *cell, IdString port, IdString param, dict<IdString, Property> &params)
    {
        unsigned init_val = int_or_default(params, param);
        bool invert = need_inversion(cell, port);
        if (invert) {
            params[param] = Property(3 - init_val, 2);
        }
    }

    void update_cpe_mux(CellInfo *cell, IdString port, IdString param, int bit, dict<IdString, Property> &params)
    {
        // Mux inversion data is contained in other CPE half
        unsigned init_val = int_or_default(params, param);
        bool invert = need_inversion(cell, port);
        if (invert) {
            int old = (init_val >> bit) & 1;
            int val = (init_val & (~(1 << bit) & 0xf)) | ((!old) << bit);
            params[param] = Property(val, 4);
        }
    }

    std::vector<bool> int_to_bitvector(int val, int size)
    {
        std::vector<bool> bv;
        for (int i = 0; i < size; i++) {
            bv.push_back((val & (1 << i)) != 0);
        }
        return bv;
    }

    CfgLoc get_config_loc(int tile)
    {
        auto ti = *tile_extra_data(tile);
        CfgLoc loc;
        loc.die = ti.die;
        loc.x = ti.bit_x;
        loc.y = ti.bit_y;
        return loc;
    }

    CfgLoc get_ram_config_loc(int tile)
    {
        auto ti = *tile_extra_data(tile);
        CfgLoc loc;
        loc.die = ti.die;
        loc.x = (ti.bit_x - 17) / 16;
        loc.y = (ti.bit_y - 1) / 8;
        return loc;
    }

    void export_connection(ChipConfig &cc, PipId pip)
    {
        const auto extra_data =
                *reinterpret_cast<const GateMatePipExtraDataPOD *>(chip_pip_info(ctx->chip_info, pip).extra_data.get());
        if (extra_data.type == PipExtra::PIP_EXTRA_MUX && (extra_data.flags & MUX_VISIBLE)) {
            IdString name = IdString(extra_data.name);
            CfgLoc loc = get_config_loc(pip.tile);
            std::string word = name.c_str(ctx);
            if (extra_data.flags & MUX_CONFIG) {
                cc.configs[loc.die].add_word(word, int_to_bitvector(extra_data.value, extra_data.bits));
            } else {
                int id = tile_extra_data(pip.tile)->prim_id;
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
                    auto ti = *tile_extra_data(pip.tile);
                    tile_xy(ctx->chip_info, pip.tile, l.x, l.y);
                    l.z = CPE_LT_U_Z;
                    BelId cpe_bel = ctx->getBelByLocation(l);
                    // Only if switchbox is inside core (same as sharing location with CPE)
                    if (cpe_bel != BelId() && ctx->getBelType(cpe_bel) == id_CPE_LT_U) {
                        // Bitstream data for certain SB_DRIVES is located in other tiles
                        switch (word[14]) {
                        case '3':
                            if (ti.tile_x >= 4) {
                                loc.x -= 2;
                                word[14] = '1';
                            };
                            break;
                        case '4':
                            if (ti.tile_y >= 4) {
                                loc.y -= 2;
                                word[14] = '2';
                            };
                            break;
                        case '1':
                            if (ti.tile_x <= 3) {
                                loc.x += 2;
                                word[14] = '3';
                            };
                            break;
                        case '2':
                            if (ti.tile_y <= 3) {
                                loc.y += 2;
                                word[14] = '4';
                            };
                            break;
                        default:
                            break;
                        }
                    }
                }

                cc.tiles[loc].add_word(word, int_to_bitvector(extra_data.value, extra_data.bits));
            }
        }
    }

    void check_multipliers()
    {
        for (auto *mult : uarch->multipliers) {
            NPNR_ASSERT(mult != nullptr);

            auto should_be_inverted = mult->constr_x % 2 == 1;

            // TODO: these are errors, but downgraded to allow providing *some* output.

            // IN8
            if (need_inversion(mult, id_IN8) != should_be_inverted)
                log_warning("%s.IN8 has wrong inversion state\n", mult->name.c_str(ctx));

            // IN5
            if (need_inversion(mult, id_IN5) != should_be_inverted)
                log_warning("%s.IN5 has wrong inversion state\n", mult->name.c_str(ctx));

            // IN1
            if (need_inversion(mult, id_IN1) != should_be_inverted)
                log_warning("%s.IN1 has wrong inversion state\n", mult->name.c_str(ctx));
        }
    }

    void write_bitstream()
    {
        check_multipliers();

        ChipConfig cc;
        cc.chip_name = device;
        std::vector<std::array<int, 9>> bank(uarch->dies);
        for (auto &cell : ctx->cells) {
            CfgLoc loc = get_config_loc(cell.second.get()->bel.tile);
            auto &params = cell.second.get()->params;
            switch (cell.second->type.index) {
            case id_CPE_IBUF.index:
            case id_CPE_TOBUF.index:
            case id_CPE_OBUF.index:
            case id_CPE_IOBUF.index:
            case id_CPE_LVDS_IBUF.index:
            case id_CPE_LVDS_TOBUF.index:
            case id_CPE_LVDS_OBUF.index:
            case id_CPE_LVDS_IOBUF.index:
                for (auto &p : params) {
                    bank[loc.die][ctx->get_bel_package_pin(cell.second.get()->bel)->pad_bank] = 1;
                    cc.tiles[loc].add_word(stringf("GPIO.%s", p.first.c_str(ctx)), p.second.as_bits());
                }
                break;
            case id_CPE_CPLINES.index:
            case id_CPE_COMP.index:
            case id_CPE_L2T4.index:
            case id_CPE_ADDF.index:
            case id_CPE_ADDF2.index:
            case id_CPE_MULT.index:
            case id_CPE_MX4.index:
            case id_CPE_CONCAT.index:
            case id_CPE_FF.index:
            case id_CPE_LATCH.index:
            case id_CPE_RAMI.index:
            case id_CPE_RAMO.index:
            case id_CPE_RAMIO.index: {
                // Update configuration bits based on signal inversion
                dict<IdString, Property> params = cell.second->params;
                Loc l = ctx->getBelLocation(cell.second->bel);
                params.erase(id_L2T4_UPPER);
                int c_i1 = int_or_default(params, id_C_I1, 0);
                int c_i2 = int_or_default(params, id_C_I2, 0);
                int c_i3 = int_or_default(params, id_C_I3, 0);
                int c_i4 = int_or_default(params, id_C_I4, 0);
                if (cell.second->type.in(id_CPE_L2T4, id_CPE_LT_L, id_CPE_LT_U)) {
                    if (l.z == CPE_LT_U_Z) {
                        update_cpe_lt(cell.second.get(), id_IN1, id_INIT_L00, params, true);
                        update_cpe_lt(cell.second.get(), c_i1 ? id_PINY1 : id_IN2, id_INIT_L00, params, false);
                        update_cpe_lt(cell.second.get(), id_IN3, id_INIT_L01, params, true);
                        update_cpe_lt(cell.second.get(), c_i2 ? id_CINX : id_IN4, id_INIT_L01, params, false);
                    } else {
                        // These will be renamed later
                        update_cpe_lt(cell.second.get(), id_IN1, id_INIT_L00, params, true);
                        update_cpe_lt(cell.second.get(), c_i3 ? id_PINY1 : id_IN2, id_INIT_L00, params, false);
                        update_cpe_lt(cell.second.get(), id_IN3, id_INIT_L01, params, true);
                        update_cpe_lt(cell.second.get(), c_i4 ? id_PINX : id_IN4, id_INIT_L01, params, false);
                    }
                }
                if (l.z == CPE_LT_FULL_Z) {
                    if (!cell.second->type.in(id_CPE_MULT)) {
                        if (cell.second->type.in(id_CPE_MX4)) {
                            update_cpe_mux(cell.second.get(), id_IN1, id_INIT_L11, 0, params);
                            update_cpe_mux(cell.second.get(), id_IN2, id_INIT_L11, 1, params);
                            update_cpe_mux(cell.second.get(), id_IN3, id_INIT_L11, 2, params);
                            update_cpe_mux(cell.second.get(), id_IN4, id_INIT_L11, 3, params);
                            update_cpe_lt(cell.second.get(), id_IN5, id_INIT_L02, params, true);
                            update_cpe_lt(cell.second.get(), c_i3 ? id_PINY1 : id_IN6, id_INIT_L02, params, false);
                            update_cpe_lt(cell.second.get(), id_IN7, id_INIT_L03, params, true);
                            update_cpe_lt(cell.second.get(), c_i4 ? id_PINX : id_IN8, id_INIT_L03, params, false);
                        } else {
                            update_cpe_lt(cell.second.get(), id_IN1, id_INIT_L00, params, true);
                            update_cpe_lt(cell.second.get(), c_i1 ? id_PINY1 : id_IN2, id_INIT_L00, params, false);
                            update_cpe_lt(cell.second.get(), id_IN3, id_INIT_L01, params, true);
                            update_cpe_lt(cell.second.get(), c_i2 ? id_CINX : id_IN4, id_INIT_L01, params, false);
                            update_cpe_lt(cell.second.get(), id_IN5, id_INIT_L02, params, true);
                            update_cpe_lt(cell.second.get(), c_i3 ? id_PINY1 : id_IN6, id_INIT_L02, params, false);
                            update_cpe_lt(cell.second.get(), id_IN7, id_INIT_L03, params, true);
                            update_cpe_lt(cell.second.get(), c_i4 ? id_PINX : id_IN8, id_INIT_L03, params, false);
                        }
                    }
                }

                if (cell.second->type.in(id_CPE_FF, id_CPE_LATCH)) {
                    update_cpe_inv(cell.second.get(), id_CLK, id_C_CPE_CLK, params);
                    update_cpe_inv(cell.second.get(), id_EN, id_C_CPE_EN, params);
                    bool set = int_or_default(params, id_C_EN_SR, 0) == 1;
                    if (set)
                        update_cpe_inv(cell.second.get(), id_SR, id_C_CPE_SET, params);
                    else
                        update_cpe_inv(cell.second.get(), id_SR, id_C_CPE_RES, params);
                }
                int id = tile_extra_data(cell.second.get()->bel.tile)->prim_id;
                for (auto &p : params) {
                    IdString name = p.first;
                    switch (l.z) {
                    case CPE_LT_L_Z:
                        switch (p.first.index) {
                        case id_INIT_L00.index:
                            name = id_INIT_L02;
                            break;
                        case id_INIT_L01.index:
                            name = id_INIT_L03;
                            break;
                        case id_INIT_L10.index:
                            name = id_INIT_L11;
                            break;
                        }
                        break;
                    case CPE_RAMIO_U_Z:
                        switch (p.first.index) {
                        case id_C_RAM_I.index:
                            name = id_C_RAM_I2;
                            break;
                        case id_C_RAM_O.index:
                            name = id_C_RAM_O2;
                            break;
                        }
                        break;
                    case CPE_RAMIO_L_Z:
                        switch (p.first.index) {
                        case id_C_RAM_I.index:
                            name = id_C_RAM_I1;
                            break;
                        case id_C_RAM_O.index:
                            name = id_C_RAM_O1;
                            break;
                        }
                        break;
                    }
                    cc.tiles[loc].add_word(stringf("CPE%d.%s", id, name.c_str(ctx)), p.second.as_bits());
                }
            } break;
            case id_CLKIN.index: {
                for (auto &p : params) {
                    cc.configs[loc.die].add_word(stringf("CLKIN.%s", p.first.c_str(ctx)), p.second.as_bits());
                }
            } break;
            case id_GLBOUT.index: {
                for (auto &p : params) {
                    cc.configs[loc.die].add_word(stringf("GLBOUT.%s", p.first.c_str(ctx)), p.second.as_bits());
                }
            } break;
            case id_PLL.index: {
                Loc l = ctx->getBelLocation(cell.second->bel);
                for (auto &p : params) {
                    cc.configs[loc.die].add_word(stringf("PLL%d.%s", l.z - 2, p.first.c_str(ctx)), p.second.as_bits());
                }
            } break;
            case id_RAM.index: {
                CfgLoc loc = get_ram_config_loc(cell.second.get()->bel.tile);
                auto &bram = cc.brams[loc];
                for (auto &p : params) {
                    std::string name = p.first.c_str(ctx);
                    if (boost::starts_with(name, "RAM_cfg"))
                        bram.add_word(name, p.second.as_bits());
                }

                bool is_fifo = params.count(id_RAM_cfg_fifo_sync_enable) | params.count(id_RAM_cfg_fifo_async_enable);
                if (!is_fifo) {
                    auto &bram_data = cc.bram_data[loc];
                    bram_data = std::vector<uint8_t>(5120);
                    for (int i = 0; i < 128; i++) {
                        for (int j = 0; j < 40; j++) {
                            bram_data[i * 40 + j] = extract_bits(params, ctx->idf("INIT_%02X", i), j * 8, 8);
                        }
                    }
                }
            } break;
            case id_SERDES.index: {
                auto &serdes = cc.serdes[0];
                for (auto &p : params) {
                    serdes.add_word(p.first.c_str(ctx), p.second.as_bits());
                }
            } break;
            case id_USR_RSTN.index:
            case id_CFG_CTRL.index:
                break;
            default:
                log_error("Unhandled cell %s of type %s\n", cell.second.get()->name.c_str(ctx),
                          cell.second->type.c_str(ctx));
            }
        }

        for (int i = 0; i < uarch->dies; i++) {
            cc.configs[i].add_word("GPIO.BANK_N1", int_to_bitvector(bank[i][0], 1));
            cc.configs[i].add_word("GPIO.BANK_N2", int_to_bitvector(bank[i][1], 1));
            cc.configs[i].add_word("GPIO.BANK_E1", int_to_bitvector(bank[i][2], 1));
            cc.configs[i].add_word("GPIO.BANK_E2", int_to_bitvector(bank[i][3], 1));
            cc.configs[i].add_word("GPIO.BANK_W1", int_to_bitvector(bank[i][4], 1));
            cc.configs[i].add_word("GPIO.BANK_W2", int_to_bitvector(bank[i][5], 1));
            cc.configs[i].add_word("GPIO.BANK_S1", int_to_bitvector(bank[i][6], 1));
            cc.configs[i].add_word("GPIO.BANK_S2", int_to_bitvector(bank[i][7], 1));
            cc.configs[i].add_word("GPIO.BANK_CFG", int_to_bitvector(bank[i][8], 1));
        }
        if (uarch->dies == 2) {
            cc.configs[0].add_word("D2D.N", int_to_bitvector(1, 1));
            cc.configs[1].add_word("D2D.S", int_to_bitvector(1, 1));
        }

        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            if (ni->wires.empty())
                continue;
            for (auto &w : ni->wires) {
                if (w.second.pip != PipId())
                    export_connection(cc, w.second.pip);
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

NEXTPNR_NAMESPACE_END
