/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018-19  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2020  Pepijn de Vos <pepijn@symbioticeda.com>
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

#include <algorithm>
#include <iostream>
#include <iterator>
#include "cells.h"
#include "design_utils.h"
#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

static void make_dummy_alu(Context *ctx, int alu_idx, CellInfo *ci, CellInfo *packed_head,
                           std::vector<std::unique_ptr<CellInfo>> &new_cells)
{
    if ((alu_idx % 2) == 0) {
        return;
    }
    std::unique_ptr<CellInfo> dummy = create_generic_cell(ctx, id_SLICE, ci->name.str(ctx) + "_DUMMY_ALULC");
    if (ctx->verbose) {
        log_info("packed dummy ALU %s.\n", ctx->nameOf(dummy.get()));
    }
    dummy->params[id_ALU_MODE] = std::string("C2L");
    // add to cluster
    dummy->cluster = packed_head->name;
    dummy->constr_z = alu_idx % 6;
    dummy->constr_x = alu_idx / 6;
    dummy->constr_y = 0;
    packed_head->constr_children.push_back(dummy.get());
    new_cells.push_back(std::move(dummy));
}

// replace ALU with LUT
static void pack_alus(Context *ctx)
{
    log_info("Packing ALUs..\n");

    // cell name, CIN net name
    pool<std::pair<IdString, IdString>> alu_heads;

    // collect heads
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (is_alu(ctx, ci)) {
            NetInfo *cin = ci->ports.at(id_CIN).net;
            CellInfo *cin_ci = cin->driver.cell;

            if (cin == nullptr || cin_ci == nullptr) {
                log_error("CIN disconnected at ALU:%s\n", ctx->nameOf(ci));
                continue;
            }

            if (!is_alu(ctx, cin_ci) || cin->users.entries() > 1) {
                if (ctx->verbose) {
                    log_info("ALU head found %s. CIN net is %s\n", ctx->nameOf(ci), ctx->nameOf(cin));
                }
                alu_heads.insert(std::make_pair(ci->name, cin->name));
            }
        }
    }

    pool<IdString> packed_cells;
    pool<IdString> delete_nets;
    std::vector<std::unique_ptr<CellInfo>> new_cells;

    for (auto &head : alu_heads) {
        CellInfo *ci = ctx->cells[head.first].get();
        IdString cin_netId = head.second;
        if (ctx->verbose) {
            log_info("cell '%s' is of type '%s'\n", ctx->nameOf(ci), ci->type.c_str(ctx));
        }
        std::unique_ptr<CellInfo> packed_head = create_generic_cell(ctx, id_SLICE, ci->name.str(ctx) + "_HEAD_ALULC");

        // Head is always SLICE0
        packed_head->constr_z = 0;
        packed_head->constr_abs_z = true;
        if (ctx->verbose) {
            log_info("packed ALU head into %s. CIN net is %s\n", ctx->nameOf(packed_head.get()),
                     ctx->nameOf(cin_netId));
        }
        packed_head->connectPort(id_C, ctx->nets[ctx->id("$PACKER_VCC_NET")].get());
        if (cin_netId == ctx->id("$PACKER_GND_NET")) {
            // CIN = 0
            packed_head->params[id_ALU_MODE] = std::string("C2L");
        } else {
            if (cin_netId == ctx->id("$PACKER_VCC_NET")) {
                // CIN = 1
                packed_head->params[id_ALU_MODE] = std::string("ONE2C");
            } else {
                // CIN from logic
                packed_head->connectPort(id_B, ctx->nets[cin_netId].get());
                packed_head->connectPort(id_D, ctx->nets[cin_netId].get());
                packed_head->params[id_ALU_MODE] = std::string("0"); // ADD
            }
        }

        int alu_idx = 1;
        do { // go through the ALU chain
            auto alu_bel = ci->attrs.find(id_BEL);
            if (alu_bel != ci->attrs.end()) {
                log_error("ALU %s placement restrictions are not supported.\n", ctx->nameOf(ci));
                return;
            }
            // remove cell
            packed_cells.insert(ci->name);

            // CIN/COUT are hardwired, delete
            ci->disconnectPort(id_CIN);
            NetInfo *cout = ci->ports.at(id_COUT).net;
            ci->disconnectPort(id_COUT);

            std::unique_ptr<CellInfo> packed = create_generic_cell(ctx, id_SLICE, ci->name.str(ctx) + "_ALULC");
            if (ctx->verbose) {
                log_info("packed ALU into %s. COUT net is %s\n", ctx->nameOf(packed.get()), ctx->nameOf(cout));
            }

            int mode = int_or_default(ci->params, id_ALU_MODE);
            packed->params[id_ALU_MODE] = mode;
            if (mode == 9) { // MULT
                packed->connectPort(id_C, ctx->nets[ctx->id("$PACKER_GND_NET")].get());
            } else {
                packed->connectPort(id_C, ctx->nets[ctx->id("$PACKER_VCC_NET")].get());
            }

            // add to cluster
            packed->cluster = packed_head->name;
            packed->constr_z = alu_idx % 6;
            packed->constr_x = alu_idx / 6;
            packed->constr_y = 0;
            packed_head->constr_children.push_back(packed.get());
            ++alu_idx;

            // connect all remainig ports
            ci->movePortTo(id_SUM, packed.get(), id_F);
            switch (mode) {
            case 0: // ADD
                ci->movePortTo(id_I0, packed.get(), id_B);
                ci->movePortTo(id_I1, packed.get(), id_D);
                break;
            case 1: // SUB
                ci->movePortTo(id_I0, packed.get(), id_A);
                ci->movePortTo(id_I1, packed.get(), id_D);
                break;
            case 5: // LE
                ci->movePortTo(id_I0, packed.get(), id_A);
                ci->movePortTo(id_I1, packed.get(), id_B);
                break;
            case 9: // MULT
                ci->movePortTo(id_I0, packed.get(), id_A);
                ci->movePortTo(id_I1, packed.get(), id_B);
                packed->disconnectPort(id_D);
                packed->connectPort(id_D, ctx->nets[ctx->id("$PACKER_VCC_NET")].get());
                break;
            default:
                ci->movePortTo(id_I0, packed.get(), id_A);
                ci->movePortTo(id_I1, packed.get(), id_B);
                ci->movePortTo(id_I3, packed.get(), id_D);
            }

            new_cells.push_back(std::move(packed));

            if (cout != nullptr && cout->users.entries() > 0) {
                // if COUT used by logic
                if ((cout->users.entries() > 1) || (!is_alu(ctx, (*cout->users.begin()).cell))) {
                    if (ctx->verbose) {
                        log_info("COUT is used by logic\n");
                    }
                    // make gate C->logic
                    std::unique_ptr<CellInfo> packed_tail =
                            create_generic_cell(ctx, id_SLICE, ci->name.str(ctx) + "_TAIL_ALULC");
                    if (ctx->verbose) {
                        log_info("packed ALU tail into %s. COUT net is %s\n", ctx->nameOf(packed_tail.get()),
                                 ctx->nameOf(cout));
                    }
                    packed_tail->params[id_ALU_MODE] = std::string("C2L");
                    packed_tail->connectPort(id_F, cout);
                    // add to cluster
                    packed_tail->cluster = packed_head->name;
                    packed_tail->constr_z = alu_idx % 6;
                    packed_tail->constr_x = alu_idx / 6;
                    packed_tail->constr_y = 0;
                    ++alu_idx;
                    packed_head->constr_children.push_back(packed_tail.get());
                    new_cells.push_back(std::move(packed_tail));
                    make_dummy_alu(ctx, alu_idx, ci, packed_head.get(), new_cells);
                    break;
                }
                // next ALU
                ci = (*cout->users.begin()).cell;
                // if ALU is too big
                if (alu_idx == (ctx->gridDimX - 2) * 6 - 1) {
                    log_error("ALU %s is the %dth in the chain. Such long chains are not supported.\n", ctx->nameOf(ci),
                              alu_idx);
                    break;
                }
            } else {
                // COUT is unused
                if (ctx->verbose) {
                    log_info("cell is the ALU tail. Index is %d\n", alu_idx);
                }
                make_dummy_alu(ctx, alu_idx, ci, packed_head.get(), new_cells);
                break;
            }
        } while (1);

        // add head to the cluster
        packed_head->cluster = packed_head->name;
        new_cells.push_back(std::move(packed_head));
    }

    // actual delete, erase and move cells/nets
    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    for (auto dnet : delete_nets) {
        ctx->nets.erase(dnet);
    }
    for (auto &ncell : new_cells) {
        ctx->cells[ncell->name] = std::move(ncell);
    }
}

