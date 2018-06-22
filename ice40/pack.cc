/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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
#include <unordered_set>
#include "cells.h"
#include "design_utils.h"
#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

// Pack LUTs and LUT-FF pairs
static void pack_lut_lutffs(Context *ctx)
{
    log_info("Packing LUT-FFs..\n");

    std::unordered_set<IdString> packed_cells;
    std::vector<CellInfo *> new_cells;
    for (auto cell : sorted(ctx->cells)) {
        CellInfo *ci = cell.second;
        if (ctx->verbose)
            log_info("cell '%s' is of type '%s'\n", ci->name.c_str(ctx),
                     ci->type.c_str(ctx));
        if (is_lut(ctx, ci)) {
            CellInfo *packed = create_ice_cell(ctx, "ICESTORM_LC",
                                               ci->name.str(ctx) + "_LC");
            std::copy(ci->attrs.begin(), ci->attrs.end(),
                      std::inserter(packed->attrs, packed->attrs.begin()));
            packed_cells.insert(ci->name);
            new_cells.push_back(packed);
            if (ctx->verbose)
                log_info("packed cell %s into %s\n", ci->name.c_str(ctx),
                         packed->name.c_str(ctx));
            // See if we can pack into a DFF
            // TODO: LUT cascade
            NetInfo *o = ci->ports.at(ctx->id("O")).net;
            CellInfo *dff = net_only_drives(ctx, o, is_ff, "D", true);
            auto lut_bel = ci->attrs.find(ctx->id("BEL"));
            bool packed_dff = false;
            if (dff) {
                if (ctx->verbose)
                    log_info("found attached dff %s\n", dff->name.c_str(ctx));
                auto dff_bel = dff->attrs.find(ctx->id("BEL"));
                if (lut_bel != ci->attrs.end() && dff_bel != dff->attrs.end() &&
                    lut_bel->second != dff_bel->second) {
                    // Locations don't match, can't pack
                } else {
                    lut_to_lc(ctx, ci, packed, false);
                    dff_to_lc(ctx, dff, packed, false);
                    ctx->nets.erase(o->name);
                    if (dff_bel != dff->attrs.end())
                        packed->attrs[ctx->id("BEL")] = dff_bel->second;
                    packed_cells.insert(dff->name);
                    if (ctx->verbose)
                        log_info("packed cell %s into %s\n",
                                 dff->name.c_str(ctx), packed->name.c_str(ctx));
                    packed_dff = true;
                }
            }
            if (!packed_dff) {
                lut_to_lc(ctx, ci, packed, true);
            }
        }
    }
    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    for (auto ncell : new_cells) {
        ctx->cells[ncell->name] = ncell;
    }
}

// Pack FFs not packed as LUTFFs
static void pack_nonlut_ffs(Context *ctx)
{
    log_info("Packing non-LUT FFs..\n");

    std::unordered_set<IdString> packed_cells;
    std::vector<CellInfo *> new_cells;

    for (auto cell : sorted(ctx->cells)) {
        CellInfo *ci = cell.second;
        if (is_ff(ctx, ci)) {
            CellInfo *packed = create_ice_cell(ctx, "ICESTORM_LC",
                                               ci->name.str(ctx) + "_DFFLC");
            std::copy(ci->attrs.begin(), ci->attrs.end(),
                      std::inserter(packed->attrs, packed->attrs.begin()));
            if (ctx->verbose)
                log_info("packed cell %s into %s\n", ci->name.c_str(ctx),
                         packed->name.c_str(ctx));
            packed_cells.insert(ci->name);
            new_cells.push_back(packed);
            dff_to_lc(ctx, ci, packed, true);
        }
    }
    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    for (auto ncell : new_cells) {
        ctx->cells[ncell->name] = ncell;
    }
}

