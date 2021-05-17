
/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021 gatecat <gatecat@ds0.me>
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

#include "validity.h"
#include <boost/optional.hpp>
#include "log.h"
#include "nextpnr.h"
#include "tile_status.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {
struct FFType
{
    IdString clk_port, sr_port, ce_port, d_port, q_port;
    bool is_latch, is_async;
};
static const dict<IdString, FFType> ff_types = {
        {id_FDRE, {id_C, id_R, id_CE, id_D, id_Q, false, false}},
        {id_FDSE, {id_C, id_S, id_CE, id_D, id_Q, false, false}},
        {id_FDCE, {id_C, id_CLR, id_CE, id_D, id_Q, false, true}},
        {id_FDPE, {id_C, id_PRE, id_CE, id_D, id_Q, false, true}},
        {id_LDCE, {id_G, id_CLR, id_GE, id_D, id_Q, true, true}},
        {id_LDPE, {id_G, id_PRE, id_GE, id_D, id_Q, true, true}},
};

struct LUTType
{
    int input_count;
    enum LUTStyle
    {
        LUT,
        DPRAM,
        SPRAM,
        SRL,
        CARRY,
    } style;
};

static const dict<IdString, LUTType> lut_types = {
        {id_LUT1, {1, LUTType::LUT}},       {id_LUT2, {2, LUTType::LUT}},        {id_LUT3, {3, LUTType::LUT}},
        {id_LUT4, {4, LUTType::LUT}},       {id_LUT5, {5, LUTType::LUT}},        {id_LUT6, {6, LUTType::LUT}},
        {id_RAMD32, {5, LUTType::DPRAM}},   {id_RAMD32M64, {6, LUTType::DPRAM}}, {id_RAMD64E, {6, LUTType::DPRAM}},
        {id_RAMD64E5, {6, LUTType::DPRAM}}, {id_RAMS32, {5, LUTType::SPRAM}},    {id_RAMS64E, {6, LUTType::SPRAM}},
        {id_RAMS64E1, {6, LUTType::SPRAM}}, {id_RAMS64E5, {6, LUTType::SPRAM}},  {id_SRL16E, {4, LUTType::SRL}},
        {id_SRLC16E, {4, LUTType::SRL}},    {id_SRLC32E, {5, LUTType::SRL}},     {id_LUTCY1, {5, LUTType::CARRY}},
        {id_LUTCY2, {5, LUTType::CARRY}},
};

IdString get_lut_input(const Context *ctx, IdString cell_type, const LUTType &type_data, int i)
{
    static const std::array<IdString, 6> lut_inputs{id_I0, id_I1, id_I2, id_I3, id_I4, id_I5};
    static const std::array<IdString, 6> dpr_inputs{id_RADR0, id_RADR1, id_RADR2, id_RADR3, id_RADR4, id_RADR5};
    static const std::array<IdString, 6> spr_inputs{id_ADR0, id_ADR1, id_ADR2, id_ADR3, id_ADR4, id_ADR5};
    static const std::array<IdString, 4> srl16_inputs{id_A0, id_A1, id_A2, id_A3};

    if (type_data.style == LUTType::LUT || type_data.style == LUTType::CARRY)
        return lut_inputs.at(i);
    else if (type_data.style == LUTType::DPRAM)
        return dpr_inputs.at(i);
    else if (type_data.style == LUTType::SPRAM)
        return spr_inputs.at(i);
    else if (type_data.style == LUTType::SRL)
        return cell_type == id_SRLC32E ? ctx->id(stringf("A[%d]", i)) : srl16_inputs.at(i);
    else
        NPNR_ASSERT_FALSE("unknown LUT style :nya_confused~1:");
}

