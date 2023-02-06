/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
 *  Copyright (C) 2018  Serge Bazanski <q3k@q3k.org>
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
#include <iterator>
#include "cells.h"
#include "chains.h"
#include "design_utils.h"
#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

// Pack LUTs and LUT-FF pairs
static void pack_lut_lutffs(Context *ctx)
{
    log_info("Packing LUT-FFs..\n");
    int lut_only = 0, lut_and_ff = 0;
    pool<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ctx->verbose)
            log_info("cell '%s' is of type '%s'\n", ci->name.c_str(ctx), ci->type.c_str(ctx));
        if (is_lut(ctx, ci)) {
            std::unique_ptr<CellInfo> packed = create_ice_cell(ctx, id_ICESTORM_LC, ci->name.str(ctx) + "_LC");
            for (auto &attr : ci->attrs)
                packed->attrs[attr.first] = attr.second;
            packed_cells.insert(ci->name);
            if (ctx->verbose)
                log_info("packed cell %s into %s\n", ci->name.c_str(ctx), packed->name.c_str(ctx));
            // See if we can pack into a DFF
            // TODO: LUT cascade
            auto port = ci->ports.find(id_O);
            bool packed_dff = false;
            if (port != ci->ports.end()) {
                NetInfo *o = port->second.net;
                CellInfo *dff = net_only_drives(ctx, o, is_ff, id_D, true);
                auto lut_bel = ci->attrs.find(id_BEL);
                if (dff) {
                    if (ctx->verbose)
                        log_info("found attached dff %s\n", dff->name.c_str(ctx));
                    auto dff_bel = dff->attrs.find(id_BEL);
                    if (lut_bel != ci->attrs.end() && dff_bel != dff->attrs.end() &&
                        lut_bel->second != dff_bel->second) {
                        // Locations don't match, can't pack
                    } else {
                        lut_to_lc(ctx, ci, packed.get(), false);
                        dff_to_lc(ctx, dff, packed.get(), false);
                        ++lut_and_ff;
                        ctx->nets.erase(o->name);
                        if (dff_bel != dff->attrs.end())
                            packed->attrs[id_BEL] = dff_bel->second;
                        for (const auto &attr : dff->attrs) {
                            // BEL is dealt with specially
                            if (attr.first != id_BEL)
                                packed->attrs[attr.first] = attr.second;
                        }
                        packed_cells.insert(dff->name);
                        if (ctx->verbose)
                            log_info("packed cell %s into %s\n", dff->name.c_str(ctx), packed->name.c_str(ctx));
                        packed_dff = true;
                    }
                }
            }
            if (!packed_dff) {
                lut_to_lc(ctx, ci, packed.get(), true);
                ++lut_only;
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
    log_info("    %4d LCs used as LUT4 only\n", lut_only);
    log_info("    %4d LCs used as LUT4 and DFF\n", lut_and_ff);
}

// Pack FFs not packed as LUTFFs
static void pack_nonlut_ffs(Context *ctx)
{
    log_info("Packing non-LUT FFs..\n");

    pool<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;
    int ff_only = 0;

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (is_ff(ctx, ci)) {
            std::unique_ptr<CellInfo> packed = create_ice_cell(ctx, id_ICESTORM_LC, ci->name.str(ctx) + "_DFFLC");
            for (auto &attr : ci->attrs)
                packed->attrs[attr.first] = attr.second;
            if (ctx->verbose)
                log_info("packed cell %s into %s\n", ci->name.c_str(ctx), packed->name.c_str(ctx));
            packed_cells.insert(ci->name);
            dff_to_lc(ctx, ci, packed.get(), true);
            new_cells.push_back(std::move(packed));
            ++ff_only;
        }
    }
    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    for (auto &ncell : new_cells) {
        ctx->cells[ncell->name] = std::move(ncell);
    }
    log_info("    %4d LCs used as DFF only\n", ff_only);
}

static bool net_is_constant(const Context *ctx, NetInfo *net, bool &value)
{
    auto gnd = ctx->id("$PACKER_GND_NET");
    auto vcc = ctx->id("$PACKER_VCC_NET");
    if (net == nullptr)
        return false;
    if (net->name.in(gnd, vcc)) {
        value = (net->name == vcc);
        return true;
    } else {
        return false;
    }
}

// Pack carry logic
static void pack_carries(Context *ctx)
{
    log_info("Packing carries..\n");
    pool<IdString> exhausted_cells;
    pool<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;
    int carry_only = 0;

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (is_carry(ctx, ci)) {
            packed_cells.insert(cell.first);

            CellInfo *carry_ci_lc;
            bool ci_value;
            bool ci_const = net_is_constant(ctx, ci->ports.at(id_CI).net, ci_value);
            if (ci_const) {
                carry_ci_lc = nullptr;
            } else {
                carry_ci_lc = net_only_drives(ctx, ci->ports.at(id_CI).net, is_lc, id_I3, false);
            }

            std::set<IdString> i0_matches, i1_matches;
            NetInfo *i0_net = ci->ports.at(id_I0).net;
            NetInfo *i1_net = ci->ports.at(id_I1).net;
            // Find logic cells connected to both I0 and I1
            if (i0_net) {
                for (auto usr : i0_net->users) {
                    if (is_lc(ctx, usr.cell) && usr.port == id_I1) {
                        if (ctx->cells.find(usr.cell->name) != ctx->cells.end() &&
                            exhausted_cells.find(usr.cell->name) == exhausted_cells.end()) {
                            // This clause stops us double-packing cells
                            i0_matches.insert(usr.cell->name);
                            if (!i1_net && !usr.cell->ports.at(id_I2).net) {
                                // I1 is don't care when disconnected, duplicate I0
                                i1_matches.insert(usr.cell->name);
                            }
                        }
                    }
                }
            }
            if (i1_net) {
                for (auto usr : i1_net->users) {
                    if (is_lc(ctx, usr.cell) && usr.port == id_I2) {
                        if (ctx->cells.find(usr.cell->name) != ctx->cells.end() &&
                            exhausted_cells.find(usr.cell->name) == exhausted_cells.end()) {
                            // This clause stops us double-packing cells
                            i1_matches.insert(usr.cell->name);
                            if (!i0_net && !usr.cell->ports.at(id_I1).net) {
                                // I0 is don't care when disconnected, duplicate I1
                                i0_matches.insert(usr.cell->name);
                            }
                        }
                    }
                }
            }

            std::set<IdString> carry_lcs;
            std::set_intersection(i0_matches.begin(), i0_matches.end(), i1_matches.begin(), i1_matches.end(),
                                  std::inserter(carry_lcs, carry_lcs.end()));
            CellInfo *carry_lc = nullptr;
            if (carry_ci_lc && carry_lcs.find(carry_ci_lc->name) != carry_lcs.end()) {
                carry_lc = carry_ci_lc;
            } else if (ci_const && carry_lcs.size() == 1) {
                carry_lc = ctx->cells.at(*(carry_lcs.begin())).get();
            } else {
                // No LC to pack into matching I0/I1, insert a new one
                std::unique_ptr<CellInfo> created_lc =
                        create_ice_cell(ctx, id_ICESTORM_LC, cell.first.str(ctx) + "$CARRY");
                carry_lc = created_lc.get();
                created_lc->ports.at(id_I1).net = i0_net;
                if (i0_net) {
                    PortRef pr;
                    pr.cell = created_lc.get();
                    pr.port = id_I1;
                    created_lc->ports.at(id_I1).user_idx = i0_net->users.add(pr);
                }
                created_lc->ports.at(id_I2).net = i1_net;
                if (i1_net) {
                    PortRef pr;
                    pr.cell = created_lc.get();
                    pr.port = id_I2;
                    created_lc->ports.at(id_I2).user_idx = i1_net->users.add(pr);
                }
                new_cells.push_back(std::move(created_lc));
                ++carry_only;
            }
            carry_lc->params[id_CARRY_ENABLE] = Property::State::S1;
            ci->movePortTo(id_CI, carry_lc, id_CIN);
            ci->movePortTo(id_CO, carry_lc, id_COUT);
            if (i0_net) {
                if (ci->ports.count(id_I0) && ci->ports.at(id_I0).user_idx)
                    i0_net->users.remove(ci->ports.at(id_I0).user_idx);
            }
            if (i1_net) {
                if (ci->ports.count(id_I1) && ci->ports.at(id_I1).user_idx)
                    i1_net->users.remove(ci->ports.at(id_I1).user_idx);
            }

            // Check for constant driver on CIN
            if (carry_lc->ports.at(id_CIN).net != nullptr) {
                IdString cin_net = carry_lc->ports.at(id_CIN).net->name;
                if (cin_net == ctx->id("$PACKER_GND_NET") || cin_net == ctx->id("$PACKER_VCC_NET")) {
                    carry_lc->params[id_CIN_CONST] = Property::State::S1;
                    carry_lc->params[id_CIN_SET] =
                            cin_net == ctx->id("$PACKER_VCC_NET") ? Property::State::S1 : Property::State::S0;
                    carry_lc->ports.at(id_CIN).net = nullptr;
                    auto &cin_users = ctx->nets.at(cin_net)->users;
                    cin_users.remove(carry_lc->ports.at(id_CIN).user_idx);
                }
            }
            exhausted_cells.insert(carry_lc->name);
        }
    }
    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    for (auto &ncell : new_cells) {
        ctx->cells[ncell->name] = std::move(ncell);
    }
    log_info("    %4d LCs used as CARRY only\n", carry_only);
}