// Pack carry logic
static void pack_carries(Context *ctx)
{
    log_info("Packing carries..\n");

    std::unordered_set<IdString> packed_cells;

    for (auto cell : sorted(ctx->cells)) {
        CellInfo *ci = cell.second;
        if (is_carry(ctx, ci)) {
            packed_cells.insert(cell.first);
            CellInfo *carry_ci_lc = net_only_drives(ctx, ci->ports.at("CI").net,
                                                    is_lc, "I3", false);
            if (!ci->ports.at("I0").net)
                log_error("SB_CARRY '%s' has disconnected port I0\n",
                          cell.first.c_str(ctx));
            if (!ci->ports.at("I1").net)
                log_error("SB_CARRY '%s' has disconnected port I1\n",
                          cell.first.c_str(ctx));

            std::unordered_set<IdString> i0_matches, i1_matches;
            auto &i0_usrs = ci->ports.at("I0").net->users;
            auto &i1_usrs = ci->ports.at("I1").net->users;
            // Find logic cells connected to both I0 and I1
            for (auto usr : i0_usrs) {
                if (is_lc(ctx, usr.cell) && usr.port == ctx->id("I1"))
                    i0_matches.insert(usr.cell->name);
            }
            for (auto usr : i1_usrs) {
                if (is_lc(ctx, usr.cell) && usr.port == ctx->id("I2"))
                    i1_matches.insert(usr.cell->name);
            }
            std::set<IdString> carry_lcs;
            std::set_intersection(i0_matches.begin(), i0_matches.end(),
                                  i1_matches.begin(), i1_matches.end(),
                                  std::inserter(carry_lcs, carry_lcs.begin()));
            CellInfo *carry_lc = nullptr;
            if (carry_ci_lc) {
                if (carry_lcs.find(carry_ci_lc->name) == carry_lcs.end())
                    log_error("SB_CARRY '%s' cannot be packed into any logic "
                              "cell (I0 and I1 connections do not match I3 "
                              "connection)\n",
                              cell.first.c_str(ctx));
                carry_lc = carry_ci_lc;
            } else {
                if (carry_lcs.empty())
                    log_error(
                            "SB_CARRY '%s' cannot be packed into any logic "
                            "cell (no logic cell connects to both I0 and I1)\n",
                            cell.first.c_str(ctx));
                carry_lc = ctx->cells.at(*carry_lcs.begin());
            }
            carry_lc->attrs[ctx->id("CARRY_ENABLE")] = "1";
            replace_port(ci, "CI", carry_lc, "CIN");
            replace_port(ci, "CO", carry_lc, "COUT");

            i0_usrs.erase(std::remove_if(i0_usrs.begin(), i0_usrs.end(),
                                         [ci, ctx](const PortRef &pr) {
                                             return pr.cell == ci &&
                                                    pr.port == ctx->id("I0");
                                         }));

            i1_usrs.erase(std::remove_if(i1_usrs.begin(), i1_usrs.end(),
                                         [ci, ctx](const PortRef &pr) {
                                             return pr.cell == ci &&
                                                    pr.port == ctx->id("I1");
                                         }));
        }
    }
    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
}

// "Pack" RAMs
static void pack_ram(Context *ctx)
{
    log_info("Packing RAMs..\n");

    std::unordered_set<IdString> packed_cells;
    std::vector<CellInfo *> new_cells;

    for (auto cell : sorted(ctx->cells)) {
        CellInfo *ci = cell.second;
        if (is_ram(ctx, ci)) {
            CellInfo *packed = create_ice_cell(ctx, "ICESTORM_RAM",
                                               ci->name.str(ctx) + "_RAM");
            packed_cells.insert(ci->name);
            new_cells.push_back(packed);
            for (auto param : ci->params)
                packed->params[param.first] = param.second;
            packed->params["NEG_CLK_W"] =
                    std::to_string(ci->type == ctx->id("SB_RAM40_4KNW") ||
                                   ci->type == ctx->id("SB_RAM40_4KNRNW"));
            packed->params["NEG_CLK_R"] =
                    std::to_string(ci->type == ctx->id("SB_RAM40_4KNR") ||
                                   ci->type == ctx->id("SB_RAM40_4KNRNW"));
            packed->type = ctx->id("ICESTORM_RAM");
            for (auto port : ci->ports) {
                PortInfo &pi = port.second;
                std::string newname = pi.name;
                size_t bpos = newname.find('[');
                if (bpos != std::string::npos) {
                    newname = newname.substr(0, bpos) + "_" +
                              newname.substr(bpos + 1,
                                             (newname.size() - bpos) - 2);
                }
                replace_port(ci, pi.name, packed, newname);
            }
        }
    }

    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    for (auto ncell : new_cells) {
        ctx->cells[ncell->name] = ncell;
    }
}

