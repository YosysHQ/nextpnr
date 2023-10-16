/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2023  Myrtle Shah <gatecat@ds0.me>
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

#include "pack.h"
#include <algorithm>
#include <boost/optional.hpp>
#include <iterator>
#include <queue>
#include <unordered_set>
#include "chain_utils.h"
#include "design_utils.h"
#include "extra_data.h"
#include "log.h"
#include "nextpnr.h"
#include "pins.h"

#define HIMBAECHEL_CONSTIDS "uarch/xilinx/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

// Process the contents of packed_cells
void XilinxPacker::flush_cells()
{
    for (auto pcell : packed_cells) {
        for (auto &port : ctx->cells[pcell]->ports) {
            ctx->cells[pcell]->disconnectPort(port.first);
        }
        ctx->cells.erase(pcell);
    }
    packed_cells.clear();
}

void XilinxPacker::xform_cell(const dict<IdString, XFormRule> &rules, CellInfo *ci)
{
    auto &rule = rules.at(ci->type);
    ci->attrs[id_X_ORIG_TYPE] = ci->type.str(ctx);
    ci->type = rule.new_type;
    std::vector<IdString> orig_port_names;
    for (auto &port : ci->ports)
        orig_port_names.push_back(port.first);

    for (auto pname : orig_port_names) {
        if (rule.port_multixform.count(pname)) {
            auto old_port = ci->ports.at(pname);
            ci->disconnectPort(pname);
            ci->ports.erase(pname);
            for (auto new_name : rule.port_multixform.at(pname)) {
                ci->ports[new_name].name = new_name;
                ci->ports[new_name].type = old_port.type;
                ci->connectPort(new_name, old_port.net);
                ci->attrs[ctx->id("X_ORIG_PORT_" + new_name.str(ctx))] = pname.str(ctx);
            }
        } else {
            IdString new_name;
            if (rule.port_xform.count(pname)) {
                new_name = rule.port_xform.at(pname);
            } else {
                std::string stripped_name;
                for (auto c : pname.str(ctx))
                    if (c != '[' && c != ']')
                        stripped_name += c;
                new_name = ctx->id(stripped_name);
            }
            if (new_name != pname) {
                ci->renamePort(pname, new_name);
            }
            ci->attrs[ctx->id("X_ORIG_PORT_" + new_name.str(ctx))] = pname.str(ctx);
        }
    }

    std::vector<IdString> xform_params;
    for (auto &param : ci->params)
        if (rule.param_xform.count(param.first))
            xform_params.push_back(param.first);
    for (auto param : xform_params)
        ci->params[rule.param_xform.at(param)] = ci->params[param];

    for (auto &attr : rule.set_attrs)
        ci->attrs[attr.first] = attr.second;

    for (auto &param : rule.set_params)
        ci->params[param.first] = param.second;
}

void XilinxPacker::generic_xform(const dict<IdString, XFormRule> &rules, bool print_summary)
{
    std::map<std::string, int> cell_count;
    std::map<std::string, int> new_types;
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (rules.count(ci->type)) {
            cell_count[ci->type.str(ctx)]++;
            xform_cell(rules, ci);
            new_types[ci->type.str(ctx)]++;
        }
    }
    if (print_summary) {
        for (auto &nt : new_types) {
            log_info("    Created %d %s cells from:\n", nt.second, nt.first.c_str());
            for (auto &cc : cell_count) {
                if (rules.at(ctx->id(cc.first)).new_type != ctx->id(nt.first))
                    continue;
                log_info("        %6dx %s\n", cc.second, cc.first.c_str());
            }
        }
    }
}

CellInfo *XilinxPacker::feed_through_lut(NetInfo *net, const std::vector<PortRef> &feed_users)
{
    NetInfo *feedthru_net = ctx->createNet(ctx->idf("%s$legal%d", net->name.c_str(ctx), ++autoidx));

    CellInfo *lut = create_lut(stringf("%s$LUT%d", net->name.c_str(ctx), ++autoidx), {net}, feedthru_net, Property(2));

    for (auto &usr : feed_users) {
        usr.cell->disconnectPort(usr.port);
        usr.cell->connectPort(usr.port, feedthru_net);
    }

    return lut;
}

