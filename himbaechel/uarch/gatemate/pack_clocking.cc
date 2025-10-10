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

#include <boost/algorithm/string.hpp>
#include "design_utils.h"

#include "gatemate_util.h"
#include "pack.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

inline bool is_bufg(const BaseCtx *ctx, const CellInfo *cell) { return cell->type.in(id_CC_BUFG); }

void GateMatePacker::sort_bufg()
{
    struct ItemBufG
    {
        CellInfo *cell;
        int32_t fan_out;
        ItemBufG(CellInfo *cell, int32_t fan_out) : cell(cell), fan_out(fan_out) {}
    };

    log_info("Sort BUFGs..\n");
    std::vector<ItemBufG> bufg;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_BUFG))
            continue;

        NetInfo *i_net = ci.getPort(id_I);
        if (!i_net) {
            log_warning("Removing BUFG cell %s since there is no input used.\n", ci.name.c_str(ctx));
            packed_cells.emplace(ci.name); // Remove if no input
            continue;
        }
        NetInfo *o_net = ci.getPort(id_O);
        if (!o_net) {
            log_warning("Removing BUFG cell %s since there is no output used.\n", ci.name.c_str(ctx));
            packed_cells.emplace(ci.name); // Remove if no output
            continue;
        }
        bufg.push_back(ItemBufG(&ci, o_net->users.entries()));
    }

    if (bufg.size() > 4) {
        log_warning("More than 4 BUFG used. Those with highest fan-out will be used.\n");
        std::sort(bufg.begin(), bufg.end(), [](const ItemBufG &a, const ItemBufG &b) { return a.fan_out > b.fan_out; });
        for (size_t i = 4; i < bufg.size(); i++) {
            log_warning("Removing BUFG cell %s.\n", bufg.at(i).cell->name.c_str(ctx));
            CellInfo *cell = bufg.at(i).cell;
            NetInfo *i_net = cell->getPort(id_I);
            NetInfo *o_net = cell->getPort(id_O);
            for (auto s : o_net->users) {
                s.cell->disconnectPort(s.port);
                s.cell->connectPort(s.port, i_net);
            }
            packed_cells.emplace(bufg.at(i).cell->name);
        }
    }
    flush_cells();
}

static int glb_mux_mapping[] = {
        // CLK0_0 CLK90_0 CLK180_0 CLK270_0 CLK0_1 CLK0_2 CLK0_3
        4, 5, 6, 7, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, // GLBOUT 0
        // CLK90_0 CLK0_1 CLK90_1 CLK180_1 CLK270_1 CLK90_2 CLK90_3
        0, 1, 0, 0, 4, 5, 6, 7, 0, 2, 0, 0, 0, 3, 0, 0, // GLBOUT 1
        // CLK180_0 CLK180_1 CLK0_2 CLK90_2 CLK180_2 CLK270_2 CLK180_3
        0, 0, 1, 0, 0, 0, 2, 0, 4, 5, 6, 7, 0, 0, 3, 0, // GLBOUT 2
        // CLK270_0 CLK270_1 CLK270_2 CLK0_3 CLK90_3 CLK180_3 CLK270_3
        0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 4, 5, 6, 7, // GLBOUT 3
};

