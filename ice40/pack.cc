/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
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

NEXTPNR_NAMESPACE_BEGIN

// Pack LUTs and LUT-FF pairs
static void pack_lut_lutffs(Design *design)
{
    log_info("Packing LUT-FFs..\n");

    std::unordered_set<IdString> packed_cells;
    std::vector<CellInfo *> new_cells;
    for (auto cell : design->cells) {
        CellInfo *ci = cell.second;
        log_info("cell '%s' is of type '%s'\n", ci->name.c_str(),
                 ci->type.c_str());
        if (is_lut(ci)) {
            CellInfo *packed = create_ice_cell(design, "ICESTORM_LC",
                                               ci->name.str() + "_LC");
            std::copy(ci->attrs.begin(), ci->attrs.end(),
                      std::inserter(packed->attrs, packed->attrs.begin()));
            packed_cells.insert(ci->name);
            new_cells.push_back(packed);
            log_info("packed cell %s into %s\n", ci->name.c_str(),
                     packed->name.c_str());
            // See if we can pack into a DFF
            // TODO: LUT cascade
            NetInfo *o = ci->ports.at("O").net;
            CellInfo *dff = net_only_drives(o, is_ff, "D", true);
            auto lut_bel = ci->attrs.find("BEL");
            bool packed_dff = false;
            if (dff) {
                log_info("found attached dff %s\n", dff->name.c_str());
                auto dff_bel = dff->attrs.find("BEL");
                if (lut_bel != ci->attrs.end() && dff_bel != dff->attrs.end() &&
                    lut_bel->second != dff_bel->second) {
                    // Locations don't match, can't pack
                } else {
                    lut_to_lc(ci, packed, false);
                    dff_to_lc(dff, packed, false);
                    design->nets.erase(o->name);
                    if (dff_bel != dff->attrs.end())
                        packed->attrs["BEL"] = dff_bel->second;
                    packed_cells.insert(dff->name);
                    log_info("packed cell %s into %s\n", dff->name.c_str(),
                             packed->name.c_str());
                    packed_dff = true;
                }
            }
            if (!packed_dff) {
                lut_to_lc(ci, packed, true);
            }
        }
    }
    for (auto pcell : packed_cells) {
        design->cells.erase(pcell);
    }
    for (auto ncell : new_cells) {
        design->cells[ncell->name] = ncell;
    }
}

// Pack FFs not packed as LUTFFs
static void pack_nonlut_ffs(Design *design)
{
    log_info("Packing non-LUT FFs..\n");

    std::unordered_set<IdString> packed_cells;
    std::vector<CellInfo *> new_cells;

    for (auto cell : design->cells) {
        CellInfo *ci = cell.second;
        if (is_ff(ci)) {
            CellInfo *packed = create_ice_cell(design, "ICESTORM_LC",
                                               ci->name.str() + "_DFFLC");
            std::copy(ci->attrs.begin(), ci->attrs.end(),
                      std::inserter(packed->attrs, packed->attrs.begin()));
            log_info("packed cell %s into %s\n", ci->name.c_str(),
                     packed->name.c_str());
            packed_cells.insert(ci->name);
            new_cells.push_back(packed);
            dff_to_lc(ci, packed, true);
        }
    }
    for (auto pcell : packed_cells) {
        design->cells.erase(pcell);
    }
    for (auto ncell : new_cells) {
        design->cells[ncell->name] = ncell;
    }
}