void assign_lut_info(Context *ctx, CellInfo *cell, const LUTType &type_data)
{
    // Defaults
    cell->lutInfo.is_memory = false;
    cell->lutInfo.is_srl = false;
    cell->lutInfo.di = nullptr;
    cell->lutInfo.wclk = nullptr;
    cell->lutInfo.wclk_inv = false;
    cell->lutInfo.we = nullptr;
    cell->lutInfo.we2 = nullptr;
    cell->lutInfo.out_casc = nullptr;

    cell->lutInfo.input_count = type_data.input_count;
    for (int i = 0; i < cell->lutInfo.input_count; i++)
        cell->lutInfo.input_sigs[i] = cell->getPort(get_lut_input(ctx, cell->type, type_data, i));

    cell->lutInfo.out = cell->getPort(type_data.style == LUTType::SRL ? id_Q : id_O);
    if (type_data.style == LUTType::SPRAM || type_data.style == LUTType::DPRAM) {
        cell->lutInfo.is_memory = true;
        cell->lutInfo.di = cell->getPort(id_I);
        cell->lutInfo.wclk = cell->getPort(id_CLK);
        cell->lutInfo.wclk_inv = bool_or_default(cell->params, ctx->id("IS_CLK_INVERTED"), false);
        cell->lutInfo.we = cell->getPort(id_WE);
        cell->lutInfo.we2 = cell->getPort(id_WE2); // N.B. Versal only
        static const std::array<IdString, 3> msb_inputs{id_WADR6, id_WADR7, id_WADR8};
        for (int i = 0; i < 3; i++)
            cell->lutInfo.address_msb[i] = cell->getPort(msb_inputs[i]);
    } else if (type_data.style == LUTType::SRL) {
        cell->lutInfo.is_srl = true;
        cell->lutInfo.di = cell->getPort(id_D);
        cell->lutInfo.wclk = cell->getPort(id_CLK);
        cell->lutInfo.wclk_inv = bool_or_default(cell->params, ctx->id("IS_CLK_INVERTED"), false);
        cell->lutInfo.we = cell->getPort(id_CE);
        cell->lutInfo.out_casc = cell->getPort((cell->type == id_SRLC32E) ? id_Q31 : id_Q15);
    }
}

void asssign_ff_info(Context *ctx, CellInfo *cell, const FFType &type_data)
{
    cell->ffInfo.is_latch = type_data.is_latch;
    cell->ffInfo.is_async = type_data.is_async;
    cell->ffInfo.is_clkinv =
            bool_or_default(cell->params, ctx->id(stringf("IS_%s_INVERTED", type_data.clk_port.c_str(ctx))));
    cell->ffInfo.is_srinv =
            bool_or_default(cell->params, ctx->id(stringf("IS_%s_INVERTED", type_data.sr_port.c_str(ctx))));
    cell->ffInfo.clk = cell->getPort(type_data.clk_port);
    cell->ffInfo.sr = cell->getPort(type_data.sr_port);
    cell->ffInfo.ce = cell->getPort(type_data.ce_port);
    cell->ffInfo.d = cell->getPort(type_data.d_port);
}