void GateMatePacker::pack_bufg()
{
    sort_bufg();

    log_info("Packing BUFGs..\n");
    auto update_bufg_port = [&](std::vector<CellInfo *> &bufg, CellInfo *cell, int port_num, int pll_num) {
        CellInfo *b = net_only_drives(ctx, cell->getPort(ctx->idf("CLK%d", 90 * port_num)), is_bufg, id_I, false);
        if (b) {
            if (bufg[port_num] == nullptr) {
                bufg[port_num] = b;
                return true;
            } else {
                if (bufg[pll_num] == nullptr) {
                    bufg[pll_num] = b;
                    return true;
                } else {
                    return false;
                }
            }
        }
        return true;
    };

    unsigned max_plls = 4 * uarch->dies;
    // Index vector for permutation
    std::vector<unsigned> indexes(max_plls);
    for (unsigned i = 0; i < max_plls; ++i)
        indexes[i] = i;

    std::vector<CellInfo *> bufg(max_plls, nullptr);
    std::vector<CellInfo *> pll(max_plls, nullptr);
    pool<IdString> used_bufg;
    bool valid = true;
    do {
        valid = true;
        std::vector<std::vector<CellInfo *>> tmp_bufg(uarch->dies, std::vector<CellInfo *>(4, nullptr));
        for (unsigned i = 0; i < max_plls; ++i) {
            if (indexes[i] < uarch->pll.size()) {
                for (int j = 0; j < 4; j++)
                    valid &= update_bufg_port(tmp_bufg[i >> 2], uarch->pll[indexes[i]], j, i & 3);
            }
        }
        if (valid) {
            for (unsigned i = 0; i < max_plls; ++i) {
                bufg[i] = tmp_bufg[i >> 2][i & 3];
                if (bufg[i])
                    used_bufg.insert(bufg[i]->name);
                if (indexes[i] < uarch->pll.size())
                    pll[i] = uarch->pll[indexes[i]];
            }
            break;
        }
    } while (std::next_permutation(indexes.begin(), indexes.end()));
    if (!valid)
        log_error("Unable to place PLLs and BUFGs\n");

    for (unsigned i = 0; i < max_plls; ++i) {

        int die = i >> 2;
        if (!pll[i])
            continue;
        CellInfo &ci = *pll[i];
        ci.cluster = ci.name;
        ci.constr_abs_z = true;
        ci.constr_z = 2 + (i & 3); // Position to a proper Z location

        Loc fixed_loc = uarch->locations[std::make_pair(ctx->idf("PLL%d", i & 3), die)];
        BelId pll_bel = ctx->getBelByLocation(fixed_loc);
        ctx->bindBel(pll_bel, &ci, PlaceStrength::STRENGTH_FIXED);

        pll_out(&ci, id_CLK0, fixed_loc);
        pll_out(&ci, id_CLK90, fixed_loc);
        pll_out(&ci, id_CLK180, fixed_loc);
        pll_out(&ci, id_CLK270, fixed_loc);

        move_ram_i_fixed(&ci, id_USR_PLL_LOCKED, fixed_loc);
        move_ram_i_fixed(&ci, id_USR_PLL_LOCKED_STDY, fixed_loc);
        move_ram_o_fixed(&ci, id_USR_LOCKED_STDY_RST, fixed_loc);
        move_ram_o_fixed(&ci, id_USR_CLK_REF, fixed_loc);
        move_ram_o_fixed(&ci, id_USR_SEL_A_B, fixed_loc);
    }

    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_BUFG))
            continue;
        if (used_bufg.count(cell.second->name))
            continue;

        NetInfo *in_net = ci.getPort(id_I);
        if (in_net) {
            if ((in_net->driver.cell && ctx->getBelBucketForCellType(in_net->driver.cell->type) != id_PLL) ||
                !in_net->driver.cell) {
                for (unsigned i = 0; i < max_plls; ++i) {
                    if (bufg[i] == nullptr && pll[i] == nullptr) { // PLL must not be used
                        bufg[i] = &ci;
                        break;
                    }
                }
            }
        }
    }

    for (unsigned j = 0; j < max_plls; ++j) {
        if (bufg[j]) {
            CellInfo &ci = *bufg[j];
            int i = j & 3;
            int die = j >> 2;
            uarch->global_signals.emplace(ci.getPort(id_O), j);
            int glb_mux = 0;
            NetInfo *in_net = ci.getPort(id_I);
            copy_constraint(in_net, ci.getPort(id_O));
            if (in_net->driver.cell) {
                bool user_glb = true;
                if (ctx->getBelBucketForCellType(in_net->driver.cell->type) == id_IOSEL) {
                    auto pad_info = uarch->bel_to_pad[in_net->driver.cell->bel];
                    if (pad_info->flags) {
                        uarch->clkin[die]->params[ctx->idf("REF%d", i)] = Property(pad_info->flags - 1, 3);
                        uarch->clkin[die]->params[ctx->idf("REF%d_INV", i)] = Property(Property::State::S0);
                        uarch->clkin[die]->connectPorts(ctx->idf("CLK_REF%d", i), uarch->glbout[die],
                                                        ctx->idf("CLK_REF_OUT%d", i));
                        int index = pad_info->flags - 1;
                        if (!uarch->clkin[die]->getPort(ctx->idf("CLK%d", index))) {
                            uarch->clkin[die]->connectPort(ctx->idf("CLK%d", index),
                                                           in_net->driver.cell->getPort(id_Y));
                        }
                        user_glb = false;
                    }
                }
                if (ctx->getBelBucketForCellType(in_net->driver.cell->type) == id_PLL) {
                    CellInfo *pll = in_net->driver.cell;
                    int pll_index = ctx->getBelLocation(pll->bel).z - 2;
                    int pll_out = 0;
                    if (pll->getPort(id_CLK0) == in_net)
                        pll_out = 0;
                    else if (pll->getPort(id_CLK90) == in_net)
                        pll_out = 1;
                    else if (pll->getPort(id_CLK180) == in_net)
                        pll_out = 2;
                    else if (pll->getPort(id_CLK270) == in_net)
                        pll_out = 3;
                    else
                        log_error("Uknown connecton on BUFG to PLL.\n");
                    glb_mux = glb_mux_mapping[i * 16 + pll_index * 4 + pll_out];
                    ci.movePortTo(id_I, uarch->glbout[die],
                                  ctx->idf("%s_%d", in_net->driver.port.c_str(ctx), pll_index));
                    user_glb = false;
                }
                if (user_glb) {
                    ci.movePortTo(id_I, uarch->glbout[die], ctx->idf("USR_GLB%d", i));
                    move_ram_o_fixed(uarch->glbout[die], ctx->idf("USR_GLB%d", i),
                                     ctx->getBelLocation(uarch->glbout[die]->bel));
                    uarch->glbout[die]->params[ctx->idf("USR_GLB%d_EN", i)] = Property(Property::State::S1);
                }
            } else {
                // SER_CLK
                uarch->clkin[die]->connectPort(id_SER_CLK, in_net);
                uarch->clkin[die]->params[ctx->idf("REF%d", i)] = Property(0b100, 3);
                uarch->clkin[die]->params[ctx->idf("REF%d_INV", i)] = Property(Property::State::S0);
                uarch->clkin[die]->connectPorts(ctx->idf("CLK_REF%d", i), uarch->glbout[die],
                                                ctx->idf("CLK_REF_OUT%d", i));
            }

            ci.movePortTo(id_O, uarch->glbout[die], ctx->idf("GLB%d", i));
            uarch->glbout[die]->params[ctx->idf("GLB%d_EN", i)] = Property(Property::State::S1);
            uarch->glbout[die]->params[ctx->idf("GLB%d_CFG", i)] = Property(glb_mux, 3);
            packed_cells.emplace(ci.name);
        }
    }

    for (auto &cell : uarch->pll) {
        CellInfo &ci = *cell;
        int i = ci.constr_z - 2;
        NetInfo *clk = ci.getPort(id_CLK_REF);
        int die = uarch->tile_extra_data(ci.bel.tile)->die;
        if (clk) {
            if (clk->driver.cell) {
                auto pad_info = uarch->bel_to_pad[clk->driver.cell->bel];
                uarch->clkin[die]->params[ctx->idf("REF%d", i)] = Property(pad_info->flags - 1, 3);
                uarch->clkin[die]->params[ctx->idf("REF%d_INV", i)] = Property(Property::State::S0);
                int index = pad_info->flags - 1;
                if (!uarch->clkin[die]->getPort(ctx->idf("CLK%d", index)))
                    ci.movePortTo(id_CLK_REF, uarch->clkin[die], ctx->idf("CLK%d", index));
                else
                    ci.disconnectPort(id_CLK_REF);
            } else {
                // SER_CLK
                uarch->clkin[die]->params[ctx->idf("REF%d", i)] = Property(0b100, 3);
                uarch->clkin[die]->params[ctx->idf("REF%d_INV", i)] = Property(Property::State::S0);
                ci.movePortTo(id_CLK_REF, uarch->clkin[die], id_SER_CLK);
            }
            uarch->clkin[die]->connectPorts(ctx->idf("CLK_REF%d", i), &ci, id_CLK_REF);
        }

        NetInfo *feedback_net = ci.getPort(id_CLK_FEEDBACK);
        if (feedback_net) {
            if (!uarch->global_signals.count(feedback_net)) {
                ci.movePortTo(id_CLK_FEEDBACK, uarch->glbout[die], ctx->idf("USR_FB%d", i));
                move_ram_o_fixed(uarch->glbout[die], ctx->idf("USR_FB%d", i),
                                 ctx->getBelLocation(uarch->glbout[die]->bel));
                uarch->glbout[die]->params[ctx->idf("USR_FB%d_EN", i)] = Property(Property::State::S1);
            } else {
                int index = uarch->global_signals[feedback_net];
                if ((index >> 2) != die)
                    log_error("TODO: Feedback signal from another die.\n");
                uarch->glbout[die]->params[ctx->idf("FB%d_CFG", i)] = Property(index, 2);
                ci.disconnectPort(id_CLK_FEEDBACK);
            }
            ci.connectPorts(id_CLK_FEEDBACK, uarch->glbout[die], ctx->idf("CLK_FB%d", i));
        }
    }

    flush_cells();
}