// "Pack" RAMs
static void pack_ram(Design *design)
{
    log_info("Packing RAMs..\n");

    std::unordered_set<IdString> packed_cells;
    std::vector<CellInfo *> new_cells;

    for (auto cell : design->cells) {
        CellInfo *ci = cell.second;
        if (is_ram(ci)) {
            CellInfo *packed = create_ice_cell(design, "ICESTORM_RAM",
                                               ci->name.str() + "_RAM");
            packed_cells.insert(ci->name);
            new_cells.push_back(packed);
            for (auto param : ci->params)
                packed->params[param.first] = param.second;
            packed->params["NEG_CLK_W"] =
                    std::to_string(ci->type == "SB_RAM40_4KNW" ||
                                   ci->type == "SB_RAM40_4KNRNW");
            packed->params["NEG_CLK_R"] =
                    std::to_string(ci->type == "SB_RAM40_4KNR" ||
                                   ci->type == "SB_RAM40_4KNRNW");
            packed->type = "ICESTORM_RAM";
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
        design->cells.erase(pcell);
    }
    for (auto ncell : new_cells) {
        design->cells[ncell->name] = ncell;
    }
}

// Merge a net into a constant net
static void set_net_constant(NetInfo *orig, NetInfo *constnet, bool constval)
{
    orig->driver.cell = nullptr;
    for (auto user : orig->users) {
        if (user.cell != nullptr) {
            CellInfo *uc = user.cell;
            log_info("%s user %s\n", orig->name.c_str(), uc->name.c_str());
            if (is_lut(uc) && (user.port.str().at(0) == 'I') && !constval) {
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
static void pack_constants(Design *design)
{
    log_info("Packing constants..\n");

    CellInfo *gnd_cell = create_ice_cell(design, "ICESTORM_LC", "$PACKER_GND");
    gnd_cell->params["LUT_INIT"] = "0";
    NetInfo *gnd_net = new NetInfo;
    gnd_net->name = "$PACKER_GND_NET";
    gnd_net->driver.cell = gnd_cell;
    gnd_net->driver.port = "O";

    CellInfo *vcc_cell = create_ice_cell(design, "ICESTORM_LC", "$PACKER_VCC");
    vcc_cell->params["LUT_INIT"] = "1";
    NetInfo *vcc_net = new NetInfo;
    vcc_net->name = "$PACKER_VCC_NET";
    vcc_net->driver.cell = vcc_cell;
    vcc_net->driver.port = "O";

    std::vector<IdString> dead_nets;

    for (auto net : design->nets) {
        NetInfo *ni = net.second;
        if (ni->driver.cell != nullptr && ni->driver.cell->type == "GND") {
            set_net_constant(ni, gnd_net, false);
            design->cells[gnd_cell->name] = gnd_cell;
            design->nets[gnd_net->name] = gnd_net;
            dead_nets.push_back(net.first);
        } else if (ni->driver.cell != nullptr &&
                   ni->driver.cell->type == "VCC") {
            set_net_constant(ni, vcc_net, true);
            design->cells[vcc_cell->name] = vcc_cell;
            design->nets[vcc_net->name] = vcc_net;
            dead_nets.push_back(net.first);
        }
    }

    for (auto dn : dead_nets)
        design->nets.erase(dn);
}

static bool is_nextpnr_iob(CellInfo *cell)
{
    return cell->type == "$nextpnr_ibuf" || cell->type == "$nextpnr_obuf" ||
           cell->type == "$nextpnr_iobuf";
}

// Pack IO buffers
static void pack_io(Design *design)
{
    std::unordered_set<IdString> packed_cells;
    std::vector<CellInfo *> new_cells;

    log_info("Packing IOs..\n");

    for (auto cell : design->cells) {
        CellInfo *ci = cell.second;
        if (is_nextpnr_iob(ci)) {
            CellInfo *sb = nullptr;
            if (ci->type == "$nextpnr_ibuf" || ci->type == "$nextpnr_iobuf") {
                sb = net_only_drives(ci->ports.at("O").net, is_sb_io,
                                     "PACKAGE_PIN", true, ci);

            } else if (ci->type == "$nextpnr_obuf") {
                sb = net_only_drives(ci->ports.at("I").net, is_sb_io,
                                     "PACKAGE_PIN", true, ci);
            }
            if (sb != nullptr) {
                // Trivial case, SB_IO used. Just destroy the net and the
                // iobuf
                log_info("%s feeds SB_IO %s, removing %s %s.\n",
                         ci->name.c_str(), sb->name.c_str(), ci->type.c_str(),
                         ci->name.c_str());
                NetInfo *net = sb->ports.at("PACKAGE_PIN").net;
                if (net != nullptr) {
                    design->nets.erase(net->name);
                    sb->ports.at("PACKAGE_PIN").net = nullptr;
                }
            } else {
                // Create a SB_IO buffer
                sb = create_ice_cell(design, "SB_IO");
                nxio_to_sb(ci, sb);
                new_cells.push_back(sb);
            }
            packed_cells.insert(ci->name);
            std::copy(ci->attrs.begin(), ci->attrs.end(),
                      std::inserter(sb->attrs, sb->attrs.begin()));
        }
    }
    for (auto pcell : packed_cells) {
        design->cells.erase(pcell);
    }
    for (auto ncell : new_cells) {
        design->cells[ncell->name] = ncell;
    }
}

static void insert_global(Design *design, NetInfo *net, bool is_reset,
                          bool is_cen)
{
    CellInfo *gb = create_ice_cell(design, "SB_GB");
    gb->ports["USER_SIGNAL_TO_GLOBAL_BUFFER"].net = net;
    PortRef pr;
    pr.cell = gb;
    pr.port = "USER_SIGNAL_TO_GLOBAL_BUFFER";
    net->users.push_back(pr);

    pr.cell = gb;
    pr.port = "GLOBAL_BUFFER_OUTPUT";
    NetInfo *glbnet = new NetInfo();
    glbnet->name = net->name.str() + std::string("_glb_") +
                   (is_reset ? "sr" : (is_cen ? "ce" : "clk"));
    glbnet->driver = pr;
    design->nets[glbnet->name] = glbnet;
    gb->ports["GLOBAL_BUFFER_OUTPUT"].net = glbnet;
    std::vector<PortRef> keep_users;
    for (auto user : net->users) {
        if (is_clock_port(user) || (is_reset && is_reset_port(user)) ||
            (is_cen && is_enable_port(user))) {
            user.cell->ports[user.port].net = glbnet;
            glbnet->users.push_back(user);
        } else {
            keep_users.push_back(user);
        }
    }
    net->users = keep_users;
    design->cells[gb->name] = gb;
}

// Simple global promoter (clock only)
static void promote_globals(Design *design)
{
    log_info("Promoting globals..\n");

    std::unordered_map<IdString, int> clock_count, reset_count, cen_count;
    for (auto net : design->nets) {
        NetInfo *ni = net.second;
        if (ni->driver.cell != nullptr && !is_global_net(ni)) {
            clock_count[net.first] = 0;
            reset_count[net.first] = 0;
            cen_count[net.first] = 0;

            for (auto user : ni->users) {
                if (is_clock_port(user))
                    clock_count[net.first]++;
                if (is_reset_port(user))
                    reset_count[net.first]++;
                if (is_enable_port(user))
                    cen_count[net.first]++;
            }
        }
    }
    int prom_globals = 0, prom_resets = 0, prom_cens = 0;
    int gbs_available = 8;
    for (auto cell : design->cells)
        if (is_gbuf(cell.second))
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
            NetInfo *rstnet = design->nets[global_reset->first];
            insert_global(design, rstnet, true, false);
            ++prom_globals;
            ++prom_resets;
            clock_count.erase(rstnet->name);
            reset_count.erase(rstnet->name);

        } else if (global_cen->second > global_clock->second && prom_cens < 4) {
            NetInfo *cennet = design->nets[global_cen->first];
            insert_global(design, cennet, false, true);
            ++prom_globals;
            ++prom_cens;
            cen_count.erase(cennet->name);
            clock_count.erase(cennet->name);
        } else if (global_clock->second != 0) {
            NetInfo *clknet = design->nets[global_clock->first];
            insert_global(design, clknet, false, false);
            ++prom_globals;
            clock_count.erase(clknet->name);
        } else {
            break;
        }
    }
}

// Main pack function
void pack_design(Design *design)
{
    pack_constants(design);
    promote_globals(design);
    pack_io(design);
    pack_lut_lutffs(design);
    pack_nonlut_ffs(design);
    pack_ram(design);
}

NEXTPNR_NAMESPACE_END