CellInfo *XilinxPacker::feed_through_muxf(NetInfo *net, IdString type, const std::vector<PortRef> &feed_users)
{
    NetInfo *feedthru_net = ctx->createNet(ctx->idf("%s$legal$%d", net->name.c_str(ctx), ++autoidx));
    CellInfo *mux = create_cell(type, ctx->idf("%s$MUX$%d", net->name.c_str(ctx), ++autoidx));
    mux->connectPort(id_I0, net);
    mux->connectPort(id_O, feedthru_net);
    mux->connectPort(id_S, ctx->nets[ctx->id("$PACKER_GND_NET")].get());

    for (auto &usr : feed_users) {
        usr.cell->disconnectPort(usr.port);
        usr.cell->connectPort(usr.port, feedthru_net);
    }

    return mux;
}

IdString XilinxPacker::int_name(IdString base, const std::string &postfix, bool is_hierarchy)
{
    return ctx->id(base.str(ctx) + (is_hierarchy ? "$subcell$" : "$intcell$") + postfix);
}

NetInfo *XilinxPacker::create_internal_net(IdString base, const std::string &postfix, bool is_hierarchy)
{
    IdString name = ctx->id(base.str(ctx) + (is_hierarchy ? "$subnet$" : "$intnet$") + postfix);
    return ctx->createNet(name);
}

void XilinxPacker::pack_luts()
{
    log_info("Packing LUTs..\n");

    dict<IdString, XFormRule> lut_rules;
    for (int k = 1; k <= 6; k++) {
        IdString lut = ctx->id("LUT" + std::to_string(k));
        lut_rules[lut].new_type = id_SLICE_LUTX;
        for (int i = 0; i < k; i++)
            lut_rules[lut].port_xform[ctx->id("I" + std::to_string(i))] = ctx->id("A" + std::to_string(i + 1));
        lut_rules[lut].port_xform[id_O] = id_O6;
    }
    lut_rules[id_LUT6_2] = XFormRule(lut_rules[id_LUT6]);
    generic_xform(lut_rules, true);
}

void XilinxPacker::pack_ffs()
{
    log_info("Packing flipflops..\n");

    dict<IdString, XFormRule> ff_rules;
    ff_rules[id_FDCE].new_type = id_SLICE_FFX;
    ff_rules[id_FDCE].port_xform[id_C] = id_CK;
    ff_rules[id_FDCE].port_xform[id_CLR] = id_SR;
    // ff_rules[id_FDCE].param_xform[id_IS_CLR_INVERTED] = id_IS_SR_INVERTED;

    ff_rules[id_FDPE].new_type = id_SLICE_FFX;
    ff_rules[id_FDPE].port_xform[id_C] = id_CK;
    ff_rules[id_FDPE].port_xform[id_PRE] = id_SR;
    // ff_rules[id_FDPE].param_xform[id_IS_PRE_INVERTED] = id_IS_SR_INVERTED;

    ff_rules[id_FDRE].new_type = id_SLICE_FFX;
    ff_rules[id_FDRE].port_xform[id_C] = id_CK;
    ff_rules[id_FDRE].port_xform[id_R] = id_SR;
    ff_rules[id_FDRE].set_attrs.emplace_back(id_X_FFSYNC, "1");
    // ff_rules[id_FDRE].param_xform[id_IS_R_INVERTED] = id_IS_SR_INVERTED;

    ff_rules[id_FDSE].new_type = id_SLICE_FFX;
    ff_rules[id_FDSE].port_xform[id_C] = id_CK;
    ff_rules[id_FDSE].port_xform[id_S] = id_SR;
    ff_rules[id_FDSE].set_attrs.emplace_back(id_X_FFSYNC, "1");
    // ff_rules[id_FDSE].param_xform[id_IS_S_INVERTED] = id_IS_SR_INVERTED;

    ff_rules[id_FDCE_1] = XFormRule(ff_rules[id_FDCE]);
    ff_rules[id_FDCE_1].set_params.emplace_back(id_IS_C_INVERTED, 1);

    ff_rules[id_FDPE_1] = XFormRule(ff_rules[id_FDPE]);
    ff_rules[id_FDPE_1].set_params.emplace_back(id_IS_C_INVERTED, 1);

    ff_rules[id_FDRE_1] = XFormRule(ff_rules[id_FDRE]);
    ff_rules[id_FDRE_1].set_params.emplace_back(id_IS_C_INVERTED, 1);

    ff_rules[id_FDSE_1] = XFormRule(ff_rules[id_FDSE]);
    ff_rules[id_FDSE_1].set_params.emplace_back(id_IS_C_INVERTED, 1);

    generic_xform(ff_rules, true);
}