void GateMatePacker::pll_out(CellInfo *cell, IdString origPort, Loc fixed)
{
    NetInfo *net = cell->getPort(origPort);
    if (!net)
        return;
    CellInfo *bufg = nullptr;
    for (auto &usr : net->users) {
        if (usr.cell->type == id_CC_BUFG)
            bufg = usr.cell;
    }
    if (bufg) {
        if (net->users.entries() != 1) {
            log_error("not handled BUFG\n");
        }
    } else {
        move_ram_i_fixed(cell, origPort, fixed);
    }
}

void GateMatePacker::insert_clocking()
{
    log_info("Insert clocking cells..\n");
    for (int i = 0; i < uarch->dies; i++) {
        Loc fixed_loc = uarch->locations[std::make_pair(id_CLKIN, i)];
        uarch->clkin.push_back(create_cell_ptr(id_CLKIN, ctx->idf("CLKIN%d", i)));
        BelId clkin_bel = ctx->getBelByLocation(fixed_loc);
        ctx->bindBel(clkin_bel, uarch->clkin.back(), PlaceStrength::STRENGTH_FIXED);
        uarch->glbout.push_back(create_cell_ptr(id_GLBOUT, ctx->idf("GLBOUT%d", i)));
        fixed_loc = uarch->locations[std::make_pair(id_GLBOUT, i)];
        BelId glbout_bel = ctx->getBelByLocation(fixed_loc);
        ctx->bindBel(glbout_bel, uarch->glbout.back(), PlaceStrength::STRENGTH_FIXED);
    }
}

void GateMatePacker::remove_clocking()
{
    log_info("Remove unused clocking cells..\n");
    auto remove_unused_cells = [&](std::vector<CellInfo *> &cells) {
        for (auto cell : cells) {
            bool used = false;
            for (auto port : cell->ports) {
                if (cell->getPort(port.first)) {
                    used = true;
                    break;
                }
            }
            if (!used) {
                BelId bel = cell->bel;
                if (bel != BelId())
                    ctx->unbindBel(bel);
                packed_cells.emplace(cell->name);
            }
        }
    };
    remove_unused_cells(uarch->clkin);
    remove_unused_cells(uarch->glbout);
    flush_cells();
}

static const char *timing_mode_to_str(int mode)
{
    switch (mode) {
    case 1:
        return "LOWPOWER";
    case 2:
        return "ECONOMY";
    default:
        return "SPEED";
    }
}

