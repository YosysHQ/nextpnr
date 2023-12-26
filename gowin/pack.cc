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
#include <cinttypes>
#include <iostream>
#include <iterator>
#include "cells.h"
#include "design_utils.h"
#include "log.h"
#include "util.h"

#include "globals.h"

NEXTPNR_NAMESPACE_BEGIN

static bool check_availability(const Context *ctx, IdString type)
{
    switch (type.hash()) {
    case ID_ELVDS_IBUF:
        if (ctx->device != "GW1NZ-1") {
            return true;
        }
        break;
    case ID_ELVDS_IOBUF:
        if (ctx->device == "GW1NZ-1") {
            return true;
        }
        break;
    case ID_TLVDS_IBUF:
        if (ctx->device != "GW1NZ-1") {
            return true;
        }
        break;
    case ID_TLVDS_OBUF:
        if (ctx->device != "GW1NZ-1" && ctx->device != "GW1N-1") {
            return true;
        }
        break;
    case ID_TLVDS_TBUF:
        if (ctx->device != "GW1NZ-1" && ctx->device != "GW1N-1") {
            return true;
        }
        break;
    case ID_TLVDS_IOBUF:
        if (ctx->device == "GW1N-4") {
            return true;
        }
        break;
    default:
        return true;
    }
    log_info("%s is not supported for device %s.\n", type.c_str(ctx), ctx->device.c_str());
    return false;
}

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

        std::unique_ptr<CellInfo> packed = create_generic_cell(ctx, id_MUX2_LUT5, ci->name.str(ctx) + "_LC");
        if (ctx->verbose) {
            log_info("packed cell %s into %s\n", ctx->nameOf(ci), ctx->nameOf(packed.get()));
        }
        // mux is the cluster root
        packed->cluster = packed->name;
        lut1->cluster = packed->name;
        lut1->constr_z = -BelZ::mux_0_z + 1;
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

        std::unique_ptr<CellInfo> packed = create_generic_cell(ctx, id_MUX2_LUT5, ci->name.str(ctx) + "_LC");
        if (ctx->verbose) {
            log_info("packed cell %s into %s\n", ctx->nameOf(ci), ctx->nameOf(packed.get()));
        }
        // mux is the cluster root
        packed->cluster = packed->name;
        lut0->cluster = packed->name;
        lut0->constr_z = -BelZ::mux_0_z;
        lut1->cluster = packed->name;
        lut1->constr_z = -BelZ::mux_0_z + 1;
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
    pack_mux2_lut(ctx, ci, is_mux2_lut5, '6', id_MUX2_LUT6, x, z, packed_cells, delete_nets, new_cells);
}

// pack MUX2_LUT7
static void pack_mux2_lut7(Context *ctx, CellInfo *ci, pool<IdString> &packed_cells, pool<IdString> &delete_nets,
                           std::vector<std::unique_ptr<CellInfo>> &new_cells)
{
    static int x[] = {0, 0};
    static int z[] = {+2, -2};
    pack_mux2_lut(ctx, ci, is_mux2_lut6, '7', id_MUX2_LUT7, x, z, packed_cells, delete_nets, new_cells);
}