// pack MUX2_LUT5
static void pack_mux2_lut5(Context *ctx, CellInfo *ci, pool<IdString> &packed_cells, pool<IdString> &delete_nets,
                           std::vector<std::unique_ptr<CellInfo>> &new_cells)
{

    if (bool_or_default(ci->attrs, id_SINGLE_INPUT_MUX)) {
        // find the muxed LUT
        NetInfo *i1 = ci->ports.at(id_I1).net;

        CellInfo *lut1 = net_driven_by(ctx, i1, is_lut, id_F);
        if (lut1 == nullptr) {
            log_error("MUX2_LUT5 '%s' port I1 isn't connected to the LUT\n", ctx->nameOf(ci));
            return;
        }
        if (ctx->verbose) {
            log_info("found attached lut1 %s\n", ctx->nameOf(lut1));
        }

        // XXX enable the placement constraints
        auto mux_bel = ci->attrs.find(id_BEL);
        auto lut1_bel = lut1->attrs.find(id_BEL);
        if (lut1_bel != lut1->attrs.end() || mux_bel != ci->attrs.end()) {
            log_error("MUX2_LUT5 '%s' placement restrictions are not supported yet\n", ctx->nameOf(ci));
            return;
        }

        std::unique_ptr<CellInfo> packed = create_generic_cell(ctx, id_GW_MUX2_LUT5, ci->name.str(ctx) + "_LC");
        if (ctx->verbose) {
            log_info("packed cell %s into %s\n", ctx->nameOf(ci), ctx->nameOf(packed.get()));
        }
        // mux is the cluster root
        packed->cluster = packed->name;
        lut1->cluster = packed->name;
        lut1->constr_z = -ctx->mux_0_z + 1;
        packed->constr_children.clear();

        // reconnect MUX ports
        ci->movePortTo(id_O, packed.get(), id_OF);
        ci->movePortTo(id_I1, packed.get(), id_I1);

        // remove cells
        packed_cells.insert(ci->name);
        // new MUX cell
        new_cells.push_back(std::move(packed));
    } else {
        // find the muxed LUTs
        NetInfo *i0 = ci->ports.at(id_I0).net;
        NetInfo *i1 = ci->ports.at(id_I1).net;

        CellInfo *lut0 = net_driven_by(ctx, i0, is_lut, id_F);
        CellInfo *lut1 = net_driven_by(ctx, i1, is_lut, id_F);
        if (lut0 == nullptr || lut1 == nullptr) {
            log_error("MUX2_LUT5 '%s' port I0 or I1 isn't connected to the LUT\n", ctx->nameOf(ci));
            return;
        }
        if (ctx->verbose) {
            log_info("found attached lut0 %s\n", ctx->nameOf(lut0));
            log_info("found attached lut1 %s\n", ctx->nameOf(lut1));
        }

        // XXX enable the placement constraints
        auto mux_bel = ci->attrs.find(id_BEL);
        auto lut0_bel = lut0->attrs.find(id_BEL);
        auto lut1_bel = lut1->attrs.find(id_BEL);
        if (lut0_bel != lut0->attrs.end() || lut1_bel != lut1->attrs.end() || mux_bel != ci->attrs.end()) {
            log_error("MUX2_LUT5 '%s' placement restrictions are not supported yet\n", ctx->nameOf(ci));
            return;
        }

        std::unique_ptr<CellInfo> packed = create_generic_cell(ctx, id_GW_MUX2_LUT5, ci->name.str(ctx) + "_LC");
        if (ctx->verbose) {
            log_info("packed cell %s into %s\n", ctx->nameOf(ci), ctx->nameOf(packed.get()));
        }
        // mux is the cluster root
        packed->cluster = packed->name;
        lut0->cluster = packed->name;
        lut0->constr_z = -ctx->mux_0_z;
        lut1->cluster = packed->name;
        lut1->constr_z = -ctx->mux_0_z + 1;
        packed->constr_children.clear();

        // reconnect MUX ports
        ci->movePortTo(id_O, packed.get(), id_OF);
        ci->movePortTo(id_S0, packed.get(), id_SEL);
        ci->movePortTo(id_I0, packed.get(), id_I0);
        ci->movePortTo(id_I1, packed.get(), id_I1);

        // remove cells
        packed_cells.insert(ci->name);
        // new MUX cell
        new_cells.push_back(std::move(packed));
    }
}