void GateMatePacker::pack_pll()
{
    log_info("Packing PLLs..\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_PLL, id_CC_PLL_ADV))
            continue;

        disconnect_if_gnd(&ci, id_CLK_REF);
        disconnect_if_gnd(&ci, id_USR_CLK_REF);
        disconnect_if_gnd(&ci, id_CLK_FEEDBACK);
        disconnect_if_gnd(&ci, id_USR_LOCKED_STDY_RST);

        if (uarch->pll.size() >= (uarch->dies * 4U))
            log_error("Used more than available PLLs.\n");

        if (ci.getPort(id_CLK_REF) == nullptr && ci.getPort(id_USR_CLK_REF) == nullptr)
            log_error("At least one reference clock (CLK_REF or USR_CLK_REF) must be set for cell '%s'.\n",
                      ci.name.c_str(ctx));

        if (ci.getPort(id_CLK_REF) != nullptr && ci.getPort(id_USR_CLK_REF) != nullptr)
            log_error("CLK_REF and USR_CLK_REF are not allowed to be set in same time for cell '%s'.\n",
                      ci.name.c_str(ctx));

        NetInfo *clk = ci.getPort(id_CLK_REF);
        delay_t period = ctx->getDelayFromNS(1.0e9 / ctx->setting<float>("target_freq"));
        if (clk) {
            if (clk->driver.cell) {
                if (ctx->getBelBucketForCellType(clk->driver.cell->type) == id_CC_BUFG) {
                    NetInfo *in = clk->driver.cell->getPort(id_I);
                    ci.disconnectPort(id_CLK_REF);
                    ci.connectPort(id_CLK_REF, in);
                    clk = in;
                }
                if (ctx->getBelBucketForCellType(clk->driver.cell->type) != id_IOSEL)
                    log_error("CLK_REF must be driven with GPIO pin for cell '%s'.\n", ci.name.c_str(ctx));
                auto pad_info = uarch->bel_to_pad[clk->driver.cell->bel];
                if (pad_info->flags == 0)
                    log_error("CLK_REF must be driven with CLK dedicated pin for cell '%s'.\n", ci.name.c_str(ctx));
            } else {
                // SER_CLK
                if (clk != net_SER_CLK)
                    log_error("CLK_REF connected to uknown pin for cell '%s'.\n", ci.name.c_str(ctx));
            }
            if (clk->clkconstr)
                period = clk->clkconstr->period.minDelay();
        }

        clk = ci.getPort(id_USR_CLK_REF);
        if (clk) {
            ci.params[ctx->id("USR_CLK_REF")] = Property(0b1, 1);
            if (clk->driver.cell) {
                if (ctx->getBelBucketForCellType(clk->driver.cell->type) == id_CC_BUFG) {
                    NetInfo *in = clk->driver.cell->getPort(id_I);
                    ci.disconnectPort(id_USR_CLK_REF);
                    ci.connectPort(id_USR_CLK_REF, in);
                    clk = in;
                }
            }
            if (clk->clkconstr)
                period = clk->clkconstr->period.minDelay();
        }

        if (ci.getPort(id_CLK_REF_OUT))
            log_error("Output CLK_REF_OUT cannot be used if PLL '%s' is used.\n", ci.name.c_str(ctx));

        double out_clk_max = 0;
        int clk270_doub = 0;
        int clk180_doub = 0;
        if (ci.type == id_CC_PLL) {
            int low_jitter = int_or_default(ci.params, id_LOW_JITTER, 0);
            int ci_const = int_or_default(ci.params, id_CI_FILTER_CONST, 0);
            int cp_const = int_or_default(ci.params, id_CP_FILTER_CONST, 0);
            clk270_doub = int_or_default(ci.params, id_CLK270_DOUB, 0);
            clk180_doub = int_or_default(ci.params, id_CLK180_DOUB, 0);
            int lock_req = int_or_default(ci.params, id_LOCK_REQ, 0);

            if (!ci.getPort(id_CLK_FEEDBACK))
                ci.params[id_LOCK_REQ] = Property(lock_req, 1);
            ci.params[id_CLK180_DOUB] = Property(clk180_doub, 1);
            ci.params[id_CLK270_DOUB] = Property(clk270_doub, 1);
            std::string mode = str_or_default(ci.params, id_PERF_MD, "SPEED");
            boost::algorithm::to_upper(mode);
            int perf_md;
            double max_freq = 0.0;
            if (mode == "LOWPOWER") {
                perf_md = 1;
                max_freq = 250.00;
            } else if (mode == "ECONOMY") {
                perf_md = 2;
                max_freq = 312.50;
            } else if (mode == "SPEED") {
                perf_md = 3;
                max_freq = 416.75;
            } else {
                log_error("Unknown PERF_MD parameter value '%s' for cell %s.\n", mode.c_str(), ci.name.c_str(ctx));
            }

            if (perf_md != uarch->timing_mode)
                log_warning("PLL '%s' timing mode is '%s' but FPGA timing mode is '%s'.\n", ci.name.c_str(ctx),
                            timing_mode_to_str(perf_md), timing_mode_to_str(uarch->timing_mode));

            double ref_clk = double_or_default(ci.params, id_REF_CLK, 0.0);
            if (ref_clk <= 0 || ref_clk > 125)
                log_error("REF_CLK parameter is out of range (0,125.00] for '%s'.\n", ci.name.c_str(ctx));

            double out_clk = double_or_default(ci.params, id_OUT_CLK, 0.0);
            if (out_clk <= 0 || out_clk > max_freq)
                log_error("OUT_CLK parameter is out of range (0,%.2lf] for '%s'.\n", max_freq, ci.name.c_str(ctx));

            if ((ci_const < 1) || (ci_const > 31)) {
                log_warning("CI const out of range. Set to default CI = 2 for '%s'\n", ci.name.c_str(ctx));
                ci_const = 2;
            }
            if ((cp_const < 1) || (cp_const > 31)) {
                log_warning("CP const out of range. Set to default CP = 4 for '%s'\n", ci.name.c_str(ctx));
                cp_const = 4;
            }
            // PLL_cfg_val_800_1400  PLL values from 11.08.2021
            bool feedback = false;
            if (ci.getPort(id_CLK_FEEDBACK)) {
                ci.params[ctx->id("CFG_A_FB_PATH")] = Property(0b1, 1);
                feedback = true;
            }
            ci.params[ctx->id("CFG_A_FINE_TUNE")] = Property(0b00011001000, 11);
            ci.params[ctx->id("CFG_A_COARSE_TUNE")] = Property(0b100, 3);
            ci.params[ctx->id("CFG_A_AO_SW")] = Property(0b01000, 5);
            ci.params[ctx->id("CFG_A_OPEN_LOOP")] = Property(0b0, 1);
            ci.params[ctx->id("CFG_A_ENFORCE_LOCK")] = Property(0b0, 1);
            ci.params[ctx->id("CFG_A_PFD_SEL")] = Property(0b0, 1);
            ci.params[ctx->id("CFG_A_LOCK_DETECT_WIN")] = Property(0b0, 1);
            ci.params[ctx->id("CFG_A_SYNC_BYPASS")] = Property(0b0, 1);
            ci.params[ctx->id("CFG_A_FILTER_SHIFT")] = Property(0b10, 2);
            ci.params[ctx->id("CFG_A_FAST_LOCK")] = Property(0b1, 1);
            ci.params[ctx->id("CFG_A_SAR_LIMIT")] = Property(0b010, 3);
            ci.params[ctx->id("CFG_A_OP_LOCK")] = Property(0b0, 1);
            ci.params[ctx->id("CFG_A_PDIV0_MUX")] = Property(0b1, 1);
            ci.params[ctx->id("CFG_A_EN_COARSE_TUNE")] = Property(0b1, 1);
            ci.params[ctx->id("CFG_A_EN_USR_CFG")] = Property(0b0, 1);
            ci.params[ctx->id("CFG_A_PLL_EN_SEL")] = Property(0b0, 1);

            ci.params[ctx->id("CFG_A_CI_FILTER_CONST")] = Property(ci_const, 5);
            ci.params[ctx->id("CFG_A_CP_FILTER_CONST")] = Property(cp_const, 5);
            /*
                clock path selection
                0-0 PDIV0_MUX = 0, FB_PATH = 0 // DCO clock with intern feedback
                1-0 PDIV0_MUX = 1, FB_PATH = 0 // divided clock: PDIV1->M1->M2 with intern feedback  DEFAULT
                0-1 not possible  f_core = f_ref will set PDIV0_MUX = 1
                1-1 PDIV0_MUX = 1, FB_PATH = 1 // divided clock: PDIV1->M1->M2  with extern feedback
            PDIV1->M1->M2->PDIV0->N1->N2 }
            */
            bool pdiv0_mux = true;
            PllCfgRecord val = get_pll_settings(ref_clk, out_clk, perf_md, low_jitter, pdiv0_mux, feedback);
            if (val.f_core > 0) { // cfg exists
                ci.params[ctx->id("CFG_A_K")] = Property(val.K, 12);
                ci.params[ctx->id("CFG_A_N1")] = Property(val.N1, 6);
                ci.params[ctx->id("CFG_A_N2")] = Property(val.N2, 10);
                ci.params[ctx->id("CFG_A_M1")] = Property(val.M1, 6);
                ci.params[ctx->id("CFG_A_M2")] = Property(val.M2, 10);
                ci.params[ctx->id("CFG_A_PDIV1_SEL")] = Property(val.PDIV1 == 2 ? 1 : 0, 1);
            } else {
                log_error("Unable to configure PLL %s\n", ci.name.c_str(ctx));
            }
            // Remove all not propagated parameters
            ci.unsetParam(id_PERF_MD);
            ci.unsetParam(id_REF_CLK);
            ci.unsetParam(id_OUT_CLK);
            ci.unsetParam(id_LOW_JITTER);
            ci.unsetParam(id_CI_FILTER_CONST);
            ci.unsetParam(id_CP_FILTER_CONST);
            out_clk_max = out_clk;
        } else {
            // Handling CC_PLL_ADV
            for (int i = 0; i < 2; i++) {
                char cfg = 'A' + i;
                IdString id = i == 0 ? id_PLL_CFG_A : id_PLL_CFG_B;
                ci.params[ctx->idf("CFG_%c_CI_FILTER_CONST", cfg)] = Property(extract_bits(ci.params, id, 0, 5), 5);
                ci.params[ctx->idf("CFG_%c_CP_FILTER_CONST", cfg)] = Property(extract_bits(ci.params, id, 5, 5), 5);
                ci.params[ctx->idf("CFG_%c_N1", cfg)] = Property(extract_bits(ci.params, id, 10, 6), 6);
                ci.params[ctx->idf("CFG_%c_N2", cfg)] = Property(extract_bits(ci.params, id, 16, 10), 10);
                ci.params[ctx->idf("CFG_%c_M1", cfg)] = Property(extract_bits(ci.params, id, 26, 6), 6);
                ci.params[ctx->idf("CFG_%c_M2", cfg)] = Property(extract_bits(ci.params, id, 32, 10), 10);
                ci.params[ctx->idf("CFG_%c_K", cfg)] = Property(extract_bits(ci.params, id, 42, 12), 12);
                ci.params[ctx->idf("CFG_%c_FB_PATH", cfg)] = Property(extract_bits(ci.params, id, 54, 1), 1);
                ci.params[ctx->idf("CFG_%c_FINE_TUNE", cfg)] = Property(extract_bits(ci.params, id, 55, 11), 11);
                ci.params[ctx->idf("CFG_%c_COARSE_TUNE", cfg)] = Property(extract_bits(ci.params, id, 66, 3), 3);
                ci.params[ctx->idf("CFG_%c_AO_SW", cfg)] = Property(extract_bits(ci.params, id, 69, 5), 5);
                ci.params[ctx->idf("CFG_%c_OPEN_LOOP", cfg)] = Property(extract_bits(ci.params, id, 74, 1), 1);
                ci.params[ctx->idf("CFG_%c_ENFORCE_LOCK", cfg)] = Property(extract_bits(ci.params, id, 75, 1), 1);
                ci.params[ctx->idf("CFG_%c_PFD_SEL", cfg)] = Property(extract_bits(ci.params, id, 76, 1), 1);
                ci.params[ctx->idf("CFG_%c_LOCK_DETECT_WIN", cfg)] = Property(extract_bits(ci.params, id, 77, 1), 1);
                ci.params[ctx->idf("CFG_%c_SYNC_BYPASS", cfg)] = Property(extract_bits(ci.params, id, 78, 1), 1);
                ci.params[ctx->idf("CFG_%c_FILTER_SHIFT", cfg)] = Property(extract_bits(ci.params, id, 79, 2), 2);
                ci.params[ctx->idf("CFG_%c_FAST_LOCK", cfg)] = Property(extract_bits(ci.params, id, 81, 1), 1);
                ci.params[ctx->idf("CFG_%c_SAR_LIMIT", cfg)] = Property(extract_bits(ci.params, id, 82, 3), 3);
                ci.params[ctx->idf("CFG_%c_OP_LOCK", cfg)] = Property(extract_bits(ci.params, id, 85, 1), 1);
                ci.params[ctx->idf("CFG_%c_PDIV1_SEL", cfg)] = Property(extract_bits(ci.params, id, 86, 1), 1);
                ci.params[ctx->idf("CFG_%c_PDIV0_MUX", cfg)] = Property(extract_bits(ci.params, id, 87, 1), 1);
                ci.params[ctx->idf("CFG_%c_EN_COARSE_TUNE", cfg)] = Property(extract_bits(ci.params, id, 88, 1), 1);
                ci.params[ctx->idf("CFG_%c_EN_USR_CFG", cfg)] = Property(extract_bits(ci.params, id, 89, 1), 1);
                ci.params[ctx->idf("CFG_%c_PLL_EN_SEL", cfg)] = Property(extract_bits(ci.params, id, 90, 1), 1);
                int N1 = int_or_default(ci.params, ctx->idf("CFG_%c_N1", cfg));
                int N2 = int_or_default(ci.params, ctx->idf("CFG_%c_N2", cfg));
                int M1 = int_or_default(ci.params, ctx->idf("CFG_%c_M1", cfg));
                int M2 = int_or_default(ci.params, ctx->idf("CFG_%c_M2", cfg));
                int K = int_or_default(ci.params, ctx->idf("CFG_%c_K", cfg));
                int PDIV1 = bool_or_default(ci.params, ctx->idf("CFG_%c_PDIV1_SEL", cfg)) ? 2 : 0;
                double out_clk;
                double ref_clk = 1000.0f / ctx->getDelayNS(period);
                if (!bool_or_default(ci.params, ctx->idf("CFG_%c_FB_PATH", cfg))) {
                    if (bool_or_default(ci.params, ctx->idf("CFG_%c_PDIV0_MUX", cfg))) {
                        out_clk = (ref_clk * N1 * N2) / (K * 2 * M1 * M2);
                    } else {
                        out_clk = (ref_clk / K) * N1 * N2 * PDIV1;
                    }
                } else {
                    out_clk = (ref_clk / K) * N1 * N2;
                }
                if (out_clk > out_clk_max)
                    out_clk_max = out_clk;
            }
            NetInfo *select_net = ci.getPort(id_USR_SEL_A_B);
            if (select_net == nullptr || select_net == net_PACKER_GND) {
                ci.params[ctx->id("SET_SEL")] = Property(0b0, 1);
                ci.params[ctx->id("USR_SET")] = Property(0b0, 1);
                ci.disconnectPort(id_USR_SEL_A_B);
            } else if (select_net == net_PACKER_VCC) {
                ci.params[ctx->id("SET_SEL")] = Property(0b1, 1);
                ci.params[ctx->id("USR_SET")] = Property(0b0, 1);
                ci.disconnectPort(id_USR_SEL_A_B);
            } else {
                ci.params[ctx->id("USR_SET")] = Property(0b1, 1);
            }
            ci.params[ctx->id("LOCK_REQ")] = Property(0b1, 1);
            ci.unsetParam(id_PLL_CFG_A);
            ci.unsetParam(id_PLL_CFG_B);
            if (!ci.getPort(id_CLK_FEEDBACK))
                ci.params[ctx->id("LOCK_REQ")] = Property(0b1, 1);
        }

        // PLL control register A
        ci.params[ctx->id("PLL_RST")] = Property(0b1, 1);
        ci.params[ctx->id("PLL_EN")] = Property(0b1, 1);
        // PLL_AUTN - for Autonomous Mode - not set
        // SET_SEL - handled in CC_PLL_ADV
        // USR_SET - handled in CC_PLL_ADV
        // USR_CLK_REF - based on signals used
        ci.params[ctx->id("CLK_OUT_EN")] = Property(0b1, 1);
        // LOCK_REQ - set by CC_PLL parameter

        // PLL control register B
        // AUTN_CT_I - for Autonomous Mode - not set
        // CLK180_DOUB - set by CC_PLL parameter
        // CLK270_DOUB - set by CC_PLL parameter
        // bits 6 and 7 are unused
        // USR_CLK_OUT - part of routing, mux from chipdb

        if (ci.getPort(id_CLK0))
            ctx->addClock(ci.getPort(id_CLK0)->name, out_clk_max);
        if (ci.getPort(id_CLK90))
            ctx->addClock(ci.getPort(id_CLK90)->name, out_clk_max);
        if (ci.getPort(id_CLK180))
            ctx->addClock(ci.getPort(id_CLK180)->name, clk180_doub ? out_clk_max * 2 : out_clk_max);
        if (ci.getPort(id_CLK270))
            ctx->addClock(ci.getPort(id_CLK270)->name, clk270_doub ? out_clk_max * 2 : out_clk_max);

        ci.type = id_PLL;

        uarch->pll.push_back(&ci);
    }
}