// pack MUX2_LUT8
static void pack_mux2_lut8(Context *ctx, CellInfo *ci, pool<IdString> &packed_cells, pool<IdString> &delete_nets,
                           std::vector<std::unique_ptr<CellInfo>> &new_cells)
{
    static int x[] = {1, 0};
    static int z[] = {-4, -4};
    pack_mux2_lut(ctx, ci, is_mux2_lut7, '8', id_MUX2_LUT8, x, z, packed_cells, delete_nets, new_cells);
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
static void set_net_constant(const Context *ctx, NetInfo *orig, NetInfo *constnet)
{
    orig->driver.cell = nullptr;
    for (auto user : orig->users) {
        if (user.cell != nullptr) {
            CellInfo *uc = user.cell;
            if (ctx->verbose)
                log_info("%s user %s\n", ctx->nameOf(orig), ctx->nameOf(uc));

            if (is_lut(ctx, uc) && (user.port.str(ctx).at(0) == 'I')) {
                auto it_param = uc->params.find(id_INIT);
                if (it_param == uc->params.end())
                    log_error("No initialization for lut found.\n");

                int64_t uc_init = it_param->second.intval;
                int64_t mask = 0;
                uint8_t amt = 0;

                if (user.port == id_I0) {
                    mask = 0x5555;
                    amt = 1;
                } else if (user.port == id_I1) {
                    mask = 0x3333;
                    amt = 2;
                } else if (user.port == id_I2) {
                    mask = 0x0F0F;
                    amt = 4;
                } else if (user.port == id_I3) {
                    mask = 0x00FF;
                    amt = 8;
                } else {
                    log_error("Port number invalid.\n");
                }

                if ((constnet->name == ctx->id("$PACKER_GND_NET"))) {
                    uc_init = (uc_init & mask) | ((uc_init & mask) << amt);
                } else {
                    uc_init = (uc_init & (mask << amt)) | ((uc_init & (mask << amt)) >> amt);
                }

                size_t uc_init_len = it_param->second.to_string().length();
                uc_init &= (1LL << uc_init_len) - 1;

                if (ctx->verbose)
                    log_info("%s lut config modified from 0x%" PRIX64 " to 0x%" PRIX64 "\n", ctx->nameOf(uc),
                             it_param->second.intval, uc_init);

                it_param->second = Property(uc_init, uc_init_len);
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

    std::unique_ptr<CellInfo> gnd_cell = create_generic_cell(ctx, id_GND, "$PACKER_GND");
    auto gnd_net = std::make_unique<NetInfo>(ctx->id("$PACKER_GND_NET"));
    gnd_net->driver.cell = gnd_cell.get();
    gnd_net->driver.port = id_G;
    gnd_cell->ports.at(id_G).net = gnd_net.get();

    std::unique_ptr<CellInfo> vcc_cell = create_generic_cell(ctx, id_VCC, "$PACKER_VCC");
    auto vcc_net = std::make_unique<NetInfo>(ctx->id("$PACKER_VCC_NET"));
    vcc_net->driver.cell = vcc_cell.get();
    vcc_net->driver.port = id_V;
    vcc_cell->ports.at(id_V).net = vcc_net.get();

    std::vector<IdString> dead_nets;

    bool gnd_used = true; // XXX May be needed for simplified IO

    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        if (ni->driver.cell != nullptr && ni->driver.cell->type == id_GND) {
            IdString drv_cell = ni->driver.cell->name;
            set_net_constant(ctx, ni, gnd_net.get());
            gnd_used = true;
            dead_nets.push_back(net.first);
            ctx->cells.erase(drv_cell);
        } else if (ni->driver.cell != nullptr && ni->driver.cell->type == id_VCC) {
            IdString drv_cell = ni->driver.cell->name;
            set_net_constant(ctx, ni, vcc_net.get());
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

// Pack global set-reset
static void pack_gsr(Context *ctx)
{
    log_info("Packing GSR..\n");

    bool user_gsr = false;
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ctx->verbose)
            log_info("cell '%s' is of type '%s'\n", ctx->nameOf(ci), ci->type.c_str(ctx));
        if (ci->type == id_GSR) {
            user_gsr = true;
            break;
        }
    }
    if (!user_gsr) {
        // XXX
        bool have_gsr_bel = false;
        for (auto bi : ctx->bels) {
            if (bi.second.type == id_GSR) {
                have_gsr_bel = true;
                break;
            }
        }
        if (have_gsr_bel) {
            // make default GSR
            std::unique_ptr<CellInfo> gsr_cell = create_generic_cell(ctx, id_GSR, "GSR");
            gsr_cell->connectPort(id_GSRI, ctx->nets[ctx->id("$PACKER_VCC_NET")].get());
            ctx->cells[gsr_cell->name] = std::move(gsr_cell);
        } else {
            log_info("No GSR in the chip base\n");
        }
    }
}

// Pack shadow RAM
void pack_sram(Context *ctx)
{
    log_info("Packing Shadow RAM..\n");

    pool<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (is_sram(ctx, ci)) {

            // Create RAMW slice
            std::unique_ptr<CellInfo> ramw_slice = create_generic_cell(ctx, id_RAMW, ci->name.str(ctx) + "$RAMW_SLICE");
            sram_to_ramw_split(ctx, ci, ramw_slice.get());
            ramw_slice->connectPort(id_CE, ctx->nets[ctx->id("$PACKER_VCC_NET")].get());

            // Create actual RAM slices
            std::unique_ptr<CellInfo> ram_comb[4];
            for (int i = 0; i < 4; i++) {
                ram_comb[i] = create_generic_cell(ctx, id_SLICE, ci->name.str(ctx) + "$SRAM_SLICE" + std::to_string(i));
                ram_comb[i]->params[id_FF_USED] = 1;
                ram_comb[i]->params[id_FF_TYPE] = std::string("RAM");
                sram_to_slice(ctx, ci, ram_comb[i].get(), i);
            }
            // Create 'block' SLICEs as a placement hint that these cells are mutually exclusive with the RAMW
            std::unique_ptr<CellInfo> ramw_block[2];
            for (int i = 0; i < 2; i++) {
                ramw_block[i] =
                        create_generic_cell(ctx, id_SLICE, ci->name.str(ctx) + "$RAMW_BLOCK" + std::to_string(i));
                ram_comb[i]->params[id_FF_USED] = 1;
                ramw_block[i]->params[id_FF_TYPE] = std::string("RAM");
            }

            // Disconnect ports of original cell after packing
            // ci->disconnectPort(id_WCK);
            // ci->disconnectPort(id_WRE);

            for (int i = 0; i < 4; i++)
                ci->disconnectPort(ctx->idf("RAD[%d]", i));

            // Setup placement constraints
            // Use the 0th bit as an anchor
            ram_comb[0]->constr_abs_z = true;
            ram_comb[0]->constr_z = 0;
            ram_comb[0]->cluster = ram_comb[0]->name;
            for (int i = 1; i < 4; i++) {
                ram_comb[i]->cluster = ram_comb[0]->name;
                ram_comb[i]->constr_abs_z = true;
                ram_comb[i]->constr_x = 0;
                ram_comb[i]->constr_y = 0;
                ram_comb[i]->constr_z = i;
                ram_comb[0]->constr_children.push_back(ram_comb[i].get());
            }
            for (int i = 0; i < 2; i++) {
                ramw_block[i]->cluster = ram_comb[0]->name;
                ramw_block[i]->constr_abs_z = true;
                ramw_block[i]->constr_x = 0;
                ramw_block[i]->constr_y = 0;
                ramw_block[i]->constr_z = i + 4;
                ram_comb[0]->constr_children.push_back(ramw_block[i].get());
            }

            ramw_slice->cluster = ram_comb[0]->name;
            ramw_slice->constr_abs_z = true;
            ramw_slice->constr_x = 0;
            ramw_slice->constr_y = 0;
            ramw_slice->constr_z = BelZ::lutram_0_z;
            ram_comb[0]->constr_children.push_back(ramw_slice.get());

            for (int i = 0; i < 4; i++)
                new_cells.push_back(std::move(ram_comb[i]));
            for (int i = 0; i < 2; i++)
                new_cells.push_back(std::move(ramw_block[i]));
            new_cells.push_back(std::move(ramw_slice));
            packed_cells.insert(ci->name);
        }
    }
    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    for (auto &ncell : new_cells) {
        ctx->cells[ncell->name] = std::move(ncell);
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
    case ID_TLVDS_TBUF:
    case ID_TLVDS_IBUF:
    case ID_TLVDS_IOBUF:
    case ID_ELVDS_OBUF:
    case ID_ELVDS_TBUF:
    case ID_ELVDS_IBUF:
    case ID_ELVDS_IOBUF:
        return true;
    default:
        return false;
    }
}

static bool is_gowin_iologic(const Context *ctx, const CellInfo *cell)
{
    switch (cell->type.index) {
    case ID_ODDR:   /* fall-through*/
    case ID_ODDRC:  /* fall-through*/
    case ID_OSER4:  /* fall-through*/
    case ID_OSER8:  /* fall-through*/
    case ID_OSER10: /* fall-through*/
    case ID_OSER16: /* fall-through*/
    case ID_OVIDEO: /* fall-through*/
    case ID_IDDR:   /* fall-through*/
    case ID_IDDRC:  /* fall-through*/
    case ID_IDES4:  /* fall-through*/
    case ID_IDES8:  /* fall-through*/
    case ID_IDES10: /* fall-through*/
    case ID_IDES16: /* fall-through*/
    case ID_IVIDEO:
        return true;
    default:
        return false;
    }
}

// IDES has different outputs
static void reconnect_ides_outs(CellInfo *ci)
{
    switch (ci->type.hash()) {
    case ID_IDDR: /* fall-through*/
    case ID_IDDRC:
        ci->renamePort(id_Q1, id_Q9);
        ci->renamePort(id_Q0, id_Q8);
        break;
    case ID_IDES4:
        ci->renamePort(id_Q3, id_Q9);
        ci->renamePort(id_Q2, id_Q8);
        ci->renamePort(id_Q1, id_Q7);
        ci->renamePort(id_Q0, id_Q6);
        break;
    case ID_IVIDEO:
        ci->renamePort(id_Q6, id_Q9);
        ci->renamePort(id_Q5, id_Q8);
        ci->renamePort(id_Q4, id_Q7);
        ci->renamePort(id_Q3, id_Q6);
        ci->renamePort(id_Q2, id_Q5);
        ci->renamePort(id_Q1, id_Q4);
        ci->renamePort(id_Q0, id_Q3);
        break;
    case ID_IDES8:
        ci->renamePort(id_Q7, id_Q9);
        ci->renamePort(id_Q6, id_Q8);
        ci->renamePort(id_Q5, id_Q7);
        ci->renamePort(id_Q4, id_Q6);
        ci->renamePort(id_Q3, id_Q5);
        ci->renamePort(id_Q2, id_Q4);
        ci->renamePort(id_Q1, id_Q3);
        ci->renamePort(id_Q0, id_Q2);
        break;
    default:
        break;
    }
}

static void get_next_oser16_loc(std::string device, Loc &loc)
{
    if (device == "GW1NSR-4C") {
        if (loc.y == 0) {
            ++loc.x;
        } else {
            ++loc.y;
        }
    } else {
        if (device == "GW1NR-9" || device == "GW1NR-9C") {
            ++loc.x;
        }
    }
}

// create IOB connections for gowin_pack
static void make_iob_nets(Context *ctx, CellInfo *iob)
{
    for (const auto &port : iob->ports) {
        const NetInfo *net = iob->getPort(port.first);
        std::string connected_net = "NET";
        if (net != nullptr) {
            if (ctx->verbose) {
                log_info("%s: %s - %s\n", ctx->nameOf(iob), port.first.c_str(ctx), ctx->nameOf(net));
            }
            if (net->name == ctx->id("$PACKER_VCC_NET")) {
                connected_net = "VCC";
            } else if (net->name == ctx->id("$PACKER_GND_NET")) {
                connected_net = "GND";
            }
            iob->setParam(ctx->idf("NET_%s", port.first.c_str(ctx)), connected_net);
        }
    }
}

// Pack IO logic
static void pack_iologic(Context *ctx)
{
    pool<IdString> packed_cells;
    pool<IdString> delete_nets;

    std::vector<std::unique_ptr<CellInfo>> new_cells;
    log_info("Packing IO logic..\n");

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ctx->verbose)
            log_info("cell '%s' is of type '%s'\n", ctx->nameOf(ci), ci->type.c_str(ctx));
        if (ci->type == id_IOB) {
            make_iob_nets(ctx, ci);
        }
        if (is_gowin_iologic(ctx, ci)) {
            CellInfo *q0_dst = nullptr;
            CellInfo *q1_dst = nullptr;
            switch (ci->type.hash()) {
            case ID_ODDR:   /* fall-through */
            case ID_ODDRC:  /* fall-through */
            case ID_OSER4:  /* fall-through */
            case ID_OSER8:  /* fall-through */
            case ID_OSER10: /* fall-through */
            case ID_OVIDEO: {
                IdString output = id_Q;
                IdString output_1 = IdString();
                if (ci->type == id_ODDR || ci->type == id_ODDRC || ci->type == id_OSER4 || ci->type == id_OSER8) {
                    output = id_Q0;
                    output_1 = id_Q1;
                }
                q0_dst = net_only_drives(ctx, ci->ports.at(output).net, is_iob, id_I);
                NPNR_ASSERT(q0_dst != nullptr);

                auto iob_bel = q0_dst->attrs.find(id_BEL);
                if (iob_bel == q0_dst->attrs.end()) {
                    log_error("No constraints for %s. The pins for IDES/OSER must be specified explicitly.\n",
                              ctx->nameOf(q0_dst));
                }

                Loc loc = ctx->getBelLocation(ctx->getBelByNameStr(iob_bel->second.as_string()));
                loc.z += BelZ::iologic_z;
                ci->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                BelId bel = ctx->getBelByLocation(loc);
                if (bel == BelId()) {
                    log_info("No bel for %s at %s. Can't place IDES/OSER here\n", ctx->nameOf(ci),
                             iob_bel->second.as_string().c_str());
                }

                std::string out_mode;
                switch (ci->type.hash()) {
                case ID_ODDR:
                case ID_ODDRC:
                    out_mode = "ODDRX1";
                    break;
                case ID_OSER4:
                    out_mode = "ODDRX2";
                    break;
                case ID_OSER8:
                    out_mode = "ODDRX4";
                    break;
                case ID_OSER10:
                    out_mode = "ODDRX5";
                    break;
                case ID_OVIDEO:
                    out_mode = "VIDEORX";
                    break;
                }
                ci->setParam(ctx->id("OUTMODE"), out_mode);

                // mark IOB as used by IOLOGIC
                q0_dst->setParam(id_IOLOGIC_IOB, 1);

                bool use_diff_io = false;
                if (q0_dst->attrs.count(id_DIFF_TYPE)) {
                    ci->setAttr(id_OBUF_TYPE, std::string("DBUF")); // XXX compatibility
                    ci->setParam(id_OBUF_TYPE, std::string("DBUF"));
                    use_diff_io = true;
                } else {
                    ci->setAttr(id_OBUF_TYPE, std::string("SBUF")); // XXX compatibility
                    ci->setParam(id_OBUF_TYPE, std::string("SBUF"));
                }

                // disconnect Q output: it is wired internally
                delete_nets.insert(ci->ports.at(output).net->name);
                q0_dst->disconnectPort(id_I);
                ci->disconnectPort(output);
                if (ctx->bels.at(ctx->getBelByNameStr(iob_bel->second.as_string())).pins.count(id_GW9C_ALWAYS_LOW1)) {
                    q0_dst->disconnectPort(id_GW9C_ALWAYS_LOW1);
                    q0_dst->connectPort(id_GW9C_ALWAYS_LOW1, ctx->nets[ctx->id("$PACKER_VCC_NET")].get());
                }
                if (ctx->bels.at(ctx->getBelByLocation(loc)).pins.count(id_DAADJ0)) {
                    ci->addInput(id_DAADJ0);
                    ci->connectPort(id_DAADJ0, ctx->nets[ctx->id("$PACKER_GND_NET")].get());
                }
                if (ctx->bels.at(ctx->getBelByLocation(loc)).pins.count(id_DAADJ1)) {
                    ci->addInput(id_DAADJ1);
                    ci->connectPort(id_DAADJ1, ctx->nets[ctx->id("$PACKER_VCC_NET")].get());
                }

                // if Q1 is connected then disconnet it too
                if (output_1 != IdString() && port_used(ci, output_1)) {
                    q1_dst = net_only_drives(ctx, ci->ports.at(output_1).net, is_iob, id_OEN);
                    if (q1_dst != nullptr) {
                        delete_nets.insert(ci->ports.at(output_1).net->name);
                        q0_dst->disconnectPort(id_OEN);
                        ci->disconnectPort(output_1);
                        ci->setAttr(id_IOBUF, 1);
                    }
                }
                ci->setAttr(id_IOBUF, 0);
                ci->setAttr(id_IOLOGIC_TYPE, ci->type.str(ctx));

                if (ci->type == id_OSER4 || ci->type == id_ODDR || ci->type == id_ODDRC) {
                    if (ci->type == id_OSER4) {
                        // two OSER4 share FCLK, check it
                        Loc other_loc = loc;
                        other_loc.z = 1 - loc.z + 2 * BelZ::iologic_z;
                        BelId other_bel = ctx->getBelByLocation(other_loc);
                        CellInfo *other_cell = ctx->getBoundBelCell(other_bel);
                        if (other_cell != nullptr) {
                            NPNR_ASSERT(other_cell->type == id_OSER4);
                            if (ci->ports.at(id_FCLK).net != other_cell->ports.at(id_FCLK).net) {
                                log_error("%s and %s have differnet FCLK nets\n", ctx->nameOf(ci),
                                          ctx->nameOf(other_cell));
                            }
                        }
                    }
                } else {
                    std::unique_ptr<CellInfo> dummy =
                            create_generic_cell(ctx, id_DUMMY_CELL, ci->name.str(ctx) + "_IOLOGIC_IO");
                    loc.z = 1 - loc.z + BelZ::iologic_z;
                    if (!use_diff_io) {
                        dummy->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                        new_cells.push_back(std::move(dummy));
                    }
                    loc.z += BelZ::iologic_z;

                    std::unique_ptr<CellInfo> aux_cell =
                            create_generic_cell(ctx, id_IOLOGIC, ci->name.str(ctx) + "_AUX");
                    aux_cell->setAttr(id_IOLOGIC_TYPE, std::string("DUMMY"));
                    aux_cell->setParam(ctx->id("OUTMODE"), std::string("DDRENABLE"));
                    aux_cell->setAttr(ctx->id("IOLOGIC_MASTER_CELL"), ci->name.str(ctx));
                    aux_cell->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                    if (port_used(ci, id_RESET)) {
                        aux_cell->connectPort(id_RESET, ci->ports.at(id_RESET).net);
                    }
                    if (port_used(ci, id_PCLK)) {
                        aux_cell->connectPort(id_PCLK, ci->ports.at(id_PCLK).net);
                    }
                    new_cells.push_back(std::move(aux_cell));
                }
                ci->type = id_IOLOGIC;
            } break;
            case ID_IDDR:   /* fall-through */
            case ID_IDDRC:  /* fall-through */
            case ID_IDES4:  /* fall-through */
            case ID_IDES8:  /* fall-through */
            case ID_IDES10: /* fall-through */
            case ID_IVIDEO: {
                CellInfo *d_src = net_driven_by(ctx, ci->getPort(id_D), is_iob, id_O);
                NPNR_ASSERT(d_src != nullptr);

                auto iob_bel = d_src->attrs.find(id_BEL);
                if (iob_bel == d_src->attrs.end()) {
                    log_error("No constraints for %s. The pins for IDES/OSER must be specified explicitly.\n",
                              ctx->nameOf(d_src));
                }

                Loc loc = ctx->getBelLocation(ctx->getBelByNameStr(iob_bel->second.as_string()));
                loc.z += BelZ::iologic_z;
                ci->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                BelId bel = ctx->getBelByLocation(loc);
                if (bel == BelId()) {
                    log_error("No bel for %s at %s. Can't place IDES/OSER here\n", ctx->nameOf(ci),
                              iob_bel->second.as_string().c_str());
                }
                std::string in_mode;
                switch (ci->type.hash()) {
                case ID_IDDR: /* fall-through */
                case ID_IDDRC:
                    in_mode = "IDDRX1";
                    break;
                case ID_IDES4:
                    in_mode = "IDDRX2";
                    break;
                case ID_IDES8:
                    in_mode = "IDDRX4";
                    break;
                case ID_IDES10:
                    in_mode = "IDDRX5";
                    break;
                case ID_IVIDEO:
                    in_mode = "VIDEORX";
                    break;
                }
                ci->setParam(ctx->id("INMODE"), in_mode);

                // mark IOB as used by IOLOGIC
                d_src->setParam(id_IOLOGIC_IOB, 1);

                bool use_diff_io = false;
                if (d_src->attrs.count(id_DIFF_TYPE)) {
                    ci->setAttr(id_IBUF_TYPE, std::string("DBUF")); // XXX compatibility
                    ci->setParam(id_IBUF_TYPE, std::string("DBUF"));
                    use_diff_io = true;
                } else {
                    ci->setAttr(id_IBUF_TYPE, std::string("SBUF")); // XXX compatibility
                    ci->setParam(id_IBUF_TYPE, std::string("SBUF"));
                }

                // disconnect D input: it is wired internally
                delete_nets.insert(ci->getPort(id_D)->name);
                d_src->disconnectPort(id_O);
                ci->disconnectPort(id_D);
                ci->setAttr(id_IOLOGIC_TYPE, ci->type.str(ctx));
                reconnect_ides_outs(ci);

                // common clock inputs
                if (ci->type == id_IDES4 || ci->type == id_IDDR || ci->type == id_IDDRC) {
                    if (ci->type == id_IDES4) {
                        // two IDER4 share FCLK, check it
                        Loc other_loc = loc;
                        other_loc.z = 1 - loc.z + 2 * BelZ::iologic_z;
                        BelId other_bel = ctx->getBelByLocation(other_loc);
                        CellInfo *other_cell = ctx->getBoundBelCell(other_bel);
                        if (other_cell != nullptr) {
                            NPNR_ASSERT(other_cell->type == id_IDES4);
                            if (ci->ports.at(id_FCLK).net != other_cell->ports.at(id_FCLK).net) {
                                log_error("%s and %s have differnet FCLK nets\n", ctx->nameOf(ci),
                                          ctx->nameOf(other_cell));
                            }
                        }
                    }
                } else {
                    std::unique_ptr<CellInfo> dummy =
                            create_generic_cell(ctx, id_DUMMY_CELL, ci->name.str(ctx) + "_IOLOGIC_IO");
                    loc.z = 1 - loc.z + BelZ::iologic_z;
                    if (!use_diff_io) {
                        dummy->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                        new_cells.push_back(std::move(dummy));
                    }
                    loc.z += BelZ::iologic_z;

                    std::unique_ptr<CellInfo> aux_cell =
                            create_generic_cell(ctx, id_IOLOGIC, ci->name.str(ctx) + "_AUX");
                    aux_cell->setAttr(id_IOLOGIC_TYPE, std::string("DUMMY"));
                    aux_cell->setParam(ctx->id("INMODE"), std::string("DDRENABLE"));
                    aux_cell->setAttr(ctx->id("IOLOGIC_MASTER_CELL"), ci->name.str(ctx));
                    aux_cell->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                    if (port_used(ci, id_RESET)) {
                        aux_cell->connectPort(id_RESET, ci->ports.at(id_RESET).net);
                    }
                    if (port_used(ci, id_PCLK)) {
                        aux_cell->connectPort(id_PCLK, ci->ports.at(id_PCLK).net);
                    }
                    new_cells.push_back(std::move(aux_cell));
                }
                ci->type = id_IOLOGIC;
            } break;
            case ID_OSER16: {
                IdString output = id_Q;
                q0_dst = net_only_drives(ctx, ci->ports.at(output).net, is_iob, id_I);
                NPNR_ASSERT(q0_dst != nullptr);

                auto iob_bel = q0_dst->attrs.find(id_BEL);
                if (iob_bel == q0_dst->attrs.end()) {
                    log_error("No constraints for %s. The pins for IDES/OSER must be specified explicitly.\n",
                              ctx->nameOf(q0_dst));
                }

                Loc loc = ctx->getBelLocation(ctx->getBelByNameStr(iob_bel->second.as_string()));
                if (loc.z != BelZ::ioba_z) {
                    log_error("IDES16/OSER16 %s must be an A pin.\n", ctx->nameOf(ci));
                }

                loc.z = BelZ::oser16_z;
                ci->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                BelId bel = ctx->getBelByLocation(loc);
                if (bel == BelId()) {
                    log_error("No bel for %s at %s. Can't place IDES/OSER here\n", ctx->nameOf(ci),
                              iob_bel->second.as_string().c_str());
                }

                // mark IOB as used by IOLOGIC
                q0_dst->setParam(id_IOLOGIC_IOB, 1);

                bool use_diff_io = false;
                if (q0_dst->attrs.count(id_DIFF_TYPE)) {
                    ci->setAttr(id_OBUF_TYPE, std::string("DBUF")); // compatibility
                    ci->setParam(id_OBUF_TYPE, std::string("DBUF"));
                    use_diff_io = true;
                } else {
                    ci->setAttr(id_OBUF_TYPE, std::string("SBUF")); // compatibility
                    ci->setParam(id_OBUF_TYPE, std::string("SBUF"));
                }

                // disconnect Q output: it is wired internally
                delete_nets.insert(ci->ports.at(output).net->name);
                q0_dst->disconnectPort(id_I);
                ci->disconnectPort(output);
                loc.z = BelZ::ioba_z;
                if (ctx->bels.at(ctx->getBelByLocation(loc)).pins.count(id_GW9C_ALWAYS_LOW1)) {
                    q0_dst->disconnectPort(id_GW9C_ALWAYS_LOW1);
                    q0_dst->connectPort(id_GW9C_ALWAYS_LOW1, ctx->nets[ctx->id("$PACKER_VCC_NET")].get());
                }
                if (ctx->bels.at(ctx->getBelByLocation(loc)).pins.count(id_GW9_ALWAYS_LOW0)) {
                    q0_dst->disconnectPort(id_GW9_ALWAYS_LOW0);
                    q0_dst->connectPort(id_GW9_ALWAYS_LOW0, ctx->nets[ctx->id("$PACKER_VCC_NET")].get());
                }

                // make aux cells
                std::unique_ptr<CellInfo> dummy =
                        create_generic_cell(ctx, id_DUMMY_CELL, ci->name.str(ctx) + "_IOLOGIC_IO");
                loc.z = BelZ::iobb_z;
                if (!use_diff_io) {
                    dummy->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                    new_cells.push_back(std::move(dummy));
                }
                loc.z = BelZ::iologic_z;

                // main iologic cell
                std::string master_name = ci->name.str(ctx) + "_MAIN";

                // aux cells
                std::unique_ptr<CellInfo> aux_cell = create_generic_cell(ctx, id_IOLOGIC, ci->name.str(ctx) + "_AUX0");
                aux_cell->setAttr(id_IOLOGIC_TYPE, std::string("OSER16"));
                aux_cell->setAttr(ctx->id("IOLOGIC_MASTER_CELL"), master_name);
                aux_cell->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                aux_cell->setParam(ctx->id("OUTMODE"), std::string("ODDRX8"));
                aux_cell->setParam(ctx->id("UPDATE"), std::string("SAME"));
                if (port_used(ci, id_RESET)) {
                    aux_cell->connectPort(id_RESET, ci->ports.at(id_RESET).net);
                }
                if (port_used(ci, id_PCLK)) {
                    aux_cell->connectPort(id_PCLK, ci->ports.at(id_PCLK).net);
                }
                new_cells.push_back(std::move(aux_cell));

                // aux iologic cells
                loc.z = BelZ::iologic_z + 1;
                aux_cell = create_generic_cell(ctx, id_IOLOGIC, ci->name.str(ctx) + "_AUX1");
                aux_cell->setAttr(id_IOLOGIC_TYPE, std::string("DUMMY"));
                aux_cell->setAttr(ctx->id("IOLOGIC_MASTER_CELL"), master_name);
                aux_cell->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                aux_cell->setParam(ctx->id("OUTMODE"), std::string("DDRENABLE16"));
                aux_cell->setParam(ctx->id("UPDATE"), std::string("SAME"));
                if (port_used(ci, id_RESET)) {
                    aux_cell->connectPort(id_RESET, ci->ports.at(id_RESET).net);
                }
                if (port_used(ci, id_PCLK)) {
                    aux_cell->connectPort(id_PCLK, ci->ports.at(id_PCLK).net);
                }
                new_cells.push_back(std::move(aux_cell));

                // master
                get_next_oser16_loc(ctx->device, loc);
                loc.z = BelZ::iologic_z;
                aux_cell = create_generic_cell(ctx, id_IOLOGIC, master_name);
                aux_cell->setAttr(id_IOLOGIC_TYPE, std::string("DUMMY"));
                aux_cell->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                aux_cell->setParam(ctx->id("OUTMODE"), std::string("DDRENABLE16"));
                aux_cell->setParam(ctx->id("UPDATE"), std::string("SAME"));
                if (port_used(ci, id_RESET)) {
                    aux_cell->connectPort(id_RESET, ci->ports.at(id_RESET).net);
                }
                if (port_used(ci, id_PCLK)) {
                    aux_cell->connectPort(id_PCLK, ci->ports.at(id_PCLK).net);
                }
                ci->movePortTo(id_FCLK, aux_cell.get(), id_FCLK);
                ci->movePortTo(id_D12, aux_cell.get(), id_D0);
                ci->movePortTo(id_D13, aux_cell.get(), id_D1);
                ci->movePortTo(id_D14, aux_cell.get(), id_D2);
                ci->movePortTo(id_D15, aux_cell.get(), id_D3);
                new_cells.push_back(std::move(aux_cell));

                // bottom row is special and may need two additional ports
                loc.z = BelZ::ioba_z;
                if (ctx->getBelByLocation(loc) != BelId()) {
                    dummy = create_generic_cell(ctx, id_DUMMY_CELL, ci->name.str(ctx) + "_IOLOGIC_IO0");
                    dummy->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                    new_cells.push_back(std::move(dummy));
                }

                // XXX Prohibit the use of 4th IO and IOLOGIC
                loc.z = BelZ::iobb_z;
                if (ctx->getBelByLocation(loc) != BelId()) {
                    dummy = create_generic_cell(ctx, id_DUMMY_CELL, ci->name.str(ctx) + "_IOLOGIC_IO1");
                    dummy->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                    new_cells.push_back(std::move(dummy));
                }
                master_name = ci->name.str(ctx) + "_AUX2";
                loc.z = BelZ::iologic_z + 1;
                dummy = create_generic_cell(ctx, id_DUMMY_CELL, master_name);
                dummy->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                new_cells.push_back(std::move(dummy));
            } break;
            case ID_IDES16: {
                CellInfo *d_src = net_driven_by(ctx, ci->getPort(id_D), is_iob, id_O);
                NPNR_ASSERT(d_src != nullptr);

                auto iob_bel = d_src->attrs.find(id_BEL);
                if (iob_bel == d_src->attrs.end()) {
                    log_error("No constraints for %s. The pins for IDES/OSER must be specified explicitly.\n",
                              ctx->nameOf(d_src));
                }
                Loc loc = ctx->getBelLocation(ctx->getBelByNameStr(iob_bel->second.as_string()));
                if (loc.z != BelZ::ioba_z) {
                    log_error("IDES16/OSER16 %s must be an A pin.\n", ctx->nameOf(ci));
                }

                loc.z += BelZ::ides16_z;
                ci->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                BelId bel = ctx->getBelByLocation(loc);
                if (bel == BelId()) {
                    log_error("No bel for %s at %s. Can't place IDES/OSER here\n", ctx->nameOf(ci),
                              iob_bel->second.as_string().c_str());
                }
                // mark IOB as used by IOLOGIC
                d_src->setParam(id_IOLOGIC_IOB, 1);

                bool use_diff_io = false;
                if (d_src->attrs.count(id_DIFF_TYPE)) {
                    ci->setAttr(id_IBUF_TYPE, std::string("DBUF")); // XXX compatibility
                    ci->setParam(id_IBUF_TYPE, std::string("DBUF"));
                    use_diff_io = true;
                } else {
                    ci->setAttr(id_IBUF_TYPE, std::string("SBUF")); // XXX compatibility
                    ci->setParam(id_IBUF_TYPE, std::string("SBUF"));
                }
                // disconnect D input: it is wired internally
                delete_nets.insert(ci->getPort(id_D)->name);
                d_src->disconnectPort(id_O);
                ci->disconnectPort(id_D);
                ci->setAttr(id_IOLOGIC_TYPE, ci->type.str(ctx));

                // make aux cells
                std::unique_ptr<CellInfo> dummy =
                        create_generic_cell(ctx, id_DUMMY_CELL, ci->name.str(ctx) + "_IOLOGIC_IO");
                loc.z = BelZ::iobb_z;
                if (!use_diff_io) {
                    dummy->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                    new_cells.push_back(std::move(dummy));
                }
                loc.z = BelZ::iologic_z;

                // main iologic cell
                std::string master_name = ci->name.str(ctx) + "_MAIN";

                // aux cells
                std::unique_ptr<CellInfo> aux_cell = create_generic_cell(ctx, id_IOLOGIC, ci->name.str(ctx) + "_AUX0");
                aux_cell->setAttr(id_IOLOGIC_TYPE, std::string("IDES16"));
                aux_cell->setAttr(ctx->id("IOLOGIC_MASTER_CELL"), master_name);
                aux_cell->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                aux_cell->setParam(ctx->id("INMODE"), std::string("IDDRX8"));
                aux_cell->setParam(ctx->id("UPDATE"), std::string("SAME"));
                if (port_used(ci, id_RESET)) {
                    aux_cell->connectPort(id_RESET, ci->ports.at(id_RESET).net);
                }
                if (port_used(ci, id_PCLK)) {
                    aux_cell->connectPort(id_PCLK, ci->ports.at(id_PCLK).net);
                }
                new_cells.push_back(std::move(aux_cell));

                // aux iologic cells
                loc.z = BelZ::iologic_z + 1;
                aux_cell = create_generic_cell(ctx, id_IOLOGIC, ci->name.str(ctx) + "_AUX1");
                aux_cell->setAttr(id_IOLOGIC_TYPE, std::string("DUMMY"));
                aux_cell->setAttr(ctx->id("IOLOGIC_MASTER_CELL"), master_name);
                aux_cell->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                aux_cell->setParam(ctx->id("INMODE"), std::string("DDRENABLE16"));
                aux_cell->setParam(ctx->id("UPDATE"), std::string("SAME"));
                if (port_used(ci, id_RESET)) {
                    aux_cell->connectPort(id_RESET, ci->ports.at(id_RESET).net);
                }
                if (port_used(ci, id_PCLK)) {
                    aux_cell->connectPort(id_PCLK, ci->ports.at(id_PCLK).net);
                }
                new_cells.push_back(std::move(aux_cell));

                // master
                get_next_oser16_loc(ctx->device, loc);
                loc.z = BelZ::iologic_z;
                aux_cell = create_generic_cell(ctx, id_IOLOGIC, master_name);
                aux_cell->setAttr(id_IOLOGIC_TYPE, std::string("DUMMY"));
                aux_cell->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                aux_cell->setParam(ctx->id("INMODE"), std::string("DDRENABLE16"));
                aux_cell->setParam(ctx->id("UPDATE"), std::string("SAME"));
                if (port_used(ci, id_RESET)) {
                    aux_cell->connectPort(id_RESET, ci->ports.at(id_RESET).net);
                }
                if (port_used(ci, id_PCLK)) {
                    aux_cell->connectPort(id_PCLK, ci->ports.at(id_PCLK).net);
                }
                ci->movePortTo(id_FCLK, aux_cell.get(), id_FCLK);
                ci->movePortTo(id_Q0, aux_cell.get(), id_Q6);
                ci->movePortTo(id_Q1, aux_cell.get(), id_Q7);
                ci->movePortTo(id_Q2, aux_cell.get(), id_Q8);
                ci->movePortTo(id_Q3, aux_cell.get(), id_Q9);
                new_cells.push_back(std::move(aux_cell));

                // bottom row is special and may need two additional ports
                loc.z = BelZ::ioba_z;
                if (ctx->getBelByLocation(loc) != BelId()) {
                    dummy = create_generic_cell(ctx, id_DUMMY_CELL, ci->name.str(ctx) + "_IOLOGIC_IO0");
                    dummy->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                    new_cells.push_back(std::move(dummy));
                }

                // XXX Prohibit the use of 4th IO and IOLOGIC
                loc.z = BelZ::iobb_z;
                if (ctx->getBelByLocation(loc) != BelId()) {
                    dummy = create_generic_cell(ctx, id_DUMMY_CELL, ci->name.str(ctx) + "_IOLOGIC_IO1");
                    dummy->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                    new_cells.push_back(std::move(dummy));
                }
                master_name = ci->name.str(ctx) + "_AUX2";
                loc.z = BelZ::iologic_z + 1;
                dummy = create_generic_cell(ctx, id_DUMMY_CELL, master_name);
                dummy->setAttr(id_BEL, ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx));
                new_cells.push_back(std::move(dummy));
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
            case ID_ELVDS_IOBUF: /* fall-through*/
            case ID_ELVDS_IBUF:  /* fall-through*/
            case ID_ELVDS_TBUF:  /* fall-through*/
            case ID_ELVDS_OBUF:  /* fall-through*/
            case ID_TLVDS_IOBUF: /* fall-through*/
            case ID_TLVDS_IBUF:  /* fall-through*/
            case ID_TLVDS_TBUF:  /* fall-through*/
            case ID_TLVDS_OBUF: {
                NPNR_ASSERT(check_availability(ctx, ci->type));
                if (ci->type.in(id_TLVDS_TBUF, id_TLVDS_OBUF, id_TLVDS_IOBUF, id_ELVDS_TBUF, id_ELVDS_OBUF,
                                id_ELVDS_IOBUF)) {
                    iob_p = net_only_drives(ctx, ci->ports.at(id_O).net, is_iob, id_I);
                    iob_n = net_only_drives(ctx, ci->ports.at(id_OB).net, is_iob, id_I);
                } else {
                    iob_p = net_driven_by(ctx, ci->ports.at(id_I).net, is_iob, id_O);
                    iob_n = net_driven_by(ctx, ci->ports.at(id_IB).net, is_iob, id_O);
                }
                NPNR_ASSERT(iob_p != nullptr);
                NPNR_ASSERT(iob_n != nullptr);
                auto iob_p_bel_a = iob_p->attrs.find(id_BEL);
                if (iob_p_bel_a == iob_p->attrs.end()) {
                    log_error("LVDS '%s' must be restricted.\n", ctx->nameOf(ci));
                }
                BelId iob_p_bel = ctx->getBelByNameStr(iob_p_bel_a->second.as_string());
                Loc loc_p = ctx->getBelLocation(iob_p_bel);
                // restrict the N buffer
                ++loc_p.z;
                BelId n_bel = ctx->getBelByLocation(loc_p);
                if (n_bel == BelId()) {
                    log_error("Invalid pin for '%s'.\n", ctx->nameOf(ci));
                }
                iob_n->attrs[id_BEL] = ctx->getBelName(n_bel).str(ctx);
                iob_n->type = iob_p->type;
                // mark IOBs as part of DS pair
                std::string io_type = ci->type.c_str(ctx);
                // XXX compatibility
                iob_n->setAttr(id_DIFF, std::string("N"));
                iob_n->setAttr(id_DIFF_TYPE, io_type);
                iob_p->setAttr(id_DIFF, std::string("P"));
                iob_p->setAttr(id_DIFF_TYPE, io_type);

                iob_n->setParam(id_DIFF, std::string("N"));
                iob_n->setParam(id_DIFF_TYPE, io_type);
                iob_p->setParam(id_DIFF, std::string("P"));
                iob_p->setParam(id_DIFF_TYPE, io_type);

                if (ci->type.in(id_TLVDS_TBUF, id_TLVDS_OBUF, id_ELVDS_TBUF, id_ELVDS_OBUF)) {
                    // disconnect N input: it is wired internally
                    delete_nets.insert(iob_n->ports.at(id_I).net->name);
                    iob_n->disconnectPort(id_I);
                    ci->disconnectPort(id_OB);
                    // disconnect P output
                    delete_nets.insert(ci->ports.at(id_O).net->name);
                    ci->disconnectPort(id_O);
                    // connect TLVDS input to P input
                    ci->movePortTo(id_I, iob_p, id_I);
                    if (ci->type.in(id_TLVDS_TBUF, id_ELVDS_TBUF)) {
                        if (iob_p->type == id_IOBS) {
                            iob_p->disconnectPort(id_OEN);
                            iob_n->disconnectPort(id_OEN);
                        }
                        ci->movePortTo(id_OEN, iob_p, id_OEN);
                    }
                }
                if (ci->type.in(id_TLVDS_IBUF, id_ELVDS_IBUF)) {
                    // disconnect N input: it is wired internally
                    delete_nets.insert(iob_n->ports.at(id_O).net->name);
                    iob_n->disconnectPort(id_O);
                    ci->disconnectPort(id_IB);
                    // disconnect P input
                    delete_nets.insert(ci->ports.at(id_I).net->name);
                    ci->disconnectPort(id_I);
                    // connect TLVDS output to P output
                    ci->movePortTo(id_O, iob_p, id_O);
                }
                if (ci->type.in(id_TLVDS_IOBUF, id_ELVDS_IOBUF)) {
                    // disconnect N io: it is wired internally
                    // O port is missing after iopadmap so leave it as is
                    delete_nets.insert(iob_n->getPort(id_I)->name);
                    iob_n->disconnectPort(id_I);
                    iob_n->disconnectPort(id_OEN);
                    ci->disconnectPort(id_IOB);

                    // disconnect P io
                    delete_nets.insert(ci->getPort(id_IO)->name);
                    iob_p->disconnectPort(id_I);
                    iob_p->disconnectPort(id_OEN);
                    ci->disconnectPort(id_IO);
                    ci->movePortTo(id_I, iob_p, id_I);
                    ci->movePortTo(id_O, iob_p, id_O);
                    // OEN
                    if (iob_p->type == id_IOBS) {
                        iob_p->disconnectPort(id_OEN);
                        iob_n->disconnectPort(id_OEN);
                    }
                    ci->movePortTo(id_OEN, iob_p, id_OEN);
                }
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

static bool is_pll(const Context *ctx, const CellInfo *cell)
{
    switch (cell->type.hash()) {
    case ID_rPLL:
        return true;
    case ID_PLLVR:
        return true;
    default:
        return false;
    }
}

static void pll_disable_unused_ports(Context *ctx, CellInfo *ci)
{
    // Unused ports will be disabled during image generation. Here we add flags for such ports.
    Property pr_enable("ENABLE"), pr_disable("DISABLE");
    IdString ports[][2] = {
            {id_CLKOUTP, id_CLKOUTPS}, {id_CLKOUTD, id_CLKOUTDIV}, {id_CLKOUTD3, id_CLKOUTDIV3}, {id_LOCK, id_FLOCK}};
    for (int i = 0; i < 4; ++i) {
        ci->setParam(ports[i][1], port_used(ci, ports[i][0]) ? pr_enable : pr_disable);
    }
    // resets
    NetInfo *net = ci->getPort(id_RESET);
    ci->setParam(id_RSTEN, pr_enable);
    if (!port_used(ci, id_RESET) || net->name == ctx->id("$PACKER_VCC_NET") ||
        net->name == ctx->id("$PACKER_GND_NET")) {
        ci->setParam(id_RSTEN, pr_disable);
    }
    ci->setParam(id_PWDEN, pr_enable);
    net = ci->getPort(id_RESET_P);
    if (!port_used(ci, id_RESET_P) || net->name == ctx->id("$PACKER_VCC_NET") ||
        net->name == ctx->id("$PACKER_GND_NET")) {
        ci->setParam(id_PWDEN, pr_disable);
    }
}

// Pack PLLs
static void pack_plls(Context *ctx)
{
    pool<IdString> packed_cells;
    pool<IdString> delete_nets;

    std::vector<std::unique_ptr<CellInfo>> new_cells;
    log_info("Packing PLLs..\n");

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ctx->verbose)
            log_info("cell '%s' is of type '%s'\n", ctx->nameOf(ci), ci->type.c_str(ctx));
        if (is_pll(ctx, ci)) {
            std::string parm_device = str_or_default(ci->params, id_DEVICE, ctx->device.c_str());
            if (parm_device != ctx->device) {
                log_error("Cell '%s': wrong PLL device:%s instead of %s\n", ctx->nameOf(ci), parm_device.c_str(),
                          ctx->device.c_str());
                continue;
            }

            switch (ci->type.hash()) {
            case ID_rPLL: {
                if (parm_device == "GW1N-1" || parm_device == "GW1NZ-1" || parm_device == "GW1NR-9C" ||
                    parm_device == "GW1NR-9" || parm_device == "GW1N-4" || parm_device == "GW1NS-2C") {
                    pll_disable_unused_ports(ctx, ci);
                    // A cell
                    std::unique_ptr<CellInfo> cell = create_generic_cell(ctx, id_rPLL, ci->name.str(ctx) + "$rpll");
                    reconnect_rpll(ctx, ci, cell.get());
                    new_cells.push_back(std::move(cell));
                    auto pll_cell = new_cells.back().get();

                    // need params for gowin_pack
                    for (auto &parm : ci->params) {
                        pll_cell->setParam(parm.first, parm.second);
                    }
                    packed_cells.insert(ci->name);
                } else {
                    log_error("rPLL isn't supported for %s\n", ctx->device.c_str());
                }
            } break;
            case ID_PLLVR: {
                if (parm_device == "GW1NSR-4C") {
                    pll_disable_unused_ports(ctx, ci);
                    std::unique_ptr<CellInfo> cell = create_generic_cell(ctx, id_PLLVR, ci->name.str(ctx) + "$pllvr");
                    reconnect_pllvr(ctx, ci, cell.get());
                    new_cells.push_back(std::move(cell));
                    auto pll_cell = new_cells.back().get();

                    // need params for gowin_pack
                    for (auto &parm : ci->params) {
                        pll_cell->setParam(parm.first, parm.second);
                    }
                    packed_cells.insert(ci->name);
                } else {
                    log_error("PLLVR isn't supported for %s\n", ctx->device.c_str());
                }
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
            bool have_xxx_port = false;
            bool have_xxx0_port = false;
            // check whether the given IO is limited to simplified IO cells
            auto constr_bel = ci->attrs.find(id_BEL);
            if (constr_bel != ci->attrs.end()) {
                constr_bel_name = constr_bel->second.as_string();
            }
            if (iob != nullptr) {
                constr_bel = iob->attrs.find(id_BEL);
                if (constr_bel != iob->attrs.end()) {
                    constr_bel_name = constr_bel->second.as_string();
                }
            }
            if (!constr_bel_name.empty()) {
                BelId constr_bel = ctx->getBelByNameStr(constr_bel_name);
                if (constr_bel != BelId()) {
                    new_cell_type = ctx->bels.at(constr_bel).type;
                    if (ctx->gw1n9_quirk) {
                        have_xxx_port = ctx->bels.at(constr_bel).pins.count(id_GW9_ALWAYS_LOW0) != 0;
                    }
                    have_xxx0_port = ctx->bels.at(constr_bel).pins.count(id_GW9C_ALWAYS_LOW0) != 0;
                }
            }

            // Create a IOB buffer
            std::unique_ptr<CellInfo> ice_cell = create_generic_cell(ctx, new_cell_type, ci->name.str(ctx) + "$iob");
            gwio_to_iob(ctx, ci, ice_cell.get(), packed_cells);
            new_cells.push_back(std::move(ice_cell));
            auto gwiob = new_cells.back().get();
            // XXX GW1NR-9 quirks
            if (have_xxx_port && ci->type != id_IBUF) {
                gwiob->addInput(id_GW9_ALWAYS_LOW0);
                gwiob->connectPort(id_GW9_ALWAYS_LOW0, ctx->nets[ctx->id("$PACKER_GND_NET")].get());
                gwiob->addInput(id_GW9_ALWAYS_LOW1);
                gwiob->connectPort(id_GW9_ALWAYS_LOW1, ctx->nets[ctx->id("$PACKER_GND_NET")].get());
            }
            if (have_xxx0_port && ci->type != id_IBUF) {
                gwiob->addInput(id_GW9C_ALWAYS_LOW0);
                gwiob->connectPort(id_GW9C_ALWAYS_LOW0, ctx->nets[ctx->id("$PACKER_GND_NET")].get());
                gwiob->addInput(id_GW9C_ALWAYS_LOW1);
                gwiob->connectPort(id_GW9C_ALWAYS_LOW1, ctx->nets[ctx->id("$PACKER_GND_NET")].get());
            }

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
        pre_pack(ctx);
        pack_constants(ctx);
        pack_sram(ctx);
        pack_gsr(ctx);
        pack_io(ctx);
        pack_diff_io(ctx);
        pack_iologic(ctx);
        pack_wideluts(ctx);
        pack_alus(ctx);
        pack_lut_lutffs(ctx);
        pack_nonlut_ffs(ctx);
        pack_plls(ctx);
        post_pack(ctx);
        ctx->settings[id_pack] = 1;
        ctx->assignArchInfo();
        log_info("Checksum: 0x%08x\n", ctx->checksum());
        return true;
    } catch (log_execution_error_exception) {
        return false;
    }
}

NEXTPNR_NAMESPACE_END
