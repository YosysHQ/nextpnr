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
    log_info("Packing BUFGs..\n");
    CellInfo *bufg[4] = {nullptr};
    CellInfo *pll[4] = {nullptr};

    auto update_bufg_port = [&](CellInfo *cell, int port_num, int pll_num) {
        CellInfo *b = net_only_drives(ctx, cell->getPort(ctx->idf("CLK%d", 90 * port_num)), is_bufg, id_I, false);
        if (b) {
            if (bufg[port_num] == nullptr) {
                bufg[port_num] = b;
            } else {
                if (bufg[pll_num] == nullptr) {
                    bufg[pll_num] = b;
                } else {
                    log_error("Unable to place BUFG for PLL.\n");
                }
            }
        }
    };

    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_PLL))
            continue;
        int index = ci.constr_z - 2;
        pll[index] = &ci;
        for (int j = 0; j < 4; j++)
            update_bufg_port(pll[index], j, index);
    }

    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_BUFG))
            continue;

        NetInfo *in_net = ci.getPort(id_I);
        int die = uarch->preferred_die;
        if (in_net) {
            if (in_net->driver.cell) {
                if (ctx->getBelBucketForCellType(in_net->driver.cell->type) == id_GPIO) {
                    auto pad_info = uarch->bel_to_pad[in_net->driver.cell->bel];
                    if (pad_info->flags) {
                        int index = pad_info->flags - 1;
                        die = uarch->tile_extra_data(in_net->driver.cell->bel.tile)->die;
                        if (!clkin[die]->getPort(ctx->idf("CLK%d", index)))
                            clkin[die]->connectPort(ctx->idf("CLK%d", index), in_net->driver.cell->getPort(id_I));
                    }
                }
            } else {
                // SER_CLK
                clkin[die]->connectPort(id_SER_CLK, in_net);
            }

            copy_constraint(in_net, ci.getPort(id_O));

            if ((in_net->driver.cell && ctx->getBelBucketForCellType(in_net->driver.cell->type) != id_PLL) ||
                !in_net->driver.cell) {
                for (int i = 0; i < 4; i++) {
                    if (bufg[i] == nullptr) {
                        bufg[i] = &ci;
                        break;
                    }
                }
            }
        }
    }

    for (int i = 0; i < 4; i++) {
        if (bufg[i]) {
            CellInfo &ci = *bufg[i];
            global_signals.emplace(ci.getPort(id_O), i);
            int glb_mux = 0;
            NetInfo *in_net = ci.getPort(id_I);
            int die = uarch->preferred_die;
            if (in_net->driver.cell) {
                bool user_glb = true;
                if (ctx->getBelBucketForCellType(in_net->driver.cell->type) == id_GPIO) {
                    auto pad_info = uarch->bel_to_pad[in_net->driver.cell->bel];
                    if (pad_info->flags) {
                        die = uarch->tile_extra_data(in_net->driver.cell->bel.tile)->die;
                        clkin[die]->params[ctx->idf("REF%d", i)] = Property(pad_info->flags - 1, 3);
                        clkin[die]->params[ctx->idf("REF%d_INV", i)] = Property(Property::State::S0);
                        NetInfo *conn = ctx->createNet(ci.name);
                        clkin[die]->connectPort(ctx->idf("CLK_REF%d", i), conn);
                        glbout[die]->connectPort(ctx->idf("CLK_REF_OUT%d", i), conn);
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
                    ci.movePortTo(id_I, glbout[die], ctx->idf("%s_%d", in_net->driver.port.c_str(ctx), pll_index));
                    user_glb = false;
                }
                if (user_glb) {
                    ci.movePortTo(id_I, glbout[die], ctx->idf("USR_GLB%d", i));
                    move_ram_o_fixed(glbout[die], ctx->idf("USR_GLB%d", i), ctx->getBelLocation(glbout[die]->bel));
                    glbout[die]->params[ctx->idf("USR_GLB%d", i)] = Property(Property::State::S1);
                }
            } else {
                // SER_CLK
                clkin[die]->params[ctx->idf("REF%d", i)] = Property(0b100, 3);
                clkin[die]->params[ctx->idf("REF%d_INV", i)] = Property(Property::State::S0);
            }

            ci.movePortTo(id_O, glbout[die], ctx->idf("GLB%d", i));
            glbout[die]->params[ctx->idf("GLB%d_EN", i)] = Property(Property::State::S1);
            glbout[die]->params[ctx->idf("GLB%d", i)] = Property(glb_mux, 3);
            packed_cells.emplace(ci.name);
        }
    }

    for (int i = 0; i < 4; i++) {
        if (pll[i]) {
            NetInfo *feedback_net = pll[i]->getPort(id_CLK_FEEDBACK);
            int die = uarch->tile_extra_data(pll[i]->bel.tile)->die;
            if (feedback_net) {
                if (!global_signals.count(feedback_net)) {
                    pll[i]->movePortTo(id_CLK_FEEDBACK, glbout[die], ctx->idf("USR_FB%d", i));
                    move_ram_o_fixed(glbout[die], ctx->idf("USR_FB%d", i), ctx->getBelLocation(glbout[die]->bel));
                    glbout[die]->params[ctx->idf("USR_FB%d", i)] = Property(Property::State::S1);
                } else {
                    int index = global_signals[feedback_net];
                    glbout[die]->params[ctx->idf("FB%d", i)] = Property(index, 2);
                    pll[i]->disconnectPort(id_CLK_FEEDBACK);
                }
                NetInfo *conn =
                        ctx->createNet(ctx->idf("%s_%s", glbout[die]->name.c_str(ctx), feedback_net->name.c_str(ctx)));
                pll[i]->connectPort(id_CLK_FEEDBACK, conn);
                glbout[die]->connectPort(ctx->idf("CLK_FB%d", i), conn);
            }
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

void GateMatePacker::insert_bufg(CellInfo *cell, IdString port)
{
    NetInfo *clk = cell->getPort(port);
    if (clk) {
        if (!(clk->users.entries() == 1 && (*clk->users.begin()).cell->type == id_CC_BUFG)) {
            CellInfo *bufg =
                    create_cell_ptr(id_CC_BUFG, ctx->idf("%s$BUFG_%s", cell->name.c_str(ctx), port.c_str(ctx)));
            cell->movePortTo(port, bufg, id_O);
            cell->ports[port].name = port;
            cell->ports[port].type = PORT_OUT;
            NetInfo *net = ctx->createNet(ctx->idf("%s", bufg->name.c_str(ctx)));
            cell->connectPort(port, net);
            bufg->connectPort(id_I, net);
            log_info("Added BUFG for cell '%s' signal %s\n", cell->name.c_str(ctx), port.c_str(ctx));
        }
    }
}

void GateMatePacker::insert_pll_bufg()
{
    for (int i = 0; i < uarch->dies; i++) {
        Loc fixed_loc = uarch->locations[std::make_pair(id_CLKIN, i)];
        clkin.push_back(create_cell_ptr(id_CLKIN, ctx->idf("CLKIN%d", i)));
        BelId clkin_bel = ctx->getBelByLocation(fixed_loc);
        ctx->bindBel(clkin_bel, clkin.back(), PlaceStrength::STRENGTH_FIXED);
        glbout.push_back(create_cell_ptr(id_GLBOUT, ctx->idf("GLBOUT%d", i)));
        fixed_loc = uarch->locations[std::make_pair(id_GLBOUT, i)];
        BelId glbout_bel = ctx->getBelByLocation(fixed_loc);
        ctx->bindBel(glbout_bel, glbout.back(), PlaceStrength::STRENGTH_FIXED);
    }
    std::vector<CellInfo *> cells;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_PLL, id_CC_PLL_ADV))
            continue;
        cells.push_back(&ci);
    }
    for (auto &cell : cells) {
        insert_bufg(cell, id_CLK0);
        insert_bufg(cell, id_CLK90);
        insert_bufg(cell, id_CLK180);
        insert_bufg(cell, id_CLK270);
    }
}