// Common MUX2 packing routine
static void pack_mux2_lut(Context *ctx, CellInfo *ci, bool (*pred)(const BaseCtx *, const CellInfo *),
                          char const type_suffix, IdString const type_id, int const x[2], int const z[2],
                          pool<IdString> &packed_cells, pool<IdString> &delete_nets,
                          std::vector<std::unique_ptr<CellInfo>> &new_cells)
{
    // find the muxed LUTs
    NetInfo *i0 = ci->ports.at(id_I0).net;
    NetInfo *i1 = ci->ports.at(id_I1).net;

    CellInfo *mux0 = net_driven_by(ctx, i0, pred, id_OF);
    CellInfo *mux1 = net_driven_by(ctx, i1, pred, id_OF);
    if (mux0 == nullptr || mux1 == nullptr) {
        log_error("MUX2_LUT%c '%s' port I0 or I1 isn't connected to the MUX\n", type_suffix, ctx->nameOf(ci));
        return;
    }
    if (ctx->verbose) {
        log_info("found attached mux0 %s\n", ctx->nameOf(mux0));
        log_info("found attached mux1 %s\n", ctx->nameOf(mux1));
    }

    // XXX enable the placement constraints
    auto mux_bel = ci->attrs.find(id_BEL);
    auto mux0_bel = mux0->attrs.find(id_BEL);
    auto mux1_bel = mux1->attrs.find(id_BEL);
    if (mux0_bel != mux0->attrs.end() || mux1_bel != mux1->attrs.end() || mux_bel != ci->attrs.end()) {
        log_error("MUX2_LUT%c '%s' placement restrictions are not supported yet\n", type_suffix, ctx->nameOf(ci));
        return;
    }

    std::unique_ptr<CellInfo> packed = create_generic_cell(ctx, type_id, ci->name.str(ctx) + "_LC");
    if (ctx->verbose) {
        log_info("packed cell %s into %s\n", ctx->nameOf(ci), ctx->nameOf(packed.get()));
    }
    // mux is the cluster root
    packed->cluster = packed->name;
    mux0->cluster = packed->name;
    mux0->constr_x = x[0];
    mux0->constr_y = 0;
    mux0->constr_z = z[0];
    for (auto &child : mux0->constr_children) {
        child->cluster = packed->name;
        child->constr_x += mux0->constr_x;
        child->constr_z += mux0->constr_z;
        packed->constr_children.push_back(child);
    }
    mux0->constr_children.clear();
    mux1->cluster = packed->name;
    mux1->constr_x = x[1];
    mux0->constr_y = 0;
    mux1->constr_z = z[1];
    for (auto &child : mux1->constr_children) {
        child->cluster = packed->name;
        child->constr_x += mux1->constr_x;
        child->constr_z += mux1->constr_z;
        packed->constr_children.push_back(child);
    }
    mux1->constr_children.clear();
    packed->constr_children.push_back(mux0);
    packed->constr_children.push_back(mux1);

    // reconnect MUX ports
    ci->movePortTo(id_O, packed.get(), id_OF);
    ci->movePortTo(id_S0, packed.get(), id_SEL);
    ci->movePortTo(id_I0, packed.get(), id_I0);
    ci->movePortTo(id_I1, packed.get(), id_I1);

    // remove cells
    packed_cells.insert(ci->name);
    // new MUX cell
    new_cells.push_back(std::move(packed));
}

