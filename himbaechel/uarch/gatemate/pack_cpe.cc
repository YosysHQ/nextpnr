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

bool GateMatePacker::are_ffs_compatible(CellInfo *dff, CellInfo *other)
{
    if (!other)
        return true;
    if (dff->getPort(id_CLK) != other->getPort(id_CLK))
        return false;
    if (dff->getPort(id_EN) != other->getPort(id_EN))
        return false;
    if (dff->getPort(id_SR) != other->getPort(id_SR))
        return false;
    if (uarch->get_dff_config(dff) != uarch->get_dff_config(other))
        return false;
    return true;
}

void GateMatePacker::dff_to_cpe(CellInfo *dff)
{
    bool invert;
    bool is_latch = dff->type == id_CC_DLT;
    if (is_latch) {
        NetInfo *g_net = dff->getPort(id_G);
        invert = bool_or_default(dff->params, id_G_INV, 0);
        if (g_net) {
            if (g_net == net_PACKER_GND) {
                dff->params[id_C_CPE_CLK] = Property(invert ? 0b11 : 0b00, 2);
                dff->disconnectPort(id_G);
            } else if (g_net == net_PACKER_VCC) {
                dff->params[id_C_CPE_CLK] = Property(invert ? 0b00 : 0b11, 2);
                dff->disconnectPort(id_G);
            } else {
                dff->params[id_C_CPE_CLK] = Property(invert ? 0b01 : 0b10, 2);
            }
        } else {
            dff->params[id_C_CPE_CLK] = Property(invert ? 0b11 : 0b00, 2);
        }
        dff->unsetParam(id_G_INV);
        dff->renamePort(id_G, id_CLK);

        dff->params[id_C_CPE_EN] = Property(0b11, 2);
        dff->params[id_C_L_D] = Property(0b1, 1);
    } else {
        NetInfo *en_net = dff->getPort(id_EN);
        bool invert = bool_or_default(dff->params, id_EN_INV, 0);
        if (en_net) {
            if (en_net == net_PACKER_GND) {
                dff->params[id_C_CPE_EN] = Property(invert ? 0b11 : 0b00, 2);
                dff->disconnectPort(id_EN);
            } else if (en_net == net_PACKER_VCC) {
                dff->params[id_C_CPE_EN] = Property(invert ? 0b00 : 0b11, 2);
                dff->disconnectPort(id_EN);
            } else {
                dff->params[id_C_CPE_EN] = Property(invert ? 0b01 : 0b10, 2);
            }
        } else {
            dff->params[id_C_CPE_EN] = Property(invert ? 0b11 : 0b00, 2);
        }
        dff->unsetParam(id_EN_INV);

        NetInfo *clk_net = dff->getPort(id_CLK);
        invert = bool_or_default(dff->params, id_CLK_INV, 0);
        if (clk_net) {
            if (clk_net == net_PACKER_GND) {
                dff->params[id_C_CPE_CLK] = Property(invert ? 0b11 : 0b00, 2);
                dff->disconnectPort(id_CLK);
            } else if (clk_net == net_PACKER_VCC) {
                dff->params[id_C_CPE_CLK] = Property(invert ? 0b00 : 0b11, 2);
                dff->disconnectPort(id_CLK);
            } else {
                dff->params[id_C_CPE_CLK] = Property(invert ? 0b01 : 0b10, 2);
            }
        } else {
            dff->params[id_C_CPE_CLK] = Property(invert ? 0b11 : 0b00, 2);
        }
        dff->unsetParam(id_CLK_INV);
    }

    NetInfo *sr_net = dff->getPort(id_SR);
    invert = bool_or_default(dff->params, id_SR_INV, 0);
    bool sr_val = bool_or_default(dff->params, id_SR_VAL, 0);
    if (sr_net) {
        if (sr_net == net_PACKER_VCC || sr_net == net_PACKER_GND) {
            bool sr_signal = sr_net == net_PACKER_VCC;
            if (sr_signal ^ invert) {
                if (sr_val) {
                    dff->params[id_C_CPE_RES] = Property(0b11, 2);
                    dff->params[id_C_CPE_SET] = Property(0b00, 2);
                } else {
                    dff->params[id_C_CPE_RES] = Property(0b00, 2);
                    dff->params[id_C_CPE_SET] = Property(0b11, 2);
                }
            } else {
                dff->params[id_C_CPE_RES] = Property(0b11, 2);
                dff->params[id_C_CPE_SET] = Property(0b11, 2);
            }
            dff->disconnectPort(id_SR);
        } else {
            if (sr_val) {
                dff->params[id_C_CPE_RES] = Property(0b11, 2);
                dff->params[id_C_CPE_SET] = Property(invert ? 0b10 : 0b01, 2);
                if (is_latch)
                    dff->renamePort(id_SR, id_EN);
                else
                    dff->params[id_C_EN_SR] = Property(0b1, 1);
            } else {
                dff->params[id_C_CPE_RES] = Property(invert ? 0b10 : 0b01, 2);
                dff->params[id_C_CPE_SET] = Property(0b11, 2);
            }
        }
    } else {
        dff->params[id_C_CPE_RES] = Property(0b11, 2);
        dff->params[id_C_CPE_SET] = Property(0b11, 2);
    }
    dff->unsetParam(id_SR_VAL);
    dff->unsetParam(id_SR_INV);

    if (dff->params.count(id_INIT) && dff->params[id_INIT].is_fully_def()) {
        bool init = bool_or_default(dff->params, id_INIT, 0);
        if (init)
            dff->params[id_FF_INIT] = Property(0b11, 2);
        else
            dff->params[id_FF_INIT] = Property(0b10, 2);
        dff->unsetParam(id_INIT);
    } else {
        dff->unsetParam(id_INIT);
    }
}