void XilinxPacker::pack_lutffs()
{
    int pairs = 0;
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->cluster != ClusterId() || !ci->constr_children.empty())
            continue;
        if (ci->type != id_SLICE_FFX)
            continue;
        NetInfo *d = ci->getPort(id_D);
        if (d->driver.cell == nullptr || d->driver.cell->type != id_SLICE_LUTX || d->driver.port != id_O6)
            continue;
        CellInfo *lut = d->driver.cell;
        if (lut->cluster != ClusterId() || !lut->constr_children.empty())
            continue;
        lut->constr_children.push_back(ci);
        lut->cluster = lut->name;
        ci->cluster = lut->name;
        ci->constr_x = 0;
        ci->constr_y = 0;
        ci->constr_z = (BEL_FF - BEL_6LUT);
        ++pairs;
    }
    log_info("Constrained %d LUTFF pairs.\n", pairs);
}

bool XilinxPacker::is_constrained(const CellInfo *cell) { return cell->cluster != ClusterId(); }

void XilinxPacker::legalise_muxf_tree(CellInfo *curr, std::vector<CellInfo *> &mux_roots)
{
    if (curr->type.str(ctx).substr(0, 3) == "LUT")
        return;
    for (IdString p : {id_I0, id_I1}) {
        NetInfo *pn = curr->getPort(p);
        if (pn == nullptr || pn->driver.cell == nullptr)
            continue;
        if (curr->type == id_MUXF7) {
            if (pn->driver.cell->type.str(ctx).substr(0, 3) != "LUT" || is_constrained(pn->driver.cell)) {
                PortRef pr;
                pr.cell = curr;
                pr.port = p;
                feed_through_lut(pn, {pr});
                continue;
            }
        } else {
            IdString next_type;
            if (curr->type == id_MUXF9)
                next_type = id_MUXF8;
            else if (curr->type == id_MUXF8)
                next_type = id_MUXF7;
            else
                NPNR_ASSERT_FALSE("bad mux type");
            if (pn->driver.cell->type != next_type || is_constrained(pn->driver.cell) ||
                bool_or_default(pn->driver.cell->attrs, id_MUX_TREE_ROOT)) {
                PortRef pr;
                pr.cell = curr;
                pr.port = p;
                feed_through_muxf(pn, next_type, {pr});
                continue;
            }
        }
        legalise_muxf_tree(pn->driver.cell, mux_roots);
    }
}