void assign_carry_info(Context *ctx, CellInfo *cell)
{
    if (cell->type.in(id_CARRY4, id_CARRY8)) {
        int carry_height = (cell->type == id_CARRY4 ? 4 : 8);
        NetInfo *ci = cell->getPort(id_CI);
        NetInfo *cyinit = cell->getPort(id_CYINIT);
        if ((!ci || ci->name == id_GLOBAL_LOGIC0) && cyinit)
            ci = cyinit;
        cell->carryInfo.ci_using_ax = false;
        if (ci && ci->driver.cell && ci->name != id_GLOBAL_LOGIC0 && ci->name != id_GLOBAL_LOGIC1 &&
            cell->cluster_info.tile_dy == 0) {
            // AX for CIN
            cell->carryInfo.x[0] = ci;
            cell->carryInfo.ci_using_ax = true;
        }
        for (int i = 0; i < carry_height; i++) {
            cell->carryInfo.di_using_x[i] = false;
            // Sum/carry outputs
            cell->carryInfo.out[i] = cell->getPort(ctx->id(stringf("O[%d]", i)));
            cell->carryInfo.cout[i] = cell->getPort(ctx->id(stringf("CO[%d]", i)));
            // Process DI/X inputs
            cell->carryInfo.di_port[i] = ctx->id(stringf("DI[%d]", i)).index;
            NetInfo *di = cell->getPort(IdString(cell->carryInfo.di_port[i]));
            cell->carryInfo.di[i] = di;
            if (i != 0 || !cell->carryInfo.ci_using_ax)
                cell->carryInfo.x[i] = nullptr;
            // Check if DI is using the direct path or using X
            if (!di || !di->driver.cell)
                continue;
            if (di->driver.cell->cluster != cell->cluster)
                goto using_x;
            {
                auto &di_c = di->driver.cell->cluster_info, &cy_c = cell->cluster_info;
                LogicBelIdx di_idx(di_c.place_idx);
                if (di_c.tile_dy != cy_c.tile_dy)
                    goto using_x;
                if (di_idx.bel() != LogicBelIdx::LUT5 || int(di_idx.eighth()) != i)
                    goto using_x;
            }
            if (0) {
            using_x:
                cell->carryInfo.x[i] = di;
                cell->carryInfo.di_using_x[i] = true;
            }
        }
    } else if (cell->type == id_LOOKAHEAD8) {
        // TODO
    }
}

void assign_mux_info(Context *ctx, CellInfo *cell)
{
    cell->muxInfo.out = cell->getPort(id_O);
    cell->muxInfo.sel = cell->getPort(id_S);
}

void assign_cell_info(Context *ctx, CellInfo *cell)
{
    auto fnd_lut = lut_types.find(cell->type);
    if (fnd_lut != lut_types.end()) {
        assign_lut_info(ctx, cell, fnd_lut->second);
        return;
    }
    auto fnd_ff = ff_types.find(cell->type);
    if (fnd_ff != ff_types.end()) {
        asssign_ff_info(ctx, cell, fnd_ff->second);
    }
    if (cell->type.in(id_CARRY4, id_CARRY8, id_LOOKAHEAD8)) {
        assign_carry_info(ctx, cell);
    }
    if (cell->type.in(id_MUXF7, id_MUXF8, id_MUXF9)) {
        assign_mux_info(ctx, cell);
    }
}

#if 0
#define reject                                                                                                         \
    do {                                                                                                               \
        log_info("%d %d\n", eighth, __LINE__);                                                                         \
        return false;                                                                                                  \
    } while (0)
#else
#define reject return false
#endif

