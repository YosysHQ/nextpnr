/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@symbioticeda.com>
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
#include <unordered_set>
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

    std::unordered_set<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;
    for (auto cell : sorted(ctx->cells)) {
        CellInfo *ci = cell.second;
        if (ctx->verbose)
            log_info("cell '%s' is of type '%s'\n", ci->name.c_str(ctx), ci->type.c_str(ctx));
        if (is_lut(ctx, ci)) {
            std::unique_ptr<CellInfo> packed = create_ice_cell(ctx, ctx->id("ICESTORM_LC"), ci->name.str(ctx) + "_LC");
            std::copy(ci->attrs.begin(), ci->attrs.end(), std::inserter(packed->attrs, packed->attrs.begin()));
            packed_cells.insert(ci->name);
            if (ctx->verbose)
                log_info("packed cell %s into %s\n", ci->name.c_str(ctx), packed->name.c_str(ctx));
            // See if we can pack into a DFF
            // TODO: LUT cascade
            NetInfo *o = ci->ports.at(ctx->id("O")).net;
            CellInfo *dff = net_only_drives(ctx, o, is_ff, ctx->id("D"), true);
            auto lut_bel = ci->attrs.find(ctx->id("BEL"));
            bool packed_dff = false;
            if (dff) {
                if (ctx->verbose)
                    log_info("found attached dff %s\n", dff->name.c_str(ctx));
                auto dff_bel = dff->attrs.find(ctx->id("BEL"));
                if (lut_bel != ci->attrs.end() && dff_bel != dff->attrs.end() && lut_bel->second != dff_bel->second) {
                    // Locations don't match, can't pack
                } else {
                    lut_to_lc(ctx, ci, packed.get(), false);
                    dff_to_lc(ctx, dff, packed.get(), false);
                    ctx->nets.erase(o->name);
                    if (dff_bel != dff->attrs.end())
                        packed->attrs[ctx->id("BEL")] = dff_bel->second;
                    packed_cells.insert(dff->name);
                    if (ctx->verbose)
                        log_info("packed cell %s into %s\n", dff->name.c_str(ctx), packed->name.c_str(ctx));
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

    std::unordered_set<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;

    for (auto cell : sorted(ctx->cells)) {
        CellInfo *ci = cell.second;
        if (is_ff(ctx, ci)) {
            std::unique_ptr<CellInfo> packed =
                    create_ice_cell(ctx, ctx->id("ICESTORM_LC"), ci->name.str(ctx) + "_DFFLC");
            std::copy(ci->attrs.begin(), ci->attrs.end(), std::inserter(packed->attrs, packed->attrs.begin()));
            if (ctx->verbose)
                log_info("packed cell %s into %s\n", ci->name.c_str(ctx), packed->name.c_str(ctx));
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

static bool net_is_constant(const Context *ctx, NetInfo *net, bool &value)
{
    if (net == nullptr)
        return false;
    if (net->name == ctx->id("$PACKER_GND_NET") || net->name == ctx->id("$PACKER_VCC_NET")) {
        value = (net->name == ctx->id("$PACKER_VCC_NET"));
        return true;
    } else {
        return false;
    }
}

// Pack carry logic
static void pack_carries(Context *ctx)
{
    log_info("Packing carries..\n");
    std::unordered_set<IdString> exhausted_cells;
    std::unordered_set<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;

    for (auto cell : sorted(ctx->cells)) {
        CellInfo *ci = cell.second;
        if (is_carry(ctx, ci)) {
            packed_cells.insert(cell.first);

            CellInfo *carry_ci_lc;
            bool ci_value;
            bool ci_const = net_is_constant(ctx, ci->ports.at(ctx->id("CI")).net, ci_value);
            if (ci_const) {
                carry_ci_lc = nullptr;
            } else {
                carry_ci_lc = net_only_drives(ctx, ci->ports.at(ctx->id("CI")).net, is_lc, ctx->id("I3"), false);
            }

            std::set<IdString> i0_matches, i1_matches;
            NetInfo *i0_net = ci->ports.at(ctx->id("I0")).net;
            NetInfo *i1_net = ci->ports.at(ctx->id("I1")).net;
            // Find logic cells connected to both I0 and I1
            if (i0_net) {
                for (auto usr : i0_net->users) {
                    if (is_lc(ctx, usr.cell) && usr.port == ctx->id("I1")) {
                        if (ctx->cells.find(usr.cell->name) != ctx->cells.end() &&
                            exhausted_cells.find(usr.cell->name) == exhausted_cells.end()) {
                            // This clause stops us double-packing cells
                            i0_matches.insert(usr.cell->name);
                            if (!i1_net && !usr.cell->ports.at(ctx->id("I2")).net) {
                                // I1 is don't care when disconnected, duplicate I0
                                i1_matches.insert(usr.cell->name);
                            }
                        }
                    }
                }
            }
            if (i1_net) {
                for (auto usr : i1_net->users) {
                    if (is_lc(ctx, usr.cell) && usr.port == ctx->id("I2")) {
                        if (ctx->cells.find(usr.cell->name) != ctx->cells.end() &&
                            exhausted_cells.find(usr.cell->name) == exhausted_cells.end()) {
                            // This clause stops us double-packing cells
                            i1_matches.insert(usr.cell->name);
                            if (!i0_net && !usr.cell->ports.at(ctx->id("I1")).net) {
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
            } else {
                // No LC to pack into matching I0/I1, insert a new one
                std::unique_ptr<CellInfo> created_lc =
                        create_ice_cell(ctx, ctx->id("ICESTORM_LC"), cell.first.str(ctx) + "$CARRY");
                carry_lc = created_lc.get();
                created_lc->ports.at(ctx->id("I1")).net = i0_net;
                if (i0_net) {
                    PortRef pr;
                    pr.cell = created_lc.get();
                    pr.port = ctx->id("I1");
                    i0_net->users.push_back(pr);
                }
                created_lc->ports.at(ctx->id("I2")).net = i1_net;
                if (i1_net) {
                    PortRef pr;
                    pr.cell = created_lc.get();
                    pr.port = ctx->id("I2");
                    i1_net->users.push_back(pr);
                }
                new_cells.push_back(std::move(created_lc));
            }
            carry_lc->params[ctx->id("CARRY_ENABLE")] = "1";
            replace_port(ci, ctx->id("CI"), carry_lc, ctx->id("CIN"));
            replace_port(ci, ctx->id("CO"), carry_lc, ctx->id("COUT"));
            if (i0_net) {
                auto &i0_usrs = i0_net->users;
                i0_usrs.erase(std::remove_if(i0_usrs.begin(), i0_usrs.end(), [ci, ctx](const PortRef &pr) {
                    return pr.cell == ci && pr.port == ctx->id("I0");
                }));
            }
            if (i1_net) {
                auto &i1_usrs = i1_net->users;
                i1_usrs.erase(std::remove_if(i1_usrs.begin(), i1_usrs.end(), [ci, ctx](const PortRef &pr) {
                    return pr.cell == ci && pr.port == ctx->id("I1");
                }));
            }

            // Check for constant driver on CIN
            if (carry_lc->ports.at(ctx->id("CIN")).net != nullptr) {
                IdString cin_net = carry_lc->ports.at(ctx->id("CIN")).net->name;
                if (cin_net == ctx->id("$PACKER_GND_NET") || cin_net == ctx->id("$PACKER_VCC_NET")) {
                    carry_lc->params[ctx->id("CIN_CONST")] = "1";
                    carry_lc->params[ctx->id("CIN_SET")] = cin_net == ctx->id("$PACKER_VCC_NET") ? "1" : "0";
                    carry_lc->ports.at(ctx->id("CIN")).net = nullptr;
                    auto &cin_users = ctx->nets.at(cin_net)->users;
                    cin_users.erase(
                            std::remove_if(cin_users.begin(), cin_users.end(), [carry_lc, ctx](const PortRef &pr) {
                                return pr.cell == carry_lc && pr.port == ctx->id("CIN");
                            }));
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
}

// "Pack" RAMs
static void pack_ram(Context *ctx)
{
    log_info("Packing RAMs..\n");

    std::unordered_set<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;

    for (auto cell : sorted(ctx->cells)) {
        CellInfo *ci = cell.second;
        if (is_ram(ctx, ci)) {
            std::unique_ptr<CellInfo> packed =
                    create_ice_cell(ctx, ctx->id("ICESTORM_RAM"), ci->name.str(ctx) + "_RAM");
            packed_cells.insert(ci->name);
            for (auto param : ci->params)
                packed->params[param.first] = param.second;
            packed->params[ctx->id("NEG_CLK_W")] =
                    std::to_string(ci->type == ctx->id("SB_RAM40_4KNW") || ci->type == ctx->id("SB_RAM40_4KNRNW"));
            packed->params[ctx->id("NEG_CLK_R")] =
                    std::to_string(ci->type == ctx->id("SB_RAM40_4KNR") || ci->type == ctx->id("SB_RAM40_4KNRNW"));
            packed->type = ctx->id("ICESTORM_RAM");
            for (auto port : ci->ports) {
                PortInfo &pi = port.second;
                std::string newname = pi.name.str(ctx);
                size_t bpos = newname.find('[');
                if (bpos != std::string::npos) {
                    newname = newname.substr(0, bpos) + "_" + newname.substr(bpos + 1, (newname.size() - bpos) - 2);
                }
                if (pi.name == ctx->id("RCLKN"))
                    newname = "RCLK";
                else if (pi.name == ctx->id("WCLKN"))
                    newname = "WCLK";
                replace_port(ci, ctx->id(pi.name.c_str(ctx)), packed.get(), ctx->id(newname));
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
            } else if ((is_sb_mac16(ctx, uc) || uc->type == ctx->id("ICESTORM_DSP")) &&
                       (user.port != ctx->id("CLK") &&
                        ((constval && user.port == ctx->id("CE")) || (!constval && user.port != ctx->id("CE"))))) {
                uc->ports[user.port].net = nullptr;
            } else if (is_ram(ctx, uc) && !constval && user.port != ctx->id("RCLK") && user.port != ctx->id("RCLKN") &&
                       user.port != ctx->id("WCLK") && user.port != ctx->id("WCLKN") && user.port != ctx->id("RCLKE") &&
                       user.port != ctx->id("WCLKE")) {
                uc->ports[user.port].net = nullptr;
            } else {
                uc->ports[user.port].net = constnet;
                constnet->users.push_back(user);
            }
        }
    }
    orig->users.clear();
}

// Pack constants (simple implementation)
static void pack_constants(Context *ctx)
{
    log_info("Packing constants..\n");

    std::unique_ptr<CellInfo> gnd_cell = create_ice_cell(ctx, ctx->id("ICESTORM_LC"), "$PACKER_GND");
    gnd_cell->params[ctx->id("LUT_INIT")] = "0";
    std::unique_ptr<NetInfo> gnd_net = std::unique_ptr<NetInfo>(new NetInfo);
    gnd_net->name = ctx->id("$PACKER_GND_NET");
    gnd_net->driver.cell = gnd_cell.get();
    gnd_net->driver.port = ctx->id("O");
    gnd_cell->ports.at(ctx->id("O")).net = gnd_net.get();

    std::unique_ptr<CellInfo> vcc_cell = create_ice_cell(ctx, ctx->id("ICESTORM_LC"), "$PACKER_VCC");
    vcc_cell->params[ctx->id("LUT_INIT")] = "1";
    std::unique_ptr<NetInfo> vcc_net = std::unique_ptr<NetInfo>(new NetInfo);
    vcc_net->name = ctx->id("$PACKER_VCC_NET");
    vcc_net->driver.cell = vcc_cell.get();
    vcc_net->driver.port = ctx->id("O");
    vcc_cell->ports.at(ctx->id("O")).net = vcc_net.get();

    std::vector<IdString> dead_nets;

    bool gnd_used = false;

    for (auto net : sorted(ctx->nets)) {
        NetInfo *ni = net.second;
        if (ni->driver.cell != nullptr && ni->driver.cell->type == ctx->id("GND")) {
            IdString drv_cell = ni->driver.cell->name;
            set_net_constant(ctx, ni, gnd_net.get(), false);
            gnd_used = true;
            dead_nets.push_back(net.first);
            ctx->cells.erase(drv_cell);
        } else if (ni->driver.cell != nullptr && ni->driver.cell->type == ctx->id("VCC")) {
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

static std::unique_ptr<CellInfo> create_padin_gbuf(Context *ctx, CellInfo *cell, IdString port_name,
                                                   std::string gbuf_name)
{
    // Find the matching SB_GB BEL connected to the same global network
    BelId gb_bel;
    BelId bel = ctx->getBelByName(ctx->id(cell->attrs[ctx->id("BEL")]));
    auto wire = ctx->getBelPinWire(bel, port_name);
    for (auto src_bel : ctx->getWireBelPins(wire)) {
        if (ctx->getBelType(src_bel.bel) == id_SB_GB && src_bel.pin == id_GLOBAL_BUFFER_OUTPUT) {
            gb_bel = src_bel.bel;
            break;
        }
    }

    NPNR_ASSERT(gb_bel != BelId());

    // Create a SB_GB Cell and lock it there
    std::unique_ptr<CellInfo> gb = create_ice_cell(ctx, ctx->id("SB_GB"), gbuf_name);
    gb->attrs[ctx->id("FOR_PAD_IN")] = "1";
    gb->attrs[ctx->id("BEL")] = ctx->getBelName(gb_bel).str(ctx);

    // Reconnect the net to that port for easier identification it's a global net
    replace_port(cell, port_name, gb.get(), id_GLOBAL_BUFFER_OUTPUT);

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
    std::unordered_set<IdString> packed_cells;
    std::unordered_set<IdString> delete_nets;

    std::vector<std::unique_ptr<CellInfo>> new_cells;
    log_info("Packing IOs..\n");

    for (auto cell : sorted(ctx->cells)) {
        CellInfo *ci = cell.second;
        if (is_nextpnr_iob(ctx, ci)) {
            CellInfo *sb = nullptr, *rgb = nullptr;
            if (ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_iobuf")) {
                sb = net_only_drives(ctx, ci->ports.at(ctx->id("O")).net, is_ice_iob, ctx->id("PACKAGE_PIN"), true, ci);

            } else if (ci->type == ctx->id("$nextpnr_obuf")) {
                NetInfo *net = ci->ports.at(ctx->id("I")).net;
                sb = net_only_drives(ctx, net, is_ice_iob, ctx->id("PACKAGE_PIN"), true, ci);
                if (net && net->driver.cell && is_sb_rgba_drv(ctx, net->driver.cell))
                    rgb = net->driver.cell;
            }
            if (sb != nullptr) {
                // Trivial case, SB_IO used. Just destroy the net and the
                // iobuf
                log_info("%s feeds SB_IO %s, removing %s %s.\n", ci->name.c_str(ctx), sb->name.c_str(ctx),
                         ci->type.c_str(ctx), ci->name.c_str(ctx));
                NetInfo *net = sb->ports.at(ctx->id("PACKAGE_PIN")).net;
                if (((ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_iobuf")) &&
                     net->users.size() > 1) ||
                    (ci->type == ctx->id("$nextpnr_obuf") && (net->users.size() > 2 || net->driver.cell != nullptr)))
                    log_error("PACKAGE_PIN of %s '%s' connected to more than a single top level IO.\n",
                              sb->type.c_str(ctx), sb->name.c_str(ctx));

                if (net != nullptr) {
                    delete_nets.insert(net->name);
                    sb->ports.at(ctx->id("PACKAGE_PIN")).net = nullptr;
                }
                if (ci->type == ctx->id("$nextpnr_iobuf")) {
                    NetInfo *net2 = ci->ports.at(ctx->id("I")).net;
                    if (net2 != nullptr) {
                        delete_nets.insert(net2->name);
                    }
                }
            } else if (rgb != nullptr) {
                log_info("%s use by SB_RGBA_DRV %s, not creating SB_IO\n", ci->name.c_str(ctx), rgb->name.c_str(ctx));
                disconnect_port(ctx, ci, ctx->id("I"));
                packed_cells.insert(ci->name);
                continue;
            } else {
                // Create a SB_IO buffer
                std::unique_ptr<CellInfo> ice_cell =
                        create_ice_cell(ctx, ctx->id("SB_IO"), ci->name.str(ctx) + "$sb_io");
                nxio_to_sb(ctx, ci, ice_cell.get(), packed_cells);
                new_cells.push_back(std::move(ice_cell));
                sb = new_cells.back().get();
            }
            packed_cells.insert(ci->name);
            std::copy(ci->attrs.begin(), ci->attrs.end(), std::inserter(sb->attrs, sb->attrs.begin()));
        } else if (is_sb_io(ctx, ci) || is_sb_gb_io(ctx, ci)) {
            NetInfo *net = ci->ports.at(ctx->id("PACKAGE_PIN")).net;
            if ((net != nullptr) && (net->users.size() > 1))
                log_error("PACKAGE_PIN of %s '%s' connected to more than a single top level IO.\n", ci->type.c_str(ctx),
                          ci->name.c_str(ctx));
        }
    }
    for (auto cell : sorted(ctx->cells)) {
        CellInfo *ci = cell.second;
        if (is_sb_gb_io(ctx, ci)) {
            // If something is connecto the GLOBAL OUTPUT, create the fake 'matching' SB_GB
            std::unique_ptr<CellInfo> gb =
                    create_padin_gbuf(ctx, ci, id_GLOBAL_BUFFER_OUTPUT, "$gbuf_" + ci->name.str(ctx) + "_io");
            new_cells.push_back(std::move(gb));

            // Make it a normal SB_IO with global marker
            ci->type = ctx->id("SB_IO");
            ci->attrs[ctx->id("GLOBAL")] = "1";
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
    std::unique_ptr<CellInfo> gb = create_ice_cell(ctx, ctx->id("SB_GB"), "$gbuf_" + glb_name);
    gb->ports[ctx->id("USER_SIGNAL_TO_GLOBAL_BUFFER")].net = net;
    PortRef pr;
    pr.cell = gb.get();
    pr.port = ctx->id("USER_SIGNAL_TO_GLOBAL_BUFFER");
    net->users.push_back(pr);

    pr.cell = gb.get();
    pr.port = ctx->id("GLOBAL_BUFFER_OUTPUT");
    std::unique_ptr<NetInfo> glbnet = std::unique_ptr<NetInfo>(new NetInfo());
    glbnet->name = ctx->id(glb_name);
    glbnet->driver = pr;
    gb->ports[ctx->id("GLOBAL_BUFFER_OUTPUT")].net = glbnet.get();
    std::vector<PortRef> keep_users;
    for (auto user : net->users) {
        if (is_clock_port(ctx, user) || (is_reset && is_reset_port(ctx, user)) ||
            (is_cen && is_enable_port(ctx, user)) || (is_logic && is_logic_port(ctx, user))) {
            user.cell->ports[user.port].net = glbnet.get();
            glbnet->users.push_back(user);
        } else {
            keep_users.push_back(user);
        }
    }
    net->users = keep_users;

    if (net->clkconstr) {
        glbnet->clkconstr = std::unique_ptr<ClockConstraint>(new ClockConstraint());
        glbnet->clkconstr->low = net->clkconstr->low;
        glbnet->clkconstr->high = net->clkconstr->high;
        glbnet->clkconstr->period = net->clkconstr->period;
    }

    ctx->nets[glbnet->name] = std::move(glbnet);
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
    for (auto net : sorted(ctx->nets)) {
        NetInfo *ni = net.second;
        if (ni->driver.cell != nullptr && !ctx->isGlobalNet(ni)) {
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
            if (cell.second->attrs.find(ctx->id("BEL")) != cell.second->attrs.end()) {
                /* If the SB_GB is locked, doesn't matter what it drives */
                BelId bel = ctx->getBelByName(ctx->id(cell.second->attrs[ctx->id("BEL")]));
                int glb_id = ctx->getDrivenGlobalNetwork(bel);
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
            bool_or_default(ctx->settings, ctx->id("promote_logic"), false)) {
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

// Figure out where to place PLLs
static void place_plls(Context *ctx)
{
    std::map<BelId, std::pair<BelPin, BelPin>> pll_all_bels;
    std::map<BelId, CellInfo *> pll_used_bels;
    std::vector<CellInfo *> pll_cells;
    std::map<BelId, CellInfo *> bel2io;

    log_info("Placing PLLs..\n");

    // Find all the PLLs BELs and matching IO sites
    for (auto bel : ctx->getBels()) {
        if (ctx->getBelType(bel) != id_ICESTORM_PLL)
            continue;
        if (ctx->isBelLocked(bel))
            continue;

        auto io_a_pin = ctx->getIOBSharingPLLPin(bel, id_PLLOUT_A);
        auto io_b_pin = ctx->getIOBSharingPLLPin(bel, id_PLLOUT_B);

        pll_all_bels[bel] = std::make_pair(io_a_pin, io_b_pin);
    }

    // Find all the PLLs cells we need to place and do pre-checks
    for (auto cell : sorted(ctx->cells)) {
        CellInfo *ci = cell.second;
        if (!is_sb_pll40(ctx, ci))
            continue;

        // If it's constrained already, add to already used list
        if (ci->attrs.count(ctx->id("BEL"))) {
            BelId bel_constrain = ctx->getBelByName(ctx->id(ci->attrs[ctx->id("BEL")]));
            if (pll_all_bels.count(bel_constrain) == 0)
                log_error("PLL '%s' is constrained to invalid BEL '%s'\n", ci->name.c_str(ctx),
                          ci->attrs[ctx->id("BEL")].c_str());
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
        if (!ci->ports.count(ctx->id("PACKAGEPIN")))
            log_error("PLL '%s' is of PAD type but doesn't have a PACKAGEPIN port\n", ci->name.c_str(ctx));

        NetInfo *ni = ci->ports.at(ctx->id("PACKAGEPIN")).net;
        if (ni == nullptr || ni->driver.cell == nullptr)
            log_error("PLL '%s' is of PAD type but doesn't have a valid PACKAGEPIN connection\n", ci->name.c_str(ctx));

        CellInfo *io_cell = ni->driver.cell;
        if (io_cell->type != id_SB_IO || ni->driver.port != id_D_IN_0)
            log_error("PLL '%s' has a PACKAGEPIN driven by an %s, should be directly connected to an input "
                      "SB_IO.D_IN_0 port\n",
                      ci->name.c_str(ctx), io_cell->type.c_str(ctx));
        if (ni->users.size() != 1)
            log_error("PLL '%s' clock input '%s' can only drive PLL\n", ci->name.c_str(ctx), ni->name.c_str(ctx));
        if (!io_cell->attrs.count(ctx->id("BEL")))
            log_error("PLL '%s' PACKAGEPIN SB_IO '%s' is unconstrained\n", ci->name.c_str(ctx),
                      io_cell->name.c_str(ctx));

        BelId io_bel = ctx->getBelByName(ctx->id(io_cell->attrs.at(ctx->id("BEL"))));
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
            log_error("PLL '%s' PACKAGEPIN forces it to BEL %s but BEL is already assigned to PLL '%s'\n",
                      ci->name.c_str(ctx), ctx->getBelName(found_bel).c_str(ctx), conflict_cell->name.c_str(ctx));
        }

        // Is it user constrained ?
        if (ci->attrs.count(ctx->id("BEL"))) {
            // Yes. Check it actually matches !
            BelId bel_constrain = ctx->getBelByName(ctx->id(ci->attrs[ctx->id("BEL")]));
            if (bel_constrain != found_bel)
                log_error("PLL '%s' is user constrained to %s but can only be placed in %s based on its PACKAGEPIN "
                          "connection\n",
                          ci->name.c_str(ctx), ctx->getBelName(bel_constrain).c_str(ctx),
                          ctx->getBelName(found_bel).c_str(ctx));
        } else {
            // No, we can constrain it ourselves
            ci->attrs[ctx->id("BEL")] = ctx->getBelName(found_bel).str(ctx);
            pll_used_bels[found_bel] = ci;
        }

        // Inform user
        log_info("  constrained PLL '%s' to %s\n", ci->name.c_str(ctx), ctx->getBelName(found_bel).c_str(ctx));
    }

    // Scan all SB_IOs to check for conflict with PLL BELs
    for (auto io_cell : sorted(ctx->cells)) {
        CellInfo *io_ci = io_cell.second;
        if (!is_sb_io(ctx, io_ci))
            continue;

        // Only consider bound IO that are used as inputs
        if (!io_ci->attrs.count(ctx->id("BEL")))
            continue;
        if ((!io_ci->ports.count(id_D_IN_0) || (io_ci->ports[id_D_IN_0].net == nullptr)) &&
            (!io_ci->ports.count(id_D_IN_1) || (io_ci->ports[id_D_IN_1].net == nullptr)))
            continue;

        // Check all placed PLL (either forced by user, or forced by PACKAGEPIN)
        BelId io_bel = ctx->getBelByName(ctx->id(io_ci->attrs[ctx->id("BEL")]));

        for (auto placed_pll : pll_used_bels) {
            BelPin pll_io_a, pll_io_b;
            std::tie(pll_io_a, pll_io_b) = pll_all_bels[placed_pll.first];
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

            // Could this be a PAD PLL ?
            bool could_be_pad = false;
            BelId pad_bel;
            if (ni->users.size() == 1 && is_sb_io(ctx, ni->driver.cell) && ni->driver.cell->attrs.count(ctx->id("BEL")))
                pad_bel = ctx->getBelByName(ctx->id(ni->driver.cell->attrs[ctx->id("BEL")]));

            // Find a BEL for it
            BelId found_bel;
            for (auto bel_pll : pll_all_bels) {
                BelPin pll_io_a, pll_io_b;
                std::tie(pll_io_a, pll_io_b) = bel_pll.second;
                if (bel2io.count(pll_io_a.bel)) {
                    if (pll_io_a.bel == pad_bel)
                        could_be_pad = !bel2io.count(pll_io_b.bel) || !is_sb_pll40_dual(ctx, ci);
                    continue;
                }
                if (bel2io.count(pll_io_b.bel) && is_sb_pll40_dual(ctx, ci))
                    continue;
                found_bel = bel_pll.first;
                break;
            }

            // Apply constrain & Inform user of result
            if (found_bel == BelId())
                log_error("PLL '%s' couldn't be placed anywhere, no suitable BEL found.%s\n", ci->name.c_str(ctx),
                          could_be_pad ? " Did you mean to use a PAD PLL ?" : "");

            log_info("  constrained PLL '%s' to %s\n", ci->name.c_str(ctx), ctx->getBelName(found_bel).c_str(ctx));
            if (could_be_pad)
                log_info("  (given its connections, this PLL could have been a PAD PLL)\n");

            ci->attrs[ctx->id("BEL")] = ctx->getBelName(found_bel).str(ctx);
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
    std::unique_ptr<CellInfo> pt = create_ice_cell(ctx, ctx->id("ICESTORM_LC"),
                                                   ci->name.str(ctx) + "$nextpnr_" + portId.str(ctx) + "_lut_through");
    pt->params[ctx->id("LUT_INIT")] = "65280"; // output is always I3

    // Create LUT output net.
    std::unique_ptr<NetInfo> out_net = std::unique_ptr<NetInfo>(new NetInfo);
    out_net->name = ctx->id(ci->name.str(ctx) + "$nextnr_" + portId.str(ctx) + "_lut_through_net");
    out_net->driver.cell = pt.get();
    out_net->driver.port = ctx->id("O");
    pt->ports.at(ctx->id("O")).net = out_net.get();

    // New users of the original cell's port
    std::vector<PortRef> new_users;
    for (const auto &user : port.net->users) {
        if (onlyNonLUTs && user.cell->type == ctx->id("ICESTORM_LC")) {
            new_users.push_back(user);
            continue;
        }
        // Rewrite pointer into net in user.
        user.cell->ports[user.port].net = out_net.get();
        // Add user to net.
        PortRef pr;
        pr.cell = user.cell;
        pr.port = user.port;
        out_net->users.push_back(pr);
    }

    // Add LUT to new users.
    PortRef pr;
    pr.cell = pt.get();
    pr.port = ctx->id("I3");
    new_users.push_back(pr);
    pt->ports.at(ctx->id("I3")).net = port.net;

    // Replace users of the original net.
    port.net->users = new_users;

    ctx->nets[out_net->name] = std::move(out_net);
    return pt;
}

// Force placement for cells that are unique anyway
static BelId cell_place_unique(Context *ctx, CellInfo *ci)
{
    for (auto bel : ctx->getBels()) {
        if (ctx->getBelType(bel) != ci->type)
            continue;
        if (ctx->isBelLocked(bel))
            continue;
        IdString bel_name = ctx->getBelName(bel);
        ci->attrs[ctx->id("BEL")] = bel_name.str(ctx);
        log_info("  constrained %s '%s' to %s\n", ci->type.c_str(ctx), ci->name.c_str(ctx), bel_name.c_str(ctx));
        return bel;
    }
    log_error("Unable to place cell '%s' of type '%s'\n", ci->name.c_str(ctx), ci->type.c_str(ctx));
}

// Pack special functions
static void pack_special(Context *ctx)
{
    log_info("Packing special functions..\n");

    std::unordered_set<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;

    for (auto cell : sorted(ctx->cells)) {
        CellInfo *ci = cell.second;
        if (is_sb_lfosc(ctx, ci)) {
            std::unique_ptr<CellInfo> packed =
                    create_ice_cell(ctx, ctx->id("ICESTORM_LFOSC"), ci->name.str(ctx) + "_OSC");
            packed_cells.insert(ci->name);
            cell_place_unique(ctx, packed.get());
            replace_port(ci, ctx->id("CLKLFEN"), packed.get(), ctx->id("CLKLFEN"));
            replace_port(ci, ctx->id("CLKLFPU"), packed.get(), ctx->id("CLKLFPU"));
            if (bool_or_default(ci->attrs, ctx->id("ROUTE_THROUGH_FABRIC"))) {
                replace_port(ci, ctx->id("CLKLF"), packed.get(), ctx->id("CLKLF_FABRIC"));
            } else {
                replace_port(ci, ctx->id("CLKLF"), packed.get(), ctx->id("CLKLF"));
                std::unique_ptr<CellInfo> gb =
                        create_padin_gbuf(ctx, packed.get(), ctx->id("CLKLF"), "$gbuf_" + ci->name.str(ctx) + "_lfosc");
                new_cells.push_back(std::move(gb));
            }
            new_cells.push_back(std::move(packed));
        } else if (is_sb_hfosc(ctx, ci)) {
            std::unique_ptr<CellInfo> packed =
                    create_ice_cell(ctx, ctx->id("ICESTORM_HFOSC"), ci->name.str(ctx) + "_OSC");
            packed_cells.insert(ci->name);
            cell_place_unique(ctx, packed.get());
            packed->params[ctx->id("CLKHF_DIV")] = str_or_default(ci->params, ctx->id("CLKHF_DIV"), "0b00");
            replace_port(ci, ctx->id("CLKHFEN"), packed.get(), ctx->id("CLKHFEN"));
            replace_port(ci, ctx->id("CLKHFPU"), packed.get(), ctx->id("CLKHFPU"));
            if (bool_or_default(ci->attrs, ctx->id("ROUTE_THROUGH_FABRIC"))) {
                replace_port(ci, ctx->id("CLKHF"), packed.get(), ctx->id("CLKHF_FABRIC"));
            } else {
                replace_port(ci, ctx->id("CLKHF"), packed.get(), ctx->id("CLKHF"));
                std::unique_ptr<CellInfo> gb =
                        create_padin_gbuf(ctx, packed.get(), ctx->id("CLKHF"), "$gbuf_" + ci->name.str(ctx) + "_hfosc");
                new_cells.push_back(std::move(gb));
            }
            new_cells.push_back(std::move(packed));
        } else if (is_sb_spram(ctx, ci)) {
            std::unique_ptr<CellInfo> packed =
                    create_ice_cell(ctx, ctx->id("ICESTORM_SPRAM"), ci->name.str(ctx) + "_RAM");
            packed_cells.insert(ci->name);
            for (auto port : ci->ports) {
                PortInfo &pi = port.second;
                std::string newname = pi.name.str(ctx);
                size_t bpos = newname.find('[');
                if (bpos != std::string::npos) {
                    newname = newname.substr(0, bpos) + "_" + newname.substr(bpos + 1, (newname.size() - bpos) - 2);
                }
                replace_port(ci, ctx->id(pi.name.c_str(ctx)), packed.get(), ctx->id(newname));
            }
            new_cells.push_back(std::move(packed));
        } else if (is_sb_mac16(ctx, ci)) {
            std::unique_ptr<CellInfo> packed =
                    create_ice_cell(ctx, ctx->id("ICESTORM_DSP"), ci->name.str(ctx) + "_DSP");
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
                replace_port(ci, ctx->id(pi.name.c_str(ctx)), packed.get(), ctx->id(newname));
            }
            new_cells.push_back(std::move(packed));
        } else if (is_sb_rgba_drv(ctx, ci)) {
            /* Force placement (no choices anyway) */
            cell_place_unique(ctx, ci);

            /* Disconnect all external ports and check there is no users (they should have been
             * dealth with during IO packing */
            for (auto port : ci->ports) {
                PortInfo &pi = port.second;
                NetInfo *net = pi.net;

                if (net == nullptr)
                    continue;
                if ((pi.name != ctx->id("RGB0")) && (pi.name != ctx->id("RGB1")) && (pi.name != ctx->id("RGB2")))
                    continue;

                if (net->users.size() > 0)
                    log_error("SB_RGBA_DRV port connected to more than just package pin !\n");

                ctx->nets.erase(net->name);
            }
            ci->ports.erase(ctx->id("RGB0"));
            ci->ports.erase(ctx->id("RGB1"));
            ci->ports.erase(ctx->id("RGB2"));
        } else if (is_sb_ledda_ip(ctx, ci)) {
            /* Force placement (no choices anyway) */
            cell_place_unique(ctx, ci);

        } else if (is_sb_pll40(ctx, ci)) {
            bool is_pad = is_sb_pll40_pad(ctx, ci);
            bool is_core = !is_pad;

            std::unique_ptr<CellInfo> packed =
                    create_ice_cell(ctx, ctx->id("ICESTORM_PLL"), ci->name.str(ctx) + "_PLL");
            packed->attrs[ctx->id("TYPE")] = ci->type.str(ctx);
            packed_cells.insert(ci->name);

            for (auto attr : ci->attrs)
                packed->attrs[attr.first] = attr.second;
            for (auto param : ci->params)
                packed->params[param.first] = param.second;

            const std::map<IdString, IdString> pos_map_name = {
                    {ctx->id("PLLOUT_SELECT"), ctx->id("PLLOUT_SELECT_A")},
                    {ctx->id("PLLOUT_SELECT_PORTA"), ctx->id("PLLOUT_SELECT_A")},
                    {ctx->id("PLLOUT_SELECT_PORTB"), ctx->id("PLLOUT_SELECT_B")},
            };
            const std::map<std::string, int> pos_map_val = {
                    {"GENCLK", 0},
                    {"GENCLK_HALF", 1},
                    {"SHIFTREG_90deg", 2},
                    {"SHIFTREG_0deg", 3},
            };
            for (auto param : ci->params)
                if (pos_map_name.find(param.first) != pos_map_name.end()) {
                    if (pos_map_val.find(param.second) == pos_map_val.end())
                        log_error("Invalid PLL output selection '%s'\n", param.second.c_str());
                    packed->params[pos_map_name.at(param.first)] = std::to_string(pos_map_val.at(param.second));
                }

            auto feedback_path = packed->params[ctx->id("FEEDBACK_PATH")];
            std::string fbp_value = feedback_path == "DELAY"
                                            ? "0"
                                            : feedback_path == "SIMPLE"
                                                      ? "1"
                                                      : feedback_path == "PHASE_AND_DELAY"
                                                                ? "2"
                                                                : feedback_path == "EXTERNAL" ? "6" : feedback_path;
            if (!std::all_of(fbp_value.begin(), fbp_value.end(), isdigit))
                log_error("PLL '%s' has unsupported FEEDBACK_PATH value '%s'\n", ci->name.c_str(ctx),
                          feedback_path.c_str());
            packed->params[ctx->id("FEEDBACK_PATH")] = fbp_value;
            packed->params[ctx->id("PLLTYPE")] = std::to_string(sb_pll40_type(ctx, ci));

            NetInfo *pad_packagepin_net = nullptr;

            for (auto port : ci->ports) {
                PortInfo &pi = port.second;
                std::string newname = pi.name.str(ctx);
                size_t bpos = newname.find('[');
                if (bpos != std::string::npos) {
                    newname = newname.substr(0, bpos) + "_" + newname.substr(bpos + 1, (newname.size() - bpos) - 2);
                }

                if (pi.name == ctx->id("PLLOUTCOREA") || pi.name == ctx->id("PLLOUTCORE"))
                    newname = "PLLOUT_A";
                if (pi.name == ctx->id("PLLOUTCOREB"))
                    newname = "PLLOUT_B";
                if (pi.name == ctx->id("PLLOUTGLOBALA") || pi.name == ctx->id("PLLOUTGLOBAL"))
                    newname = "PLLOUT_A_GLOBAL";
                if (pi.name == ctx->id("PLLOUTGLOBALB"))
                    newname = "PLLOUT_B_GLOBAL";

                if (pi.name == ctx->id("PACKAGEPIN")) {
                    if (!is_pad) {
                        log_error("PLL '%s' has a PACKAGEPIN but is not a PAD PLL\n", ci->name.c_str(ctx));
                    } else {
                        // We drop this port and instead place the PLL adequately below.
                        pad_packagepin_net = port.second.net;
                        NPNR_ASSERT(pad_packagepin_net != nullptr);
                        continue;
                    }
                }
                if (pi.name == ctx->id("REFERENCECLK")) {
                    if (!is_core)
                        log_error("PLL '%s' has a REFERENCECLK but is not a CORE PLL\n", ci->name.c_str(ctx));
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
                replace_port(ci, ctx->id(pi.name.c_str(ctx)), packed.get(), ctx->id(newname));
            }

            // PLL must have been placed already in place_plls()
            BelId pll_bel = ctx->getBelByName(ctx->id(packed->attrs[ctx->id("BEL")]));
            NPNR_ASSERT(pll_bel != BelId());

            // Deal with PAD PLL peculiarities
            if (is_pad) {
                NPNR_ASSERT(pad_packagepin_net != nullptr);
                auto pll_packagepin_driver = pad_packagepin_net->driver;
                NPNR_ASSERT(pll_packagepin_driver.cell != nullptr);
                auto packagepin_cell = pll_packagepin_driver.cell;
                auto packagepin_bel_name = packagepin_cell->attrs.find(ctx->id("BEL"));

                // Set an attribute about this PLL's PAD SB_IO.
                packed->attrs[ctx->id("BEL_PAD_INPUT")] = packagepin_bel_name->second;

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
            auto port = packed->ports[ctx->id("LOCK")];
            if (port.net != nullptr) {
                log_info("  PLL '%s' has LOCK output, need to pass all outputs via LUT\n", ci->name.c_str(ctx));
                bool found_lut = false;
                bool all_luts = true;
                bool found_carry = false;
                unsigned int lut_count = 0;
                for (const auto &user : port.net->users) {
                    NPNR_ASSERT(user.cell != nullptr);
                    if (user.cell->type == ctx->id("ICESTORM_LC")) {
                        if (bool_or_default(user.cell->params, ctx->id("CARRY_ENABLE"), false)) {
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
                    NPNR_ASSERT(user.cell->type == ctx->id("ICESTORM_LC"));

                    // TODO(q3k): handle when the Bel might be already the
                    // target of another constraint.
                    NPNR_ASSERT(z < 8);
                    auto target_bel = ctx->getBelByLocation(Loc(x, y, z++));
                    auto target_bel_name = ctx->getBelName(target_bel).str(ctx);
                    user.cell->attrs[ctx->id("BEL")] = target_bel_name;
                    log_info("  constrained '%s' to %s\n", user.cell->name.c_str(ctx), target_bel_name.c_str());
                }
            }

            // Handle the global buffer connections
            for (auto port : packed->ports) {
                PortInfo &pi = port.second;
                bool is_b_port;

                if (pi.name == ctx->id("PLLOUT_A_GLOBAL"))
                    is_b_port = false;
                else if (pi.name == ctx->id("PLLOUT_B_GLOBAL"))
                    is_b_port = true;
                else
                    continue;

                std::unique_ptr<CellInfo> gb =
                        create_padin_gbuf(ctx, packed.get(), pi.name,
                                          "$gbuf_" + ci->name.str(ctx) + "_pllout_" + (is_b_port ? "b" : "a"));
                new_cells.push_back(std::move(gb));
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
        pack_ram(ctx);
        place_plls(ctx);
        pack_special(ctx);
        if (!bool_or_default(ctx->settings, ctx->id("no_promote_globals"), false))
            promote_globals(ctx);
        ctx->assignArchInfo();
        constrain_chains(ctx);
        ctx->assignArchInfo();
        log_info("Checksum: 0x%08x\n", ctx->checksum());
        return true;
    } catch (log_execution_error_exception) {
        return false;
    }
}

NEXTPNR_NAMESPACE_END