void XilinxPacker::constrain_muxf_tree(CellInfo *curr, CellInfo *base, int zoffset)
{

    if (curr->type == id_SLICE_LUTX && (curr->constr_abs_z || curr->cluster != ClusterId()))
        return;

    int base_z = 0;
    if (base->type == id_MUXF7)
        base_z = BEL_F7MUX;
    else if (base->type == id_MUXF8)
        base_z = BEL_F8MUX;
    else if (base->type == id_MUXF9)
        base_z = BEL_F9MUX;
    else if (base->constr_abs_z)
        base_z = base->constr_z;
    else
        NPNR_ASSERT_FALSE("unexpected mux base type");
    int curr_z = zoffset * 16;
    int input_spacing = 0;
    if (curr->type == id_MUXF7) {
        curr_z += BEL_F7MUX;
        input_spacing = 1;
    } else if (curr->type == id_MUXF8) {
        curr_z += BEL_F8MUX;
        input_spacing = 2;
    } else if (curr->type == id_MUXF9) {
        curr_z += BEL_F9MUX;
        input_spacing = 4;
    } else
        curr_z += BEL_6LUT;
    if (curr != base) {
        curr->constr_x = 0;
        curr->constr_y = 0;
        curr->constr_z = curr_z - base_z;
        curr->constr_abs_z = false;
        curr->cluster = base->name;
        base->constr_children.push_back(curr);
    }
    if (curr->type.in(id_MUXF7, id_MUXF8, id_MUXF9)) {
        NetInfo *i0 = curr->getPort(id_I0), *i1 = curr->getPort(id_I1);
        if (i0 != nullptr && i0->driver.cell != nullptr)
            constrain_muxf_tree(i0->driver.cell, base, zoffset + input_spacing);
        if (i1 != nullptr && i1->driver.cell != nullptr)
            constrain_muxf_tree(i1->driver.cell, base, zoffset);
    }
}

void XilinxPacker::pack_muxfs()
{
    log_info("Packing MUX[789]s..\n");
    std::vector<CellInfo *> mux_roots;
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        ci->attrs.erase(id_MUX_TREE_ROOT);
        if (ci->type == id_MUXF9) {
            log_error("MUXF9 is not supported on xc7!\n");
        } else if (ci->type == id_MUXF8) {
            NetInfo *o = ci->getPort(id_O);
            if (o == nullptr || o->users.entries() != 1 || (*o->users.begin()).cell->type != id_MUXF9 ||
                is_constrained((*o->users.begin()).cell) || (*o->users.begin()).port == id_S)
                mux_roots.push_back(ci);
        } else if (ci->type == id_MUXF7) {
            NetInfo *o = ci->getPort(id_O);
            if (o == nullptr || o->users.entries() != 1 || (*o->users.begin()).cell->type != id_MUXF8 ||
                is_constrained((*o->users.begin()).cell) || (*o->users.begin()).port == id_S)
                mux_roots.push_back(ci);
        }
    }
    for (auto root : mux_roots)
        root->attrs[id_MUX_TREE_ROOT] = 1;
    for (auto root : mux_roots)
        legalise_muxf_tree(root, mux_roots);
    for (auto root : mux_roots) {
        root->cluster = root->name;
        constrain_muxf_tree(root, root, 0);
    }
}

void XilinxPacker::finalise_muxfs()
{
    dict<IdString, XFormRule> muxf_rules;
    muxf_rules[id_MUXF9].new_type = id_F9MUX;
    muxf_rules[id_MUXF9].port_xform[id_I0] = id_0;
    muxf_rules[id_MUXF9].port_xform[id_I1] = id_1;
    muxf_rules[id_MUXF9].port_xform[id_S] = id_S0;
    muxf_rules[id_MUXF9].port_xform[id_O] = id_OUT;
    muxf_rules[id_MUXF8].new_type = id_SELMUX2_1;
    muxf_rules[id_MUXF8].port_xform = muxf_rules[id_MUXF9].port_xform;
    muxf_rules[id_MUXF7].new_type = id_SELMUX2_1;
    muxf_rules[id_MUXF7].port_xform = muxf_rules[id_MUXF9].port_xform;
    generic_xform(muxf_rules, true);
}