void GateMatePacker::remove_clocking()
{
    auto remove_unused_cells = [&](std::vector<CellInfo *> &cells, const char *type) {
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
    remove_unused_cells(clkin, "CLKIN");
    remove_unused_cells(glbout, "GLBOUT");
    flush_cells();
}

void GateMatePacker::pack_pll()
{
    std::vector<int> pll_index(uarch->dies);
    log_info("Packing PLLss..\n");
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_PLL, id_CC_PLL_ADV))
            continue;

        disconnect_if_gnd(&ci, id_CLK_REF);
        disconnect_if_gnd(&ci, id_USR_CLK_REF);
        disconnect_if_gnd(&ci, id_CLK_FEEDBACK);
        disconnect_if_gnd(&ci, id_USR_LOCKED_STDY_RST);

        int die = uarch->preferred_die;

        NetInfo *clk = ci.getPort(id_CLK_REF);
        if (clk) {
            if (ctx->getBelBucketForCellType(clk->driver.cell->type) == id_CC_BUFG) {
                clk = clk->driver.cell->getPort(id_I);
            }
            if (ctx->getBelBucketForCellType(clk->driver.cell->type) == id_GPIO) {
                auto pad_info = uarch->bel_to_pad[clk->driver.cell->bel];
                if (pad_info->flags != 0) {
                    die = uarch->tile_extra_data(clk->driver.cell->bel.tile)->die;
                }
            }
        }

        ci.cluster = ci.name;
        ci.constr_abs_z = true;
        ci.constr_z = 2 + pll_index[die]; // Position to a proper Z location

        Loc fixed_loc = uarch->locations[std::make_pair(ctx->idf("PLL%d", pll_index[die]), die)];
        BelId pll_bel = ctx->getBelByLocation(fixed_loc);
        ctx->bindBel(pll_bel, &ci, PlaceStrength::STRENGTH_FIXED);

        if (pll_index[die] > 4)
            log_error("Used more than available PLLs.\n");

        if (ci.getPort(id_CLK_REF) == nullptr && ci.getPort(id_USR_CLK_REF) == nullptr)
            log_error("At least one reference clock (CLK_REF or USR_CLK_REF) must be set.\n");

        if (ci.getPort(id_CLK_REF) != nullptr && ci.getPort(id_USR_CLK_REF) != nullptr)
            log_error("CLK_REF and USR_CLK_REF are not allowed to be set in same time.\n");

        clk = ci.getPort(id_CLK_REF);
        delay_t period = ctx->getDelayFromNS(1.0e9 / ctx->setting<float>("target_freq"));
        if (clk) {
            if (ctx->getBelBucketForCellType(clk->driver.cell->type) == id_CC_BUFG) {
                NetInfo *in = clk->driver.cell->getPort(id_I);
                ci.disconnectPort(id_CLK_REF);
                ci.connectPort(id_CLK_REF, in);
                clk = in;
            }
            if (ctx->getBelBucketForCellType(clk->driver.cell->type) != id_GPIO)
                log_error("CLK_REF must be driven with GPIO pin.\n");
            auto pad_info = uarch->bel_to_pad[clk->driver.cell->bel];
            if (pad_info->flags == 0)
                log_error("CLK_REF must be driven with CLK dedicated pin.\n");
            if (clk->clkconstr)
                period = clk->clkconstr->period.minDelay();

            ci.movePortTo(id_CLK_REF, clkin[die], ctx->idf("CLK%d", pad_info->flags - 1));

            NetInfo *conn = ctx->createNet(ctx->idf("%s_CLK_REF", ci.name.c_str(ctx)));
            clkin[die]->connectPort(ctx->idf("CLK_REF%d", pll_index[die]), conn);
            ci.connectPort(id_CLK_REF, conn);
        }

        clk = ci.getPort(id_USR_CLK_REF);
        if (clk) {
            move_ram_o_fixed(&ci, id_USR_CLK_REF, fixed_loc);
            ci.params[ctx->id("USR_CLK_REF")] = Property(0b1, 1);
            if (clk->clkconstr)
                period = clk->clkconstr->period.minDelay();
        }

        if (ci.getPort(id_CLK_REF_OUT))
            log_error("Output CLK_REF_OUT cannot be used if PLL is used.\n");

        pll_out(&ci, id_CLK0, fixed_loc);
        pll_out(&ci, id_CLK90, fixed_loc);
        pll_out(&ci, id_CLK180, fixed_loc);
        pll_out(&ci, id_CLK270, fixed_loc);

        move_ram_i_fixed(&ci, id_USR_PLL_LOCKED, fixed_loc);
        move_ram_i_fixed(&ci, id_USR_PLL_LOCKED_STDY, fixed_loc);
        move_ram_o_fixed(&ci, id_USR_LOCKED_STDY_RST, fixed_loc);

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

            double ref_clk = double_or_default(ci.params, id_REF_CLK, 0.0);
            if (ref_clk <= 0 || ref_clk > 125)
                log_error("REF_CLK parameter is out of range (0,125.00].\n");

            double out_clk = double_or_default(ci.params, id_OUT_CLK, 0.0);
            if (out_clk <= 0 || out_clk > max_freq)
                log_error("OUT_CLK parameter is out of range (0,%.2lf].\n", max_freq);

            if ((ci_const < 1) || (ci_const > 31)) {
                log_warning("CI const out of range. Set to default CI = 2\n");
                ci_const = 2;
            }
            if ((cp_const < 1) || (cp_const > 31)) {
                log_warning("CP const out of range. Set to default CP = 4\n");
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
            if (select_net == nullptr || select_net->name == ctx->id("$PACKER_GND")) {
                ci.params[ctx->id("SET_SEL")] = Property(0b0, 1);
                ci.params[ctx->id("USR_SET")] = Property(0b0, 1);
                ci.disconnectPort(id_USR_SEL_A_B);
            } else if (select_net->name == ctx->id("$PACKER_VCC")) {
                ci.params[ctx->id("SET_SEL")] = Property(0b1, 1);
                ci.params[ctx->id("USR_SET")] = Property(0b0, 1);
                ci.disconnectPort(id_USR_SEL_A_B);
            } else {
                ci.params[ctx->id("USR_SET")] = Property(0b1, 1);
                move_ram_o_fixed(&ci, id_USR_SEL_A_B, fixed_loc);
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

        pll_index[die]++;
    }
}

NEXTPNR_NAMESPACE_END