// pack MUX2_LUT6
static void pack_mux2_lut6(Context *ctx, CellInfo *ci, pool<IdString> &packed_cells, pool<IdString> &delete_nets,
                           std::vector<std::unique_ptr<CellInfo>> &new_cells)
{
    static int x[] = {0, 0};
    static int z[] = {+1, -1};
    pack_mux2_lut(ctx, ci, is_gw_mux2_lut5, '6', id_GW_MUX2_LUT6, x, z, packed_cells, delete_nets, new_cells);
}

// pack MUX2_LUT7
static void pack_mux2_lut7(Context *ctx, CellInfo *ci, pool<IdString> &packed_cells, pool<IdString> &delete_nets,
                           std::vector<std::unique_ptr<CellInfo>> &new_cells)
{
    static int x[] = {0, 0};
    static int z[] = {+2, -2};
    pack_mux2_lut(ctx, ci, is_gw_mux2_lut6, '7', id_GW_MUX2_LUT7, x, z, packed_cells, delete_nets, new_cells);
}

// pack MUX2_LUT8
static void pack_mux2_lut8(Context *ctx, CellInfo *ci, pool<IdString> &packed_cells, pool<IdString> &delete_nets,
                           std::vector<std::unique_ptr<CellInfo>> &new_cells)
{
    static int x[] = {1, 0};
    static int z[] = {-4, -4};
    pack_mux2_lut(ctx, ci, is_gw_mux2_lut7, '8', id_GW_MUX2_LUT8, x, z, packed_cells, delete_nets, new_cells);
}