void GateMatePacker::rewire_ram_o(CellInfo *first, IdString port, CellInfo *second)
{
    NetInfo *net = first->getPort(port);
    if (net && net->driver.cell) {
        net = net->driver.cell->getPort(id_I);
        if (net && net->driver.cell) {
            uint8_t val = int_or_default(net->driver.cell->params, id_INIT_L00, 0);
            switch (val) {
            case LUT_ZERO:
                net = net_PACKER_GND;
                break;
            case LUT_ONE:
                net = net_PACKER_VCC;
                break;
            case LUT_D0:
                net = net->driver.cell->getPort(id_IN1);
                break;
            default:
                log_error("Unsupported config, rewire from '%s' port '%s'\n", first->name.c_str(ctx), port.c_str(ctx));
                break;
            }
            second->connectPort(port, net);
        } else {
            log_error("Missing cell, rewire from '%s' port '%s'\n", first->name.c_str(ctx), port.c_str(ctx));
        }
    } else {
        log_error("Missing cell, rewire from '%s' port '%s'\n", first->name.c_str(ctx), port.c_str(ctx));
    }
}

void GateMatePacker::copy_clocks()
{
    if (uarch->dies == 1)
        return;
    switch (strategy) {
    case MultiDieStrategy::REUSE_CLK1:
        if (uarch->global_signals.size() > 1 || uarch->pll.size() > 1)
            log_error("Unable to use REUSE CLK1 strategy when there is more than one clock/PLL.\n");
        strategy_clk1();
        break;
    case MultiDieStrategy::CLOCK_MIRROR:
        if (uarch->global_signals.size() > 4 || uarch->pll.size() > 4)
            log_error("Unable to use MIRROR CLOCK strategy when there is more than 4 clocks/PLLs.\n");
        strategy_mirror();
        break;
    }
}

