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

#include "design_utils.h"
#include "gatemate_util.h"
#include "pack.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

// Return true if a cell is a flipflop
inline bool is_dff(const BaseCtx *ctx, const CellInfo *cell) { return cell->type.in(id_CC_DFF, id_CC_DLT); }

void GateMatePacker::dff_to_cpe(CellInfo *dff, CellInfo *cpe)
{
    bool invert;
    bool is_latch = dff->type == id_CC_DLT;
    if (is_latch) {
        NetInfo *g_net = cpe->getPort(id_G);
        invert = bool_or_default(dff->params, id_G_INV, 0);
        if (g_net) {
            if (g_net->name == ctx->id("$PACKER_GND")) {
                cpe->params[id_C_CPE_CLK] = Property(invert ? 0b11 : 0b00, 2);
                cpe->disconnectPort(id_G);
            } else if (g_net->name == ctx->id("$PACKER_VCC")) {
                cpe->params[id_C_CPE_CLK] = Property(invert ? 0b00 : 0b11, 2);
                cpe->disconnectPort(id_G);
            } else {
                cpe->params[id_C_CPE_CLK] = Property(invert ? 0b01 : 0b10, 2);
            }
        } else {
            cpe->params[id_C_CPE_CLK] = Property(invert ? 0b11 : 0b00, 2);
        }
        dff->unsetParam(id_G_INV);
        cpe->renamePort(id_G, id_CLK);

        cpe->params[id_C_CPE_EN] = Property(0b11, 2);
        cpe->params[id_C_L_D] = Property(0b1, 1);
    } else {
        NetInfo *en_net = cpe->getPort(id_EN);
        bool invert = bool_or_default(dff->params, id_EN_INV, 0);
        if (en_net) {
            if (en_net->name == ctx->id("$PACKER_GND")) {
                cpe->params[id_C_CPE_EN] = Property(invert ? 0b11 : 0b00, 2);
                cpe->disconnectPort(id_EN);
            } else if (en_net->name == ctx->id("$PACKER_VCC")) {
                cpe->params[id_C_CPE_EN] = Property(invert ? 0b00 : 0b11, 2);
                cpe->disconnectPort(id_EN);
            } else {
                cpe->params[id_C_CPE_EN] = Property(invert ? 0b01 : 0b10, 2);
            }
        } else {
            cpe->params[id_C_CPE_EN] = Property(invert ? 0b11 : 0b00, 2);
        }
        dff->unsetParam(id_EN_INV);

        NetInfo *clk_net = cpe->getPort(id_CLK);
        invert = bool_or_default(dff->params, id_CLK_INV, 0);
        if (clk_net) {
            if (clk_net->name == ctx->id("$PACKER_GND")) {
                cpe->params[id_C_CPE_CLK] = Property(invert ? 0b11 : 0b00, 2);
                cpe->disconnectPort(id_CLK);
            } else if (clk_net->name == ctx->id("$PACKER_VCC")) {
                cpe->params[id_C_CPE_CLK] = Property(invert ? 0b00 : 0b11, 2);
                cpe->disconnectPort(id_CLK);
            } else {
                cpe->params[id_C_CPE_CLK] = Property(invert ? 0b01 : 0b10, 2);
            }
        } else {
            cpe->params[id_C_CPE_CLK] = Property(invert ? 0b11 : 0b00, 2);
        }
        dff->unsetParam(id_CLK_INV);
    }

    NetInfo *sr_net = cpe->getPort(id_SR);
    invert = bool_or_default(dff->params, id_SR_INV, 0);
    bool sr_val = bool_or_default(dff->params, id_SR_VAL, 0);
    if (sr_net) {
        if (sr_net->name.in(ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC"))) {
            bool sr_signal = sr_net->name == ctx->id("$PACKER_VCC");
            if (sr_signal ^ invert)
                log_error("Currently unsupported DFF configuration for '%s'\n.", dff->name.c_str(ctx));
            cpe->params[id_C_CPE_RES] = Property(0b11, 2);
            cpe->params[id_C_CPE_SET] = Property(0b11, 2);
            cpe->disconnectPort(id_SR);
        } else {
            if (sr_val) {
                cpe->params[id_C_CPE_RES] = Property(0b11, 2);
                cpe->params[id_C_CPE_SET] = Property(invert ? 0b10 : 0b01, 2);
                if (is_latch)
                    cpe->renamePort(id_SR, id_EN);
                else
                    cpe->params[id_C_EN_SR] = Property(0b1, 1);
            } else {
                cpe->params[id_C_CPE_RES] = Property(invert ? 0b10 : 0b01, 2);
                cpe->params[id_C_CPE_SET] = Property(0b11, 2);
            }
        }
    } else {
        cpe->params[id_C_CPE_RES] = Property(0b11, 2);
        cpe->params[id_C_CPE_SET] = Property(0b11, 2);
    }
    dff->unsetParam(id_SR_VAL);
    dff->unsetParam(id_SR_INV);

    if (dff->params.count(id_INIT) && dff->params[id_INIT].is_fully_def()) {
        bool init = bool_or_default(dff->params, id_INIT, 0);
        if (init)
            cpe->params[id_FF_INIT] = Property(0b11, 2);
        else
            cpe->params[id_FF_INIT] = Property(0b10, 2);
        dff->unsetParam(id_INIT);
    } else {
        dff->unsetParam(id_INIT);
    }
    cpe->timing_index = ctx->get_cell_timing_idx(id_CPE_DFF);
//    cpe->params[id_C_O] = Property(0b00, 2);
}

void GateMatePacker::pack_cpe()
{
    log_info("Packing CPEs..\n");
    std::vector<CellInfo *> l2t5_list;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_L2T4, id_CC_L2T5, id_CC_LUT2, id_CC_LUT1, id_CC_MX2))
            continue;
        if (ci.type == id_CC_L2T5) {
            l2t5_list.push_back(&ci);
            ci.renamePort(id_I0, id_IN1);
            ci.renamePort(id_I1, id_IN2);
            ci.renamePort(id_I2, id_IN3);
            ci.renamePort(id_I3, id_IN4);

            ci.renamePort(id_O, id_OUT);

            //ci.params[id_C_O] = Property(0b11, 2);
            ci.type = id_CPE_L2T5_L;
        } else if (ci.type == id_CC_MX2) {
            //ci.params[id_C_O] = Property(0b11, 2);
            ci.renamePort(id_D1, id_IN1);
            NetInfo *sel = ci.getPort(id_S0);
            ci.renamePort(id_S0, id_IN2);
            ci.ports[id_IN3].name = id_IN3;
            ci.ports[id_IN3].type = PORT_IN;
            ci.connectPort(id_IN3, sel);
            ci.renamePort(id_D0, id_IN4);
            ci.disconnectPort(id_D1);
            ci.params[id_INIT_L00] = Property(0b1000, 4); // AND
            ci.params[id_INIT_L01] = Property(0b0100, 4); // AND inv D0
            ci.params[id_INIT_L10] = Property(0b1110, 4); // OR
            ci.renamePort(id_Y, id_OUT);
            ci.type = id_CPE_L2T4;
        } else {
            ci.renamePort(id_I0, id_IN1);
            ci.renamePort(id_I1, id_IN2);
            ci.renamePort(id_I2, id_IN3);
            ci.renamePort(id_I3, id_IN4);
            ci.renamePort(id_O, id_OUT);
            ///ci.params[id_C_O] = Property(0b11, 2);
            if (ci.type.in(id_CC_LUT1, id_CC_LUT2)) {
                uint8_t val = int_or_default(ci.params, id_INIT, 0);
                if (ci.type == id_CC_LUT1)
                    val = val << 2 | val;
                ci.params[id_INIT_L00] = Property(val, 4);
                ci.unsetParam(id_INIT);
                ci.params[id_INIT_L10] = Property(0b1010, 4);
            }
            ci.type = id_CPE_L2T4;
        }
        NetInfo *o = ci.getPort(id_OUT);
        if (o) {
            CellInfo *dff = net_only_drives(ctx, o, is_dff, id_D, true);
            if (dff) {
                if (dff->type == id_CC_DLT) {
                    dff->movePortTo(id_G, &ci, id_G);
                } else {
                    dff->movePortTo(id_EN, &ci, id_EN);
                    dff->movePortTo(id_CLK, &ci, id_CLK);
                }
                dff->movePortTo(id_SR, &ci, id_SR);
                dff->disconnectPort(id_D);
                ci.disconnectPort(id_OUT);
                dff->movePortTo(id_Q, &ci, id_OUT);
                dff_to_cpe(dff, &ci);
                packed_cells.insert(dff->name);
            }
        }
    }

    for (auto ci : l2t5_list) {
        CellInfo *upper = create_cell_ptr(id_CPE_L2T5_U, ctx->idf("%s$upper", ci->name.c_str(ctx)));
        upper->cluster = ci->name;
        upper->constr_abs_z = false;
        upper->constr_z = -1;
        ci->cluster = ci->name;
        ci->movePortTo(id_I4, upper, id_IN1);
        upper->params[id_INIT_L00] = Property(0b1010, 4);
        upper->params[id_INIT_L10] = Property(0b1010, 4);
        ci->constr_children.push_back(upper);
    }
    l2t5_list.clear();

    flush_cells();

    std::vector<CellInfo *> mux_list;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_MX4))
            continue;
        mux_list.push_back(&ci);
    }
    for (auto &cell : mux_list) {
        CellInfo &ci = *cell;
        ci.cluster = ci.name;
        ci.renamePort(id_Y, id_OUT);

        ci.renamePort(id_S0, id_IN2); // IN6
        ci.renamePort(id_S1, id_IN4); // IN8

        uint8_t select = 0;
        uint8_t invert = 0;
        for (int i = 0; i < 4; i++) {
            NetInfo *net = ci.getPort(ctx->idf("D%d", i));
            if (net) {
                if (net->name.in(ctx->id("$PACKER_GND"), ctx->id("$PACKER_VCC"))) {
                    if (net->name == ctx->id("$PACKER_VCC"))
                        invert |= 1 << i;
                    ci.disconnectPort(ctx->idf("D%d", i));
                } else {
                    select |= 1 << i;
                }
            }
        }
        ci.params[id_C_FUNCTION] = Property(C_MX4, 3);
        ci.params[id_INIT_L02] = Property(0b1100, 4); // IN6
        ci.params[id_INIT_L03] = Property(0b1100, 4); // IN8
        ci.params[id_INIT_L11] = Property(invert, 4); // Inversion bits
        // ci.params[id_INIT_L20] = Property(0b1100, 4); // Always D1
        //ci.params[id_C_O] = Property(0b11, 2);
        ci.type = id_CPE_LT_L;

        CellInfo *upper = create_cell_ptr(id_CPE_LT_U, ctx->idf("%s$upper", ci.name.c_str(ctx)));
        upper->cluster = ci.name;
        upper->constr_abs_z = false;
        upper->constr_z = -1;
        upper->params[id_INIT_L10] = Property(select, 4); // Selection bits
        upper->params[id_C_FUNCTION] = Property(C_MX4, 3);

        ci.movePortTo(id_D0, upper, id_IN1);
        ci.movePortTo(id_D1, upper, id_IN2);
        ci.movePortTo(id_D2, upper, id_IN3);
        ci.movePortTo(id_D3, upper, id_IN4);
        ci.constr_children.push_back(upper);
    }
    mux_list.clear();

    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_DFF, id_CC_DLT))
            continue;
        ci.renamePort(id_Q, id_OUT);
        NetInfo *d_net = ci.getPort(id_D);
        if (d_net->name == ctx->id("$PACKER_GND")) {
            ci.params[id_INIT_L00] = Property(0b0000, 4);
            ci.disconnectPort(id_D);
        } else if (d_net->name == ctx->id("$PACKER_VCC")) {
            ci.params[id_INIT_L00] = Property(0b1111, 4);
            ci.disconnectPort(id_D);
        } else {
            ci.params[id_INIT_L00] = Property(0b1010, 4);
        }
        ci.params[id_INIT_L10] = Property(0b1010, 4);
        ci.renamePort(id_D, id_IN1);
        dff_to_cpe(&ci, &ci);
        ci.type = id_CPE_LT;
    }
}