// Pack wide LUTs
static void pack_wideluts(Context *ctx)
{
    log_info("Packing wide LUTs..\n");

    pool<IdString> packed_cells;
    pool<IdString> delete_nets;
    std::vector<std::unique_ptr<CellInfo>> new_cells;

    pool<IdString> mux2lut6;
    pool<IdString> mux2lut7;
    pool<IdString> mux2lut8;

    // do MUX2_LUT5 and collect LUT6/7/8
    log_info("Packing LUT5s..\n");
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ctx->verbose) {
            log_info("cell '%s' is of type '%s'\n", ctx->nameOf(ci), ci->type.c_str(ctx));
        }
        if (is_widelut(ctx, ci)) {
            if (is_mux2_lut5(ctx, ci)) {
                pack_mux2_lut5(ctx, ci, packed_cells, delete_nets, new_cells);
            } else {
                if (is_mux2_lut6(ctx, ci)) {
                    mux2lut6.insert(ci->name);
                } else {
                    if (is_mux2_lut7(ctx, ci)) {
                        mux2lut7.insert(ci->name);
                    } else {
                        if (is_mux2_lut8(ctx, ci)) {
                            mux2lut8.insert(ci->name);
                        }
                    }
                }
            }
        }
    }
    // do MUX_LUT6
    log_info("Packing LUT6s..\n");
    for (auto &cell_name : mux2lut6) {
        pack_mux2_lut6(ctx, ctx->cells[cell_name].get(), packed_cells, delete_nets, new_cells);
    }

    // do MUX_LUT7
    log_info("Packing LUT7s..\n");
    for (auto &cell_name : mux2lut7) {
        pack_mux2_lut7(ctx, ctx->cells[cell_name].get(), packed_cells, delete_nets, new_cells);
    }

    // do MUX_LUT8
    log_info("Packing LUT8s..\n");
    for (auto &cell_name : mux2lut8) {
        pack_mux2_lut8(ctx, ctx->cells[cell_name].get(), packed_cells, delete_nets, new_cells);
    }

    // actual delete, erase and move cells/nets
    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    for (auto dnet : delete_nets) {
        ctx->nets.erase(dnet);
    }
    for (auto &ncell : new_cells) {
        ctx->cells[ncell->name] = std::move(ncell);
    }
}

// Pack LUTs and LUT-FF pairs
static void pack_lut_lutffs(Context *ctx)
{
    log_info("Packing LUT-FFs..\n");

    pool<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ctx->verbose)
            log_info("cell '%s' is of type '%s'\n", ctx->nameOf(ci), ci->type.c_str(ctx));
        if (is_lut(ctx, ci)) {
            std::unique_ptr<CellInfo> packed = create_generic_cell(ctx, id_SLICE, ci->name.str(ctx) + "_LC");
            for (auto &attr : ci->attrs)
                packed->attrs[attr.first] = attr.second;
            packed_cells.insert(ci->name);
            if (ctx->verbose)
                log_info("packed cell %s into %s\n", ctx->nameOf(ci), ctx->nameOf(packed.get()));
            // See if we can pack into a DFF
            // TODO: LUT cascade
            NetInfo *o = ci->ports.at(id_F).net;
            CellInfo *dff = net_only_drives(ctx, o, is_ff, id_D, true);
            auto lut_bel = ci->attrs.find(id_BEL);
            bool packed_dff = false;
            if (dff) {
                if (ctx->verbose)
                    log_info("found attached dff %s\n", ctx->nameOf(dff));
                auto dff_bel = dff->attrs.find(id_BEL);
                if (lut_bel != ci->attrs.end() && dff_bel != dff->attrs.end() && lut_bel->second != dff_bel->second) {
                    // Locations don't match, can't pack
                } else {
                    lut_to_lc(ctx, ci, packed.get(), false);
                    dff_to_lc(ctx, dff, packed.get(), false);
                    ctx->nets.erase(o->name);
                    if (dff_bel != dff->attrs.end())
                        packed->attrs[id_BEL] = dff_bel->second;
                    packed_cells.insert(dff->name);
                    if (ctx->verbose)
                        log_info("packed cell %s into %s\n", ctx->nameOf(dff), ctx->nameOf(packed.get()));
                    packed_dff = true;
                }
            }
            if (!packed_dff) {
                lut_to_lc(ctx, ci, packed.get(), true);
            }
            new_cells.push_back(std::move(packed));
        }
    }
    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    for (auto &ncell : new_cells) {
        ctx->cells[ncell->name] = std::move(ncell);
    }
}