bool check_logic_eighth(const LogicSiteStatus &site, ArchFamily family, int eighth)
{
    const CellInfo *lut5 = site.get_cell(eighth, LogicBelIdx::LUT5);
    const CellInfo *lut6 = site.get_cell(eighth, LogicBelIdx::LUT6);

    // Fracturable LUT related checks whenever both are used
    if (lut5 && lut6) {
        if (lut5->lutInfo.is_srl != lut6->lutInfo.is_srl)
            reject;
        if (lut5->lutInfo.is_memory != lut6->lutInfo.is_memory)
            reject;
        if (lut6->lutInfo.input_count == 6)
            reject; // LUT6 fully used means LUT5 cannot be used (TODO: check LUT function too?)
        // If more than 5 total inputs are used, count how many can be shared
        if ((lut6->lutInfo.input_count + lut5->lutInfo.input_count) > 5) {
            int shared = 0, need_shared = (lut6->lutInfo.input_count + lut5->lutInfo.input_count - 5);
            for (int j = 0; j < lut6->lutInfo.input_count; j++) {
                for (int k = 0; k < lut5->lutInfo.input_count; k++) {
                    if (lut6->lutInfo.input_sigs[j] == lut5->lutInfo.input_sigs[k])
                        shared++;
                    if (shared >= need_shared)
                        break;
                }
            }
            if (shared < need_shared)
                reject;
        }
    }

    // Check (over)usage of DI and X inputs
    const NetInfo *x_net = nullptr, *i_net = nullptr;
    if (lut6 && lut6->lutInfo.di) {
        // RAM32/SRL16 uses DI2, otherwise DI1 used
        if ((lut6->lutInfo.is_srl && lut6->lutInfo.input_count == 4) ||
            (lut6->lutInfo.is_memory && lut6->lutInfo.input_count == 5))
            x_net = lut6->lutInfo.di;
        else
            i_net = lut6->lutInfo.di;
    }

    if (lut6 && lut6->lutInfo.we2) {
        NPNR_ASSERT(x_net == nullptr);
        x_net = lut6->lutInfo.we2;
    }

    if (lut5 && lut5->lutInfo.di) {
        if (i_net == nullptr)
            i_net = lut5->lutInfo.di;
        else if (i_net != lut5->lutInfo.di)
            reject;
    }

    const CellInfo *out_fmux = nullptr;

    if (family != ArchFamily::VERSAL) {

        CellInfo *mux = nullptr;
        // Eighths A, C, E, G: F7MUX uses X input
        if (eighth == 0 || eighth == 2 || eighth == 4 || eighth == 6)
            mux = site.get_cell(eighth, LogicBelIdx::F7MUX);
        // Eighths B, F: F8MUX uses X input
        if (eighth == 1 || eighth == 5)
            mux = site.get_cell(eighth - 1, LogicBelIdx::F8MUX);
        // Eighths D: F9MUX uses X input
        if (eighth == 3)
            mux = site.get_cell(0, LogicBelIdx::F9MUX);

        if (mux) {
            if (x_net == nullptr)
                x_net = mux->muxInfo.sel;
            else if (x_net != mux->muxInfo.sel)
                reject;
        }

        // Eighths B, D, F, H: F7MUX connects to F7F8 out
        if (eighth == 1 || eighth == 3 || eighth == 5 || eighth == 7)
            out_fmux = site.get_cell(eighth - 1, LogicBelIdx::F7MUX);
        // Eighths C, G: F8MUX connects to F7F8 out
        if (eighth == 2 || eighth == 6)
            out_fmux = site.get_cell(eighth - 2, LogicBelIdx::F8MUX);
        // Eighths E: F9MUX connects to F7F8 out
        if (eighth == 4)
            out_fmux = site.get_cell(0, LogicBelIdx::F9MUX);
    }

    const CellInfo *carry = site.get_cell(0, LogicBelIdx::CARRY);

    // FF1 input might use X, if it isn't driven directly
    const CellInfo *ff1 = site.get_cell(eighth, LogicBelIdx::FF);
    if (ff1 && ff1->ffInfo.d) {
        NetInfo *d = ff1->ffInfo.d;
        if ((lut5 && d == lut5->lutInfo.out) || (lut6 && d == lut6->lutInfo.out) ||
            (out_fmux && d == out_fmux->muxInfo.out) ||
            (carry && family != ArchFamily::VERSAL &&
             (d == carry->carryInfo.out[eighth] || d == carry->carryInfo.cout[eighth]))) {
            // Direct, OK
        } else {
            // Indirect, must use X input
            if (x_net == nullptr)
                x_net = d;
            else if (x_net != d)
                reject;
        }
    }

    // FF2 input might use I/X, if it isn't driven directly
    const CellInfo *ff2 = site.get_cell(eighth, LogicBelIdx::FF2);
    if (ff2 && ff2->ffInfo.d) {
        NetInfo *d = ff2->ffInfo.d;
        if (family == ArchFamily::XC7) {
            // xc7 has limited 5FF mux and no I input
            if (lut5 && d == lut5->lutInfo.out) {
                // Direct, OK
            } else {
                // Indirect, must use X input
                if (x_net == nullptr)
                    x_net = d;
                else if (x_net != d)
                    reject;
            }
        } else {
            if ((lut5 && d == lut5->lutInfo.out && family != ArchFamily::VERSAL) || (lut6 && d == lut6->lutInfo.out) ||
                (out_fmux && d == out_fmux->muxInfo.out) ||
                (carry && (d == carry->carryInfo.out[eighth] || d == carry->carryInfo.cout[eighth]))) {
                // Direct, OK
            } else {
                // Indirect, must use X input
                if (i_net == nullptr)
                    i_net = d;
                else if (i_net != d)
                    reject;
            }
        }
    }

    // Carry input bypassing LUT
    if (carry && carry->carryInfo.x[eighth]) {
        if (x_net == nullptr)
            x_net = carry->carryInfo.x[eighth];
        else
            reject;
    }

    if (family == ArchFamily::VERSAL) {
        bool q1_used = (ff1 != nullptr), q2_used = (ff2 != nullptr);
        if (carry && carry->carryInfo.cout[eighth]) {
            const NetInfo *co = carry->carryInfo.cout[eighth];
            for (const auto &usr : co->users) {
                if (usr.cell != ff2 || usr.port != id_D) {
                    if (q2_used)
                        reject;
                    else
                        q2_used = true;
                }
            }
        }
        if (lut5 && lut5->lutInfo.out) {
            const NetInfo *l5o = lut5->lutInfo.out;
            for (const auto &usr : l5o->users) {
                if (usr.cell == ff1 && usr.port == id_D)
                    continue;
                if (!q1_used) {
                    q1_used = true;
                    break;
                }
                reject;
            }
        }
    } else {
        // Write address MSBs (not used for Versal)
        const CellInfo *top_lut = site.get_cell(family == ArchFamily::XC7 ? 3 : 7, LogicBelIdx::LUT6);
        if (top_lut && top_lut->lutInfo.is_memory && top_lut->lutInfo.input_count == 6) {
            if (eighth == (family == ArchFamily::XC7 ? 2 : 6) && x_net != top_lut->lutInfo.address_msb[0])
                reject;
            if (eighth == (family == ArchFamily::XC7 ? 1 : 5) && x_net != top_lut->lutInfo.address_msb[1])
                reject;
            if (family != ArchFamily::XC7 && eighth == 3 && x_net != top_lut->lutInfo.address_msb[2])
                reject;
        }
        // 'Mux' output legality
        bool mux_output_used = false;
        if (lut5 && lut5->lutInfo.out)
            for (auto &usr : lut5->lutInfo.out->users) {
                // Dedicated paths
                if ((usr.cell == ff1 || usr.cell == ff2) && usr.port == id_D)
                    continue;
                if (usr.cell == carry && usr.port == IdString(carry->carryInfo.di_port[eighth]))
                    continue;
                mux_output_used = true;
                break;
            }
        auto check_omux_net = [&](const NetInfo *net, bool is_co) {
            if (net) {
                for (auto &usr : net->users) {
                    if ((usr.cell == ff1 || (family != ArchFamily::XC7 && usr.cell == ff2)) && usr.port == id_D)
                        continue;
                    if (is_co && eighth == (family == ArchFamily::XC7 ? 3 : 7) &&
                        usr.cell->type == (family == ArchFamily::XC7 ? id_CARRY4 : id_CARRY8) && usr.port == id_CI &&
                        usr.cell->cluster_info.tile_dy != 0)
                        continue;
                    if (mux_output_used) {
                        return false;
                    } else {
                        mux_output_used = true;
                        break;
                    }
                }
            }
            return true;
        };
        // Mux output can be used for O[i]/CO[i]
        if (carry) {
            if (!check_omux_net(carry->carryInfo.out[eighth], false))
                reject;
            if (!check_omux_net(carry->carryInfo.cout[eighth], true))
                reject;
        }
        // Mux output can be used for F[789]MUX
        if (out_fmux && out_fmux->muxInfo.out) {
            if (!check_omux_net(out_fmux->muxInfo.out, false))
                reject;
        }
        if (eighth == (family == ArchFamily::XC7 ? 3 : 0)) {
            // Mux output can be used for end of SRL cascade chain
            const CellInfo *casc_lut = (family == ArchFamily::XC7 ? site.get_cell(0, LogicBelIdx::LUT6) : lut6);
            if (casc_lut && casc_lut->lutInfo.out_casc) {
                if (mux_output_used)
                    reject;
                else
                    mux_output_used = true;
            }
        }
    }

    return true;
}