void XilinxPacker::pack_srls()
{
    dict<IdString, XFormRule> srl_rules;
    srl_rules[id_SRL16E].new_type = id_SLICE_LUTX;
    srl_rules[id_SRL16E].port_xform[id_CLK] = id_CLK;
    srl_rules[id_SRL16E].port_xform[id_CE] = id_WE;
    srl_rules[id_SRL16E].port_xform[id_D] = id_DI2;
    srl_rules[id_SRL16E].port_xform[id_Q] = id_O6;
    srl_rules[id_SRL16E].set_attrs.emplace_back(id_X_LUT_AS_SRL, "1");

    srl_rules[id_SRLC32E].new_type = id_SLICE_LUTX;
    srl_rules[id_SRLC32E].port_xform[id_CLK] = id_CLK;
    srl_rules[id_SRLC32E].port_xform[id_CE] = id_WE;
    srl_rules[id_SRLC32E].port_xform[id_D] = id_DI1;
    srl_rules[id_SRLC32E].port_xform[id_Q] = id_O6;
    srl_rules[id_SRLC32E].set_attrs.emplace_back(id_X_LUT_AS_SRL, "1");
    // FIXME: Q31 support
    generic_xform(srl_rules, true);
    // Fixup SRL inputs
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type != id_SLICE_LUTX)
            continue;
        std::string orig_type = str_or_default(ci->attrs, id_X_ORIG_TYPE);
        if (orig_type == "SRL16E") {
            for (int i = 3; i >= 0; i--) {
                ci->renamePort(ctx->id("A" + std::to_string(i)), ctx->id("A" + std::to_string(i + 2)));
            }
            for (auto tp : {id_A1, id_A6}) {
                ci->ports[tp].name = tp;
                ci->ports[tp].type = PORT_IN;
                ci->connectPort(tp, ctx->nets[ctx->id("$PACKER_VCC_NET")].get());
            }
        } else if (orig_type == "SRLC32E") {
            for (int i = 4; i >= 0; i--) {
                ci->renamePort(ctx->id("A" + std::to_string(i)), ctx->id("A" + std::to_string(i + 2)));
            }
            for (auto tp : {id_A1}) {
                ci->ports[tp].name = tp;
                ci->ports[tp].type = PORT_IN;
                ci->connectPort(tp, ctx->nets[ctx->id("$PACKER_VCC_NET")].get());
            }
        }
    }
}

void XilinxPacker::pack_constants()
{
    log_info("Packing constants..\n");
    if (tied_pins.empty())
        get_tied_pins(ctx, tied_pins);
    if (invertible_pins.empty())
        get_invertible_pins(ctx, invertible_pins);
    if (!ctx->cells.count(ctx->id("$PACKER_GND_DRV"))) {
        CellInfo *gnd_cell = ctx->createCell(ctx->id("$PACKER_GND_DRV"), id_PSEUDO_GND);
        gnd_cell->addOutput(id_Y);
        NetInfo *gnd_net = ctx->createNet(ctx->id("$PACKER_GND_NET"));
        gnd_net->constant_value = id_GND;
        gnd_cell->connectPort(id_Y, gnd_net);

        CellInfo *vcc_cell = ctx->createCell(ctx->id("$PACKER_VCC_DRV"), id_PSEUDO_VCC);
        vcc_cell->addOutput(id_Y);
        NetInfo *vcc_net = ctx->createNet(ctx->id("$PACKER_VCC_NET"));
        vcc_net->constant_value = id_VCC;
        vcc_cell->connectPort(id_Y, vcc_net);
    }
    NetInfo *gnd = ctx->nets[ctx->id("$PACKER_GND_NET")].get(), *vcc = ctx->nets[ctx->id("$PACKER_VCC_NET")].get();

    std::vector<IdString> dead_nets;

    std::vector<std::tuple<CellInfo *, IdString, bool>> const_ports;

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (!tied_pins.count(ci->type))
            continue;
        auto &tp = tied_pins.at(ci->type);
        for (auto port : tp) {
            if (cell.second->ports.count(port.first) && cell.second->ports.at(port.first).net != nullptr &&
                cell.second->ports.at(port.first).net->driver.cell != nullptr)
                continue;
            const_ports.emplace_back(ci, port.first, port.second);
        }
    }

    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        if (ni->driver.cell != nullptr && ni->driver.cell->type == id_GND) {
            IdString drv_cell = ni->driver.cell->name;
            for (auto &usr : ni->users) {
                const_ports.emplace_back(usr.cell, usr.port, false);
                usr.cell->ports.at(usr.port).net = nullptr;
            }
            dead_nets.push_back(net.first);
            ctx->cells.erase(drv_cell);
        } else if (ni->driver.cell != nullptr && ni->driver.cell->type == id_VCC) {
            IdString drv_cell = ni->driver.cell->name;
            for (auto &usr : ni->users) {
                const_ports.emplace_back(usr.cell, usr.port, true);
                usr.cell->ports.at(usr.port).net = nullptr;
            }
            dead_nets.push_back(net.first);
            ctx->cells.erase(drv_cell);
        }
    }

    for (auto port : const_ports) {
        CellInfo *ci;
        IdString pname;
        bool cval;
        std::tie(ci, pname, cval) = port;

        if (!ci->ports.count(pname)) {
            ci->addInput(pname);
        }
        if (ci->ports.at(pname).net != nullptr) {
            // Case where a port with a default tie value is previously connected to an undriven net
            NPNR_ASSERT(ci->ports.at(pname).net->driver.cell == nullptr);
            ci->disconnectPort(pname);
        }

        if (!cval && invertible_pins.count(ci->type) && invertible_pins.at(ci->type).count(pname)) {
            // Invertible pins connected to zero are optimised to a connection to Vcc (which is easier to route)
            // and an inversion
            ci->params[ctx->idf("IS_%s_INVERTED", pname.c_str(ctx))] = Property(1);
            cval = true;
        }

        ci->connectPort(pname, cval ? vcc : gnd);
    }

    for (auto dn : dead_nets) {
        ctx->nets.erase(dn);
    }
}