// Pack FFs not packed as LUTFFs
static void pack_nonlut_ffs(Context *ctx)
{
    log_info("Packing non-LUT FFs..\n");

    pool<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (is_ff(ctx, ci)) {
            std::unique_ptr<CellInfo> packed = create_generic_cell(ctx, id_SLICE, ci->name.str(ctx) + "_DFFLC");
            for (auto &attr : ci->attrs)
                packed->attrs[attr.first] = attr.second;
            if (ctx->verbose)
                log_info("packed cell %s into %s\n", ctx->nameOf(ci), ctx->nameOf(packed.get()));
            packed_cells.insert(ci->name);
            dff_to_lc(ctx, ci, packed.get(), true);
            new_cells.push_back(std::move(packed));
        }
    }
    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    for (auto &ncell : new_cells) {
        ctx->cells[ncell->name] = std::move(ncell);
    }
}

// Merge a net into a constant net
static void set_net_constant(const Context *ctx, NetInfo *orig, NetInfo *constnet, bool constval)
{
    orig->driver.cell = nullptr;
    for (auto user : orig->users) {
        if (user.cell != nullptr) {
            CellInfo *uc = user.cell;
            if (ctx->verbose)
                log_info("%s user %s\n", ctx->nameOf(orig), ctx->nameOf(uc));
            if ((is_lut(ctx, uc) || is_lc(ctx, uc)) && (user.port.str(ctx).at(0) == 'I') && !constval) {
                uc->ports[user.port].net = nullptr;
                uc->ports[user.port].user_idx = {};
            } else {
                uc->ports[user.port].net = constnet;
                uc->ports[user.port].user_idx = constnet->users.add(user);
            }
        }
    }
    orig->users.clear();
}

// Pack constants (simple implementation)
static void pack_constants(Context *ctx)
{
    log_info("Packing constants..\n");

    std::unique_ptr<CellInfo> gnd_cell = create_generic_cell(ctx, id_SLICE, "$PACKER_GND");
    gnd_cell->params[id_INIT] = Property(0, 1 << 4);
    auto gnd_net = std::make_unique<NetInfo>(ctx->id("$PACKER_GND_NET"));
    gnd_net->driver.cell = gnd_cell.get();
    gnd_net->driver.port = id_F;
    gnd_cell->ports.at(id_F).net = gnd_net.get();

    std::unique_ptr<CellInfo> vcc_cell = create_generic_cell(ctx, id_SLICE, "$PACKER_VCC");
    // Fill with 1s
    vcc_cell->params[id_INIT] = Property(Property::S1).extract(0, (1 << 4), Property::S1);
    auto vcc_net = std::make_unique<NetInfo>(ctx->id("$PACKER_VCC_NET"));
    vcc_net->driver.cell = vcc_cell.get();
    vcc_net->driver.port = id_F;
    vcc_cell->ports.at(id_F).net = vcc_net.get();

    std::vector<IdString> dead_nets;

    bool gnd_used = true; // XXX May be needed for simplified IO

    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        if (ni->driver.cell != nullptr && ni->driver.cell->type == id_GND) {
            IdString drv_cell = ni->driver.cell->name;
            set_net_constant(ctx, ni, gnd_net.get(), false);
            gnd_used = true;
            dead_nets.push_back(net.first);
            ctx->cells.erase(drv_cell);
        } else if (ni->driver.cell != nullptr && ni->driver.cell->type == id_VCC) {
            IdString drv_cell = ni->driver.cell->name;
            set_net_constant(ctx, ni, vcc_net.get(), true);
            dead_nets.push_back(net.first);
            ctx->cells.erase(drv_cell);
        }
    }

    if (gnd_used) {
        ctx->cells[gnd_cell->name] = std::move(gnd_cell);
        ctx->nets[gnd_net->name] = std::move(gnd_net);
    }
    // Vcc cell always inserted for now, as it may be needed during carry legalisation (TODO: trim later if actually
    // never used?)
    ctx->cells[vcc_cell->name] = std::move(vcc_cell);
    ctx->nets[vcc_net->name] = std::move(vcc_net);

    for (auto dn : dead_nets) {
        ctx->nets.erase(dn);
    }
}