static bool update_check_validity(const LogicSiteStatus &site, ArchFamily family)
{
    for (int i = 0; i < (family == ArchFamily::XC7 ? 4 : 8); i++) {
        if (site.eighth_status[i].dirty) {
            site.eighth_status[i].valid = check_logic_eighth(site, family, i);
            site.eighth_status[i].dirty = false;
        }
        if (!site.eighth_status[i].valid)
            return false;
    }
    for (int i = 0; i < (family == ArchFamily::XC7 ? 1 : 2); i++) {
        if (site.half_status[i].dirty) {
            site.half_status[i].valid = check_ff_ctrlset(site, family, i);
            site.half_status[i].dirty = false;
        }
        if (!site.half_status[i].valid)
            return false;
    }
    if (site.tile_dirty) {
        site.tile_valid = check_tile_ctrlset(site, family);
        site.tile_dirty = false;
    }
    if (!site.tile_valid)
        return false;
    return true;
}

}; // namespace

bool check_ff_ctrlset(const LogicSiteStatus &site, ArchFamily family, int half)
{
    const NetInfo *clk = nullptr, *sr = nullptr;
    std::array<const NetInfo *, 2> ce{nullptr, nullptr};
    bool clkinv = false, srinv = false, async = false, latch = false;
    std::array<bool, 2> found{false, false};
    for (int i = 0; i < 4; i++) {
        for (LogicBelIdx::LogicBel bel : {LogicBelIdx::FF, LogicBelIdx::FF2}) {
            const CellInfo *ff = site.get_cell(half * 4 + i, bel);
            if (!ff)
                continue;
            // Only US/US+ has CLK/SR per half rather than per tile
            if (family == ArchFamily::XCU || family == ArchFamily::XCUP) {
                if (found[0] || found[1]) {
                    if (clk != ff->ffInfo.clk)
                        return false;
                    if (clkinv != ff->ffInfo.is_clkinv)
                        return false;
                    if (sr != ff->ffInfo.sr)
                        return false;
                    if (srinv != ff->ffInfo.is_srinv)
                        return false;
                    if (latch != ff->ffInfo.is_latch)
                        return false;
                    if (async != ff->ffInfo.is_async)
                        return false;
                } else {
                    clk = ff->ffInfo.clk;
                    clkinv = ff->ffInfo.is_clkinv;
                    sr = ff->ffInfo.sr;
                    srinv = ff->ffInfo.is_srinv;
                    latch = ff->ffInfo.is_latch;
                    async = ff->ffInfo.is_async;
                }
            }
            // Work out which CE group we are in
            int ce_idx = (family == ArchFamily::VERSAL)
                                 ? (i / 2)
                                 : (family == ArchFamily::XC7 ? 0 : (bel == LogicBelIdx::FF2 ? 1 : 0));
            if (found[ce_idx]) {
                if (ce[ce_idx] != ff->ffInfo.ce)
                    return false;
            } else {
                found[ce_idx] = true;
                ce[ce_idx] = ff->ffInfo.ce;
            }
        }
    }
    return true;
}