static bool is_addf_ci(NetInfo *net)
{
    return net && net->users.entries() == 1 && (*net->users.begin()).cell->type == id_CC_ADDF &&
           (*net->users.begin()).port == id_CI;
}

void GateMatePacker::pack_addf()
{
    log_info("Packing ADDFs..\n");

    std::vector<CellInfo *> root_cys;
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type != id_CC_ADDF)
            continue;
        NetInfo *ci_net = ci->getPort(id_CI);

        if (!ci_net || !ci_net->driver.cell ||
            !(ci_net->driver.cell->type == id_CC_ADDF && ci_net->driver.port == id_CO)) {
            root_cys.push_back(ci);
        }
    }
    std::vector<std::vector<CellInfo *>> groups;
    for (auto root : root_cys) {
        std::vector<CellInfo *> group;
        CellInfo *cy = root;
        group.push_back(cy);
        while (true) {
            NetInfo *co_net = cy->getPort(id_CO);
            if (co_net) {
                bool found = false;
                for (auto &usr : co_net->users) {
                    if (usr.cell->type == id_CC_ADDF && usr.port == id_CI) {
                        if (found)
                            log_error("Only one other ADDF can be connected.\n");
                        cy = usr.cell;
                        group.push_back(cy);
                        found = true;
                    }
                }
                if (!found)
                    break;
            } else
                break;
        }
        groups.push_back(group);
    }

    // Merge two ADDF cells to one CPE when possible
    // use artificial CC_ADDF2 cell for that
    for (size_t i = 0; i < groups.size(); i++) {
        std::vector<CellInfo *> regrouped;
        size_t pos = 0;
        auto &grp = groups.at(i);
        while (pos < grp.size()) {
            bool merged = false;
            CellInfo *cy = grp.at(pos);
            NetInfo *co_net = cy->getPort(id_CO);
            bool last = pos + 1 == grp.size();
            if (!last && is_addf_ci(co_net)) {
                CellInfo *cy2 = grp.at(pos + 1);
                co_net = cy2->getPort(id_CO);
                last = pos + 2 == grp.size();
                if (!co_net || last || is_addf_ci(co_net)) {
                    cy2->type = id_CC_ADDF2;
                    cy2->disconnectPort(id_CI);
                    // Do actual merge of cells
                    cy->movePortTo(id_A, cy2, id_A2);
                    cy->movePortTo(id_B, cy2, id_B2);
                    cy->movePortTo(id_S, cy2, id_S2);
                    cy->disconnectPort(id_CO);
                    cy->movePortTo(id_CI, cy2, id_CI);
                    packed_cells.insert(cy->name);
                    regrouped.push_back(cy2);
                    merged = true;
                    pos++;
                }
            }
            if (!merged)
                regrouped.push_back(cy);
            pos++;
        }
        grp = regrouped;
    }
    flush_cells();

    for (auto &grp : splitNestedVector(groups)) {
        CellInfo *root = grp.front();
        root->cluster = root->name;

        CellInfo *ci_upper = create_cell_ptr(id_CPE_LT_U, ctx->idf("%s$ci_upper", root->name.c_str(ctx)));
        root->constr_children.push_back(ci_upper);
        ci_upper->cluster = root->name;
        ci_upper->constr_abs_z = false;
        ci_upper->constr_z = -1;
        ci_upper->constr_y = -1;

        CellInfo *ci_lower = create_cell_ptr(id_CPE_LT_L, ctx->idf("%s$ci_lower", root->name.c_str(ctx)));
        root->constr_children.push_back(ci_lower);
        ci_lower->cluster = root->name;
        ci_lower->constr_abs_z = false;
        ci_lower->constr_y = -1;
        //ci_lower->params[id_C_O] = Property(0b11, 2);
        ci_lower->params[id_C_SELY1] = Property(1, 1);
        ci_lower->params[id_C_CY1_I] = Property(1, 1);
        ci_lower->params[id_INIT_L10] = Property(0b1010, 4); // D0

        NetInfo *ci_net = root->getPort(id_CI);
        if (ci_net->name == ctx->id("$PACKER_GND")) {
            ci_lower->params[id_INIT_L00] = Property(0b0000, 4);
            root->disconnectPort(id_CI);
        } else if (ci_net->name == ctx->id("$PACKER_VCC")) {
            ci_lower->params[id_INIT_L00] = Property(0b1111, 4);
            root->disconnectPort(id_CI);
        } else {
            root->movePortTo(id_CI, ci_lower, id_IN1);
            ci_lower->params[id_INIT_L00] = Property(0b1010, 4); // IN5
        }

        NetInfo *ci_conn = ctx->createNet(ctx->idf("%s$ci", root->name.c_str(ctx)));
        ci_lower->connectPort(id_COUTY1, ci_conn);

        root->ports[id_CINY1].name = id_CINY1;
        root->ports[id_CINY1].type = PORT_IN;
        root->connectPort(id_CINY1, ci_conn);

        for (size_t i = 0; i < grp.size(); i++) {
            CellInfo *cy = grp.at(i);
            if (i != 0) {
                cy->cluster = root->name;
                root->constr_children.push_back(cy);
                cy->constr_abs_z = false;
                cy->constr_y = +i;
                cy->renamePort(id_CI, id_CINY1);
            }

            bool merged = cy->type != id_CC_ADDF;
            if (merged) {
                NetInfo *a_net = cy->getPort(id_A2);
                if (a_net->name == ctx->id("$PACKER_GND")) {
                    cy->params[id_INIT_L02] = Property(0b0000, 4);
                    cy->disconnectPort(id_A2);
                } else if (a_net->name == ctx->id("$PACKER_VCC")) {
                    cy->params[id_INIT_L02] = Property(0b1111, 4);
                    cy->disconnectPort(id_A2);
                } else {
                    cy->renamePort(id_A2, id_IN1);
                    cy->params[id_INIT_L02] = Property(0b1010, 4); // IN1
                }
                NetInfo *b_net = cy->getPort(id_B2);
                if (b_net->name == ctx->id("$PACKER_GND")) {
                    cy->params[id_INIT_L03] = Property(0b0000, 4);
                    cy->disconnectPort(id_B2);
                } else if (b_net->name == ctx->id("$PACKER_VCC")) {
                    cy->params[id_INIT_L03] = Property(0b1111, 4);
                    cy->disconnectPort(id_B2);
                } else {
                    cy->renamePort(id_B2, id_IN3);
                    cy->params[id_INIT_L03] = Property(0b1010, 4); // IN3
                }
                cy->params[id_INIT_L11] = Property(0b0110, 4); // XOR
                cy->renamePort(id_S2, id_OUT);
            } else {
                cy->params[id_INIT_L02] = Property(0b0000, 4); // 0
                cy->params[id_INIT_L03] = Property(0b0000, 4); // 0
                cy->params[id_INIT_L11] = Property(0b0110, 4); // XOR
                cy->params[id_INIT_L20] = Property(0b0110, 4); // XOR
            }
            cy->params[id_C_FUNCTION] = Property(merged ? C_ADDF2 : C_ADDF, 3);
            //cy->params[id_C_O] = Property(0b11, 2);
            cy->type = id_CPE_LT_L;

            CellInfo *upper = create_cell_ptr(id_CPE_LT_U, ctx->idf("%s$upper", cy->name.c_str(ctx)));
            upper->cluster = root->name;
            root->constr_children.push_back(upper);
            upper->constr_abs_z = false;
            upper->constr_y = +i;
            upper->constr_z = -1;
            if (merged) {
                cy->movePortTo(id_S, upper, id_OUT);
                //upper->params[id_C_O] = Property(0b11, 2);
            } else {
                cy->renamePort(id_S, id_OUT);
            }

            NetInfo *a_net = cy->getPort(id_A);
            if (a_net->name == ctx->id("$PACKER_GND")) {
                upper->params[id_INIT_L00] = Property(0b0000, 4);
                cy->disconnectPort(id_A);
            } else if (a_net->name == ctx->id("$PACKER_VCC")) {
                upper->params[id_INIT_L00] = Property(0b1111, 4);
                cy->disconnectPort(id_A);
            } else {
                cy->movePortTo(id_A, upper, id_IN1);
                upper->params[id_INIT_L00] = Property(0b1010, 4); // IN1
            }
            NetInfo *b_net = cy->getPort(id_B);
            if (b_net->name == ctx->id("$PACKER_GND")) {
                upper->params[id_INIT_L01] = Property(0b0000, 4);
                cy->disconnectPort(id_B);
            } else if (b_net->name == ctx->id("$PACKER_VCC")) {
                upper->params[id_INIT_L01] = Property(0b1111, 4);
                cy->disconnectPort(id_B);
            } else {
                cy->movePortTo(id_B, upper, id_IN3);
                upper->params[id_INIT_L01] = Property(0b1010, 4); // IN3
            }

            upper->params[id_INIT_L10] = Property(0b0110, 4); // XOR
            upper->params[id_C_FUNCTION] = Property(merged ? C_ADDF2 : C_ADDF, 3);

            if (i == grp.size() - 1) {
                if (!cy->getPort(id_CO))
                    break;
                CellInfo *co_upper = create_cell_ptr(id_CPE_LT_U, ctx->idf("%s$co_upper", cy->name.c_str(ctx)));
                co_upper->cluster = root->name;
                root->constr_children.push_back(co_upper);
                co_upper->constr_abs_z = false;
                co_upper->constr_z = -1;
                co_upper->constr_y = +i + 1;
                CellInfo *co_lower = create_cell_ptr(id_CPE_LT_L, ctx->idf("%s$co_lower", cy->name.c_str(ctx)));
                co_lower->cluster = root->name;
                root->constr_children.push_back(co_lower);
                co_lower->constr_abs_z = false;
                co_lower->constr_y = +i + 1;
                //co_lower->params[id_C_O] = Property(0b11, 2);
                co_lower->params[id_C_FUNCTION] = Property(C_EN_CIN, 3);
                co_lower->params[id_INIT_L11] = Property(0b1100, 4);
                co_lower->params[id_INIT_L20] = Property(0b1100, 4);

                NetInfo *co_conn = ctx->createNet(ctx->idf("%s$co", cy->name.c_str(ctx)));

                co_lower->ports[id_CINY1].name = id_CINY1;
                co_lower->ports[id_CINY1].type = PORT_IN;
                co_lower->connectPort(id_CINY1, co_conn);
                cy->ports[id_COUTY1].name = id_COUTY1;
                cy->ports[id_COUTY1].type = PORT_OUT;
                cy->connectPort(id_COUTY1, co_conn);

                cy->movePortTo(id_CO, co_lower, id_OUT);
            } else {
                NetInfo *co_net = cy->getPort(id_CO);
                if (!co_net || co_net->users.entries() == 1) {
                    cy->renamePort(id_CO, id_COUTY1);
                } else {
                    for (auto &usr : co_net->users) {
                        if (usr.cell->type == id_CC_ADDF || usr.port == id_CI) {
                            usr.cell->disconnectPort(id_CI);
                            NetInfo *co_conn = ctx->createNet(ctx->idf("%s$co", cy->name.c_str(ctx)));
                            cy->ports[id_COUTY1].name = id_COUTY1;
                            cy->ports[id_COUTY1].type = PORT_OUT;
                            cy->connectPort(id_COUTY1, co_conn);
                            usr.cell->connectPort(id_CI, co_conn);
                            break;
                        }
                    }
                    //upper->params[id_C_O] = Property(0b10, 2);
                    cy->movePortTo(id_CO, upper, id_OUT);
                }
            }
        }
    }
}