static bool is_nextpnr_iob(const Context *ctx, CellInfo *cell)
{
    return cell->type == ctx->id("$nextpnr_ibuf") || cell->type == ctx->id("$nextpnr_obuf") ||
           cell->type == ctx->id("$nextpnr_iobuf");
}

static bool is_gowin_iob(const Context *ctx, const CellInfo *cell)
{
    switch (cell->type.index) {
    case ID_IBUF:
    case ID_OBUF:
    case ID_IOBUF:
    case ID_TBUF:
        return true;
    default:
        return false;
    }
}

static bool is_gowin_diff_iob(const Context *ctx, const CellInfo *cell)
{
    switch (cell->type.index) {
    case ID_TLVDS_OBUF:
        return true;
    default:
        return false;
    }
}

static bool is_iob(const Context *ctx, const CellInfo *cell) { return (cell->type.index == ID_IOB); }

// Pack differential IO buffers
static void pack_diff_io(Context *ctx)
{
    pool<IdString> packed_cells;
    pool<IdString> delete_nets;

    std::vector<std::unique_ptr<CellInfo>> new_cells;
    log_info("Packing diff IOs..\n");

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ctx->verbose)
            log_info("cell '%s' is of type '%s'\n", ctx->nameOf(ci), ci->type.c_str(ctx));
        if (is_gowin_diff_iob(ctx, ci)) {
            CellInfo *iob_p = nullptr;
            CellInfo *iob_n = nullptr;
            switch (ci->type.index) {
            case ID_TLVDS_OBUF: {
                iob_p = net_only_drives(ctx, ci->ports.at(id_O).net, is_iob, id_I);
                iob_n = net_only_drives(ctx, ci->ports.at(id_OB).net, is_iob, id_I);
                NPNR_ASSERT(iob_p != nullptr);
                NPNR_ASSERT(iob_n != nullptr);
                auto iob_p_bel_a = iob_p->attrs.find(id_BEL);
                if (iob_p_bel_a == ci->attrs.end()) {
                    log_error("LVDS '%s' must be restricted.\n", ctx->nameOf(ci));
                    continue;
                }
                BelId iob_p_bel = ctx->getBelByNameStr(iob_p_bel_a->second.as_string());
                Loc loc_p = ctx->getBelLocation(iob_p_bel);
                if (loc_p.z != 0) {
                    log_error("LVDS '%s' positive pin is not A.\n", ctx->nameOf(ci));
                    continue;
                }
                // restrict the N buffer
                loc_p.z = 1;
                iob_n->attrs[id_BEL] = ctx->getBelName(ctx->getBelByLocation(loc_p)).str(ctx);
                // mark IOBs as part of DS pair
                iob_n->attrs[id_DIFF] = std::string("N");
                iob_n->attrs[id_DIFF_TYPE] = std::string("TLVDS_OBUF");
                iob_p->attrs[id_DIFF] = std::string("P");
                iob_p->attrs[id_DIFF_TYPE] = std::string("TLVDS_OBUF");
                // disconnect N input: it is wired internally
                delete_nets.insert(iob_n->ports.at(id_I).net->name);
                iob_n->disconnectPort(id_I);
                ci->disconnectPort(id_OB);
                // disconnect P output
                delete_nets.insert(ci->ports.at(id_O).net->name);
                ci->disconnectPort(id_O);
                // connect TLVDS input to P input
                ci->movePortTo(id_I, iob_p, id_I);
                packed_cells.insert(ci->name);
            } break;
            default:
                break;
            }
        }
    }
    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    for (auto dnet : delete_nets) {
        ctx->nets.erase(dnet);
    }
    for (auto &ncell : new_cells) {
        ctx->cells[ncell->name] = std::move(ncell);
    }
}
// Pack IO buffers
static void pack_io(Context *ctx)
{
    pool<IdString> packed_cells;
    pool<IdString> delete_nets;

    std::vector<std::unique_ptr<CellInfo>> new_cells;
    log_info("Packing IOs..\n");

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ctx->verbose)
            log_info("cell '%s' is of type '%s'\n", ctx->nameOf(ci), ci->type.c_str(ctx));
        if (is_gowin_iob(ctx, ci)) {
            CellInfo *iob = nullptr;
            switch (ci->type.index) {
            case ID_IBUF:
                iob = net_driven_by(ctx, ci->ports.at(id_I).net, is_nextpnr_iob, id_O);
                break;
            case ID_OBUF:
                iob = net_only_drives(ctx, ci->ports.at(id_O).net, is_nextpnr_iob, id_I);
                break;
            case ID_IOBUF:
                iob = net_driven_by(ctx, ci->ports.at(id_IO).net, is_nextpnr_iob, id_O);
                break;
            case ID_TBUF:
                iob = net_only_drives(ctx, ci->ports.at(id_O).net, is_nextpnr_iob, id_I);
                break;
            default:
                break;
            }
            if (iob != nullptr) {
                // delete the $nexpnr_[io]buf
                for (auto &p : iob->ports) {
                    IdString netname = p.second.net->name;
                    iob->disconnectPort(p.first);
                    delete_nets.insert(netname);
                }
                packed_cells.insert(iob->name);
            }
            // what type to create
            IdString new_cell_type = id_IOB;
            std::string constr_bel_name = std::string("");
            // check whether the given IO is limited to simplified IO cells
            auto constr_bel = ci->attrs.find(id_BEL);
            if (constr_bel != ci->attrs.end()) {
                constr_bel_name = constr_bel->second.as_string();
            }
            constr_bel = iob->attrs.find(id_BEL);
            if (constr_bel != iob->attrs.end()) {
                constr_bel_name = constr_bel->second.as_string();
            }
            if (!constr_bel_name.empty()) {
                BelId constr_bel = ctx->getBelByNameStr(constr_bel_name);
                if (constr_bel != BelId()) {
                    new_cell_type = ctx->bels[constr_bel].type;
                }
            }

            // Create a IOB buffer
            std::unique_ptr<CellInfo> ice_cell = create_generic_cell(ctx, new_cell_type, ci->name.str(ctx) + "$iob");
            gwio_to_iob(ctx, ci, ice_cell.get(), packed_cells);
            new_cells.push_back(std::move(ice_cell));
            auto gwiob = new_cells.back().get();

            packed_cells.insert(ci->name);
            if (iob != nullptr) {
                // in Gowin .CST port attributes take precedence over cell attributes.
                // first copy cell attrs related to IO
                for (auto &attr : ci->attrs) {
                    if (attr.first == IdString(ID_BEL) || attr.first.str(ctx)[0] == '&') {
                        gwiob->setAttr(attr.first, attr.second);
                    }
                }
                // rewrite attributes from the port
                for (auto &attr : iob->attrs) {
                    gwiob->setAttr(attr.first, attr.second);
                }
            }
        }
    }
    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    for (auto dnet : delete_nets) {
        ctx->nets.erase(dnet);
    }
    for (auto &ncell : new_cells) {
        ctx->cells[ncell->name] = std::move(ncell);
    }
}

// Main pack function
bool Arch::pack()
{
    Context *ctx = getCtx();
    try {
        log_break();
        pack_constants(ctx);
        pack_io(ctx);
        pack_diff_io(ctx);
        pack_wideluts(ctx);
        pack_alus(ctx);
        pack_lut_lutffs(ctx);
        pack_nonlut_ffs(ctx);
        ctx->settings[id_pack] = 1;
        ctx->assignArchInfo();
        log_info("Checksum: 0x%08x\n", ctx->checksum());
        return true;
    } catch (log_execution_error_exception) {
        return false;
    }
}

NEXTPNR_NAMESPACE_END