void GateMatePacker::dff_update_params()
{
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_DFF, id_CC_DLT))
            continue;
        dff_to_cpe(&ci);
    }
}

void GateMatePacker::pack_cpe()
{
    log_info("Packing CPEs..\n");
    std::vector<CellInfo *> l2t5_list;

    auto merge_dff = [&](CellInfo &ci, CellInfo *dff) {
        dff->cluster = ci.name;
        dff->constr_abs_z = false;
        dff->constr_z = +2;
        ci.cluster = ci.name;
        ci.constr_children.push_back(dff);
        dff->renamePort(id_D, id_DIN);
        dff->renamePort(id_Q, id_DOUT);
        dff->type = (dff->type == id_CC_DLT) ? id_CPE_LATCH : id_CPE_FF;
    };

    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_L2T4, id_CC_L2T5, id_CC_LUT2, id_CC_LUT1, id_CC_MX2))
            continue;
        bool is_l2t5 = false;
        if (ci.type == id_CC_L2T5) {
            l2t5_list.push_back(&ci);
            ci.renamePort(id_I0, id_IN1);
            ci.renamePort(id_I1, id_IN2);
            ci.renamePort(id_I2, id_IN3);
            ci.renamePort(id_I3, id_IN4);

            ci.renamePort(id_O, id_OUT);
            rename_param(&ci, id_INIT_L02, id_INIT_L00, 4);
            rename_param(&ci, id_INIT_L03, id_INIT_L01, 4);
            rename_param(&ci, id_INIT_L11, id_INIT_L10, 4);
            ci.cluster = ci.name;
            ci.constr_abs_z = true;
            ci.constr_z = CPE_LT_L_Z;
            ci.type = id_CPE_L2T4;
            is_l2t5 = true;
        } else if (ci.type == id_CC_MX2) {
            ci.renamePort(id_D1, id_IN1);
            NetInfo *sel = ci.getPort(id_S0);
            ci.renamePort(id_S0, id_IN2);
            ci.ports[id_IN3].name = id_IN3;
            ci.ports[id_IN3].type = PORT_IN;
            ci.connectPort(id_IN3, sel);
            ci.renamePort(id_D0, id_IN4);
            ci.disconnectPort(id_D1);
            ci.params[id_INIT_L00] = Property(LUT_AND, 4);
            ci.params[id_INIT_L01] = Property(LUT_AND_INV_D0, 4);
            ci.params[id_INIT_L10] = Property(LUT_OR, 4);
            ci.renamePort(id_Y, id_OUT);
            ci.type = id_CPE_L2T4;
        } else {
            ci.renamePort(id_I0, id_IN1);
            ci.renamePort(id_I1, id_IN2);
            ci.renamePort(id_I2, id_IN3);
            ci.renamePort(id_I3, id_IN4);
            ci.renamePort(id_O, id_OUT);
            if (ci.type.in(id_CC_LUT1, id_CC_LUT2)) {
                uint8_t val = int_or_default(ci.params, id_INIT, 0);
                if (ci.type == id_CC_LUT1)
                    val = val << 2 | val;
                ci.params[id_INIT_L00] = Property(val, 4);
                ci.unsetParam(id_INIT);
                ci.params[id_INIT_L10] = Property(LUT_D0, 4);
            }
            ci.type = id_CPE_L2T4;
        }
        NetInfo *o = ci.getPort(id_OUT);
        if (o) {
            if (o->users.entries() == 1) {
                // When only it is driving FF
                CellInfo *dff = net_only_drives(ctx, o, is_dff, id_D, true);
                if (dff)
                    merge_dff(ci, dff);
            } else if (!is_l2t5) {
                CellInfo *dff = net_only_drives(ctx, o, is_dff, id_D, false);
                // When driving FF + other logic
                if (dff) {
                    // Make sure main logic is in upper half
                    ci.constr_abs_z = true;
                    ci.constr_z = CPE_LT_U_Z;

                    merge_dff(ci, dff);

                    // Lower half propagate output from upper one
                    CellInfo *lower = create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$lower", ci.name.c_str(ctx)));
                    ci.constr_children.push_back(lower);
                    lower->cluster = ci.name;
                    lower->constr_abs_z = true;
                    lower->constr_z = CPE_LT_L_Z;
                    lower->params[id_INIT_L20] = Property(LUT_D0, 4);
                    ci.movePortTo(id_OUT, lower, id_OUT);

                    // Reconnect net
                    NetInfo *ci_out_conn = ctx->createNet(ctx->idf("%s$out", ci.name.c_str(ctx)));
                    ci.connectPort(id_OUT, ci_out_conn);
                    lower->ports[id_COMBIN].name = id_COMBIN;
                    lower->ports[id_COMBIN].type = PORT_IN;
                    lower->connectPort(id_COMBIN, ci_out_conn);
                    dff->disconnectPort(id_DIN);
                    dff->connectPort(id_DIN, ci_out_conn);

                    // Attach if only remaining cell is FF
                    CellInfo *other = net_only_drives(ctx, o, is_dff, id_D, true);
                    if (other && are_ffs_compatible(dff, other)) {
                        merge_dff(ci, other);
                        other->constr_abs_z = true;
                        other->constr_z = 3;
                    }
                }
            }
        }
    }
    for (auto ci : l2t5_list) {
        CellInfo *upper = create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$upper", ci->name.c_str(ctx)));
        upper->cluster = ci->name;
        upper->constr_abs_z = true;
        upper->constr_z = CPE_LT_U_Z;
        ci->movePortTo(id_I4, upper, id_IN1);
        upper->params[id_INIT_L00] = Property(LUT_D0, 4);
        upper->params[id_INIT_L10] = Property(LUT_D0, 4);
        ci->constr_children.push_back(upper);

        NetInfo *ci_out_conn = ctx->createNet(ctx->idf("%s$combin", ci->name.c_str(ctx)));
        upper->connectPort(id_OUT, ci_out_conn);
        ci->ports[id_COMBIN].name = id_COMBIN;
        ci->ports[id_COMBIN].type = PORT_IN;
        ci->connectPort(id_COMBIN, ci_out_conn);
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
                if (net == net_PACKER_GND) {
                    ci.disconnectPort(ctx->idf("D%d", i));
                } else if (net == net_PACKER_VCC) {
                    invert |= 1 << i;
                    ci.disconnectPort(ctx->idf("D%d", i));
                } else {
                    select |= 1 << i;
                }
            }
        }
        ci.params[id_C_FUNCTION] = Property(C_MX4, 3);
        ci.params[id_INIT_L02] = Property(LUT_D1, 4); // IN6
        ci.params[id_INIT_L03] = Property(LUT_D1, 4); // IN8
        ci.params[id_INIT_L11] = Property(invert, 4); // Inversion bits
        ci.params[id_INIT_L20] = Property(LUT_D1, 4); // Always D1
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

        NetInfo *o = ci.getPort(id_OUT);
        if (o) {
            CellInfo *dff = net_only_drives(ctx, o, is_dff, id_D, true);
            if (dff)
                merge_dff(ci, dff);
        }
    }
    mux_list.clear();

    std::vector<CellInfo *> dff_list;
    for (auto &cell : ctx->cells) {
        CellInfo &ci = *cell.second;
        if (!ci.type.in(id_CC_DFF, id_CC_DLT))
            continue;
        dff_list.push_back(&ci);
    }
    for (auto &cell : dff_list) {
        CellInfo &ci = *cell;
        CellInfo *lt = create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$lt", ci.name.c_str(ctx)));
        lt->cluster = ci.name;
        lt->constr_abs_z = false;
        lt->constr_z = -2;
        ci.cluster = ci.name;
        ci.constr_children.push_back(lt);
        ci.renamePort(id_Q, id_DOUT);
        NetInfo *d_net = ci.getPort(id_D);
        if (d_net == net_PACKER_GND) {
            lt->params[id_INIT_L00] = Property(LUT_ZERO, 4);
            ci.disconnectPort(id_D);
        } else if (d_net == net_PACKER_VCC) {
            lt->params[id_INIT_L00] = Property(LUT_ONE, 4);
            ci.disconnectPort(id_D);
        } else {
            lt->params[id_INIT_L00] = Property(LUT_D0, 4);
        }
        lt->params[id_INIT_L10] = Property(LUT_D0, 4);
        ci.movePortTo(id_D, lt, id_IN1);
        ci.type = (ci.type == id_CC_DLT) ? id_CPE_LATCH : id_CPE_FF;
        NetInfo *conn = ctx->createNet(ctx->idf("%s$di", ci.name.c_str(ctx)));
        lt->connectPort(id_OUT, conn);
        ci.ports[id_DIN].name = id_DIN;
        ci.ports[id_DIN].type = PORT_IN;
        ci.connectPort(id_DIN, conn);
    }
    dff_list.clear();
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

    auto merge_input = [&](CellInfo *cell, CellInfo *target, IdString port, IdString config, IdString in1,
                           IdString in2) {
        NetInfo *net = cell->getPort(port);
        if (net == net_PACKER_GND) {
            target->params[config] = Property(LUT_ZERO, 4);
            cell->disconnectPort(port);
        } else if (net == net_PACKER_VCC) {
            target->params[config] = Property(LUT_ONE, 4);
            cell->disconnectPort(port);
        } else {
            if (net && net->driver.cell && net->driver.cell->type.in(id_CC_LUT1, id_CC_LUT2) &&
                (net->users.entries() == 1)) {
                CellInfo *lut2 = net->driver.cell;
                uint8_t val = int_or_default(lut2->params, id_INIT, 0);
                if (lut2->type == id_CC_LUT1)
                    val = val << 2 | val;

                target->params[config] = Property(val, 4);
                lut2->movePortTo(id_I0, target, in1);
                lut2->movePortTo(id_I1, target, in2);
                cell->disconnectPort(port);
                packed_cells.insert(lut2->name);
            } else {
                if (cell == target)
                    cell->renamePort(port, in1);
                else
                    cell->movePortTo(port, target, in1);
                target->params[config] = Property(LUT_D0, 4);
            }
        }
    };

    auto merge_dff = [&](CellInfo *cell, IdString port, CellInfo *other) -> CellInfo * {
        NetInfo *o = cell->getPort(port);
        if (o) {
            CellInfo *dff = net_only_drives(ctx, o, is_dff, id_D, true);
            if (dff && are_ffs_compatible(dff, other)) {
                dff->cluster = cell->cluster;
                dff->constr_abs_z = false;
                dff->constr_z = +2;
                cell->constr_children.push_back(dff);
                dff->renamePort(id_D, id_DIN);
                dff->renamePort(id_Q, id_DOUT);
                dff->type = (dff->type == id_CC_DLT) ? id_CPE_LATCH : id_CPE_FF;
                return dff;
            }
        }
        return nullptr;
    };

    for (auto &grp : splitNestedVector(groups)) {
        CellInfo *root = grp.front();
        root->cluster = root->name;

        CellInfo *ci_upper = create_cell_ptr(id_CPE_DUMMY, ctx->idf("%s$ci_upper", root->name.c_str(ctx)));
        root->constr_children.push_back(ci_upper);
        ci_upper->cluster = root->name;
        ci_upper->constr_abs_z = false;
        ci_upper->constr_z = -1;
        ci_upper->constr_y = -1;

        CellInfo *ci_lower = create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$ci", root->name.c_str(ctx)));
        root->constr_children.push_back(ci_lower);
        ci_lower->cluster = root->name;
        ci_lower->constr_abs_z = false;
        ci_lower->constr_y = -1;
        ci_lower->params[id_INIT_L00] = Property(LUT_ZERO, 4);
        ci_lower->params[id_INIT_L10] = Property(LUT_D0, 4);

        CellInfo *ci_cplines = create_cell_ptr(id_CPE_CPLINES, ctx->idf("%s$ci_cplines", root->name.c_str(ctx)));
        ci_cplines->params[id_C_SELY1] = Property(1, 1);
        ci_cplines->params[id_C_CY1_I] = Property(1, 1);
        root->constr_children.push_back(ci_cplines);
        ci_cplines->cluster = root->name;
        ci_cplines->constr_abs_z = true;
        ci_cplines->constr_y = -1;
        ci_cplines->constr_z = CPE_CPLINES_Z;
        NetInfo *ci_out_conn = ctx->createNet(ctx->idf("%s$out", ci_lower->name.c_str(ctx)));
        ci_lower->connectPort(id_OUT, ci_out_conn);
        ci_cplines->connectPort(id_OUT1, ci_out_conn);

        NetInfo *ci_net = root->getPort(id_CI);
        if (ci_net == net_PACKER_GND) {
            ci_lower->params[id_INIT_L00] = Property(LUT_ZERO, 4);
            root->disconnectPort(id_CI);
        } else if (ci_net == net_PACKER_VCC) {
            ci_lower->params[id_INIT_L00] = Property(LUT_ONE, 4);
            root->disconnectPort(id_CI);
        } else {
            root->movePortTo(id_CI, ci_lower, id_IN1); // IN5
            ci_lower->params[id_INIT_L00] = Property(LUT_D0, 4);
        }

        NetInfo *ci_conn = ctx->createNet(ctx->idf("%s$ci_net", root->name.c_str(ctx)));
        ci_cplines->connectPort(id_COUTY1, ci_conn);

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
                merge_input(cy, cy, id_A2, id_INIT_L02, id_IN1, id_IN2); // IN5,IN6
                merge_input(cy, cy, id_B2, id_INIT_L03, id_IN3, id_IN4); // IN7,IN8
                cy->params[id_INIT_L11] = Property(LUT_XOR, 4);
            } else {
                cy->params[id_INIT_L02] = Property(LUT_ZERO, 4);
                cy->params[id_INIT_L03] = Property(LUT_ZERO, 4);
                cy->params[id_INIT_L11] = Property(LUT_XOR, 4);
                cy->params[id_INIT_L20] = Property(LUT_XOR, 4);
            }
            cy->params[id_C_FUNCTION] = Property(merged ? C_ADDF2 : C_ADDF, 3);
            cy->type = id_CPE_LT_L;

            CellInfo *upper = create_cell_ptr(id_CPE_LT_U, ctx->idf("%s$upper", cy->name.c_str(ctx)));
            upper->cluster = root->name;
            root->constr_children.push_back(upper);
            upper->constr_abs_z = false;
            upper->constr_y = +i;
            upper->constr_z = -1;
            CellInfo *other_dff = nullptr;
            if (merged) {
                cy->movePortTo(id_S, upper, id_OUT);
                cy->renamePort(id_S2, id_OUT);
                other_dff = merge_dff(upper, id_OUT, other_dff);
            } else {
                cy->renamePort(id_S, id_OUT);
            }
            merge_dff(cy, id_OUT, other_dff);
            merge_input(cy, upper, id_A, id_INIT_L00, id_IN1, id_IN2);
            merge_input(cy, upper, id_B, id_INIT_L01, id_IN3, id_IN4);
            upper->params[id_INIT_L10] = Property(LUT_XOR, 4);
            upper->params[id_C_FUNCTION] = Property(merged ? C_ADDF2 : C_ADDF, 3);

            if (i == grp.size() - 1) {
                if (!cy->getPort(id_CO))
                    break;
                CellInfo *co_upper = create_cell_ptr(id_CPE_DUMMY, ctx->idf("%s$co_upper", cy->name.c_str(ctx)));
                co_upper->cluster = root->name;
                root->constr_children.push_back(co_upper);
                co_upper->constr_abs_z = false;
                co_upper->constr_z = -1;
                co_upper->constr_y = +i + 1;
                CellInfo *co_lower = create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$co", cy->name.c_str(ctx)));
                co_lower->cluster = root->name;
                root->constr_children.push_back(co_lower);
                co_lower->constr_abs_z = false;
                co_lower->constr_y = +i + 1;
                co_lower->params[id_C_FUNCTION] = Property(C_EN_CIN, 3);
                co_lower->params[id_INIT_L10] = Property(LUT_D1, 4);
                co_lower->params[id_INIT_L20] = Property(LUT_D1, 4);

                NetInfo *co_conn = ctx->createNet(ctx->idf("%s$co_net", cy->name.c_str(ctx)));

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
                            NetInfo *co_conn = ctx->createNet(ctx->idf("%s$co_net", cy->name.c_str(ctx)));
                            cy->ports[id_COUTY1].name = id_COUTY1;
                            cy->ports[id_COUTY1].type = PORT_OUT;
                            cy->connectPort(id_COUTY1, co_conn);
                            usr.cell->connectPort(id_CI, co_conn);
                            break;
                        }
                    }
                    cy->movePortTo(id_CO, upper, id_CPOUT);
                }
            }
        }
    }
    flush_cells();
}