void GateMatePacker::pack_constants()
{
    log_info("Packing constants..\n");
    // Replace constants with LUTs
    const dict<IdString, Property> vcc_params = {{id_INIT_L10, Property(0b1111, 4)}};
    const dict<IdString, Property> gnd_params = {{id_INIT_L10, Property(0b0000, 4)}};

    h.replace_constants(CellTypePort(id_CPE_LT, id_OUT), CellTypePort(id_CPE_LT, id_OUT), vcc_params, gnd_params);
}

void GateMatePacker::remove_constants()
{
    log_info("Removing constants..\n");
    auto fnd_cell = ctx->cells.find(ctx->id("$PACKER_VCC_DRV"));
    if (fnd_cell != ctx->cells.end()) {
        auto fnd_net = ctx->nets.find(ctx->id("$PACKER_VCC"));
        if (fnd_net != ctx->nets.end() && fnd_net->second->users.entries() == 0) {
            BelId bel = (*fnd_cell).second.get()->bel;
            if (bel != BelId())
                ctx->unbindBel(bel);
            ctx->cells.erase(fnd_cell);
            ctx->nets.erase(fnd_net);
            log_info("    Removed unused VCC cell\n");
        }
    }
    fnd_cell = ctx->cells.find(ctx->id("$PACKER_GND_DRV"));
    if (fnd_cell != ctx->cells.end()) {
        auto fnd_net = ctx->nets.find(ctx->id("$PACKER_GND"));
        if (fnd_net != ctx->nets.end() && fnd_net->second->users.entries() == 0) {
            BelId bel = (*fnd_cell).second.get()->bel;
            if (bel != BelId())
                ctx->unbindBel(bel);
            ctx->cells.erase(fnd_cell);
            ctx->nets.erase(fnd_net);
            log_info("    Removed unused GND cell\n");
        }
    }
}

NEXTPNR_NAMESPACE_END
