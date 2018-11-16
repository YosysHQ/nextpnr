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

static bool is_nextpnr_iob(Context *ctx, CellInfo *cell)
{
    return cell->type == ctx->id("$nextpnr_ibuf") || cell->type == ctx->id("$nextpnr_obuf") ||
           cell->type == ctx->id("$nextpnr_iobuf");
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
            CellInfo *sb = nullptr;
            if (ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_iobuf")) {
                sb = net_only_drives(ctx, ci->ports.at(ctx->id("O")).net, is_sb_io, ctx->id("PACKAGE_PIN"), true, ci);

            } else if (ci->type == ctx->id("$nextpnr_obuf")) {
                sb = net_only_drives(ctx, ci->ports.at(ctx->id("I")).net, is_sb_io, ctx->id("PACKAGE_PIN"), true, ci);
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
                    log_error("PACKAGE_PIN of SB_IO '%s' connected to more than a single top level IO.\n",
                              sb->name.c_str(ctx));

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
        } else if (is_sb_io(ctx, ci)) {
            NetInfo *net = ci->ports.at(ctx->id("PACKAGE_PIN")).net;
            if ((net != nullptr) && (net->users.size() > 1))
                log_error("PACKAGE_PIN of SB_IO '%s' connected to more than a single top level IO.\n",
                          ci->name.c_str(ctx));
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
    return !is_sb_io(ctx, port.cell) && !is_sb_pll40(ctx, port.cell) && !is_sb_pll40_pad(ctx, port.cell) &&
           port.cell->type != ctx->id("SB_GB");
}

static void insert_global(Context *ctx, NetInfo *net, bool is_reset, bool is_cen, bool is_logic)
{
    log_info("promoting %s%s%s%s\n", net->name.c_str(ctx), is_reset ? " [reset]" : "", is_cen ? " [cen]" : "",
             is_logic ? " [logic]" : "");

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
    const int enable_fanout_thresh = 5;
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
    int gbs_available = 8;
    for (auto &cell : ctx->cells)
        if (is_gbuf(ctx, cell.second.get()))
            --gbs_available;
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
            (global_logic->second > global_cen->second || prom_cens >= 4) &&
            (global_logic->second > global_reset->second || prom_resets >= 4)) {
            NetInfo *logicnet = ctx->nets[global_logic->first].get();
            insert_global(ctx, logicnet, false, false, true);
            ++prom_globals;
            ++prom_logics;
            clock_count.erase(logicnet->name);
            reset_count.erase(logicnet->name);
            cen_count.erase(logicnet->name);
            logic_count.erase(logicnet->name);
        } else if (global_reset->second > global_clock->second && prom_resets < 4) {
            NetInfo *rstnet = ctx->nets[global_reset->first].get();
            insert_global(ctx, rstnet, true, false, false);
            ++prom_globals;
            ++prom_resets;
            clock_count.erase(rstnet->name);
            reset_count.erase(rstnet->name);
            cen_count.erase(rstnet->name);
            logic_count.erase(rstnet->name);
        } else if (global_cen->second > global_clock->second && prom_cens < 4 &&
                   global_cen->second > enable_fanout_thresh) {
            NetInfo *cennet = ctx->nets[global_cen->first].get();
            insert_global(ctx, cennet, false, true, false);
            ++prom_globals;
            ++prom_cens;
            clock_count.erase(cennet->name);
            reset_count.erase(cennet->name);
            cen_count.erase(cennet->name);
            logic_count.erase(cennet->name);
        } else if (global_clock->second != 0) {
            NetInfo *clknet = ctx->nets[global_clock->first].get();
            insert_global(ctx, clknet, false, false, false);
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
            replace_port(ci, ctx->id("CLKLFEN"), packed.get(), ctx->id("CLKLFEN"));
            replace_port(ci, ctx->id("CLKLFPU"), packed.get(), ctx->id("CLKLFPU"));
            if (/*bool_or_default(ci->attrs, ctx->id("ROUTE_THROUGH_FABRIC"))*/ true) { // FIXME
                replace_port(ci, ctx->id("CLKLF"), packed.get(), ctx->id("CLKLF_FABRIC"));
            } else {
                replace_port(ci, ctx->id("CLKLF"), packed.get(), ctx->id("CLKLF"));
            }
            new_cells.push_back(std::move(packed));
        } else if (is_sb_hfosc(ctx, ci)) {
            std::unique_ptr<CellInfo> packed =
                    create_ice_cell(ctx, ctx->id("ICESTORM_HFOSC"), ci->name.str(ctx) + "_OSC");
            packed_cells.insert(ci->name);
            packed->params[ctx->id("CLKHF_DIV")] = str_or_default(ci->params, ctx->id("CLKHF_DIV"), "0b00");
            replace_port(ci, ctx->id("CLKHFEN"), packed.get(), ctx->id("CLKHFEN"));
            replace_port(ci, ctx->id("CLKHFPU"), packed.get(), ctx->id("CLKHFPU"));
            if (/*bool_or_default(ci->attrs, ctx->id("ROUTE_THROUGH_FABRIC"))*/ true) { // FIXME
                replace_port(ci, ctx->id("CLKHF"), packed.get(), ctx->id("CLKHF_FABRIC"));
            } else {
                replace_port(ci, ctx->id("CLKHF"), packed.get(), ctx->id("CLKHF"));
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

            auto feedback_path = packed->params[ctx->id("FEEDBACK_PATH")];
            packed->params[ctx->id("FEEDBACK_PATH")] =
                    feedback_path == "DELAY"
                            ? "0"
                            : feedback_path == "SIMPLE" ? "1"
                                                        : feedback_path == "PHASE_AND_DELAY"
                                                                  ? "2"
                                                                  : feedback_path == "EXTERNAL" ? "6" : feedback_path;
            packed->params[ctx->id("PLLTYPE")] = std::to_string(sb_pll40_type(ctx, ci));

            NetInfo *pad_packagepin_net = nullptr;

            for (auto port : ci->ports) {
                PortInfo &pi = port.second;
                std::string newname = pi.name.str(ctx);
                size_t bpos = newname.find('[');
                if (bpos != std::string::npos) {
                    newname = newname.substr(0, bpos) + "_" + newname.substr(bpos + 1, (newname.size() - bpos) - 2);
                }
                if (pi.name == ctx->id("PLLOUTCOREA"))
                    newname = "PLLOUT_A";
                if (pi.name == ctx->id("PLLOUTCOREB"))
                    newname = "PLLOUT_B";
                if (pi.name == ctx->id("PLLOUTCORE"))
                    newname = "PLLOUT_A";

                if (pi.name == ctx->id("PLLOUTGLOBAL") ||
                    pi.name == ctx->id("PLLOUTGLOBALA") ||
                    pi.name == ctx->id("PLLOUTGLOBALB"))
                    /* We deal with those after PLL has been constrained */
                    continue;

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

            // If PLL is not constrained already, do that - we need this
            // information to then constrain the LOCK LUT.
            BelId pll_bel;
            bool constrained = false;
            if (packed->attrs.find(ctx->id("BEL")) == packed->attrs.end()) {
                for (auto bel : ctx->getBels()) {
                    if (ctx->getBelType(bel) != id_ICESTORM_PLL)
                        continue;

                    // A PAD PLL must have its' PACKAGEPIN on the SB_IO that's shared
                    // with PLLOUT_A.
                    if (is_pad) {
                        auto pll_sb_io_belpin = ctx->getIOBSharingPLLPin(bel, id_PLLOUT_A);
                        NPNR_ASSERT(pad_packagepin_net != nullptr);
                        auto pll_packagepin_driver = pad_packagepin_net->driver;
                        NPNR_ASSERT(pll_packagepin_driver.cell != nullptr);
                        if (pll_packagepin_driver.cell->type != ctx->id("SB_IO")) {
                            log_error("PLL '%s' has a PACKAGEPIN driven by "
                                      "an %s, should be directly connected to an input SB_IO\n",
                                      ci->name.c_str(ctx), pll_packagepin_driver.cell->type.c_str(ctx));
                        }

                        auto packagepin_cell = pll_packagepin_driver.cell;
                        auto packagepin_bel_name = packagepin_cell->attrs.find(ctx->id("BEL"));
                        if (packagepin_bel_name == packagepin_cell->attrs.end()) {
                            log_error("PLL '%s' PACKAGEPIN SB_IO '%s' is unconstrained\n", ci->name.c_str(ctx),
                                      packagepin_cell->name.c_str(ctx));
                        }
                        auto packagepin_bel = ctx->getBelByName(ctx->id(packagepin_bel_name->second));
                        if (pll_sb_io_belpin.bel != packagepin_bel) {
                            log_error("PLL '%s' PACKAGEPIN is connected to pin %s, can only be pin %s\n",
                                      ci->name.c_str(ctx), ctx->getBelPackagePin(packagepin_bel).c_str(),
                                      ctx->getBelPackagePin(pll_sb_io_belpin.bel).c_str());
                        }
                        if (pad_packagepin_net->users.size() != 1) {
                            log_error("PLL '%s' clock input '%s' can only drive PLL\n", ci->name.c_str(ctx),
                                      pad_packagepin_net->name.c_str(ctx));
                        }
                        // Set an attribute about this PLL's PAD SB_IO.
                        packed->attrs[ctx->id("BEL_PAD_INPUT")] = packagepin_bel_name->second;
                        // Remove the connection from the SB_IO to the PLL.
                        packagepin_cell->ports.erase(pll_packagepin_driver.port);
                    }

                    log_info("  constrained PLL '%s' to %s\n", packed->name.c_str(ctx),
                             ctx->getBelName(bel).c_str(ctx));
                    packed->attrs[ctx->id("BEL")] = ctx->getBelName(bel).str(ctx);
                    pll_bel = bel;
                    constrained = true;
                }
                if (!constrained) {
                    log_error("Could not constrain PLL '%s' to any PLL Bel (too many PLLs?)\n",
                              packed->name.c_str(ctx));
                }
            }

            // Delete the original PACKAGEPIN net if needed.
            if (pad_packagepin_net != nullptr) {
                for (auto user : pad_packagepin_net->users) {
                    user.cell->ports.erase(user.port);
                }
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
                unsigned int lut_count = 0;
                for (const auto &user : port.net->users) {
                    NPNR_ASSERT(user.cell != nullptr);
                    if (user.cell->type == ctx->id("ICESTORM_LC")) {
                        found_lut = true;
                        lut_count++;
                    } else {
                        all_luts = false;
                    }
                }

                if (found_lut && all_luts) {
                    // Every user is a LUT, carry on now.
                } else if (found_lut && !all_luts && lut_count < 8) {
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
            for (auto port : ci->ports) {
                PortInfo &pi = port.second;
                bool is_b_port;
                int x=-1, y=-1;

                if (pi.name == ctx->id("PLLOUTGLOBAL") || pi.name == ctx->id("PLLOUTGLOBALA"))
                    is_b_port = false;
                else if (pi.name == ctx->id("PLLOUTGLOBALB"))
                    is_b_port = true;
                else
                    continue;

                std::unique_ptr<CellInfo> gb = create_ice_cell(ctx, ctx->id("SB_GB"), "$gbuf_" + ci->name.str(ctx) + "_out_" + (is_b_port ? "b" : "a"));
                gb->gbInfo.forIO = true;

                replace_port(ci, ctx->id(pi.name.c_str(ctx)), gb.get(), ctx->id("GLOBAL_BUFFER_OUTPUT"));

                BelId bel_out = ctx->getIOBSharingPLLPin(pll_bel, is_b_port ? id_PLLOUT_B : id_PLLOUT_A).bel;
                Loc loc_out= ctx->getBelLocation(bel_out);

                for (int i=0; i<ctx->chip_info->num_global_networks; i++)
                    if ((ctx->chip_info->global_network_info[i].pi_gb_x == loc_out.x) &&
                        (ctx->chip_info->global_network_info[i].pi_gb_y == loc_out.y) &&
                        (ctx->chip_info->global_network_info[i].pi_gb_pio == loc_out.z)) {
                        x = ctx->chip_info->global_network_info[i].gb_x;
                        y = ctx->chip_info->global_network_info[i].gb_y;
                        break;
                    }

                if (x < 0 || y < 0)
                    log_error("Unable to find global network matching PLL output\n");

                auto gb_bel = ctx->getBelByLocation(Loc(x, y, 2));
                auto gb_bel_name = ctx->getBelName(gb_bel).str(ctx);

                NPNR_ASSERT(ctx->getBelType(gb_bel) == id_SB_GB);

                gb->attrs[ctx->id("BEL")] = gb_bel_name;
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
        pack_special(ctx);
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