void GateMatePacker::pack_constants()
{
    log_info("Packing constants..\n");
    // Replace constants with LUTs
    const dict<IdString, Property> vcc_params = {{id_INIT_L10, Property(LUT_ONE, 4)}};
    const dict<IdString, Property> gnd_params = {{id_INIT_L10, Property(LUT_ZERO, 4)}};

    h.replace_constants(CellTypePort(id_CPE_L2T4, id_OUT), CellTypePort(id_CPE_L2T4, id_OUT), vcc_params, gnd_params);
    net_PACKER_VCC = ctx->nets.at(ctx->id("$PACKER_VCC")).get();
    net_PACKER_GND = ctx->nets.at(ctx->id("$PACKER_GND")).get();
}

void GateMatePacker::remove_constants()
{
    log_info("Removing unused constants..\n");
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

std::pair<CellInfo *, CellInfo *> GateMatePacker::move_ram_i(CellInfo *cell, IdString origPort, bool place, Loc cpe_loc)
{
    CellInfo *cpe_half = nullptr;
    CellInfo *cpe_ramio = nullptr;
    NetInfo *net = cell->getPort(origPort);
    if (net) {
        cpe_ramio = create_cell_ptr(id_CPE_RAMI, ctx->idf("%s$%s_rami", cell->name.c_str(ctx), origPort.c_str(ctx)));
        if (place) {
            cell->constr_children.push_back(cpe_ramio);
            cpe_ramio->cluster = cell->cluster;
            cpe_ramio->constr_abs_z = false;
            cpe_ramio->constr_z = PLACE_DB_CONSTR + origPort.index;
        } else {
            BelId b = ctx->getBelByLocation(cpe_loc);
            ctx->bindBel(b, cpe_ramio, PlaceStrength::STRENGTH_FIXED);
        }
        CellInfo *cpe_half =
                create_cell_ptr(id_CPE_DUMMY, ctx->idf("%s$%s_cpe", cell->name.c_str(ctx), origPort.c_str(ctx)));
        if (place) {
            cpe_ramio->constr_children.push_back(cpe_half);
            cpe_half->cluster = cell->cluster;
            cpe_half->constr_abs_z = false;
            cpe_half->constr_z = -4;
        } else {
            BelId b = ctx->getBelByLocation(Loc(cpe_loc.x, cpe_loc.y, cpe_loc.z - 4));
            ctx->bindBel(b, cpe_half, PlaceStrength::STRENGTH_FIXED);
        }

        cpe_ramio->params[id_C_RAM_I] = Property(1, 1);

        NetInfo *ram_i = ctx->createNet(ctx->idf("%s$ram_i", cpe_ramio->name.c_str(ctx)));
        cell->movePortTo(origPort, cpe_ramio, id_OUT);
        cell->connectPort(origPort, ram_i);
        cpe_ramio->connectPort(id_RAM_I, ram_i);
    }
    return std::make_pair(cpe_half, cpe_ramio);
}

std::pair<CellInfo *, CellInfo *> GateMatePacker::move_ram_o(CellInfo *cell, IdString origPort, bool place, Loc cpe_loc)
{
    CellInfo *cpe_half = nullptr;
    CellInfo *cpe_ramio = nullptr;
    NetInfo *net = cell->getPort(origPort);
    if (net) {
        cpe_ramio = create_cell_ptr(id_CPE_RAMO, ctx->idf("%s$%s_ramo", cell->name.c_str(ctx), origPort.c_str(ctx)));
        if (place) {
            cell->constr_children.push_back(cpe_ramio);
            cpe_ramio->cluster = cell->cluster;
            cpe_ramio->constr_abs_z = false;
            cpe_ramio->constr_z = PLACE_DB_CONSTR + origPort.index;
        } else {
            BelId b = ctx->getBelByLocation(cpe_loc);
            ctx->bindBel(b, cpe_ramio, PlaceStrength::STRENGTH_FIXED);
        }
        cpe_half = create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$%s_cpe", cell->name.c_str(ctx), origPort.c_str(ctx)));
        if (place) {
            cpe_ramio->constr_children.push_back(cpe_half);
            cpe_half->cluster = cell->cluster;
            cpe_half->constr_abs_z = false;
            cpe_half->constr_z = -4;
        } else {
            BelId b = ctx->getBelByLocation(Loc(cpe_loc.x, cpe_loc.y, cpe_loc.z - 4));
            ctx->bindBel(b, cpe_half, PlaceStrength::STRENGTH_FIXED);
        }
        if (net == net_PACKER_GND) {
            cpe_half->params[id_INIT_L00] = Property(LUT_ZERO, 4);
            cell->disconnectPort(origPort);
        } else if (net == net_PACKER_VCC) {
            cpe_half->params[id_INIT_L00] = Property(LUT_ONE, 4);
            cell->disconnectPort(origPort);
        } else {
            cpe_half->params[id_INIT_L00] = Property(LUT_D0, 4);
            cell->movePortTo(origPort, cpe_half, id_IN1);
        }
        cpe_half->params[id_INIT_L10] = Property(LUT_D0, 4);

        cpe_ramio->params[id_C_RAM_O] = Property(1, 1);
        NetInfo *ram_o = ctx->createNet(ctx->idf("%s$ram_o", cpe_half->name.c_str(ctx)));
        cell->connectPort(origPort, ram_o);
        cpe_ramio->connectPort(id_RAM_O, ram_o);

        NetInfo *out = ctx->createNet(ctx->idf("%s$out", cpe_half->name.c_str(ctx)));
        cpe_half->connectPort(id_OUT, out);
        cpe_ramio->connectPort(id_I, out);
    }
    return std::make_pair(cpe_half, cpe_ramio);
}

std::pair<CellInfo *, CellInfo *> GateMatePacker::move_ram_io(CellInfo *cell, IdString iPort, IdString oPort,
                                                              bool place, Loc cpe_loc)
{
    NetInfo *i_net = cell->getPort(iPort);
    NetInfo *o_net = cell->getPort(oPort);
    if (!i_net && !o_net)
        return std::make_pair(nullptr, nullptr);

    CellInfo *cpe_ramio =
            create_cell_ptr(id_CPE_RAMIO, ctx->idf("%s$%s_ramio", cell->name.c_str(ctx), oPort.c_str(ctx)));
    if (place) {
        cell->constr_children.push_back(cpe_ramio);
        cpe_ramio->cluster = cell->cluster;
        cpe_ramio->constr_abs_z = false;
        cpe_ramio->constr_z = PLACE_DB_CONSTR + oPort.index;
    } else {
        BelId b = ctx->getBelByLocation(cpe_loc);
        ctx->bindBel(b, cpe_ramio, PlaceStrength::STRENGTH_FIXED);
    }
    CellInfo *cpe_half = create_cell_ptr(id_CPE_L2T4, ctx->idf("%s$%s_cpe", cell->name.c_str(ctx), oPort.c_str(ctx)));
    if (place) {
        cpe_ramio->constr_children.push_back(cpe_half);
        cpe_half->cluster = cell->cluster;
        cpe_half->constr_abs_z = false;
        cpe_half->constr_z = -4;
    } else {
        BelId b = ctx->getBelByLocation(Loc(cpe_loc.x, cpe_loc.y, cpe_loc.z - 4));
        ctx->bindBel(b, cpe_half, PlaceStrength::STRENGTH_FIXED);
    }

    if (o_net) {
        if (o_net == net_PACKER_GND) {
            cpe_half->params[id_INIT_L00] = Property(LUT_ZERO, 4);
            cell->disconnectPort(oPort);
        } else if (o_net == net_PACKER_VCC) {
            cpe_half->params[id_INIT_L00] = Property(LUT_ONE, 4);
            cell->disconnectPort(oPort);
        } else {
            cpe_half->params[id_INIT_L00] = Property(LUT_D0, 4);
            cell->movePortTo(oPort, cpe_half, id_IN1);
        }
        cpe_half->params[id_INIT_L10] = Property(LUT_D0, 4);
        cpe_ramio->params[id_C_RAM_O] = Property(1, 1);

        NetInfo *ram_o = ctx->createNet(ctx->idf("%s$ram_o", cpe_half->name.c_str(ctx)));
        cell->connectPort(oPort, ram_o);
        cpe_ramio->connectPort(id_RAM_O, ram_o);

        NetInfo *out = ctx->createNet(ctx->idf("%s$out", cpe_half->name.c_str(ctx)));
        cpe_half->connectPort(id_OUT, out);
        cpe_ramio->connectPort(id_I, out);
    }
    if (i_net) {
        cpe_ramio->params[id_C_RAM_I] = Property(1, 1);

        NetInfo *ram_i = ctx->createNet(ctx->idf("%s$ram_i", cpe_half->name.c_str(ctx)));
        cell->movePortTo(iPort, cpe_ramio, id_OUT);
        cell->connectPort(iPort, ram_i);
        cpe_ramio->connectPort(id_RAM_I, ram_i);
    }
    return std::make_pair(cpe_half, cpe_ramio);
}

std::pair<CellInfo *, CellInfo *> GateMatePacker::move_ram_i_fixed(CellInfo *cell, IdString origPort, Loc fixed)
{
    return move_ram_i(cell, origPort, false, uarch->getRelativeConstraint(fixed, origPort));
}

std::pair<CellInfo *, CellInfo *> GateMatePacker::move_ram_o_fixed(CellInfo *cell, IdString origPort, Loc fixed)
{
    return move_ram_o(cell, origPort, false, uarch->getRelativeConstraint(fixed, origPort));
}

std::pair<CellInfo *, CellInfo *> GateMatePacker::move_ram_io_fixed(CellInfo *cell, IdString iPort, IdString oPort,
                                                                    Loc fixed)
{
    return move_ram_io(cell, iPort, oPort, false, uarch->getRelativeConstraint(fixed, oPort));
}

NEXTPNR_NAMESPACE_END