void GateMatePacker::strategy_clk1()
{
    log_info("Reuse CLK1 for clock distribution..\n");
    NetInfo *net = uarch->glbout[0]->getPort(id_GLB0);
    NetInfo *new_clk1 = ctx->createNet(ctx->id("$clk1$pin"));
    for (int new_die = 0; new_die < uarch->dies; new_die++) {
        CellInfo *iosel = create_cell_ptr(id_IOSEL, ctx->idf("$iosel_clk1$die%d", new_die));
        iosel->setParam(id_DELAY_IBF, Property(1, 16));
        iosel->setParam(id_INPUT_ENABLE, Property(1, 1));
        if (new_die == 0) {
            // On die 0 it should be output as well
            iosel->setParam(id_DELAY_OBF, Property(1, 16));
            iosel->setParam(id_OE_ENABLE, Property(1, 1));
            iosel->setParam(id_OUT_SIGNAL, Property(1, 1));
            iosel->setParam(id_SLEW, Property(1, 1));
        }

        BelId bel = ctx->getBelByLocation(uarch->locations[std::make_pair(ctx->id("IO_SB_A7"), new_die)]);
        ctx->bindBel(bel, iosel, PlaceStrength::STRENGTH_FIXED);

        CellInfo *gpio = create_cell_ptr((new_die ? id_CPE_IBUF : id_CPE_IOBUF), ctx->idf("$clk1$die%d", new_die));
        Loc loc = ctx->getBelLocation(bel);
        ctx->bindBel(ctx->getBelByLocation({loc.x, loc.y, 0}), gpio, PlaceStrength::STRENGTH_FIXED);

        uarch->clkin[new_die]->connectPort(id_CLK1, new_clk1);
        uarch->clkin[new_die]->params[ctx->id("REF1")] = Property(1, 3);
        uarch->glbout[new_die]->params[ctx->id("GLB1_EN")] = Property(Property::State::S1);
        uarch->glbout[new_die]->params[ctx->id("GLB1_CFG")] = Property(0, 3);
        uarch->clkin[new_die]->connectPorts(ctx->id("CLK_REF1"), uarch->glbout[new_die], ctx->id("CLK_REF_OUT1"));

        gpio->connectPorts(id_Y, iosel, id_GPIO_IN);

        if (new_die == 0) {
            iosel->connectPort(id_OUT1, ctx->getNetByAlias(uarch->global_signals.begin()->first->name));
            CellInfo *cpe = move_ram_o_fixed(iosel, id_OUT1, loc).first;
            uarch->ignore.emplace(cpe->name);

            iosel->connectPorts(id_GPIO_OUT, gpio, id_A);
            iosel->connectPorts(id_GPIO_EN, gpio, id_T);
            gpio->connectPort(id_IO, new_clk1);
        } else
            gpio->connectPort(id_I, new_clk1);

        NetInfo *new_signal = ctx->createNet(ctx->idf("%s$die%d", net->name.c_str(ctx), new_die));
        uarch->glbout[new_die]->connectPort(ctx->id("GLB1"), new_signal);
        copy_constraint(net, new_signal);
        uarch->global_mapping.emplace(std::make_pair(net->name, new_die), new_signal);
        uarch->global_clk_mapping.emplace(std::make_pair(id_CLOCK1, new_die), id_CLOCK2);
    }
}