bool check_tile_ctrlset(const LogicSiteStatus &site, ArchFamily family)
{
    bool is_memory = false, is_srl = false, found_clk_usr = false, found_sr_usr = false;
    const NetInfo *clk = nullptr, *sr = nullptr;
    bool clkinv = false, srinv = false, async = false, latch = false;
    for (int i = 0; i < (family == ArchFamily::XC7 ? 4 : 8); i++) {
        for (LogicBelIdx::LogicBel bel : {LogicBelIdx::LUT6, LogicBelIdx::LUT5}) {
            const CellInfo *lut = site.get_cell(i, bel);
            if (!lut)
                continue;
            // Mixing of memory and SRL not allowed
            if (lut->lutInfo.is_memory) {
                if (is_srl)
                    return false;
                is_memory = true;
            } else if (lut->lutInfo.is_srl) {
                if (is_memory)
                    return false;
                is_srl = true;
            } else {
                continue;
            }
            // Shared write clock
            if (!found_clk_usr) {
                clk = lut->lutInfo.wclk;
                clkinv = lut->lutInfo.wclk_inv;
            } else {
                if (clk != lut->lutInfo.wclk)
                    return false;
                if (clkinv != lut->lutInfo.wclk_inv)
                    return false;
            }
        }
    }
    // Everything but XCU/XCUP shares FF CLK/SR across tile and FF CLK with LUT WCLK
    if (family != ArchFamily::XCU && family != ArchFamily::XCUP) {
        for (int i = 0; i < (family == ArchFamily::XC7 ? 4 : 8); i++) {
            for (LogicBelIdx::LogicBel bel : {LogicBelIdx::FF, LogicBelIdx::FF2}) {
                const CellInfo *ff = site.get_cell(i, bel);
                if (!ff)
                    continue;
                if (!found_clk_usr) {
                    clk = ff->ffInfo.clk;
                    clkinv = ff->ffInfo.is_clkinv;
                    found_clk_usr = true;
                } else {
                    if (clk != ff->ffInfo.clk)
                        return false;
                    if (clkinv != ff->ffInfo.is_clkinv)
                        return false;
                }
                if (!found_sr_usr) {
                    sr = ff->ffInfo.sr;
                    srinv = ff->ffInfo.is_srinv;
                    latch = ff->ffInfo.is_latch;
                    async = ff->ffInfo.is_async;
                    found_sr_usr = true;
                } else {
                    if (sr != ff->ffInfo.sr)
                        return false;
                    if (srinv != ff->ffInfo.is_srinv)
                        return false;
                    if (latch != ff->ffInfo.is_latch)
                        return false;
                    if (async != ff->ffInfo.is_async)
                        return false;
                }
            }
        }
    }
    return true;
}