// Merge a net into a constant net
static void set_net_constant(const Context *ctx, NetInfo *orig,
                             NetInfo *constnet, bool constval)
{
    orig->driver.cell = nullptr;
    for (auto user : orig->users) {
        if (user.cell != nullptr) {
            CellInfo *uc = user.cell;
            if (ctx->verbose)
                log_info("%s user %s\n", orig->name.c_str(ctx),
                         uc->name.c_str(ctx));
            if ((is_lut(ctx, uc) || is_lc(ctx, uc)) &&
                (user.port.str(ctx).at(0) == 'I') && !constval) {
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

    CellInfo *gnd_cell = create_ice_cell(ctx, "ICESTORM_LC", "$PACKER_GND");
    gnd_cell->params[ctx->id("LUT_INIT")] = "0";
    NetInfo *gnd_net = new NetInfo;
    gnd_net->name = "$PACKER_GND_NET";
    gnd_net->driver.cell = gnd_cell;
    gnd_net->driver.port = ctx->id("O");

    CellInfo *vcc_cell = create_ice_cell(ctx, "ICESTORM_LC", "$PACKER_VCC");
    vcc_cell->params[ctx->id("LUT_INIT")] = "1";
    NetInfo *vcc_net = new NetInfo;
    vcc_net->name = ctx->id("$PACKER_VCC_NET");
    vcc_net->driver.cell = vcc_cell;
    vcc_net->driver.port = ctx->id("O");

    std::vector<IdString> dead_nets;

    bool gnd_used = false, vcc_used = false;

    for (auto net : sorted(ctx->nets)) {
        NetInfo *ni = net.second;
        if (ni->driver.cell != nullptr &&
            ni->driver.cell->type == ctx->id("GND")) {
            set_net_constant(ctx, ni, gnd_net, false);
            gnd_used = true;
            dead_nets.push_back(net.first);
        } else if (ni->driver.cell != nullptr &&
                   ni->driver.cell->type == ctx->id("VCC")) {
            set_net_constant(ctx, ni, vcc_net, true);
            vcc_used = true;
            dead_nets.push_back(net.first);
        }
    }

    if (gnd_used) {
        ctx->cells[gnd_cell->name] = gnd_cell;
        ctx->nets[gnd_net->name] = gnd_net;
    }

    if (vcc_used) {
        ctx->cells[vcc_cell->name] = vcc_cell;
        ctx->nets[vcc_net->name] = vcc_net;
    }

    for (auto dn : dead_nets)
        ctx->nets.erase(dn);
}

static bool is_nextpnr_iob(Context *ctx, CellInfo *cell)
{
    return cell->type == ctx->id("$nextpnr_ibuf") ||
           cell->type == ctx->id("$nextpnr_obuf") ||
           cell->type == ctx->id("$nextpnr_iobuf");
}

// Pack IO buffers
static void pack_io(Context *ctx)
{
    std::unordered_set<IdString> packed_cells;
    std::vector<CellInfo *> new_cells;

    log_info("Packing IOs..\n");

    for (auto cell : sorted(ctx->cells)) {
        CellInfo *ci = cell.second;
        if (is_nextpnr_iob(ctx, ci)) {
            CellInfo *sb = nullptr;
            if (ci->type == ctx->id("$nextpnr_ibuf") ||
                ci->type == ctx->id("$nextpnr_iobuf")) {
                sb = net_only_drives(ctx, ci->ports.at("O").net, is_sb_io,
                                     "PACKAGE_PIN", true, ci);

            } else if (ci->type == ctx->id("$nextpnr_obuf")) {
                sb = net_only_drives(ctx, ci->ports.at("I").net, is_sb_io,
                                     "PACKAGE_PIN", true, ci);
            }
            if (sb != nullptr) {
                // Trivial case, SB_IO used. Just destroy the net and the
                // iobuf
                log_info("%s feeds SB_IO %s, removing %s %s.\n",
                         ci->name.c_str(ctx), sb->name.c_str(ctx),
                         ci->type.c_str(ctx), ci->name.c_str(ctx));
                NetInfo *net = sb->ports.at("PACKAGE_PIN").net;
                if (net != nullptr) {
                    ctx->nets.erase(net->name);
                    sb->ports.at("PACKAGE_PIN").net = nullptr;
                }
            } else {
                // Create a SB_IO buffer
                sb = create_ice_cell(ctx, "SB_IO",
                                     ci->name.str(ctx) + "$sb_io");
                nxio_to_sb(ctx, ci, sb);
                new_cells.push_back(sb);
            }
            packed_cells.insert(ci->name);
            std::copy(ci->attrs.begin(), ci->attrs.end(),
                      std::inserter(sb->attrs, sb->attrs.begin()));
        }
    }
    for (auto pcell : packed_cells) {
        ctx->cells.erase(pcell);
    }
    for (auto ncell : new_cells) {
        ctx->cells[ncell->name] = ncell;
    }
}

static void insert_global(Context *ctx, NetInfo *net, bool is_reset,
                          bool is_cen)
{
    std::string glb_name = net->name.str(ctx) + std::string("_$glb_") +
                           (is_reset ? "sr" : (is_cen ? "ce" : "clk"));
    CellInfo *gb = create_ice_cell(ctx, "SB_GB", "$gbuf_" + glb_name);
    gb->ports[ctx->id("USER_SIGNAL_TO_GLOBAL_BUFFER")].net = net;
    PortRef pr;
    pr.cell = gb;
    pr.port = ctx->id("USER_SIGNAL_TO_GLOBAL_BUFFER");
    net->users.push_back(pr);

    pr.cell = gb;
    pr.port = ctx->id("GLOBAL_BUFFER_OUTPUT");
    NetInfo *glbnet = new NetInfo();
    glbnet->name = ctx->id(glb_name);
    glbnet->driver = pr;
    ctx->nets[glbnet->name] = glbnet;
    gb->ports[ctx->id("GLOBAL_BUFFER_OUTPUT")].net = glbnet;
    std::vector<PortRef> keep_users;
    for (auto user : net->users) {
        if (is_clock_port(ctx, user) ||
            (is_reset && is_reset_port(ctx, user)) ||
            (is_cen && is_enable_port(ctx, user))) {
            user.cell->ports[user.port].net = glbnet;
            glbnet->users.push_back(user);
        } else {
            keep_users.push_back(user);
        }
    }
    net->users = keep_users;
    ctx->cells[gb->name] = gb;
}

// Simple global promoter (clock only)
static void promote_globals(Context *ctx)
{
    log_info("Promoting globals..\n");

    std::map<IdString, int> clock_count, reset_count, cen_count;
    for (auto net : sorted(ctx->nets)) {
        NetInfo *ni = net.second;
        if (ni->driver.cell != nullptr && !is_global_net(ctx, ni)) {
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
            }
        }
    }
    int prom_globals = 0, prom_resets = 0, prom_cens = 0;
    int gbs_available = 8;
    for (auto cell : ctx->cells)
        if (is_gbuf(ctx, cell.second))
            --gbs_available;
    while (prom_globals < gbs_available) {
        auto global_clock =
                std::max_element(clock_count.begin(), clock_count.end(),
                                 [](const std::pair<IdString, int> &a,
                                    const std::pair<IdString, int> &b) {
                                     return a.second < b.second;
                                 });

        auto global_reset =
                std::max_element(reset_count.begin(), reset_count.end(),
                                 [](const std::pair<IdString, int> &a,
                                    const std::pair<IdString, int> &b) {
                                     return a.second < b.second;
                                 });
        auto global_cen =
                std::max_element(cen_count.begin(), cen_count.end(),
                                 [](const std::pair<IdString, int> &a,
                                    const std::pair<IdString, int> &b) {
                                     return a.second < b.second;
                                 });
        if (global_reset->second > global_clock->second && prom_resets < 4) {
            NetInfo *rstnet = ctx->nets[global_reset->first];
            insert_global(ctx, rstnet, true, false);
            ++prom_globals;
            ++prom_resets;
            clock_count.erase(rstnet->name);
            reset_count.erase(rstnet->name);
            cen_count.erase(rstnet->name);
        } else if (global_cen->second > global_clock->second && prom_cens < 4) {
            NetInfo *cennet = ctx->nets[global_cen->first];
            insert_global(ctx, cennet, false, true);
            ++prom_globals;
            ++prom_cens;
            clock_count.erase(cennet->name);
            reset_count.erase(cennet->name);
            cen_count.erase(cennet->name);
        } else if (global_clock->second != 0) {
            NetInfo *clknet = ctx->nets[global_clock->first];
            insert_global(ctx, clknet, false, false);
            ++prom_globals;
            clock_count.erase(clknet->name);
            reset_count.erase(clknet->name);
            cen_count.erase(clknet->name);
        } else {
            break;
        }
    }
}

// Main pack function
bool pack_design(Context *ctx)
{
    try {
        log_break();
        pack_constants(ctx);
        promote_globals(ctx);
        pack_io(ctx);
        pack_lut_lutffs(ctx);
        pack_nonlut_ffs(ctx);
        pack_ram(ctx);
        log_info("Checksum: 0x%08x\n", ctx->checksum());
        return true;
    } catch (log_execution_error_exception) {
        return false;
    }
}

NEXTPNR_NAMESPACE_END