static void merge_carry_luts(Context *ctx)
{
    // Find carrys
    log_info("Packing indirect carry+LUT pairs...\n");
    // Find cases where a less-than-LUT2 is driving a carry and pack them together
    //    +----+    +-----+ |
    // A--|LUT2|----|CARRY| |
    // B--|    |  C-|     |-+
    //    +----+  +-|     |
    //            | +-----+
    //            |
    pool<IdString> packed_cells;
    auto rewrite_init = [](unsigned lut_init) {
        // I0 -> LUT I2
        // I1, I2 -> carry; don't care
        // I3 -> LUT I3
        unsigned result = 0;
        for (unsigned i = 0; i < 16; i++) {
            unsigned j = 0;
            if ((i & 1))
                j |= 4;
            if ((i & 8))
                j |= 8;
            if (lut_init & (1 << j))
                result |= (1 << i);
        }
        return result;
    };
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type != id_ICESTORM_LC || !bool_or_default(ci->params, id_CARRY_ENABLE))
            continue; // not a carry LC
        if (ci->getPort(id_O))
            continue;                      // LUT output is already used
        for (auto port : {id_I1, id_I2}) { // check carry inputs
            NetInfo *i = ci->getPort(port);
            if (!i)
                continue;
            CellInfo *drv = i->driver.cell;
            if (i->driver.port != id_O)
                continue;
            if (!drv || drv->type != id_ICESTORM_LC || packed_cells.count(drv->name) ||
                bool_or_default(drv->params, id_CARRY_ENABLE) || bool_or_default(drv->params, id_DFF_ENABLE))
                continue; // not driven by a LUT, or driver already swallowed
            // Check cardinality - must be LUT2 or less, noting top inputs used first
            if (drv->getPort(id_I0) || drv->getPort(id_I1))
                continue;
            // Pack into carry
            drv->movePortTo(id_I2, ci, id_I0);
            drv->movePortTo(id_I3, ci, id_I3);
            drv->movePortTo(id_O, ci, id_O);
            ci->params[id_LUT_INIT] = Property(rewrite_init(int_or_default(drv->params, id_LUT_INIT)), 16);
            packed_cells.insert(drv->name);
            break;
        }
    }
    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    log_info("    %4d LUTs merged into carry LCs\n", int(packed_cells.size()));
}