void GateMatePacker::strategy_mirror()
{
    log_info("Mirror clocks..\n");

    // Save first CLKIN inputs
    std::vector<CellInfo *> clk_iosel(4, nullptr);
    bool use_ser_clk = false;
    for (int i = 0; i < 4; i++) {
        NetInfo *in_net = uarch->clkin[0]->getPort(ctx->idf("CLK%d", i));
        if (in_net) {
            if (in_net->driver.cell)
                clk_iosel[i] = in_net->driver.cell;
            else
                use_ser_clk = true;
        }
        uarch->clkin[0]->disconnectPort(ctx->idf("CLK%d", i));
    }

    for (int new_die = 0; new_die < uarch->dies; new_die++) {
        // Reconnect CLKIN and create appropriate GPIO and IOSEL cells
        for (int i = 0; i < 4; i++) {
            if (clk_iosel[i]) {
                CellInfo *iosel = clk_iosel[i];
                auto pad_info = uarch->bel_to_pad[iosel->bel];
                Loc l = uarch->locations[std::make_pair(IdString(pad_info->package_pin), new_die)];
                CellInfo *iosel_new = ctx->getBoundBelCell(ctx->getBelByLocation(l));
                if (!iosel_new) {
                    iosel_new = create_cell_ptr(iosel->type, ctx->idf("%s$die%d", iosel->name.c_str(ctx), new_die));
                    iosel_new->params = iosel->params;
                    ctx->bindBel(ctx->getBelByLocation(l), iosel_new, PlaceStrength::STRENGTH_FIXED);

                    CellInfo *gpio = iosel->getPort(id_GPIO_IN)->driver.cell;
                    CellInfo *gpio_new =
                            create_cell_ptr(gpio->type, ctx->idf("%s$die%d", gpio->name.c_str(ctx), new_die));
                    gpio_new->params = gpio->params;
                    ctx->bindBel(ctx->getBelByLocation({l.x, l.y, 0}), gpio_new, PlaceStrength::STRENGTH_FIXED);

                    // Duplicate input connection
                    gpio_new->connectPort(id_I, gpio->getPort(id_I));
                    // Connect IOSEL and CPE_IBUF
                    gpio_new->connectPorts(id_Y, iosel_new, id_GPIO_IN);
                }
                if (iosel_new->getPort(id_IN1))
                    uarch->clkin[new_die]->connectPort(ctx->idf("CLK%d", i), iosel_new->getPort(id_IN1));
                else
                    iosel_new->connectPorts(id_IN1, uarch->clkin[new_die], ctx->idf("CLK%d", i));
            }
        }
        if (use_ser_clk)
            uarch->clkin[new_die]->connectPort(id_SER_CLK, net_SER_CLK);

        if (new_die != 0) {
            // Copy configuration from first die to other dies
            uarch->clkin[new_die]->params = uarch->clkin[0]->params;
            uarch->glbout[new_die]->params = uarch->glbout[0]->params;

            // Copy PLLs
            for (int i = 0; i < 4; i++) {
                Loc fixed_loc = uarch->locations[std::make_pair(ctx->idf("PLL%d", i), 0)];
                BelId pll_bel = ctx->getBelByLocation(fixed_loc);
                CellInfo *pll = ctx->getBoundBelCell(pll_bel);
                if (pll) {
                    // Create new PLL
                    CellInfo *pll_new = create_cell_ptr(pll->type, ctx->idf("%s$die%d", pll->name.c_str(ctx), new_die));
                    pll_new->params = pll->params;
                    // Bind to new location
                    Loc new_loc = uarch->locations[std::make_pair(ctx->idf("PLL%d", i), new_die)];
                    BelId bel = ctx->getBelByLocation(new_loc);
                    ctx->bindBel(bel, pll_new, PlaceStrength::STRENGTH_FIXED);

                    if (pll->getPort(id_CLK_REF))
                        uarch->clkin[new_die]->connectPorts(ctx->idf("CLK_REF%d", i), pll_new, id_CLK_REF);

                    if (pll->getPort(id_CLK0))
                        pll_new->connectPorts(id_CLK0, uarch->glbout[new_die], ctx->idf("CLK0_%d", i));
                    if (pll->getPort(id_CLK90))
                        pll_new->connectPorts(id_CLK90, uarch->glbout[new_die], ctx->idf("CLK90_%d", i));
                    if (pll->getPort(id_CLK180))
                        pll_new->connectPorts(id_CLK180, uarch->glbout[new_die], ctx->idf("CLK180_%d", i));
                    if (pll->getPort(id_CLK270))
                        pll_new->connectPorts(id_CLK270, uarch->glbout[new_die], ctx->idf("CLK270_%d", i));
                    if (pll->getPort(id_USR_LOCKED_STDY_RST))
                        rewire_ram_o(pll, id_USR_LOCKED_STDY_RST, pll_new);
                    if (pll->getPort(id_USR_CLK_REF))
                        rewire_ram_o(pll, id_USR_CLK_REF, pll_new);
                    if (pll->getPort(id_USR_SEL_A_B))
                        rewire_ram_o(pll, id_USR_SEL_A_B, pll_new);
                    move_ram_o_fixed(pll_new, id_USR_LOCKED_STDY_RST, new_loc);
                    move_ram_o_fixed(pll_new, id_USR_CLK_REF, new_loc);
                    move_ram_o_fixed(pll_new, id_USR_SEL_A_B, new_loc);
                    // TODO: AND outputs of all USR_LOCKED_STDY_RST and use that signal to drive logic
                }
            }
            // Copy GLBOUT inputs
            for (int i = 0; i < 4; i++) {
                Loc new_loc = uarch->locations[std::make_pair(id_GLBOUT, new_die)];
                // Plain copy of user signals
                NetInfo *net = uarch->glbout[0]->getPort(ctx->idf("USR_GLB%d", i));
                if (net)
                    rewire_ram_o(uarch->glbout[0], ctx->idf("USR_GLB%d", i), uarch->glbout[new_die]);

                net = uarch->glbout[0]->getPort(ctx->idf("USR_FB%d", i));
                if (net)
                    rewire_ram_o(uarch->glbout[0], ctx->idf("USR_FB%d", i), uarch->glbout[new_die]);

                move_ram_o_fixed(uarch->glbout[new_die], ctx->idf("USR_GLB%d", i), new_loc);
                move_ram_o_fixed(uarch->glbout[new_die], ctx->idf("USR_FB%d", i), new_loc);

                if (uarch->glbout[0]->getPort(ctx->idf("CLK_REF_OUT%d", i)))
                    uarch->clkin[new_die]->connectPorts(ctx->idf("CLK_REF%d", i), uarch->glbout[new_die],
                                                        ctx->idf("CLK_REF_OUT%d", i));
            }
        }
        for (int i = 0; i < 4; i++) {
            NetInfo *net = uarch->glbout[0]->getPort(ctx->idf("GLB%d", i));
            if (net) {
                if (new_die != 0) {
                    NetInfo *new_signal = ctx->createNet(ctx->idf("%s$die%d", net->name.c_str(ctx), new_die));
                    uarch->glbout[new_die]->connectPort(ctx->idf("GLB%d", i), new_signal);
                    copy_constraint(net, new_signal);
                    uarch->global_mapping.emplace(std::make_pair(net->name, new_die), new_signal);
                } else {
                    uarch->global_mapping.emplace(std::make_pair(net->name, new_die), net);
                }
            }
        }
    }
}