void XilinxPacker::rename_net(IdString old, IdString newname)
{
    std::unique_ptr<NetInfo> ni;
    std::swap(ni, ctx->nets[old]);
    ctx->nets.erase(old);
    ni->name = newname;
    ctx->nets[newname] = std::move(ni);
}

void XilinxPacker::tie_port(CellInfo *ci, const std::string &port, bool value, bool inv)
{
    IdString p = ctx->id(port);
    if (!ci->ports.count(p)) {
        ci->addInput(p);
    }
    if (value || inv)
        ci->connectPort(p, ctx->nets.at(ctx->id("$PACKER_VCC_NET")).get());
    else
        ci->connectPort(p, ctx->nets.at(ctx->id("$PACKER_GND_NET")).get());
    if (!value && inv)
        ci->params[ctx->idf("IS_%s_INVERTED", port.c_str())] = Property(1);
}

void XC7Packer::pack_bram()
{
    log_info("Packing BRAM..\n");

    // Rules for normal TDP BRAM
    dict<IdString, XFormRule> bram_rules;
    bram_rules[id_RAMB18E1].new_type = id_RAMB18E1_RAMB18E1;
    bram_rules[id_RAMB18E1].port_multixform[ctx->id("WEA[0]")] = {id_WEA0, id_WEA1};
    bram_rules[id_RAMB18E1].port_multixform[ctx->id("WEA[1]")] = {id_WEA2, id_WEA3};
    bram_rules[id_RAMB36E1].new_type = id_RAMB36E1_RAMB36E1;

    // Some ports have upper/lower bel pins in 36-bit mode
    std::vector<std::pair<IdString, std::vector<std::string>>> ul_pins;
    get_bram36_ul_pins(ctx, ul_pins);
    for (auto &ul : ul_pins) {
        for (auto &bp : ul.second)
            bram_rules[id_RAMB36E1].port_multixform[ul.first].push_back(ctx->id(bp));
    }
    bram_rules[id_RAMB36E1].port_multixform[ctx->id("ADDRARDADDR[15]")].push_back(id_ADDRARDADDRL15);
    bram_rules[id_RAMB36E1].port_multixform[ctx->id("ADDRBWRADDR[15]")].push_back(id_ADDRBWRADDRL15);

    // Special rules for SDP rules, relating to WE connectivity
    dict<IdString, XFormRule> sdp_bram_rules = bram_rules;
    for (int i = 0; i < 4; i++) {
        // Connects to two WEBWE bel pins
        sdp_bram_rules[id_RAMB18E1].port_multixform[ctx->idf("WEBWE[%d]", i)].push_back(ctx->idf("WEBWE%d", i * 2));
        sdp_bram_rules[id_RAMB18E1].port_multixform[ctx->idf("WEBWE[%d]", i)].push_back(ctx->idf("WEBWE%d", i * 2 + 1));
        // Not used in SDP mode
        sdp_bram_rules[id_RAMB18E1].port_multixform[ctx->idf("WEA[%d]", i)] = {};
    }

    for (int i = 0; i < 8; i++) {
        sdp_bram_rules[id_RAMB36E1].port_multixform[ctx->idf("WEBWE[%d]", i)].clear();
        // Connects to two WEBWE bel pins
        sdp_bram_rules[id_RAMB36E1].port_multixform[ctx->idf("WEBWE[%d]", i)].push_back(ctx->idf("WEBWEL%d", i));
        sdp_bram_rules[id_RAMB36E1].port_multixform[ctx->idf("WEBWE[%d]", i)].push_back(ctx->idf("WEBWEU%d", i));
        // Not used in SDP mode
        sdp_bram_rules[id_RAMB36E1].port_multixform[ctx->idf("WEA[%d]", i)] = {};
    }

    // 72-bit BRAMs: drop upper bits of WEB in TDP mode
    for (int i = 4; i < 8; i++)
        bram_rules[id_RAMB36E1].port_multixform[ctx->idf("WEBWE[%d]", i)] = {};

    // Process SDP BRAM first
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if ((ci->type == id_RAMB18E1 && int_or_default(ci->params, ctx->id("WRITE_WIDTH_B"), 0) == 36) ||
            (ci->type == id_RAMB36E1 && int_or_default(ci->params, ctx->id("WRITE_WIDTH_B"), 0) == 72))
            xform_cell(sdp_bram_rules, ci);
    }

    // Rewrite byte enables according to data width
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type.in(id_RAMB18E1, id_RAMB36E1)) {
            for (char port : {'A', 'B'}) {
                int write_width = int_or_default(ci->params, ctx->idf("WRITE_WIDTH_%c", port), 18);
                int we_width;
                if (ci->type == id_RAMB36E1)
                    we_width = 4;
                else
                    we_width = (port == 'B') ? 4 : 2;
                if (write_width >= (9 * we_width))
                    continue;
                int used_we_width = std::max(write_width / 9, 1);
                for (int i = used_we_width; i < we_width; i++) {
                    NetInfo *low_we = ci->getPort(ctx->id(std::string(port == 'B' ? "WEBWE[" : "WEA[") +
                                                          std::to_string(i % used_we_width) + "]"));
                    IdString curr_we = ctx->id(std::string(port == 'B' ? "WEBWE[" : "WEA[") + std::to_string(i) + "]");
                    if (!ci->ports.count(curr_we)) {
                        ci->ports[curr_we].type = PORT_IN;
                        ci->ports[curr_we].name = curr_we;
                    }
                    ci->disconnectPort(curr_we);
                    ci->connectPort(curr_we, low_we);
                }
            }
        }
    }

    generic_xform(bram_rules, false);

    // These pins have no logical mapping, so must be tied after transformation
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type == id_RAMB18E1_RAMB18E1) {
            int wwa = int_or_default(ci->params, id_WRITE_WIDTH_A, 0);
            for (int i = ((wwa == 0) ? 0 : 2); i < 4; i++) {
                IdString port = ctx->idf("WEA%d", i);
                if (!ci->ports.count(port)) {
                    ci->ports[port].name = port;
                    ci->ports[port].type = PORT_IN;
                    ci->connectPort(port, ctx->nets[ctx->id("$PACKER_GND_NET")].get());
                }
            }
            int wwb = int_or_default(ci->params, id_WRITE_WIDTH_B, 0);
            if (wwb != 36) {
                for (int i = 4; i < 8; i++) {
                    IdString port = ctx->id("WEBWE" + std::to_string(i));
                    if (!ci->ports.count(port)) {
                        ci->ports[port].name = port;
                        ci->ports[port].type = PORT_IN;
                        ci->connectPort(port, ctx->nets[ctx->id("$PACKER_GND_NET")].get());
                    }
                }
            }
            for (auto p : {id_ADDRATIEHIGH0, id_ADDRATIEHIGH1, id_ADDRBTIEHIGH0, id_ADDRBTIEHIGH1}) {
                if (!ci->ports.count(p)) {
                    ci->ports[p].name = p;
                    ci->ports[p].type = PORT_IN;
                } else {
                    ci->disconnectPort(p);
                }
                ci->connectPort(p, ctx->nets[ctx->id("$PACKER_VCC_NET")].get());
            }
        } else if (ci->type == id_RAMB36E1_RAMB36E1) {
            for (auto p : {id_ADDRARDADDRL15, id_ADDRBWRADDRL15}) {
                if (!ci->ports.count(p)) {
                    ci->ports[p].name = p;
                    ci->ports[p].type = PORT_IN;
                } else {
                    ci->disconnectPort(p);
                }
                ci->connectPort(p, ctx->nets[ctx->id("$PACKER_VCC_NET")].get());
            }
            if (int_or_default(ci->params, id_WRITE_WIDTH_A, 0) == 1) {
                ci->disconnectPort(id_DIADI1);
                ci->connectPort(id_DIADI1, ci->getPort(id_DIADI0));
                ci->attrs[id_X_ORIG_PORT_DIADI1] = std::string("DIADI[0]");
                ci->disconnectPort(id_DIPADIP0);
                ci->disconnectPort(id_DIPADIP1);
            }
            if (int_or_default(ci->params, id_WRITE_WIDTH_B, 0) == 1) {
                ci->disconnectPort(id_DIBDI1);
                ci->connectPort(id_DIBDI1, ci->getPort(id_DIBDI0));
                ci->attrs[id_X_ORIG_PORT_DIBDI1] = std::string("DIBDI[0]");
                ci->disconnectPort(id_DIPBDIP0);
                ci->disconnectPort(id_DIPBDIP1);
            }
            if (int_or_default(ci->params, id_WRITE_WIDTH_B, 0) != 72) {
                for (std::string s : {"L", "U"}) {
                    for (int i = 4; i < 8; i++) {
                        IdString port = ctx->idf("WEBWE%s%d", s.c_str(), i);
                        if (!ci->ports.count(port)) {
                            ci->ports[port].name = port;
                            ci->ports[port].type = PORT_IN;
                            ci->connectPort(port, ctx->nets[ctx->id("$PACKER_GND_NET")].get());
                        }
                    }
                }
            } else {
                // Tie WEA low
                for (std::string s : {"L", "U"}) {
                    for (int i = 0; i < 4; i++) {
                        IdString port = ctx->idf("WEA%s%d", s.c_str(), i);
                        if (!ci->ports.count(port)) {
                            ci->ports[port].name = port;
                            ci->ports[port].type = PORT_IN;
                            ci->connectPort(port, ctx->nets[ctx->id("$PACKER_GND_NET")].get());
                        }
                    }
                }
            }
        }
    }
}

void XilinxPacker::pack_inverters()
{
    // FIXME: fold where possible
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type == id_INV) {
            ci->params[id_INIT] = Property(1, 2);
            ci->renamePort(id_I, id_I0);
            ci->type = id_LUT1;
        }
    }
}

void XilinxImpl::pack()
{
    const ArchArgs &args = ctx->args;
    if (args.options.count("xdc")) {
        parse_xdc(args.options.at("xdc"));
    }

    XC7Packer packer(ctx, this);
    packer.pack_constants();
    packer.pack_inverters();
    packer.pack_io();
    packer.prepare_clocking();
    packer.pack_constants();
    // packer.pack_iologic();
    // packer.pack_idelayctrl();
    packer.pack_clocking();
    packer.pack_muxfs();
    packer.pack_carries();
    packer.pack_srls();
    packer.pack_luts();
    packer.pack_dram();
    packer.pack_bram();
    // packer.pack_dsps();
    packer.pack_ffs();
    packer.finalise_muxfs();
    packer.pack_lutffs();
}
NEXTPNR_NAMESPACE_END