bool check_bram_tile_conflicts(const TileTypePOD &tile_type, const TileStatus &tile_status)
{
    bool ram36_used = false, ram18_used = false;
    for (int i = 0; i < tile_type.sites.ssize(); i++) {
        auto &site = tile_type.sites[i];
        auto &site_status = tile_status.sites.at(i);
        IdString prefix(site.site_prefix);
        NPNR_ASSERT(prefix.in(id_RAMB18_, id_RAMB36_));
        if (site_status.bound_count == 0)
            continue;
        if (prefix == id_RAMB36_)
            ram36_used = true;
        else
            ram18_used = true;
    }
    return !ram36_used || !ram18_used; // RAM18 and RAM36 can't be used at once
}

bool Arch::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    auto &bel_data = chip_bel_info(chip_info, bel);
    if (bel_data.site == -1)
        return true;

    if (is_logic_site(bel.tile, bel_data.site)) {
        return update_check_validity(*(tile_status.at(bel.tile).sites.at(bel_data.site).logic.get()), family);
    }

    if (is_bram_site(bel.tile, bel_data.site)) {
        return check_bram_tile_conflicts(chip_tile_info(chip_info, bel.tile), tile_status.at(bel.tile));
    }

    return true;
}

void Arch::assignArchInfo()
{
    for (auto &cell : cells)
        assign_cell_info(getCtx(), cell.second.get());
}

NEXTPNR_NAMESPACE_END