static int clk_config_val(IdString name)
{
    switch (name.index) {
    case id_CLOCK1.index:
        return 0b00100011;
    case id_CLOCK2.index:
        return 0b00110011;
    case id_CLOCK3.index:
        return 0b00000011;
    case id_CLOCK4.index:
        return 0b00010011;
    }
    return 0;
}

static int ioclk_config_val(IdString name)
{
    switch (name.index) {
    case id_CLOCK1.index:
        return 0;
    case id_CLOCK2.index:
        return 1;
    case id_CLOCK3.index:
        return 2;
    case id_CLOCK4.index:
        return 3;
    }
    return 0;
}

void GateMatePacker::reassign_clocks()
{
    if (uarch->dies == 1)
        return;
    log_info("Reassign clocks..\n");

    std::vector<std::vector<NetInfo *>> new_bufg(uarch->dies, std::vector<NetInfo *>(4));

    for (auto &glob : uarch->global_signals) {
        const NetInfo *net = glob.first;
        auto users = net->users; // make a copy
        int count = 0;
        for (auto &user : users) {
            int cell_die = uarch->tile_extra_data(user.cell->bel.tile)->die;
            if (uarch->global_mapping.count(std::make_pair(net->name, cell_die))) {
                NetInfo *new_net = uarch->global_mapping.at(std::make_pair(net->name, cell_die));
                if (uarch->ignore.count(user.cell->name))
                    continue;

                if (new_net == net)
                    continue;

                user.cell->disconnectPort(user.port);

                if (user.port.in(id_CLOCK1, id_CLOCK2, id_CLOCK3, id_CLOCK4) &&
                    uarch->global_clk_mapping.count(std::make_pair(user.port, cell_die))) {
                    IdString newPort = uarch->global_clk_mapping.at(std::make_pair(user.port, cell_die));
                    if (!user.cell->ports.count(newPort))
                        user.cell->addInput(newPort);
                    user.cell->connectPort(newPort, new_net);

                    if (user.cell->type == id_RAM) {
                        int a0_clk = int_or_default(user.cell->params, id_RAM_cfg_forward_a0_clk, 0);
                        int a1_clk = int_or_default(user.cell->params, id_RAM_cfg_forward_a1_clk, 0);
                        int b0_clk = int_or_default(user.cell->params, id_RAM_cfg_forward_b0_clk, 0);
                        int b1_clk = int_or_default(user.cell->params, id_RAM_cfg_forward_b1_clk, 0);

                        if (a0_clk == clk_config_val(user.port))
                            user.cell->params[id_RAM_cfg_forward_a0_clk] = Property(clk_config_val(newPort), 8);
                        if (a1_clk == clk_config_val(user.port))
                            user.cell->params[id_RAM_cfg_forward_a1_clk] = Property(clk_config_val(newPort), 8);
                        if (b0_clk == clk_config_val(user.port))
                            user.cell->params[id_RAM_cfg_forward_b0_clk] = Property(clk_config_val(newPort), 8);
                        if (b1_clk == clk_config_val(user.port))
                            user.cell->params[id_RAM_cfg_forward_b1_clk] = Property(clk_config_val(newPort), 8);
                    }
                    if (user.cell->type == id_IOSEL) {
                        int in_clk = int_or_default(user.cell->params, id_IN_CLOCK, 0);
                        int out_clk = int_or_default(user.cell->params, id_OUT_CLOCK, 0);
                        if (in_clk == ioclk_config_val(user.port))
                            user.cell->params[id_IN_CLOCK] = Property(ioclk_config_val(newPort), 2);
                        if (out_clk == ioclk_config_val(user.port))
                            user.cell->params[id_OUT_CLOCK] = Property(ioclk_config_val(newPort), 2);
                    }
                } else
                    user.cell->connectPort(user.port, new_net);
                count++;
            } else {
                log_error("Global signal '%s' is not available in die %d.\n", net->name.c_str(ctx), cell_die);
            }
        }
        if (count)
            log_info("    reassign %d net '%s' users\n", count, net->name.c_str(ctx));
    }
}

NEXTPNR_NAMESPACE_END