// "Pack" RAMs
static void pack_ram(Context *ctx)
{
    log_info("Packing RAMs..\n");

    pool<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;

    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (is_ram(ctx, ci)) {
            std::unique_ptr<CellInfo> packed = create_ice_cell(ctx, id_ICESTORM_RAM, ci->name.str(ctx) + "_RAM");
            packed_cells.insert(ci->name);
            for (auto attr : ci->attrs)
                packed->attrs[attr.first] = attr.second;
            for (auto param : ci->params)
                packed->params[param.first] = param.second;
            packed->params[id_NEG_CLK_W] = Property(ci->type.in(id_SB_RAM40_4KNW, id_SB_RAM40_4KNRNW), 1);
            packed->params[id_NEG_CLK_R] = Property(ci->type.in(id_SB_RAM40_4KNR, id_SB_RAM40_4KNRNW), 1);
            packed->type = id_ICESTORM_RAM;
            for (auto port : ci->ports) {
                PortInfo &pi = port.second;
                std::string newname = pi.name.str(ctx);
                size_t bpos = newname.find('[');
                if (bpos != std::string::npos) {
                    newname = newname.substr(0, bpos) + "_" + newname.substr(bpos + 1, (newname.size() - bpos) - 2);
                }
                if (pi.name == id_RCLKN)
                    newname = "RCLK";
                else if (pi.name == id_WCLKN)
                    newname = "WCLK";
                ci->movePortTo(ctx->id(pi.name.c_str(ctx)), packed.get(), ctx->id(newname));
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

// Merge a net into a constant net
static void set_net_constant(const Context *ctx, NetInfo *orig, NetInfo *constnet, bool constval)
{
    orig->driver.cell = nullptr;
    for (auto user : orig->users) {
        if (user.cell != nullptr) {
            CellInfo *uc = user.cell;
            if (ctx->verbose)
                log_info("%s user %s\n", orig->name.c_str(ctx), uc->name.c_str(ctx));
            if ((is_lut(ctx, uc) || is_lc(ctx, uc) || is_carry(ctx, uc)) && (user.port.str(ctx).at(0) == 'I') &&
                !constval) {
                uc->ports[user.port].net = nullptr;
                uc->ports[user.port].user_idx = {};
            } else if ((is_sb_mac16(ctx, uc) || uc->type == id_ICESTORM_DSP) &&
                       (user.port != id_CLK &&
                        ((constval && user.port == id_CE) || (!constval && user.port != id_CE)))) {
                uc->ports[user.port].net = nullptr;
                uc->ports[user.port].user_idx = {};
            } else if (is_ram(ctx, uc) && !constval && user.port != id_RCLK && user.port != id_RCLKN &&
                       user.port != id_WCLK && user.port != id_WCLKN && user.port != id_RCLKE &&
                       user.port != id_WCLKE) {
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

    std::unique_ptr<CellInfo> gnd_cell = create_ice_cell(ctx, id_ICESTORM_LC, "$PACKER_GND");
    gnd_cell->params[id_LUT_INIT] = Property(0, 16);
    auto gnd_net = std::make_unique<NetInfo>(ctx->id("$PACKER_GND_NET"));
    gnd_net->driver.cell = gnd_cell.get();
    gnd_net->driver.port = id_O;
    gnd_cell->ports.at(id_O).net = gnd_net.get();

    NetInfo *gnd_net_info = gnd_net.get();
    if (ctx->nets.find(ctx->id("$PACKER_GND_NET")) != ctx->nets.end()) {
        gnd_net_info = ctx->nets.find(ctx->id("$PACKER_GND_NET"))->second.get();
    }

    std::unique_ptr<CellInfo> vcc_cell = create_ice_cell(ctx, id_ICESTORM_LC, "$PACKER_VCC");
    vcc_cell->params[id_LUT_INIT] = Property(1, 16);
    auto vcc_net = std::make_unique<NetInfo>(ctx->id("$PACKER_VCC_NET"));
    vcc_net->driver.cell = vcc_cell.get();
    vcc_net->driver.port = id_O;
    vcc_cell->ports.at(id_O).net = vcc_net.get();

    NetInfo *vcc_net_info = vcc_net.get();
    if (ctx->nets.find(ctx->id("$PACKER_VCC_NET")) != ctx->nets.end()) {
        vcc_net_info = ctx->nets.find(ctx->id("$PACKER_VCC_NET"))->second.get();
    }

    std::vector<IdString> dead_nets;

    bool gnd_used = false;

    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        if (ni->driver.cell != nullptr && ni->driver.cell->type == id_GND) {
            IdString drv_cell = ni->driver.cell->name;
            set_net_constant(ctx, ni, gnd_net_info, false);
            gnd_used = true;
            dead_nets.push_back(net.first);
            ctx->cells.erase(drv_cell);
        } else if (ni->driver.cell != nullptr && ni->driver.cell->type == id_VCC) {
            IdString drv_cell = ni->driver.cell->name;
            set_net_constant(ctx, ni, vcc_net_info, true);
            dead_nets.push_back(net.first);
            ctx->cells.erase(drv_cell);
        }
    }

    if (gnd_used && (gnd_net_info == gnd_net.get())) {
        ctx->cells[gnd_cell->name] = std::move(gnd_cell);
        ctx->nets[gnd_net->name] = std::move(gnd_net);
    }
    // Vcc cell always inserted for now, as it may be needed during carry legalisation (TODO: trim later if actually
    // never used?)
    if (vcc_net_info == vcc_net.get()) {
        ctx->cells[vcc_cell->name] = std::move(vcc_cell);
        ctx->nets[vcc_net->name] = std::move(vcc_net);
    }

    for (auto dn : dead_nets) {
        ctx->nets.erase(dn);
    }
}

static BelId find_padin_gbuf(Context *ctx, BelId bel, IdString port_name)
{
    BelId gb_bel;
    auto wire = ctx->getBelPinWire(bel, port_name);

    if (wire == WireId())
        log_error("BEL '%s' has no global buffer connection available\n", ctx->nameOfBel(bel));

    for (auto src_bel : ctx->getWireBelPins(wire)) {
        if (ctx->getBelType(src_bel.bel) == id_SB_GB && src_bel.pin == id_GLOBAL_BUFFER_OUTPUT) {
            gb_bel = src_bel.bel;
            break;
        }
    }

    return gb_bel;
}

static std::unique_ptr<CellInfo> create_padin_gbuf(Context *ctx, CellInfo *cell, IdString port_name,
                                                   std::string gbuf_name)
{
    // Find the matching SB_GB BEL connected to the same global network
    if (!cell->attrs.count(id_BEL))
        log_error("Unconstrained SB_GB_IO %s is not supported.\n", ctx->nameOf(cell));
    BelId bel = ctx->getBelByNameStr(cell->attrs[id_BEL].as_string());
    BelId gb_bel = find_padin_gbuf(ctx, bel, port_name);
    NPNR_ASSERT(gb_bel != BelId());

    // Create a SB_GB Cell and lock it there
    std::unique_ptr<CellInfo> gb = create_ice_cell(ctx, id_SB_GB, gbuf_name);
    gb->attrs[id_FOR_PAD_IN] = Property::State::S1;
    gb->attrs[id_BEL] = ctx->getBelName(gb_bel).str(ctx);

    // Reconnect the net to that port for easier identification it's a global net
    cell->movePortTo(port_name, gb.get(), id_GLOBAL_BUFFER_OUTPUT);

    return gb;
}

static bool is_nextpnr_iob(Context *ctx, CellInfo *cell)
{
    return cell->type == ctx->id("$nextpnr_ibuf") || cell->type == ctx->id("$nextpnr_obuf") ||
           cell->type == ctx->id("$nextpnr_iobuf");
}

static bool is_ice_iob(const Context *ctx, const CellInfo *cell)
{
    return is_sb_io(ctx, cell) || is_sb_gb_io(ctx, cell);
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
        if (is_nextpnr_iob(ctx, ci)) {
            CellInfo *sb = nullptr, *rgb = nullptr;
            if (ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_iobuf")) {
                sb = net_only_drives(ctx, ci->ports.at(id_O).net, is_ice_iob, id_PACKAGE_PIN, true, ci);

            } else if (ci->type == ctx->id("$nextpnr_obuf")) {
                NetInfo *net = ci->ports.at(id_I).net;
                sb = net_only_drives(ctx, net, is_ice_iob, id_PACKAGE_PIN, true, ci);
                if (net && net->driver.cell &&
                    (is_sb_rgba_drv(ctx, net->driver.cell) || is_sb_rgb_drv(ctx, net->driver.cell)))
                    rgb = net->driver.cell;
            }
            if (sb != nullptr) {
                // Trivial case, SB_IO used. Just destroy the iobuf
                log_info("%s feeds SB_IO %s, removing %s %s.\n", ci->name.c_str(ctx), sb->name.c_str(ctx),
                         ci->type.c_str(ctx), ci->name.c_str(ctx));
                NetInfo *net = sb->ports.at(id_PACKAGE_PIN).net;
                if (((ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_iobuf")) &&
                     net->users.entries() > 1) ||
                    (ci->type == ctx->id("$nextpnr_obuf") && (net->users.entries() > 2 || net->driver.cell != nullptr)))
                    log_error("PACKAGE_PIN of %s '%s' connected to more than a single top level IO.\n",
                              sb->type.c_str(ctx), sb->name.c_str(ctx));

                if (net != nullptr) {
                    if (net->clkconstr != nullptr) {
                        if (sb->ports.count(id_D_IN_0)) {
                            NetInfo *din0_net = sb->ports.at(id_D_IN_0).net;
                            if (din0_net != nullptr && !din0_net->clkconstr) {
                                // Copy clock constraint from IO pad to input buffer output
                                din0_net->clkconstr =
                                        std::unique_ptr<ClockConstraint>(new ClockConstraint(*net->clkconstr));
                            }
                        }
                        if (is_sb_gb_io(ctx, sb) && sb->ports.count(id_GLOBAL_BUFFER_OUTPUT)) {
                            NetInfo *gb_net = sb->ports.at(id_GLOBAL_BUFFER_OUTPUT).net;
                            if (gb_net != nullptr && !gb_net->clkconstr) {
                                // Copy clock constraint from IO pad to global buffer output
                                gb_net->clkconstr =
                                        std::unique_ptr<ClockConstraint>(new ClockConstraint(*net->clkconstr));
                            }
                        }
                    }
                }
            } else if (rgb != nullptr) {
                log_info("%s use by SB_RGBA_DRV/SB_RGB_DRV %s, not creating SB_IO\n", ci->name.c_str(ctx),
                         rgb->name.c_str(ctx));
                ci->disconnectPort(id_I);
                packed_cells.insert(ci->name);
                continue;
            } else {
                // Create a SB_IO buffer
                std::unique_ptr<CellInfo> ice_cell = create_ice_cell(ctx, id_SB_IO, ci->name.str(ctx) + "$sb_io");
                nxio_to_sb(ctx, ci, ice_cell.get(), packed_cells);
                new_cells.push_back(std::move(ice_cell));
                sb = new_cells.back().get();
            }
            for (auto port : ci->ports)
                ci->disconnectPort(port.first);
            packed_cells.insert(ci->name);
            for (auto &attr : ci->attrs)
                sb->attrs[attr.first] = attr.second;
        } else if (is_sb_io(ctx, ci) || is_sb_gb_io(ctx, ci)) {
            NetInfo *net = ci->ports.at(id_PACKAGE_PIN).net;
            if ((net != nullptr) && ((net->users.entries() > 2) ||
                                     (net->driver.cell != nullptr &&
                                      net->driver.cell->type == ctx->id("$nextpnr_obuf") && net->users.entries() > 1)))
                log_error("PACKAGE_PIN of %s '%s' connected to more than a single top level IO.\n", ci->type.c_str(ctx),
                          ci->name.c_str(ctx));
        }
    }
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (is_sb_gb_io(ctx, ci)) {
            // If something is connecto the GLOBAL OUTPUT, create the fake 'matching' SB_GB
            std::unique_ptr<CellInfo> gb =
                    create_padin_gbuf(ctx, ci, id_GLOBAL_BUFFER_OUTPUT, "$gbuf_" + ci->name.str(ctx) + "_io");
            new_cells.push_back(std::move(gb));

            // Make it a normal SB_IO with global marker
            ci->type = id_SB_IO;
            ci->attrs[id_GLOBAL] = Property::State::S1;
        } else if (is_sb_io(ctx, ci)) {
            // Disconnect unused inputs
            NetInfo *net_in0 = ci->ports.count(id_D_IN_0) ? ci->ports[id_D_IN_0].net : nullptr;
            NetInfo *net_in1 = ci->ports.count(id_D_IN_1) ? ci->ports[id_D_IN_1].net : nullptr;

            if (net_in0 != nullptr && net_in0->users.entries() == 0) {
                delete_nets.insert(net_in0->name);
                ci->ports[id_D_IN_0].net = nullptr;
            }
            if (net_in1 != nullptr && net_in1->users.entries() == 0) {
                delete_nets.insert(net_in1->name);
                ci->ports[id_D_IN_1].net = nullptr;
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

// Return true if a port counts as "logic" for global promotion
static bool is_logic_port(BaseCtx *ctx, const PortRef &port)
{
    if (is_clock_port(ctx, port) || is_reset_port(ctx, port) || is_enable_port(ctx, port))
        return false;
    return !is_sb_io(ctx, port.cell) && !is_sb_gb_io(ctx, port.cell) && !is_gbuf(ctx, port.cell) &&
           !is_sb_pll40(ctx, port.cell);
}

static void insert_global(Context *ctx, NetInfo *net, bool is_reset, bool is_cen, bool is_logic, int fanout)
{
    log_info("promoting %s%s%s%s (fanout %d)\n", net->name.c_str(ctx), is_reset ? " [reset]" : "",
             is_cen ? " [cen]" : "", is_logic ? " [logic]" : "", fanout);

    std::string glb_name = net->name.str(ctx) + std::string("_$glb_") + (is_reset ? "sr" : (is_cen ? "ce" : "clk"));
    std::unique_ptr<CellInfo> gb = create_ice_cell(ctx, id_SB_GB, "$gbuf_" + glb_name);
    gb->connectPort(id_USER_SIGNAL_TO_GLOBAL_BUFFER, net);
    NetInfo *glbnet = ctx->createNet(ctx->id(glb_name));
    gb->connectPort(id_GLOBAL_BUFFER_OUTPUT, glbnet);

    std::vector<PortRef> keep_users;
    for (auto user : net->users) {
        if (is_clock_port(ctx, user) || (is_reset && is_reset_port(ctx, user)) ||
            (is_cen && is_enable_port(ctx, user)) || (is_logic && is_logic_port(ctx, user))) {
            user.cell->ports[user.port].net = glbnet;
            user.cell->ports[user.port].user_idx = glbnet->users.add(user);
        } else {
            keep_users.push_back(user);
        }
    }
    net->users.clear();
    for (auto &user : keep_users)
        user.cell->ports[user.port].user_idx = net->users.add(user);

    ctx->cells[gb->name] = std::move(gb);
}

// Simple global promoter (clock only)
static void promote_globals(Context *ctx)
{
    log_info("Promoting globals..\n");
    const int logic_fanout_thresh = 15;
    const int enable_fanout_thresh = 15;
    const int reset_fanout_thresh = 15;
    std::map<IdString, int> clock_count, reset_count, cen_count, logic_count;
    for (auto &net : ctx->nets) {
        NetInfo *ni = net.second.get();
        if (ni->driver.cell != nullptr && !ctx->is_global_net(ni)) {
            clock_count[net.first] = 0;
            reset_count[net.first] = 0;
            cen_count[net.first] = 0;

            for (auto user : ni->users) {
                if (is_clock_port(ctx, user))
                    clock_count[net.first]++;
                if (is_reset_port(ctx, user))
                    reset_count[net.first]++;
                if (is_enable_port(ctx, user))
                    cen_count[net.first]++;
                if (is_logic_port(ctx, user))
                    logic_count[net.first]++;
            }
        }
    }
    int prom_globals = 0, prom_resets = 0, prom_cens = 0, prom_logics = 0;
    int gbs_available = 8, resets_available = 4, cens_available = 4;
    for (auto &cell : ctx->cells)
        if (is_gbuf(ctx, cell.second.get())) {
            /* One less buffer available */
            --gbs_available;

            /* And possibly limits what we can promote */
            if (cell.second->attrs.find(id_BEL) != cell.second->attrs.end()) {
                /* If the SB_GB is locked, doesn't matter what it drives */
                BelId bel = ctx->getBelByNameStr(cell.second->attrs[id_BEL].as_string());
                int glb_id = ctx->get_driven_glb_netwk(bel);
                if ((glb_id % 2) == 0)
                    resets_available--;
                else if ((glb_id % 2) == 1)
                    cens_available--;
            } else {
                /* If it's free to move around, then look at what it drives */
                NetInfo *ni = cell.second->ports[id_GLOBAL_BUFFER_OUTPUT].net;

                for (auto user : ni->users) {
                    if (is_reset_port(ctx, user)) {
                        resets_available--;
                        break;
                    } else if (is_enable_port(ctx, user)) {
                        cens_available--;
                        break;
                    }
                }
            }
        }
    while (prom_globals < gbs_available) {
        auto global_clock = std::max_element(clock_count.begin(), clock_count.end(),
                                             [](const std::pair<IdString, int> &a, const std::pair<IdString, int> &b) {
                                                 return a.second < b.second;
                                             });

        auto global_reset = std::max_element(reset_count.begin(), reset_count.end(),
                                             [](const std::pair<IdString, int> &a, const std::pair<IdString, int> &b) {
                                                 return a.second < b.second;
                                             });
        auto global_cen = std::max_element(cen_count.begin(), cen_count.end(),
                                           [](const std::pair<IdString, int> &a, const std::pair<IdString, int> &b) {
                                               return a.second < b.second;
                                           });
        auto global_logic = std::max_element(logic_count.begin(), logic_count.end(),
                                             [](const std::pair<IdString, int> &a, const std::pair<IdString, int> &b) {
                                                 return a.second < b.second;
                                             });
        if (global_clock->second == 0 && prom_logics < 4 && global_logic->second > logic_fanout_thresh &&
            (global_logic->second > global_cen->second || prom_cens >= cens_available) &&
            (global_logic->second > global_reset->second || prom_resets >= resets_available) &&
            bool_or_default(ctx->settings, id_promote_logic, false)) {
            NetInfo *logicnet = ctx->nets[global_logic->first].get();
            insert_global(ctx, logicnet, false, false, true, global_logic->second);
            ++prom_globals;
            ++prom_logics;
            clock_count.erase(logicnet->name);
            reset_count.erase(logicnet->name);
            cen_count.erase(logicnet->name);
            logic_count.erase(logicnet->name);
        } else if (global_reset->second > global_clock->second && prom_resets < resets_available &&
                   global_reset->second > reset_fanout_thresh) {
            NetInfo *rstnet = ctx->nets[global_reset->first].get();
            insert_global(ctx, rstnet, true, false, false, global_reset->second);
            ++prom_globals;
            ++prom_resets;
            clock_count.erase(rstnet->name);
            reset_count.erase(rstnet->name);
            cen_count.erase(rstnet->name);
            logic_count.erase(rstnet->name);
        } else if (global_cen->second > global_clock->second && prom_cens < cens_available &&
                   global_cen->second > enable_fanout_thresh) {
            NetInfo *cennet = ctx->nets[global_cen->first].get();
            insert_global(ctx, cennet, false, true, false, global_cen->second);
            ++prom_globals;
            ++prom_cens;
            clock_count.erase(cennet->name);
            reset_count.erase(cennet->name);
            cen_count.erase(cennet->name);
            logic_count.erase(cennet->name);
        } else if (global_clock->second != 0) {
            NetInfo *clknet = ctx->nets[global_clock->first].get();
            insert_global(ctx, clknet, false, false, false, global_clock->second);
            ++prom_globals;
            clock_count.erase(clknet->name);
            reset_count.erase(clknet->name);
            cen_count.erase(clknet->name);
            logic_count.erase(clknet->name);
        } else {
            break;
        }
    }
}

static void copy_gb_constraints(Context *ctx)
{
    // Copy constraints through GBs and PLLs
    bool did_something = false;
    do {
        did_something = false;
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (!is_gbuf(ctx, ci))
                continue;
            NetInfo *in = ci->getPort(id_USER_SIGNAL_TO_GLOBAL_BUFFER);
            NetInfo *out = ci->getPort(id_GLOBAL_BUFFER_OUTPUT);
            if (in && out && in->clkconstr && !out->clkconstr) {
                out->clkconstr = std::unique_ptr<ClockConstraint>(new ClockConstraint());
                out->clkconstr->low = in->clkconstr->low;
                out->clkconstr->high = in->clkconstr->high;
                out->clkconstr->period = in->clkconstr->period;
                did_something = true;
            }
        }
    } while (did_something);
}

// Figure out where to place PLLs
static void place_plls(Context *ctx)
{
    std::map<BelId, std::tuple<BelPin, BelId, BelPin, BelId>> pll_all_bels;
    std::map<BelId, CellInfo *> pll_used_bels;
    std::vector<CellInfo *> pll_cells;
    std::map<BelId, CellInfo *> bel2io;
    std::map<BelId, CellInfo *> bel2gb;

    log_info("Placing PLLs..\n");

    // Find all the PLLs BELs and matching IO sites and global networks
    for (auto bel : ctx->getBels()) {
        if (ctx->getBelType(bel) != id_ICESTORM_PLL)
            continue;
        if (ctx->is_bel_locked(bel))
            continue;

        auto io_a_pin = ctx->get_iob_sharing_pll_pin(bel, id_PLLOUT_A);
        auto io_b_pin = ctx->get_iob_sharing_pll_pin(bel, id_PLLOUT_B);
        auto gb_a = find_padin_gbuf(ctx, bel, id_PLLOUT_A_GLOBAL);
        auto gb_b = find_padin_gbuf(ctx, bel, id_PLLOUT_B_GLOBAL);

        pll_all_bels[bel] = std::make_tuple(io_a_pin, gb_a, io_b_pin, gb_b);
    }

    // Find all the PLLs cells we need to place and do pre-checks
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (!is_sb_pll40(ctx, ci))
            continue;

        // If it's constrained already, add to already used list
        if (ci->attrs.count(id_BEL)) {
            BelId bel_constrain = ctx->getBelByNameStr(ci->attrs[id_BEL].as_string());
            if (pll_all_bels.count(bel_constrain) == 0)
                log_error("PLL '%s' is constrained to invalid BEL '%s'\n", ci->name.c_str(ctx),
                          ci->attrs[id_BEL].as_string().c_str());
            pll_used_bels[bel_constrain] = ci;
        }

        // Add it to our list of PLLs to process
        pll_cells.push_back(ci);
    }

    // Scan all the PAD PLLs
    for (auto ci : pll_cells) {
        if (!is_sb_pll40_pad(ctx, ci))
            continue;

        // Check PACKAGEPIN connection
        if (!ci->ports.count(id_PACKAGEPIN))
            log_error("PLL '%s' is of PAD type but doesn't have a PACKAGEPIN port\n", ci->name.c_str(ctx));

        NetInfo *ni = ci->ports.at(id_PACKAGEPIN).net;
        if (ni == nullptr || ni->driver.cell == nullptr)
            log_error("PLL '%s' is of PAD type but doesn't have a valid PACKAGEPIN connection\n", ci->name.c_str(ctx));

        CellInfo *io_cell = ni->driver.cell;
        if (io_cell->type != id_SB_IO || ni->driver.port != id_D_IN_0)
            log_error("PLL '%s' has a PACKAGEPIN driven by an %s, should be directly connected to an input "
                      "SB_IO.D_IN_0 port\n",
                      ci->name.c_str(ctx), io_cell->type.c_str(ctx));
        if (ni->users.entries() != 1)
            log_error("PLL '%s' clock input '%s' can only drive PLL\n", ci->name.c_str(ctx), ni->name.c_str(ctx));
        if (!io_cell->attrs.count(id_BEL))
            log_error("PLL '%s' PACKAGEPIN SB_IO '%s' is unconstrained\n", ci->name.c_str(ctx),
                      io_cell->name.c_str(ctx));

        BelId io_bel = ctx->getBelByNameStr(io_cell->attrs.at(id_BEL).as_string());
        BelId found_bel;

        // Find the PLL BEL that would suit that connection
        for (auto pll_bel : pll_all_bels) {
            if (std::get<0>(pll_bel.second).bel == io_bel) {
                found_bel = pll_bel.first;
                break;
            }
        }

        if (found_bel == BelId())
            log_error("PLL '%s' PACKAGEPIN SB_IO '%s' is not connected to any PLL BEL\n", ci->name.c_str(ctx),
                      io_cell->name.c_str(ctx));
        if (pll_used_bels.count(found_bel)) {
            CellInfo *conflict_cell = pll_used_bels.at(found_bel);
            if (conflict_cell == ci)
                continue;
            log_error("PLL '%s' PACKAGEPIN forces it to BEL %s but BEL is already assigned to PLL '%s'\n",
                      ci->name.c_str(ctx), ctx->nameOfBel(found_bel), conflict_cell->name.c_str(ctx));
        }

        // Is it user constrained ?
        if (ci->attrs.count(id_BEL)) {
            // Yes. Check it actually matches !
            BelId bel_constrain = ctx->getBelByNameStr(ci->attrs[id_BEL].as_string());
            if (bel_constrain != found_bel)
                log_error("PLL '%s' is user constrained to %s but can only be placed in %s based on its PACKAGEPIN "
                          "connection\n",
                          ci->name.c_str(ctx), ctx->nameOfBel(bel_constrain), ctx->nameOfBel(found_bel));
        } else {
            // No, we can constrain it ourselves
            ci->attrs[id_BEL] = ctx->getBelName(found_bel).str(ctx);
            pll_used_bels[found_bel] = ci;
        }

        // Inform user
        log_info("  constrained PLL '%s' to %s\n", ci->name.c_str(ctx), ctx->nameOfBel(found_bel));
    }

    // Scan all SB_IOs to check for conflict with PLL BELs
    for (auto &io_cell : ctx->cells) {
        CellInfo *io_ci = io_cell.second.get();
        if (!is_sb_io(ctx, io_ci))
            continue;

        // Only consider bound IO that are used as inputs
        if (!io_ci->attrs.count(id_BEL))
            continue;
        if ((!io_ci->ports.count(id_D_IN_0) || (io_ci->ports[id_D_IN_0].net == nullptr)) &&
            (!io_ci->ports.count(id_D_IN_1) || (io_ci->ports[id_D_IN_1].net == nullptr)) &&
            !bool_or_default(io_ci->attrs, id_GLOBAL))
            continue;

        // Check all placed PLL (either forced by user, or forced by PACKAGEPIN)
        BelId io_bel = ctx->getBelByNameStr(io_ci->attrs[id_BEL].as_string());

        for (auto placed_pll : pll_used_bels) {
            BelPin pll_io_a, pll_io_b;
            BelId gb_a, gb_b;
            std::tie(pll_io_a, gb_a, pll_io_b, gb_b) = pll_all_bels[placed_pll.first];
            if (io_bel == pll_io_a.bel) {
                // All the PAD type PLL stuff already checked above,so only
                // check for conflict with a user placed CORE PLL
                if (!is_sb_pll40_pad(ctx, placed_pll.second))
                    log_error("PLL '%s' A output conflict with SB_IO '%s' that's used as input\n",
                              placed_pll.second->name.c_str(ctx), io_cell.second->name.c_str(ctx));
            } else if (io_bel == pll_io_b.bel) {
                if (is_sb_pll40_dual(ctx, placed_pll.second))
                    log_error("PLL '%s' B output conflicts with SB_IO '%s' that's used as input\n",
                              placed_pll.second->name.c_str(ctx), io_cell.second->name.c_str(ctx));
            }
        }

        // Save for later checks
        bel2io[io_bel] = io_ci;
    }

    // Scan all SB_GBs to check for conflicts with PLL BELs
    for (auto &gb_cell : ctx->cells) {
        CellInfo *gb_ci = gb_cell.second.get();
        if (!is_gbuf(ctx, gb_ci))
            continue;

        // Only consider the bound ones
        if (!gb_ci->attrs.count(id_BEL))
            continue;

        // Check all placed PLL (either forced by user, or forced by PACKAGEPIN)
        BelId gb_bel = ctx->getBelByNameStr(gb_ci->attrs[id_BEL].as_string());

        for (auto placed_pll : pll_used_bels) {
            CellInfo *ci = placed_pll.second;

            // Used global connections
            bool gb_a_used = ci->ports.count(id_PLLOUT_A_GLOBAL) && (ci->ports[id_PLLOUT_A_GLOBAL].net != nullptr) &&
                             (ci->ports[id_PLLOUT_A_GLOBAL].net->users.entries() > 0);
            bool gb_b_used = is_sb_pll40_dual(ctx, ci) && ci->ports.count(id_PLLOUT_B_GLOBAL) &&
                             (ci->ports[id_PLLOUT_B_GLOBAL].net != nullptr) &&
                             (ci->ports[id_PLLOUT_B_GLOBAL].net->users.entries() > 0);

            // Check for conflict
            BelPin pll_io_a, pll_io_b;
            BelId gb_a, gb_b;
            std::tie(pll_io_a, gb_a, pll_io_b, gb_b) = pll_all_bels[placed_pll.first];
            if (gb_a_used && (gb_bel == gb_a)) {
                log_error("PLL '%s' A output conflict with SB_GB '%s'\n", placed_pll.second->name.c_str(ctx),
                          gb_cell.second->name.c_str(ctx));
            }
            if (gb_b_used && (gb_bel == gb_b)) {
                log_error("PLL '%s' B output conflicts with SB_GB '%s'\n", placed_pll.second->name.c_str(ctx),
                          gb_cell.second->name.c_str(ctx));
            }
        }

        // Save for later checks
        bel2gb[gb_bel] = gb_ci;
    }

    // Scan all the CORE PLLs and place them in remaining available PLL BELs
    // (in two pass ... first do the dual ones, harder to place, then single port)
    for (int i = 0; i < 2; i++) {
        for (auto ci : pll_cells) {
            if (is_sb_pll40_pad(ctx, ci))
                continue;
            if (is_sb_pll40_dual(ctx, ci) ^ i)
                continue;

            // Check REFERENCECLK connection
            if (!ci->ports.count(id_REFERENCECLK))
                log_error("PLL '%s' is of CORE type but doesn't have a REFERENCECLK port\n", ci->name.c_str(ctx));

            NetInfo *ni = ci->ports.at(id_REFERENCECLK).net;
            if (ni == nullptr || ni->driver.cell == nullptr)
                log_error("PLL '%s' is of CORE type but doesn't have a valid REFERENCECLK connection\n",
                          ci->name.c_str(ctx));

            // Used global connections
            bool gb_a_used = ci->ports.count(id_PLLOUT_A_GLOBAL) && (ci->ports[id_PLLOUT_A_GLOBAL].net != nullptr) &&
                             (ci->ports[id_PLLOUT_A_GLOBAL].net->users.entries() > 0);
            bool gb_b_used = is_sb_pll40_dual(ctx, ci) && ci->ports.count(id_PLLOUT_B_GLOBAL) &&
                             (ci->ports[id_PLLOUT_B_GLOBAL].net != nullptr) &&
                             (ci->ports[id_PLLOUT_B_GLOBAL].net->users.entries() > 0);

            // Could this be a PAD PLL ?
            bool could_be_pad = false;
            BelId pad_bel;
            if (ni->users.entries() == 1 && is_sb_io(ctx, ni->driver.cell) && ni->driver.cell->attrs.count(id_BEL))
                pad_bel = ctx->getBelByNameStr(ni->driver.cell->attrs[id_BEL].as_string());

            // Find a BEL for it
            BelId found_bel;
            std::string conflict_str = "";
            for (auto bel_pll : pll_all_bels) {
                if (pll_used_bels.count(bel_pll.first)) {
                    conflict_str +=
                            stringf("    PLL bel '%s' is already used by '%s'.\n", ctx->nameOfBel(bel_pll.first),
                                    pll_used_bels.at(bel_pll.first)->name.c_str(ctx));
                    continue;
                }
                BelPin pll_io_a, pll_io_b;
                BelId gb_a, gb_b;
                std::tie(pll_io_a, gb_a, pll_io_b, gb_b) = bel_pll.second;
                if (bel2io.count(pll_io_a.bel)) {
                    if (pll_io_a.bel == pad_bel)
                        could_be_pad = !bel2io.count(pll_io_b.bel) || !is_sb_pll40_dual(ctx, ci);
                    auto conflict_pin = ctx->get_bel_package_pin(pll_io_a.bel);
                    conflict_str +=
                            stringf("    PLL bel '%s' cannot be used as it conflicts with input '%s' on pin '%s'.\n",
                                    ctx->nameOfBel(bel_pll.first), bel2io.at(pll_io_a.bel)->name.c_str(ctx),
                                    conflict_pin.c_str());
                    continue;
                }
                if (bel2io.count(pll_io_b.bel) && is_sb_pll40_dual(ctx, ci)) {
                    auto conflict_pin = ctx->get_bel_package_pin(pll_io_b.bel);
                    conflict_str +=
                            stringf("    PLL bel '%s' cannot be used as it conflicts with input '%s' on pin '%s'.\n",
                                    ctx->nameOfBel(bel_pll.first), bel2io.at(pll_io_b.bel)->name.c_str(ctx),
                                    conflict_pin.c_str());
                    continue;
                }
                if (gb_a_used && bel2gb.count(gb_a)) {
                    conflict_str += stringf(
                            "    PLL bel '%s' cannot be used as it conflicts with global buffer '%s' at '%s'.\n",
                            ctx->nameOfBel(bel_pll.first), bel2gb.at(gb_a)->name.c_str(ctx), ctx->nameOfBel(gb_a));
                    continue;
                }
                if (gb_b_used && bel2gb.count(gb_b)) {
                    conflict_str += stringf(
                            "    PLL bel '%s' cannot be used as it conflicts with global buffer '%s' at '%s'.\n",
                            ctx->nameOfBel(bel_pll.first), bel2gb.at(gb_b)->name.c_str(ctx), ctx->nameOfBel(gb_b));
                    continue;
                }
                found_bel = bel_pll.first;
                break;
            }

            // Apply constrain & Inform user of result
            if (found_bel == BelId()) {
                log_error("PLL '%s' couldn't be placed anywhere, no suitable BEL found.%s\n%s\n", ci->name.c_str(ctx),
                          could_be_pad ? " Did you mean to use a PAD PLL ?" : "", conflict_str.c_str());
            }

            log_info("  constrained PLL '%s' to %s\n", ci->name.c_str(ctx), ctx->nameOfBel(found_bel));
            if (could_be_pad)
                log_info("  (given its connections, this PLL could have been a PAD PLL)\n");

            ci->attrs[id_BEL] = ctx->getBelName(found_bel).str(ctx);
            pll_used_bels[found_bel] = ci;
        }
    }
}

// spliceLUT adds a pass-through LUT LC between the given cell's output port
// and either all users or only non_LUT users.
static std::unique_ptr<CellInfo> spliceLUT(Context *ctx, CellInfo *ci, IdString portId, bool onlyNonLUTs)
{
    auto port = ci->ports[portId];

    NPNR_ASSERT(port.net != nullptr);

    // Create pass-through LUT.
    std::unique_ptr<CellInfo> pt =
            create_ice_cell(ctx, id_ICESTORM_LC, ci->name.str(ctx) + "$nextpnr_" + portId.str(ctx) + "_lut_through");
    pt->params[id_LUT_INIT] = Property(65280, 16); // output is always I3

    // Create LUT output net.
    NetInfo *out_net = ctx->createNet(ctx->id(ci->name.str(ctx) + "$nextnr_" + portId.str(ctx) + "_lut_through_net"));
    out_net->driver.cell = pt.get();
    out_net->driver.port = id_O;
    pt->ports.at(id_O).net = out_net;

    // New users of the original cell's port
    std::vector<PortRef> new_users;
    for (const auto &user : port.net->users) {
        if (onlyNonLUTs && user.cell->type == id_ICESTORM_LC) {
            new_users.push_back(user);
            continue;
        }
        // Rewrite pointer into net in user.
        user.cell->ports[user.port].net = out_net;
        // Add user to net.
        PortRef pr;
        pr.cell = user.cell;
        pr.port = user.port;
        user.cell->ports[user.port].user_idx = out_net->users.add(pr);
    }

    // Add LUT to new users.
    PortRef pr;
    pr.cell = pt.get();
    pr.port = id_I3;
    new_users.push_back(pr);
    pt->ports.at(id_I3).net = port.net;

    // Replace users of the original net
    port.net->users.clear();
    for (auto &usr : new_users)
        usr.cell->ports.at(usr.port).user_idx = port.net->users.add(usr);

    return pt;
}

// Force placement for cells that are unique anyway
static BelId cell_place_unique(Context *ctx, CellInfo *ci)
{
    for (auto bel : ctx->getBels()) {
        if (ctx->getBelType(bel) != ci->type)
            continue;
        if (ctx->is_bel_locked(bel))
            continue;
        IdStringList bel_name = ctx->getBelName(bel);
        ci->attrs[id_BEL] = bel_name.str(ctx);
        log_info("  constrained %s '%s' to %s\n", ci->type.c_str(ctx), ci->name.c_str(ctx), ctx->nameOfBel(bel));
        return bel;
    }
    log_error("Unable to place cell '%s' of type '%s'\n", ci->name.c_str(ctx), ci->type.c_str(ctx));
}

namespace {
float MHz(Context *ctx, delay_t a) { return 1000.0f / ctx->getDelayNS(a); };
bool equals_epsilon(delay_t a, delay_t b) { return (std::abs(a - b) / std::max(double(b), 1.0)) < 1e-3; };
void set_period(Context *ctx, CellInfo *ci, IdString port, delay_t period)
{
    if (!ci->ports.count(port))
        return;
    NetInfo *to = ci->ports.at(port).net;
    if (to == nullptr)
        return;
    if (to->clkconstr != nullptr) {
        if (!equals_epsilon(to->clkconstr->period.maxDelay(), period))
            log_warning("    Overriding derived constraint of %.1f MHz on net %s with user-specified constraint of "
                        "%.1f MHz.\n",
                        MHz(ctx, period), to->name.c_str(ctx), MHz(ctx, to->clkconstr->period.maxDelay()));
        return;
    }
    to->clkconstr = std::unique_ptr<ClockConstraint>(new ClockConstraint());
    to->clkconstr->low = DelayPair(period / 2);
    to->clkconstr->high = DelayPair(period / 2);
    to->clkconstr->period = DelayPair(period);
    log_info("    Derived frequency constraint of %.1f MHz for net %s\n", MHz(ctx, to->clkconstr->period.maxDelay()),
             to->name.c_str(ctx));
};
bool get_period(Context *ctx, CellInfo *ci, IdString port, delay_t &period)
{
    if (!ci->ports.count(port))
        return false;
    NetInfo *from = ci->ports.at(port).net;
    if (from == nullptr || from->clkconstr == nullptr)
        return false;
    period = from->clkconstr->period.maxDelay();
    return true;
};

} // namespace

// Pack special functions
static void pack_special(Context *ctx)
{
    log_info("Packing special functions..\n");

    pool<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;

    // Handle LED_DRV_CUR first to set the ledCurConnected flag before RGB_DRV is handled below.
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (is_sb_led_drv_cur(ctx, ci)) {
            /* Force placement (no choices anyway) */
            cell_place_unique(ctx, ci);

            NetInfo *ledpu_net = ci->ports.at(id_LEDPU).net;
            for (auto &user : ledpu_net->users) {
                if (!is_sb_rgb_drv(ctx, user.cell)) {
                    log_error("SB_LED_DRV_CUR LEDPU port can only be connected to SB_RGB_DRV!\n");
                } else {
                    user.cell->ledInfo.ledCurConnected = true;
                    user.cell->ports.at(user.port).net = nullptr;
                }
            }
            ci->ports.erase(id_LEDPU);
            ctx->nets.erase(ledpu_net->name);
        }
    }
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (is_sb_lfosc(ctx, ci)) {
            std::unique_ptr<CellInfo> packed = create_ice_cell(ctx, id_ICESTORM_LFOSC, ci->name.str(ctx) + "_OSC");
            packed_cells.insert(ci->name);
            cell_place_unique(ctx, packed.get());
            ci->movePortTo(id_CLKLFEN, packed.get(), id_CLKLFEN);
            ci->movePortTo(id_CLKLFPU, packed.get(), id_CLKLFPU);
            if (bool_or_default(ci->attrs, id_ROUTE_THROUGH_FABRIC)) {
                ci->movePortTo(id_CLKLF, packed.get(), id_CLKLF_FABRIC);
                set_period(ctx, packed.get(), id_CLKLF_FABRIC, 100000000); // 10kHz
            } else {
                ci->movePortTo(id_CLKLF, packed.get(), id_CLKLF);
                std::unique_ptr<CellInfo> gb =
                        create_padin_gbuf(ctx, packed.get(), id_CLKLF, "$gbuf_" + ci->name.str(ctx) + "_lfosc");
                set_period(ctx, gb.get(), id_GLOBAL_BUFFER_OUTPUT, 100000000); // 10kHz
                new_cells.push_back(std::move(gb));
            }
            new_cells.push_back(std::move(packed));
        } else if (is_sb_hfosc(ctx, ci)) {
            std::unique_ptr<CellInfo> packed = create_ice_cell(ctx, id_ICESTORM_HFOSC, ci->name.str(ctx) + "_OSC");
            packed_cells.insert(ci->name);
            cell_place_unique(ctx, packed.get());
            packed->params[id_TRIM_EN] = str_or_default(ci->params, id_TRIM_EN, "0b0");
            packed->params[id_CLKHF_DIV] = str_or_default(ci->params, id_CLKHF_DIV, "0b00");
            ci->movePortTo(id_CLKHFEN, packed.get(), id_CLKHFEN);
            ci->movePortTo(id_CLKHFPU, packed.get(), id_CLKHFPU);
            for (int i = 0; i < 10; i++) {
                auto port = ctx->id("TRIM" + std::to_string(i));
                ci->movePortTo(port, packed.get(), port);
            }
            std::string div = packed->params[id_CLKHF_DIV].as_string();
            int frequency;
            if (div == "0b00")
                frequency = 48;
            else if (div == "0b01")
                frequency = 24;
            else if (div == "0b10")
                frequency = 12;
            else if (div == "0b11")
                frequency = 6;
            else
                log_error("Invalid HFOSC divider value '%s' - expecting 0b00, 0b01, 0b10 or 0b11\n", div.c_str());
            if (bool_or_default(ci->attrs, id_ROUTE_THROUGH_FABRIC)) {
                ci->movePortTo(id_CLKHF, packed.get(), id_CLKHF_FABRIC);
                set_period(ctx, packed.get(), id_CLKHF_FABRIC, 1000000 / frequency);
            } else {
                ci->movePortTo(id_CLKHF, packed.get(), id_CLKHF);
                std::unique_ptr<CellInfo> gb =
                        create_padin_gbuf(ctx, packed.get(), id_CLKHF, "$gbuf_" + ci->name.str(ctx) + "_hfosc");
                set_period(ctx, gb.get(), id_GLOBAL_BUFFER_OUTPUT, 1000000 / frequency);
                new_cells.push_back(std::move(gb));
            }
            new_cells.push_back(std::move(packed));
        } else if (is_sb_spram(ctx, ci)) {
            std::unique_ptr<CellInfo> packed = create_ice_cell(ctx, id_ICESTORM_SPRAM, ci->name.str(ctx) + "_RAM");
            packed_cells.insert(ci->name);
            for (auto attr : ci->attrs)
                packed->attrs[attr.first] = attr.second;
            for (auto port : ci->ports) {
                PortInfo &pi = port.second;
                std::string newname = pi.name.str(ctx);
                size_t bpos = newname.find('[');
                if (bpos != std::string::npos) {
                    newname = newname.substr(0, bpos) + "_" + newname.substr(bpos + 1, (newname.size() - bpos) - 2);
                }
                ci->movePortTo(ctx->id(pi.name.c_str(ctx)), packed.get(), ctx->id(newname));
            }
            new_cells.push_back(std::move(packed));
        } else if (is_sb_mac16(ctx, ci)) {
            std::unique_ptr<CellInfo> packed = create_ice_cell(ctx, id_ICESTORM_DSP, ci->name.str(ctx) + "_DSP");
            packed_cells.insert(ci->name);
            for (auto attr : ci->attrs)
                packed->attrs[attr.first] = attr.second;
            for (auto param : ci->params)
                packed->params[param.first] = param.second;

            for (auto port : ci->ports) {
                PortInfo &pi = port.second;
                std::string newname = pi.name.str(ctx);
                size_t bpos = newname.find('[');
                if (bpos != std::string::npos) {
                    newname = newname.substr(0, bpos) + "_" + newname.substr(bpos + 1, (newname.size() - bpos) - 2);
                }
                ci->movePortTo(ctx->id(pi.name.c_str(ctx)), packed.get(), ctx->id(newname));
            }
            new_cells.push_back(std::move(packed));
        } else if (is_sb_rgba_drv(ctx, ci) || is_sb_rgb_drv(ctx, ci)) {
            /* Force placement (no choices anyway) */
            cell_place_unique(ctx, ci);

            /* Disconnect all external ports and check there is no users (they should have been
             * dealth with during IO packing */
            for (auto port : ci->ports) {
                PortInfo &pi = port.second;
                NetInfo *net = pi.net;

                if (net == nullptr)
                    continue;

                if ((pi.name != id_RGB0) && (pi.name != id_RGB1) && (pi.name != id_RGB2))
                    continue;

                if (net->users.entries() > 0)
                    log_error("SB_RGB_DRV/SB_RGBA_DRV port connected to more than just package pin !\n");

                ctx->nets.erase(net->name);
            }

            if (is_sb_rgb_drv(ctx, ci) && !ci->ledInfo.ledCurConnected)
                log_error("Port RGBPU of SB_RGB_DRV should be driven by port LEDPU of SB_LED_DRV_CUR!\n");

            ci->ports.erase(id_RGBPU);
            ci->ports.erase(id_RGB0);
            ci->ports.erase(id_RGB1);
            ci->ports.erase(id_RGB2);
        } else if (is_sb_ledda_ip(ctx, ci)) {
            /* Force placement (no choices anyway) */
            cell_place_unique(ctx, ci);
        } else if (is_sb_i2c(ctx, ci) || is_sb_spi(ctx, ci)) {
            const std::map<std::tuple<IdString, std::string>, Loc> map_ba74 = {
                    {std::make_tuple(id_SB_SPI, "0b0000"), Loc(0, 0, 0)},
                    {std::make_tuple(id_SB_I2C, "0b0001"), Loc(0, 31, 0)},
                    {std::make_tuple(id_SB_SPI, "0b0010"), Loc(25, 0, 1)},
                    {std::make_tuple(id_SB_I2C, "0b0011"), Loc(25, 31, 0)},
            };
            std::string bus_addr74 =
                    str_or_default(ci->params, id_BUS_ADDR74, is_sb_i2c(ctx, ci) ? "0b0001" : "0b0000");
            if (map_ba74.find(std::make_tuple(ci->type, bus_addr74)) == map_ba74.end())
                log_error("Invalid value for BUS_ADDR74 for cell '%s' of type '%s'\n", ci->name.c_str(ctx),
                          ci->type.c_str(ctx));
            Loc bel_loc = map_ba74.at(std::make_tuple(ci->type, bus_addr74));
            BelId bel = ctx->getBelByLocation(bel_loc);
            if (bel == BelId() || ctx->getBelType(bel) != ci->type)
                log_error("Unable to find placement for cell '%s' of type '%s'\n", ci->name.c_str(ctx),
                          ci->type.c_str(ctx));
            IdStringList bel_name = ctx->getBelName(bel);
            ci->attrs[id_BEL] = bel_name.str(ctx);
            log_info("  constrained %s '%s' to %s\n", ci->type.c_str(ctx), ci->name.c_str(ctx), ctx->nameOfBel(bel));
        }
    }

    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    for (auto &ncell : new_cells) {
        ctx->cells[ncell->name] = std::move(ncell);
    }
}

void pack_plls(Context *ctx)
{
    log_info("Packing PLLs..\n");

    pool<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;
    for (auto &cell : ctx->cells) {
        CellInfo *ci = cell.second.get();
        if (is_sb_pll40(ctx, ci)) {
            bool is_pad = is_sb_pll40_pad(ctx, ci);
            bool is_core = !is_pad;

            std::unique_ptr<CellInfo> packed = create_ice_cell(ctx, id_ICESTORM_PLL, ci->name.str(ctx) + "_PLL");
            packed->attrs[id_TYPE] = ci->type.str(ctx);
            packed_cells.insert(ci->name);
            if (!is_sb_pll40_dual(ctx, ci)) {
                // Remove second output, so a buffer isn't created for it, for these
                // cell types with only one output
                packed->ports.erase(id_PLLOUT_B);
                packed->ports.erase(id_PLLOUT_B_GLOBAL);
            }
            for (auto attr : ci->attrs)
                packed->attrs[attr.first] = attr.second;
            for (auto param : ci->params)
                packed->params[param.first] = param.second;

            const std::map<IdString, IdString> pos_map_name = {
                    {id_PLLOUT_SELECT, id_PLLOUT_SELECT_A},
                    {id_PLLOUT_SELECT_PORTA, id_PLLOUT_SELECT_A},
                    {id_PLLOUT_SELECT_PORTB, id_PLLOUT_SELECT_B},
            };
            const std::map<std::string, int> pos_map_val = {
                    {"GENCLK", 0},
                    {"GENCLK_HALF", 1},
                    {"SHIFTREG_90deg", 2},
                    {"SHIFTREG_0deg", 3},
            };
            for (auto param : ci->params)
                if (pos_map_name.find(param.first) != pos_map_name.end()) {
                    if (pos_map_val.find(param.second.as_string()) == pos_map_val.end())
                        log_error("Invalid PLL output selection '%s'\n", param.second.as_string().c_str());
                    packed->params[pos_map_name.at(param.first)] = pos_map_val.at(param.second.as_string());
                }
            const std::map<IdString, std::pair<IdString, IdString>> delmodes = {
                    {id_DELAY_ADJUSTMENT_MODE_FEEDBACK, {id_DELAY_ADJMODE_FB, id_FDA_FEEDBACK}},
                    {id_DELAY_ADJUSTMENT_MODE_RELATIVE, {id_DELAY_ADJMODE_REL, id_FDA_RELATIVE}},
            };
            for (auto delmode : delmodes) {
                if (ci->params.count(delmode.first)) {
                    std::string value = str_or_default(ci->params, delmode.first, "");
                    if (value == "DYNAMIC") {
                        packed->params[delmode.second.first] = 1;
                        packed->params[delmode.second.second] = 15;
                    } else if (value == "FIXED")
                        packed->params[delmode.second.first] = 0;
                    else
                        log_error("Invalid PLL %s selection '%s'\n", delmode.first.c_str(ctx), value.c_str());
                }
            }
            auto feedback_path = packed->params[id_FEEDBACK_PATH].is_string
                                         ? packed->params[id_FEEDBACK_PATH].as_string()
                                         : std::to_string(packed->params[id_FEEDBACK_PATH].as_int64());
            std::string fbp_value = feedback_path == "DELAY"             ? "0"
                                    : feedback_path == "SIMPLE"          ? "1"
                                    : feedback_path == "PHASE_AND_DELAY" ? "2"
                                    : feedback_path == "EXTERNAL"        ? "6"
                                                                         : std::string(feedback_path);
            if (!std::all_of(fbp_value.begin(), fbp_value.end(), isdigit))
                log_error("PLL '%s' has unsupported FEEDBACK_PATH value '%s'\n", ci->name.c_str(ctx),
                          feedback_path.c_str());
            packed->params[id_FEEDBACK_PATH] = Property(std::stoi(fbp_value), 3);
            packed->params[id_PLLTYPE] = sb_pll40_type(ctx, ci);

            NetInfo *pad_packagepin_net = nullptr;

            bool got_input_constr = false;
            delay_t input_constr = 0;

            for (auto port : ci->ports) {
                PortInfo &pi = port.second;
                std::string newname = pi.name.str(ctx);
                size_t bpos = newname.find('[');
                if (bpos != std::string::npos) {
                    newname = newname.substr(0, bpos) + "_" + newname.substr(bpos + 1, (newname.size() - bpos) - 2);
                }

                if (pi.name.in(id_PLLOUTCOREA, id_PLLOUTCORE))
                    newname = "PLLOUT_A";
                if (pi.name == id_PLLOUTCOREB)
                    newname = "PLLOUT_B";
                if (pi.name.in(id_PLLOUTGLOBALA, id_PLLOUTGLOBAL))
                    newname = "PLLOUT_A_GLOBAL";
                if (pi.name == id_PLLOUTGLOBALB)
                    newname = "PLLOUT_B_GLOBAL";

                if (pi.name == id_PACKAGEPIN) {
                    if (!is_pad) {
                        log_error("PLL '%s' has a PACKAGEPIN but is not a PAD PLL\n", ci->name.c_str(ctx));
                    } else {
                        // We drop this port and instead place the PLL adequately below.
                        got_input_constr = get_period(ctx, ci, pi.name, input_constr);
                        pad_packagepin_net = port.second.net;
                        NPNR_ASSERT(pad_packagepin_net != nullptr);
                        continue;
                    }
                }
                if (pi.name == id_REFERENCECLK) {
                    if (!is_core)
                        log_error("PLL '%s' has a REFERENCECLK but is not a CORE PLL\n", ci->name.c_str(ctx));
                    got_input_constr = get_period(ctx, ci, pi.name, input_constr);
                }

                if (packed->ports.count(ctx->id(newname)) == 0) {
                    if (ci->ports[pi.name].net == nullptr) {
                        log_warning("PLL '%s' has unknown unconnected port '%s' - ignoring\n", ci->name.c_str(ctx),
                                    pi.name.c_str(ctx));
                        continue;
                    } else {
                        if (ctx->force) {
                            log_error("PLL '%s' has unknown connected port '%s'\n", ci->name.c_str(ctx),
                                      pi.name.c_str(ctx));
                        } else {
                            log_warning("PLL '%s' has unknown connected port '%s' - ignoring\n", ci->name.c_str(ctx),
                                        pi.name.c_str(ctx));
                            continue;
                        }
                    }
                }
                ci->movePortTo(ctx->id(pi.name.c_str(ctx)), packed.get(), ctx->id(newname));
            }

            // Compute derive constraints
            if (got_input_constr) {
                log_info("    Input frequency of PLL '%s' is constrained to %.1f MHz\n", ctx->nameOf(ci),
                         MHz(ctx, input_constr));
                // Input divider (DIVR)
                input_constr *= (int_or_default(packed->params, id_DIVR, 0) + 1);
                delay_t vco_constr = 0;
                delay_t outa_constr = 0, outb_constr = 0;
                int sr_div = 4;
                int divq = 0;

                // For dealing with the various output modes
                auto process_output = [&](IdString mode_param) {
                    int mode = int_or_default(packed->params, mode_param, 0);
                    switch (mode) {
                    case 0: // GENCLK
                        return vco_constr * divq;
                    case 1: // GENCLK_HALF
                        return vco_constr * divq * 2;
                    case 2: // SHIFTREG_90deg
                    case 3: // SHIFTREG_0deg
                        return vco_constr * divq * sr_div;
                    default:
                        NPNR_ASSERT_FALSE("bad PLL output mode");
                    }
                };

                // Lookup shiftreg divider
                int sr_div_mode = int_or_default(packed->params, id_SHIFTREG_DIV_MODE, 0);
                switch (sr_div_mode) {
                case 0:
                    sr_div = 4;
                    break;
                case 1:
                    sr_div = 7;
                    break;
                case 3:
                    sr_div = 5;
                    break;
                default: {
                    log_info("    Unsupported SHIFTREG_DIV_MODE value %d; can't derive constraints for PLL '%s'\n",
                             sr_div_mode, ctx->nameOf(ci));
                    goto constr_fail;
                }
                }
                // Determine dividers in VCO path
                vco_constr = input_constr / (int_or_default(packed->params, id_DIVF, 0) + 1);
                divq = 1 << (int_or_default(packed->params, id_DIVQ, 0));
                if (fbp_value != "1") // anything other than SIMPLE - feedback after DIVQ
                    vco_constr /= divq;
                if (fbp_value == "6") { // EXTERNAL
                    log_info("    Can't derive constraints for PLL '%s' in EXTERNAL feedback mode\n", ctx->nameOf(ci));
                    goto constr_fail;
                }
                if (fbp_value == "2") { // PHASE_AND_DELAY feedback - via shiftreg
                    // Shiftreg divider is also in the VCO feedback path
                    vco_constr /= sr_div;
                }
                log_info("    VCO frequency of PLL '%s' is constrained to %.1f MHz\n", ctx->nameOf(ci),
                         MHz(ctx, vco_constr));
                if (ci->type == id_SB_PLL40_2_PAD)
                    outa_constr = input_constr; // 2_PAD variant passes through input to OUTPUT A
                else
                    outa_constr = process_output(id_PLLOUT_SELECT_A);
                outb_constr = process_output(id_PLLOUT_SELECT_B);
                set_period(ctx, packed.get(), id_PLLOUT_A, outa_constr);
                set_period(ctx, packed.get(), id_PLLOUT_A_GLOBAL, outa_constr);
                set_period(ctx, packed.get(), id_PLLOUT_B, outb_constr);
                set_period(ctx, packed.get(), id_PLLOUT_B_GLOBAL, outb_constr);
            }
        constr_fail:
            // PLL must have been placed already in place_plls()
            BelId pll_bel = ctx->getBelByNameStr(packed->attrs[id_BEL].as_string());
            NPNR_ASSERT(pll_bel != BelId());

            // Deal with PAD PLL peculiarities
            if (is_pad) {
                NPNR_ASSERT(pad_packagepin_net != nullptr);
                auto pll_packagepin_driver = pad_packagepin_net->driver;
                NPNR_ASSERT(pll_packagepin_driver.cell != nullptr);
                auto packagepin_cell = pll_packagepin_driver.cell;
                auto packagepin_bel_name = packagepin_cell->attrs.find(id_BEL);

                // Set an attribute about this PLL's PAD SB_IO.
                packed->attrs[id_BEL_PAD_INPUT] = packagepin_bel_name->second;

                // Disconnect PACKAGEPIN (it's a physical HW link)
                for (auto user : pad_packagepin_net->users)
                    user.cell->ports.erase(user.port);
                packagepin_cell->ports.erase(pll_packagepin_driver.port);
                ctx->nets.erase(pad_packagepin_net->name);
                pad_packagepin_net = nullptr;
            }

            // The LOCK signal on iCE40 PLLs goes through the neigh_op_bnl_1 wire.
            // In practice, this means the LOCK signal can only directly reach LUT
            // inputs.
            // If we have a net connected to LOCK, make sure it only drives LUTs.
            auto port = packed->ports[id_LOCK];
            if (port.net != nullptr) {
                log_info("  PLL '%s' has LOCK output, need to pass all outputs via LUT\n", ci->name.c_str(ctx));
                bool found_lut = false;
                bool all_luts = true;
                bool found_carry = false;
                unsigned int lut_count = 0;
                for (const auto &user : port.net->users) {
                    NPNR_ASSERT(user.cell != nullptr);
                    if (user.cell->type == id_ICESTORM_LC) {
                        if (bool_or_default(user.cell->params, id_CARRY_ENABLE, false)) {
                            found_carry = true;
                            all_luts = false;
                        } else {
                            found_lut = true;
                            lut_count++;
                        }
                    } else {
                        all_luts = false;
                    }
                }

                if (found_lut && all_luts && lut_count < 8) {
                    // Every user is a LUT, carry on now.
                } else if (found_lut && !all_luts && !found_carry && lut_count < 8) {
                    // Strategy: create a pass-through LUT, move all non-LUT users behind it.
                    log_info("  LUT strategy for %s: move non-LUT users to new LUT\n", port.name.c_str(ctx));
                    auto pt = spliceLUT(ctx, packed.get(), port.name, true);
                    new_cells.push_back(std::move(pt));
                } else {
                    // Strategy: create a pass-through LUT, move every user behind it.
                    log_info("  LUT strategy for %s: move all users to new LUT\n", port.name.c_str(ctx));
                    auto pt = spliceLUT(ctx, packed.get(), port.name, false);
                    new_cells.push_back(std::move(pt));
                }

                // Find wire that will be driven by this port.
                const auto pll_out_wire = ctx->getBelPinWire(pll_bel, port.name);
                NPNR_ASSERT(pll_out_wire.index != -1);

                // Now, constrain all LUTs on the output of the signal to be at
                // the correct Bel relative to the PLL Bel.
                int x = ctx->chip_info->wire_data[pll_out_wire.index].x;
                int y = ctx->chip_info->wire_data[pll_out_wire.index].y;
                int z = 0;
                for (const auto &user : port.net->users) {
                    NPNR_ASSERT(user.cell != nullptr);
                    NPNR_ASSERT(user.cell->type == id_ICESTORM_LC);

                    // TODO(q3k): handle when the Bel might be already the
                    // target of another constraint.
                    NPNR_ASSERT(z < 8);
                    auto target_bel = ctx->getBelByLocation(Loc(x, y, z++));
                    auto target_bel_name = ctx->getBelName(target_bel).str(ctx);
                    user.cell->attrs[id_BEL] = target_bel_name;
                    log_info("  constrained '%s' to %s\n", user.cell->name.c_str(ctx), target_bel_name.c_str());
                }
            }

            // Handle the global buffer connections
            for (auto port : packed->ports) {
                PortInfo &pi = port.second;
                bool is_b_port;

                if (pi.name == id_PLLOUT_A_GLOBAL)
                    is_b_port = false;
                else if (pi.name == id_PLLOUT_B_GLOBAL)
                    is_b_port = true;
                else
                    continue;

                // Only if there is actually a net ...
                if (pi.net != nullptr) {
                    // ... and it's used
                    if (pi.net->users.entries() > 0) {
                        std::unique_ptr<CellInfo> gb =
                                create_padin_gbuf(ctx, packed.get(), pi.name,
                                                  "$gbuf_" + ci->name.str(ctx) + "_pllout_" + (is_b_port ? "b" : "a"));
                        new_cells.push_back(std::move(gb));
                    } else {
                        // If not, remove it to avoid routing issues
                        ctx->nets.erase(pi.net->name);
                        packed->ports[pi.name].net = nullptr;
                    }
                }
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

// Main pack function
bool Arch::pack()
{
    Context *ctx = getCtx();
    try {
        log_break();
        pack_constants(ctx);
        pack_io(ctx);
        pack_lut_lutffs(ctx);
        pack_nonlut_ffs(ctx);
        pack_carries(ctx);
        merge_carry_luts(ctx);
        pack_ram(ctx);
        place_plls(ctx);
        pack_special(ctx);
        pack_plls(ctx);
        if (!bool_or_default(ctx->settings, id_no_promote_globals, false))
            promote_globals(ctx);
        copy_gb_constraints(ctx);
        ctx->assignArchInfo();
        constrain_chains(ctx);
        ctx->fixupHierarchy();
        ctx->assignArchInfo();
        ctx->settings[id_pack] = 1;
        archInfoToAttributes();
        log_info("Checksum: 0x%08x\n", ctx->checksum());
        return true;
    } catch (log_execution_error_exception) {
        return false;
    }
}

NEXTPNR_NAMESPACE_END
