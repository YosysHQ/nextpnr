/*
 *  nextpnr -- Next Generation Place and Route
 *
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

#include <algorithm>
#include <boost/optional.hpp>
#include <iterator>
#include <queue>
#include <unordered_set>
#include "cells.h"
#include "chain_utils.h"
#include "design_utils.h"
#include "globals.h"
#include "log.h"
#include "timing.h"
#include "util.h"
NEXTPNR_NAMESPACE_BEGIN

static bool is_nextpnr_iob(Context *ctx, CellInfo *cell)
{
    return cell->type == ctx->id("$nextpnr_ibuf") || cell->type == ctx->id("$nextpnr_obuf") ||
           cell->type == ctx->id("$nextpnr_iobuf");
}

class Ecp5Packer
{
  public:
    Ecp5Packer(Context *ctx) : ctx(ctx){};

  private:
    // Process the contents of packed_cells and new_cells
    void flush_cells()
    {
        for (auto pcell : packed_cells) {
            ctx->cells.erase(pcell);
        }
        for (auto &ncell : new_cells) {
            ctx->cells[ncell->name] = std::move(ncell);
        }
        packed_cells.clear();
        new_cells.clear();
    }

    // Find FFs associated with LUTs, or LUT expansion muxes
    void find_lutff_pairs()
    {
        log_info("Finding LUTFF pairs...\n");
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (is_lut(ctx, ci) || is_pfumx(ctx, ci) || is_l6mux(ctx, ci)) {
                NetInfo *znet = ci->ports.at(ctx->id("Z")).net;
                if (znet != nullptr) {
                    CellInfo *ff = net_only_drives(ctx, znet, is_ff, ctx->id("DI"), false);
                    if (ff != nullptr) {
                        lutffPairs[ci->name] = ff->name;
                        fflutPairs[ff->name] = ci->name;
                    }
                }
            }
        }
    }

    const NetInfo *net_or_nullptr(CellInfo *cell, IdString port)
    {
        auto fnd = cell->ports.find(port);
        if (fnd == cell->ports.end())
            return nullptr;
        else
            return fnd->second.net;
    }

    // Return whether two FFs can be packed together in the same slice
    bool can_pack_ffs(CellInfo *ff0, CellInfo *ff1)
    {
        if (str_or_default(ff0->params, ctx->id("GSR"), "DISABLED") !=
            str_or_default(ff1->params, ctx->id("GSR"), "DISABLED"))
            return false;
        if (str_or_default(ff0->params, ctx->id("SRMODE"), "LSR_OVER_CE") !=
            str_or_default(ff1->params, ctx->id("SRMODE"), "LSR_OVER_CE"))
            return false;
        if (str_or_default(ff0->params, ctx->id("CEMUX"), "1") != str_or_default(ff1->params, ctx->id("CEMUX"), "1"))
            return false;
        if (str_or_default(ff0->params, ctx->id("LSRMUX"), "LSR") !=
            str_or_default(ff1->params, ctx->id("LSRMUX"), "LSR"))
            return false;
        if (str_or_default(ff0->params, ctx->id("CLKMUX"), "CLK") !=
            str_or_default(ff1->params, ctx->id("CLKMUX"), "CLK"))
            return false;
        if (net_or_nullptr(ff0, ctx->id("CLK")) != net_or_nullptr(ff1, ctx->id("CLK")))
            return false;
        if (net_or_nullptr(ff0, ctx->id("CE")) != net_or_nullptr(ff1, ctx->id("CE")))
            return false;
        if (net_or_nullptr(ff0, ctx->id("LSR")) != net_or_nullptr(ff1, ctx->id("LSR")))
            return false;
        return true;
    }

    // Return whether or not an FF can be added to a tile (pairing checks must also be done using the fn above)
    bool can_add_ff_to_tile(const std::vector<CellInfo *> &tile_ffs, CellInfo *ff0)
    {
        for (const auto &existing : tile_ffs) {
            if (net_or_nullptr(existing, ctx->id("CLK")) != net_or_nullptr(ff0, ctx->id("CLK")))
                return false;
            if (net_or_nullptr(existing, ctx->id("LSR")) != net_or_nullptr(ff0, ctx->id("LSR")))
                return false;
            if (str_or_default(existing->params, ctx->id("CLKMUX"), "CLK") !=
                str_or_default(ff0->params, ctx->id("CLKMUX"), "CLK"))
                return false;
            if (str_or_default(existing->params, ctx->id("LSRMUX"), "LSR") !=
                str_or_default(ff0->params, ctx->id("LSRMUX"), "LSR"))
                return false;
            if (str_or_default(existing->params, ctx->id("SRMODE"), "LSR_OVER_CE") !=
                str_or_default(ff0->params, ctx->id("SRMODE"), "LSR_OVER_CE"))
                return false;
        }
        return true;
    }

    // Return true if a FF can be added to a DPRAM slice
    bool can_pack_ff_dram(CellInfo *dpram, CellInfo *ff)
    {
        std::string wckmux = str_or_default(dpram->params, ctx->id("WCKMUX"), "WCK");
        std::string clkmux = str_or_default(ff->params, ctx->id("CLKMUX"), "CLK");
        if (wckmux != clkmux && !(wckmux == "WCK" && clkmux == "CLK"))
            return false;
        std::string wremux = str_or_default(dpram->params, ctx->id("WREMUX"), "WRE");
        std::string lsrmux = str_or_default(ff->params, ctx->id("LSRMUX"), "LSR");
        if (wremux != lsrmux && !(wremux == "WRE" && lsrmux == "LSR"))
            return false;
        return true;
    }

    // Return true if two LUTs can be paired considering FF compatibility
    bool can_pack_lutff(IdString lut0, IdString lut1)
    {
        auto ff0 = lutffPairs.find(lut0), ff1 = lutffPairs.find(lut1);
        if (ff0 != lutffPairs.end() && ff1 != lutffPairs.end()) {
            return can_pack_ffs(ctx->cells.at(ff0->second).get(), ctx->cells.at(ff1->second).get());
        } else {
            return true;
        }
    }

    // Find "closely connected" LUTs and pair them together
    void pair_luts()
    {
        log_info("Finding LUT-LUT pairs...\n");
        std::unordered_set<IdString> procdLuts;
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (is_lut(ctx, ci) && procdLuts.find(cell.first) == procdLuts.end()) {
                NetInfo *znet = ci->ports.at(ctx->id("Z")).net;
                std::vector<NetInfo *> inpnets;
                if (znet != nullptr) {
                    for (auto user : znet->users) {
                        if (is_lut(ctx, user.cell) && user.cell != ci &&
                            procdLuts.find(user.cell->name) == procdLuts.end()) {
                            if (can_pack_lutff(ci->name, user.cell->name)) {
                                procdLuts.insert(ci->name);
                                procdLuts.insert(user.cell->name);
                                lutPairs[ci->name] = user.cell->name;
                                goto paired;
                            }
                        }
                    }
                    if (false) {
                    paired:
                        continue;
                    }
                }
                if (lutffPairs.find(ci->name) != lutffPairs.end()) {
                    NetInfo *qnet = ctx->cells.at(lutffPairs[ci->name])->ports.at(ctx->id("Q")).net;
                    if (qnet != nullptr) {
                        for (auto user : qnet->users) {
                            if (is_lut(ctx, user.cell) && user.cell != ci &&
                                procdLuts.find(user.cell->name) == procdLuts.end()) {
                                if (can_pack_lutff(ci->name, user.cell->name)) {
                                    procdLuts.insert(ci->name);
                                    procdLuts.insert(user.cell->name);
                                    lutPairs[ci->name] = user.cell->name;
                                    goto paired_ff;
                                }
                            }
                        }
                        if (false) {
                        paired_ff:
                            continue;
                        }
                    }
                }
                for (const char *inp : {"A", "B", "C", "D"}) {
                    NetInfo *innet = ci->ports.at(ctx->id(inp)).net;
                    if (innet != nullptr && innet->driver.cell != nullptr) {
                        CellInfo *drv = innet->driver.cell;
                        if (is_lut(ctx, drv) && drv != ci && innet->driver.port == ctx->id("Z")) {
                            if (procdLuts.find(drv->name) == procdLuts.end()) {
                                if (can_pack_lutff(ci->name, drv->name)) {
                                    procdLuts.insert(ci->name);
                                    procdLuts.insert(drv->name);
                                    lutPairs[ci->name] = drv->name;
                                    goto paired_inlut;
                                }
                            }
                        } else if (is_ff(ctx, drv) && innet->driver.port == ctx->id("Q")) {
                            auto fflut = fflutPairs.find(drv->name);
                            if (fflut != fflutPairs.end() && fflut->second != ci->name &&
                                procdLuts.find(fflut->second) == procdLuts.end()) {
                                if (can_pack_lutff(ci->name, fflut->second)) {
                                    procdLuts.insert(ci->name);
                                    procdLuts.insert(fflut->second);
                                    lutPairs[ci->name] = fflut->second;
                                    goto paired_inlut;
                                }
                            }
                        }
                    }
                }

                // Pack LUTs feeding the same CCU2, RAM or DFF into a SLICE
                if (znet != nullptr && znet->users.size() < 10) {
                    for (auto user : znet->users) {
                        if (is_lc(ctx, user.cell) || user.cell->type == ctx->id("DP16KD") || is_ff(ctx, user.cell)) {
                            for (auto port : user.cell->ports) {
                                if (port.second.type != PORT_IN || port.second.net == nullptr ||
                                    port.second.net == znet)
                                    continue;
                                if (port.second.net->users.size() > 10)
                                    continue;
                                CellInfo *drv = port.second.net->driver.cell;
                                if (drv == nullptr)
                                    continue;
                                if (is_lut(ctx, drv) && !procdLuts.count(drv->name) &&
                                    can_pack_lutff(ci->name, drv->name)) {
                                    procdLuts.insert(ci->name);
                                    procdLuts.insert(drv->name);
                                    lutPairs[ci->name] = drv->name;
                                    goto paired_inlut;
                                }
                            }
                        }
                    }
                }

                // Pack LUTs sharing an input with a simple fanout-based heuristic
                for (const char *inp : {"A", "B", "C", "D"}) {
                    NetInfo *innet = ci->ports.at(ctx->id(inp)).net;
                    if (innet != nullptr && innet->users.size() < 5 && innet->users.size() > 1)
                        inpnets.push_back(innet);
                }
                std::sort(inpnets.begin(), inpnets.end(),
                          [&](const NetInfo *a, const NetInfo *b) { return a->users.size() < b->users.size(); });
                for (auto inet : inpnets) {
                    for (auto &user : inet->users) {
                        if (user.cell == nullptr || user.cell == ci || !is_lut(ctx, user.cell))
                            continue;
                        if (procdLuts.count(user.cell->name))
                            continue;
                        if (can_pack_lutff(ci->name, user.cell->name)) {
                            procdLuts.insert(ci->name);
                            procdLuts.insert(user.cell->name);
                            lutPairs[ci->name] = user.cell->name;
                            goto paired_inlut;
                        }
                    }
                }

                if (false) {
                paired_inlut:
                    continue;
                }
            }
        }
        if (ctx->debug) {
            log_info("Singleton LUTs (packer QoR debug): \n");
            for (auto cell : sorted(ctx->cells))
                if (is_lut(ctx, cell.second) && !procdLuts.count(cell.first))
                    log_info("     %s\n", cell.first.c_str(ctx));
        }
    }

    // Return true if an port is a top level port that provides its own IOBUF
    bool is_top_port(PortRef &port)
    {
        if (port.cell == nullptr)
            return false;
        if (port.cell->type == id_DCUA) {
            return port.port == id_CH0_HDINP || port.port == id_CH0_HDINN || port.port == id_CH0_HDOUTP ||
                   port.port == id_CH0_HDOUTN || port.port == id_CH1_HDINP || port.port == id_CH1_HDINN ||
                   port.port == id_CH1_HDOUTP || port.port == id_CH1_HDOUTN;
        } else if (port.cell->type == id_EXTREFB) {
            return port.port == id_REFCLKP || port.port == id_REFCLKN;
        } else {
            return false;
        }
    }

    // Return true if a net only drives a top port
    bool drives_top_port(NetInfo *net, PortRef &tp)
    {
        if (net == nullptr)
            return false;
        for (auto user : net->users) {
            if (is_top_port(user)) {
                if (net->users.size() > 1)
                    log_error("   port %s.%s must be connected to (and only to) a top level pin\n",
                              user.cell->name.c_str(ctx), user.port.c_str(ctx));
                tp = user;
                return true;
            }
        }
        if (net->driver.cell != nullptr && is_top_port(net->driver)) {
            if (net->users.size() > 1)
                log_error("   port %s.%s must be connected to (and only to) a top level pin\n",
                          net->driver.cell->name.c_str(ctx), net->driver.port.c_str(ctx));
            tp = net->driver;
            return true;
        }
        return false;
    }

    // Simple "packer" to remove nextpnr IOBUFs, this assumes IOBUFs are manually instantiated
    void pack_io()
    {
        log_info("Packing IOs..\n");

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (is_nextpnr_iob(ctx, ci)) {
                CellInfo *trio = nullptr;
                NetInfo *ionet = nullptr;
                PortRef tp;
                if (ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_iobuf")) {
                    ionet = ci->ports.at(ctx->id("O")).net;
                    trio = net_only_drives(ctx, ionet, is_trellis_io, ctx->id("B"), true, ci);

                } else if (ci->type == ctx->id("$nextpnr_obuf")) {
                    ionet = ci->ports.at(ctx->id("I")).net;
                    trio = net_only_drives(ctx, ci->ports.at(ctx->id("I")).net, is_trellis_io, ctx->id("B"), true, ci);
                }
                if (trio != nullptr) {
                    // Trivial case, TRELLIS_IO used. Just destroy the net and the
                    // iobuf
                    log_info("%s feeds TRELLIS_IO %s, removing %s %s.\n", ci->name.c_str(ctx), trio->name.c_str(ctx),
                             ci->type.c_str(ctx), ci->name.c_str(ctx));

                    NetInfo *net = trio->ports.at(ctx->id("B")).net;
                    if (((ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_iobuf")) &&
                         net->users.size() > 1) ||
                        (ci->type == ctx->id("$nextpnr_obuf") &&
                         (net->users.size() > 2 || net->driver.cell != nullptr)) ||
                        (ci->type == ctx->id("$nextpnr_iobuf") && ci->ports.at(ctx->id("I")).net != nullptr &&
                         ci->ports.at(ctx->id("I")).net->driver.cell != nullptr))
                        log_error("Pin B of %s '%s' connected to more than a single top level IO.\n",
                                  trio->type.c_str(ctx), trio->name.c_str(ctx));
                    if (net != nullptr) {
                        ctx->nets.erase(net->name);
                        trio->ports.at(ctx->id("B")).net = nullptr;
                    }
                    if (ci->type == ctx->id("$nextpnr_iobuf")) {
                        NetInfo *net2 = ci->ports.at(ctx->id("I")).net;
                        if (net2 != nullptr) {
                            ctx->nets.erase(net2->name);
                        }
                    }
                } else if (drives_top_port(ionet, tp)) {
                    log_info("%s feeds %s %s.%s, removing %s %s.\n", ci->name.c_str(ctx), tp.cell->type.c_str(ctx),
                             tp.cell->name.c_str(ctx), tp.port.c_str(ctx), ci->type.c_str(ctx), ci->name.c_str(ctx));
                    if (ionet != nullptr) {
                        ctx->nets.erase(ionet->name);
                        tp.cell->ports.at(tp.port).net = nullptr;
                    }
                    if (ci->type == ctx->id("$nextpnr_iobuf")) {
                        NetInfo *net2 = ci->ports.at(ctx->id("I")).net;
                        if (net2 != nullptr) {
                            ctx->nets.erase(net2->name);
                        }
                    }
                } else {
                    // Create a TRELLIS_IO buffer
                    std::unique_ptr<CellInfo> tr_cell =
                            create_ecp5_cell(ctx, ctx->id("TRELLIS_IO"), ci->name.str(ctx) + "$tr_io");
                    nxio_to_tr(ctx, ci, tr_cell.get(), new_cells, packed_cells);
                    new_cells.push_back(std::move(tr_cell));
                    trio = new_cells.back().get();
                }

                packed_cells.insert(ci->name);
                if (trio != nullptr) {
                    for (const auto &attr : ci->attrs)
                        trio->attrs[attr.first] = attr.second;

                    auto loc_attr = trio->attrs.find(ctx->id("LOC"));
                    if (loc_attr != trio->attrs.end()) {
                        std::string pin = loc_attr->second;
                        BelId pinBel = ctx->getPackagePinBel(pin);
                        if (pinBel == BelId()) {
                            log_error("IO pin '%s' constrained to pin '%s', which does not exist for package '%s'.\n",
                                      trio->name.c_str(ctx), pin.c_str(), ctx->args.package.c_str());
                        } else {
                            log_info("pin '%s' constrained to Bel '%s'.\n", trio->name.c_str(ctx),
                                     ctx->getBelName(pinBel).c_str(ctx));
                        }
                        trio->attrs[ctx->id("BEL")] = ctx->getBelName(pinBel).str(ctx);
                    }
                }
            }
        }
        flush_cells();
    }

    // Pass to pack LUT5s into a newly created slice
    void pack_lut5xs()
    {
        log_info("Packing LUT5-7s...\n");
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (is_pfumx(ctx, ci)) {
                std::unique_ptr<CellInfo> packed =
                        create_ecp5_cell(ctx, ctx->id("TRELLIS_SLICE"), ci->name.str(ctx) + "_SLICE");
                NetInfo *f0 = ci->ports.at(ctx->id("BLUT")).net;
                if (f0 == nullptr)
                    log_error("PFUMX '%s' has disconnected port 'BLUT'\n", ci->name.c_str(ctx));
                NetInfo *f1 = ci->ports.at(ctx->id("ALUT")).net;
                if (f1 == nullptr)
                    log_error("PFUMX '%s' has disconnected port 'ALUT'\n", ci->name.c_str(ctx));
                CellInfo *lut0 = net_driven_by(ctx, f0, is_lut, ctx->id("Z"));
                CellInfo *lut1 = net_driven_by(ctx, f1, is_lut, ctx->id("Z"));
                if (lut0 == nullptr)
                    log_error("PFUMX '%s' has BLUT driven by cell other than a LUT\n", ci->name.c_str(ctx));
                if (lut1 == nullptr)
                    log_error("PFUMX '%s' has ALUT driven by cell other than a LUT\n", ci->name.c_str(ctx));
                if (ctx->verbose)
                    log_info("   mux '%s' forms part of a LUT5\n", cell.first.c_str(ctx));
                replace_port(lut0, ctx->id("A"), packed.get(), ctx->id("A0"));
                replace_port(lut0, ctx->id("B"), packed.get(), ctx->id("B0"));
                replace_port(lut0, ctx->id("C"), packed.get(), ctx->id("C0"));
                replace_port(lut0, ctx->id("D"), packed.get(), ctx->id("D0"));
                replace_port(lut1, ctx->id("A"), packed.get(), ctx->id("A1"));
                replace_port(lut1, ctx->id("B"), packed.get(), ctx->id("B1"));
                replace_port(lut1, ctx->id("C"), packed.get(), ctx->id("C1"));
                replace_port(lut1, ctx->id("D"), packed.get(), ctx->id("D1"));
                replace_port(ci, ctx->id("C0"), packed.get(), ctx->id("M0"));
                replace_port(ci, ctx->id("Z"), packed.get(), ctx->id("OFX0"));
                packed->params[ctx->id("LUT0_INITVAL")] = str_or_default(lut0->params, ctx->id("INIT"), "0");
                packed->params[ctx->id("LUT1_INITVAL")] = str_or_default(lut1->params, ctx->id("INIT"), "0");

                ctx->nets.erase(f0->name);
                ctx->nets.erase(f1->name);
                sliceUsage[packed->name].lut0_used = true;
                sliceUsage[packed->name].lut1_used = true;
                sliceUsage[packed->name].mux5_used = true;

                if (lutffPairs.find(ci->name) != lutffPairs.end()) {
                    CellInfo *ff = ctx->cells.at(lutffPairs[ci->name]).get();
                    ff_to_slice(ctx, ff, packed.get(), 0, true);
                    packed_cells.insert(ff->name);
                    sliceUsage[packed->name].ff0_used = true;
                    lutffPairs.erase(ci->name);
                    fflutPairs.erase(ff->name);
                }

                new_cells.push_back(std::move(packed));
                packed_cells.insert(lut0->name);
                packed_cells.insert(lut1->name);
                packed_cells.insert(ci->name);
            }
        }
        flush_cells();
        // Pack LUT6s
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (is_l6mux(ctx, ci)) {
                NetInfo *ofx0_0 = ci->ports.at(ctx->id("D0")).net;
                if (ofx0_0 == nullptr)
                    log_error("L6MUX21 '%s' has disconnected port 'D0'\n", ci->name.c_str(ctx));
                NetInfo *ofx0_1 = ci->ports.at(ctx->id("D1")).net;
                if (ofx0_1 == nullptr)
                    log_error("L6MUX21 '%s' has disconnected port 'D1'\n", ci->name.c_str(ctx));
                CellInfo *slice0 = net_driven_by(ctx, ofx0_0, is_lc, ctx->id("OFX0"));
                CellInfo *slice1 = net_driven_by(ctx, ofx0_1, is_lc, ctx->id("OFX0"));
                if (slice0 == nullptr) {
                    if (!net_driven_by(ctx, ofx0_0, is_l6mux, ctx->id("Z")) &&
                        !net_driven_by(ctx, ofx0_0, is_lc, ctx->id("OFX1")))
                        log_error("L6MUX21 '%s' has D0 driven by cell other than a SLICE OFX0 but not a LUT7 mux "
                                  "('%s.%s')\n",
                                  ci->name.c_str(ctx), ofx0_0->driver.cell->name.c_str(ctx),
                                  ofx0_0->driver.port.c_str(ctx));
                    continue;
                }
                if (slice1 == nullptr) {
                    if (!net_driven_by(ctx, ofx0_1, is_l6mux, ctx->id("Z")) &&
                        !net_driven_by(ctx, ofx0_1, is_lc, ctx->id("OFX1")))
                        log_error("L6MUX21 '%s' has D1 driven by cell other than a SLICE OFX0 but not a LUT7 mux "
                                  "('%s.%s')\n",
                                  ci->name.c_str(ctx), ofx0_0->driver.cell->name.c_str(ctx),
                                  ofx0_0->driver.port.c_str(ctx));
                    continue;
                }
                if (ctx->verbose)
                    log_info("   mux '%s' forms part of a LUT6\n", cell.first.c_str(ctx));
                replace_port(ci, ctx->id("D0"), slice1, id_FXA);
                replace_port(ci, ctx->id("D1"), slice1, id_FXB);
                replace_port(ci, ctx->id("SD"), slice1, id_M1);
                replace_port(ci, ctx->id("Z"), slice1, id_OFX1);
                slice0->constr_z = 1;
                slice0->constr_x = 0;
                slice0->constr_y = 0;
                slice0->constr_parent = slice1;
                slice1->constr_z = 0;
                slice1->constr_abs_z = true;
                slice1->constr_children.push_back(slice0);

                if (lutffPairs.find(ci->name) != lutffPairs.end()) {
                    CellInfo *ff = ctx->cells.at(lutffPairs[ci->name]).get();
                    ff_to_slice(ctx, ff, slice1, 1, true);
                    packed_cells.insert(ff->name);
                    sliceUsage[slice1->name].ff1_used = true;
                    lutffPairs.erase(ci->name);
                    fflutPairs.erase(ff->name);
                }

                packed_cells.insert(ci->name);
            }
        }
        flush_cells();
        // Pack LUT7s
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (is_l6mux(ctx, ci)) {
                NetInfo *ofx1_0 = ci->ports.at(ctx->id("D0")).net;
                if (ofx1_0 == nullptr)
                    log_error("L6MUX21 '%s' has disconnected port 'D0'\n", ci->name.c_str(ctx));
                NetInfo *ofx1_1 = ci->ports.at(ctx->id("D1")).net;
                if (ofx1_1 == nullptr)
                    log_error("L6MUX21 '%s' has disconnected port 'D1'\n", ci->name.c_str(ctx));
                CellInfo *slice1 = net_driven_by(ctx, ofx1_0, is_lc, ctx->id("OFX1"));
                CellInfo *slice3 = net_driven_by(ctx, ofx1_1, is_lc, ctx->id("OFX1"));
                if (slice1 == nullptr)
                    log_error("L6MUX21 '%s' has D0 driven by cell other than a SLICE OFX ('%s.%s')\n",
                              ci->name.c_str(ctx), ofx1_0->driver.cell->name.c_str(ctx),
                              ofx1_0->driver.port.c_str(ctx));
                if (slice3 == nullptr)
                    log_error("L6MUX21 '%s' has D1 driven by cell other than a SLICE OFX ('%s.%s')\n",
                              ci->name.c_str(ctx), ofx1_1->driver.cell->name.c_str(ctx),
                              ofx1_1->driver.port.c_str(ctx));

                NetInfo *fxa_0 = slice1->ports.at(id_FXA).net;
                if (fxa_0 == nullptr)
                    log_error("SLICE '%s' has disconnected port 'FXA'\n", slice1->name.c_str(ctx));
                NetInfo *fxa_1 = slice3->ports.at(id_FXA).net;
                if (fxa_1 == nullptr)
                    log_error("SLICE '%s' has disconnected port 'FXA'\n", slice3->name.c_str(ctx));

                CellInfo *slice0 = net_driven_by(ctx, fxa_0, is_lc, ctx->id("OFX0"));
                CellInfo *slice2 = net_driven_by(ctx, fxa_1, is_lc, ctx->id("OFX0"));
                if (slice0 == nullptr)
                    log_error("SLICE '%s' has FXA driven by cell other than a SLICE OFX0 ('%s.%s')\n",
                              slice1->name.c_str(ctx), fxa_0->driver.cell->name.c_str(ctx),
                              fxa_0->driver.port.c_str(ctx));
                if (slice2 == nullptr)
                    log_error("SLICE '%s' has FXA driven by cell other than a SLICE OFX0 ('%s.%s')\n",
                              slice3->name.c_str(ctx), fxa_1->driver.cell->name.c_str(ctx),
                              fxa_1->driver.port.c_str(ctx));

                replace_port(ci, ctx->id("D0"), slice2, id_FXA);
                replace_port(ci, ctx->id("D1"), slice2, id_FXB);
                replace_port(ci, ctx->id("SD"), slice2, id_M1);
                replace_port(ci, ctx->id("Z"), slice2, id_OFX1);

                for (auto slice : {slice0, slice1, slice2, slice3}) {
                    slice->constr_children.clear();
                    slice->constr_abs_z = false;
                    slice->constr_x = slice->UNCONSTR;
                    slice->constr_y = slice->UNCONSTR;
                    slice->constr_z = slice->UNCONSTR;
                    slice->constr_parent = nullptr;
                }
                slice3->constr_children.clear();
                slice3->constr_abs_z = true;
                slice3->constr_z = 0;

                slice2->constr_children.clear();
                slice2->constr_abs_z = true;
                slice2->constr_z = 1;
                slice2->constr_x = 0;
                slice2->constr_y = 0;
                slice2->constr_parent = slice3;
                slice3->constr_children.push_back(slice2);

                slice1->constr_children.clear();
                slice1->constr_abs_z = true;
                slice1->constr_z = 2;
                slice1->constr_x = 0;
                slice1->constr_y = 0;
                slice1->constr_parent = slice3;
                slice3->constr_children.push_back(slice1);

                slice0->constr_children.clear();
                slice0->constr_abs_z = true;
                slice0->constr_z = 3;
                slice0->constr_x = 0;
                slice0->constr_y = 0;
                slice0->constr_parent = slice3;
                slice3->constr_children.push_back(slice0);

                if (lutffPairs.find(ci->name) != lutffPairs.end()) {
                    CellInfo *ff = ctx->cells.at(lutffPairs[ci->name]).get();
                    ff_to_slice(ctx, ff, slice2, 1, true);
                    packed_cells.insert(ff->name);
                    sliceUsage[slice2->name].ff1_used = true;
                    lutffPairs.erase(ci->name);
                    fflutPairs.erase(ff->name);
                }

                packed_cells.insert(ci->name);
            }
        }
        flush_cells();
    }
    // Create a feed in to the carry chain
    CellInfo *make_carry_feed_in(NetInfo *carry, PortRef chain_in)
    {
        std::unique_ptr<CellInfo> feedin = create_ecp5_cell(ctx, ctx->id("CCU2C"));

        feedin->params[ctx->id("INIT0")] = "10"; // LUT4 = 0; LUT2 = A
        feedin->params[ctx->id("INIT1")] = "65535";
        feedin->params[ctx->id("INJECT1_0")] = "NO";
        feedin->params[ctx->id("INJECT1_1")] = "YES";

        carry->users.erase(std::remove_if(carry->users.begin(), carry->users.end(),
                                          [chain_in](const PortRef &user) {
                                              return user.port == chain_in.port && user.cell == chain_in.cell;
                                          }),
                           carry->users.end());
        connect_port(ctx, carry, feedin.get(), id_A0);

        std::unique_ptr<NetInfo> new_carry(new NetInfo());
        new_carry->name = ctx->id(feedin->name.str(ctx) + "$COUT");
        connect_port(ctx, new_carry.get(), feedin.get(), ctx->id("COUT"));
        chain_in.cell->ports[chain_in.port].net = nullptr;
        connect_port(ctx, new_carry.get(), chain_in.cell, chain_in.port);

        CellInfo *feedin_ptr = feedin.get();
        IdString feedin_name = feedin->name;
        ctx->cells[feedin_name] = std::move(feedin);
        IdString new_carry_name = new_carry->name;
        ctx->nets[new_carry_name] = std::move(new_carry);
        return feedin_ptr;
    }

    // Create a feed out and loop through from the carry chain
    CellInfo *make_carry_feed_out(NetInfo *carry, boost::optional<PortRef> chain_next = boost::optional<PortRef>())
    {
        std::unique_ptr<CellInfo> feedout = create_ecp5_cell(ctx, ctx->id("CCU2C"));
        feedout->params[ctx->id("INIT0")] = "0";
        feedout->params[ctx->id("INIT1")] = "10"; // LUT4 = 0; LUT2 = A
        feedout->params[ctx->id("INJECT1_0")] = "NO";
        feedout->params[ctx->id("INJECT1_1")] = "NO";

        PortRef carry_drv = carry->driver;
        carry->driver.cell = nullptr;
        connect_port(ctx, carry, feedout.get(), ctx->id("S0"));

        std::unique_ptr<NetInfo> new_cin(new NetInfo());
        new_cin->name = ctx->id(feedout->name.str(ctx) + "$CIN");
        new_cin->driver = carry_drv;
        carry_drv.cell->ports.at(carry_drv.port).net = new_cin.get();
        connect_port(ctx, new_cin.get(), feedout.get(), ctx->id("CIN"));

        if (chain_next) {
            // Loop back into LUT4_1 for feedthrough
            connect_port(ctx, carry, feedout.get(), id_A1);

            carry->users.erase(std::remove_if(carry->users.begin(), carry->users.end(),
                                              [chain_next](const PortRef &user) {
                                                  return user.port == chain_next->port && user.cell == chain_next->cell;
                                              }),
                               carry->users.end());

            std::unique_ptr<NetInfo> new_cout(new NetInfo());
            new_cout->name = ctx->id(feedout->name.str(ctx) + "$COUT");
            connect_port(ctx, new_cout.get(), feedout.get(), ctx->id("COUT"));

            chain_next->cell->ports[chain_next->port].net = nullptr;
            connect_port(ctx, new_cout.get(), chain_next->cell, chain_next->port);

            IdString new_cout_name = new_cout->name;
            ctx->nets[new_cout_name] = std::move(new_cout);
        }

        CellInfo *feedout_ptr = feedout.get();
        IdString feedout_name = feedout->name;
        ctx->cells[feedout_name] = std::move(feedout);

        IdString new_cin_name = new_cin->name;
        ctx->nets[new_cin_name] = std::move(new_cin);

        return feedout_ptr;
    }

    // Split a carry chain into multiple legal chains
    std::vector<CellChain> split_carry_chain(CellChain &carryc)
    {
        bool start_of_chain = true;
        std::vector<CellChain> chains;
        const int max_length = (ctx->chip_info->width - 4) * 4 - 2;
        auto curr_cell = carryc.cells.begin();
        while (curr_cell != carryc.cells.end()) {
            CellInfo *cell = *curr_cell;
            if (start_of_chain) {
                chains.emplace_back();
                start_of_chain = false;
                if (cell->ports.at(ctx->id("CIN")).net) {
                    // CIN is not constant and not part of a chain. Must feed in from fabric
                    PortRef inport;
                    inport.cell = cell;
                    inport.port = ctx->id("CIN");
                    CellInfo *feedin = make_carry_feed_in(cell->ports.at(ctx->id("CIN")).net, inport);
                    chains.back().cells.push_back(feedin);
                }
            }
            chains.back().cells.push_back(cell);
            bool split_chain = int(chains.back().cells.size()) > max_length;
            if (split_chain) {
                CellInfo *passout = make_carry_feed_out(cell->ports.at(ctx->id("COUT")).net);
                chains.back().cells.back() = passout;
                start_of_chain = true;
            } else {
                NetInfo *carry_net = cell->ports.at(ctx->id("COUT")).net;
                bool at_end = (curr_cell == carryc.cells.end() - 1);
                if (carry_net != nullptr && (carry_net->users.size() > 1 || at_end)) {
                    boost::optional<PortRef> nextport;
                    if (!at_end) {
                        auto next_cell = *(curr_cell + 1);
                        PortRef nextpr;
                        nextpr.cell = next_cell;
                        nextpr.port = ctx->id("CIN");
                        nextport = nextpr;
                    }
                    CellInfo *passout = make_carry_feed_out(cell->ports.at(ctx->id("COUT")).net, nextport);
                    chains.back().cells.push_back(passout);
                }
                ++curr_cell;
            }
        }
        return chains;
    }

    // Pack carries and set up appropriate relative constraints
    void pack_carries()
    {
        log_info("Packing carries...\n");
        // Find all chains (including single carry cells)
        auto carry_chains = find_chains(
                ctx, [](const Context *ctx, const CellInfo *cell) { return is_carry(ctx, cell); },
                [](const Context *ctx, const CellInfo *cell) {
                    return net_driven_by(ctx, cell->ports.at(ctx->id("CIN")).net, is_carry, ctx->id("COUT"));
                },
                [](const Context *ctx, const CellInfo *cell) {
                    return net_only_drives(ctx, cell->ports.at(ctx->id("COUT")).net, is_carry, ctx->id("CIN"), false);
                },
                1);
        std::vector<CellChain> all_chains;

        // Chain splitting
        for (auto &base_chain : carry_chains) {
            if (ctx->verbose) {
                log_info("Found carry chain: \n");
                for (auto entry : base_chain.cells)
                    log_info("     %s\n", entry->name.c_str(ctx));
                log_info("\n");
            }
            std::vector<CellChain> split_chains = split_carry_chain(base_chain);
            for (auto &chain : split_chains) {
                all_chains.push_back(chain);
            }
        }

        std::vector<std::vector<CellInfo *>> packed_chains;

        // Chain packing
        std::vector<std::tuple<CellInfo *, CellInfo *, int>> ff_packing;
        for (auto &chain : all_chains) {
            int cell_count = 0;
            std::vector<CellInfo *> tile_ffs;
            std::vector<CellInfo *> packed_chain;
            for (auto &cell : chain.cells) {
                if (cell_count % 4 == 0)
                    tile_ffs.clear();
                std::unique_ptr<CellInfo> slice =
                        create_ecp5_cell(ctx, ctx->id("TRELLIS_SLICE"), cell->name.str(ctx) + "$CCU2_SLICE");

                ccu2c_to_slice(ctx, cell, slice.get());

                CellInfo *ff0 = nullptr;
                NetInfo *f0net = slice->ports.at(ctx->id("F0")).net;
                if (f0net != nullptr) {
                    ff0 = net_only_drives(ctx, f0net, is_ff, ctx->id("DI"), false);
                    if (ff0 != nullptr && can_add_ff_to_tile(tile_ffs, ff0)) {
                        ff_packing.push_back(std::make_tuple(ff0, slice.get(), 0));
                        tile_ffs.push_back(ff0);
                        packed_cells.insert(ff0->name);
                    }
                }

                CellInfo *ff1 = nullptr;
                NetInfo *f1net = slice->ports.at(ctx->id("F1")).net;
                if (f1net != nullptr) {
                    ff1 = net_only_drives(ctx, f1net, is_ff, ctx->id("DI"), false);
                    if (ff1 != nullptr && (ff0 == nullptr || can_pack_ffs(ff0, ff1)) &&
                        can_add_ff_to_tile(tile_ffs, ff1)) {
                        ff_packing.push_back(std::make_tuple(ff1, slice.get(), 1));
                        tile_ffs.push_back(ff1);
                        packed_cells.insert(ff1->name);
                    }
                }
                packed_chain.push_back(slice.get());
                new_cells.push_back(std::move(slice));
                packed_cells.insert(cell->name);
                cell_count++;
            }
            packed_chains.push_back(packed_chain);
        }

        for (auto ff : ff_packing)
            ff_to_slice(ctx, std::get<0>(ff), std::get<1>(ff), std::get<2>(ff), true);

        // Relative chain placement
        for (auto &chain : packed_chains) {
            chain.at(0)->constr_abs_z = true;
            chain.at(0)->constr_z = 0;
            for (int i = 1; i < int(chain.size()); i++) {
                chain.at(i)->constr_x = (i / 4);
                chain.at(i)->constr_y = 0;
                chain.at(i)->constr_z = i % 4;
                chain.at(i)->constr_abs_z = true;
                chain.at(i)->constr_parent = chain.at(0);
                chain.at(0)->constr_children.push_back(chain.at(i));
            }
        }

        flush_cells();
    }

    // Pack distributed RAM
    void pack_dram()
    {
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (is_dpram(ctx, ci)) {

                // Create RAMW slice
                std::unique_ptr<CellInfo> ramw_slice =
                        create_ecp5_cell(ctx, ctx->id("TRELLIS_SLICE"), ci->name.str(ctx) + "$RAMW_SLICE");
                dram_to_ramw(ctx, ci, ramw_slice.get());

                // Create actual RAM slices
                std::unique_ptr<CellInfo> ram0_slice =
                        create_ecp5_cell(ctx, ctx->id("TRELLIS_SLICE"), ci->name.str(ctx) + "$DPRAM0_SLICE");
                dram_to_ram_slice(ctx, ci, ram0_slice.get(), ramw_slice.get(), 0);

                std::unique_ptr<CellInfo> ram1_slice =
                        create_ecp5_cell(ctx, ctx->id("TRELLIS_SLICE"), ci->name.str(ctx) + "$DPRAM1_SLICE");
                dram_to_ram_slice(ctx, ci, ram1_slice.get(), ramw_slice.get(), 1);

                // Disconnect ports of original cell after packing
                disconnect_port(ctx, ci, id_WCK);
                disconnect_port(ctx, ci, id_WRE);

                disconnect_port(ctx, ci, ctx->id("RAD[0]"));
                disconnect_port(ctx, ci, ctx->id("RAD[1]"));
                disconnect_port(ctx, ci, ctx->id("RAD[2]"));
                disconnect_port(ctx, ci, ctx->id("RAD[3]"));

                // Attempt to pack FFs into RAM slices
                std::vector<std::tuple<CellInfo *, CellInfo *, int>> ff_packing;
                std::vector<CellInfo *> tile_ffs;
                for (auto slice : {ram0_slice.get(), ram1_slice.get()}) {
                    CellInfo *ff0 = nullptr;
                    NetInfo *f0net = slice->ports.at(ctx->id("F0")).net;
                    if (f0net != nullptr) {
                        ff0 = net_only_drives(ctx, f0net, is_ff, ctx->id("DI"), false);
                        if (ff0 != nullptr && can_add_ff_to_tile(tile_ffs, ff0)) {
                            if (can_pack_ff_dram(slice, ff0)) {
                                ff_packing.push_back(std::make_tuple(ff0, slice, 0));
                                tile_ffs.push_back(ff0);
                                packed_cells.insert(ff0->name);
                            }
                        }
                    }

                    CellInfo *ff1 = nullptr;
                    NetInfo *f1net = slice->ports.at(ctx->id("F1")).net;
                    if (f1net != nullptr) {
                        ff1 = net_only_drives(ctx, f1net, is_ff, ctx->id("DI"), false);
                        if (ff1 != nullptr && (ff0 == nullptr || can_pack_ffs(ff0, ff1)) &&
                            can_add_ff_to_tile(tile_ffs, ff1)) {
                            if (can_pack_ff_dram(slice, ff1)) {
                                ff_packing.push_back(std::make_tuple(ff1, slice, 1));
                                tile_ffs.push_back(ff1);
                                packed_cells.insert(ff1->name);
                            }
                        }
                    }
                }

                for (auto ff : ff_packing)
                    ff_to_slice(ctx, std::get<0>(ff), std::get<1>(ff), std::get<2>(ff), true);

                // Setup placement constraints
                ram0_slice->constr_abs_z = true;
                ram0_slice->constr_z = 0;

                ram1_slice->constr_parent = ram0_slice.get();
                ram1_slice->constr_abs_z = true;
                ram1_slice->constr_x = 0;
                ram1_slice->constr_y = 0;
                ram1_slice->constr_z = 1;
                ram0_slice->constr_children.push_back(ram1_slice.get());

                ramw_slice->constr_parent = ram0_slice.get();
                ramw_slice->constr_abs_z = true;
                ramw_slice->constr_x = 0;
                ramw_slice->constr_y = 0;
                ramw_slice->constr_z = 2;
                ram0_slice->constr_children.push_back(ramw_slice.get());

                new_cells.push_back(std::move(ram0_slice));
                new_cells.push_back(std::move(ram1_slice));
                new_cells.push_back(std::move(ramw_slice));
                packed_cells.insert(ci->name);
            }
        }
        flush_cells();
    }

    // Pack LUTs that have been paired together
    void pack_lut_pairs()
    {
        log_info("Packing paired LUTs into a SLICE...\n");
        for (auto pair : lutPairs) {
            CellInfo *lut0 = ctx->cells.at(pair.first).get();
            CellInfo *lut1 = ctx->cells.at(pair.second).get();
            std::unique_ptr<CellInfo> slice =
                    create_ecp5_cell(ctx, ctx->id("TRELLIS_SLICE"), lut0->name.str(ctx) + "_SLICE");

            lut_to_slice(ctx, lut0, slice.get(), 0);
            lut_to_slice(ctx, lut1, slice.get(), 1);

            auto ff0 = lutffPairs.find(lut0->name);

            if (ff0 != lutffPairs.end()) {
                ff_to_slice(ctx, ctx->cells.at(ff0->second).get(), slice.get(), 0, true);
                packed_cells.insert(ff0->second);
                fflutPairs.erase(ff0->second);
                lutffPairs.erase(lut0->name);
            }

            auto ff1 = lutffPairs.find(lut1->name);

            if (ff1 != lutffPairs.end()) {
                ff_to_slice(ctx, ctx->cells.at(ff1->second).get(), slice.get(), 1, true);
                packed_cells.insert(ff1->second);
                fflutPairs.erase(ff1->second);
                lutffPairs.erase(lut1->name);
            }

            new_cells.push_back(std::move(slice));
            packed_cells.insert(lut0->name);
            packed_cells.insert(lut1->name);
        }
        flush_cells();
    }

    // Pack single LUTs that weren't paired into their own slice,
    // with an optional FF also
    void pack_remaining_luts()
    {
        log_info("Packing unpaired LUTs into a SLICE...\n");
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (is_lut(ctx, ci)) {
                std::unique_ptr<CellInfo> slice =
                        create_ecp5_cell(ctx, ctx->id("TRELLIS_SLICE"), ci->name.str(ctx) + "_SLICE");
                lut_to_slice(ctx, ci, slice.get(), 1);
                auto ff = lutffPairs.find(ci->name);

                if (ff != lutffPairs.end()) {
                    ff_to_slice(ctx, ctx->cells.at(ff->second).get(), slice.get(), 1, true);
                    packed_cells.insert(ff->second);
                    fflutPairs.erase(ff->second);
                    lutffPairs.erase(ci->name);
                }

                new_cells.push_back(std::move(slice));
                packed_cells.insert(ci->name);
            }
        }
        flush_cells();
    }

    // Pack flipflops that weren't paired with a LUT
    void pack_remaining_ffs()
    {
        log_info("Packing unpaired FFs into a SLICE...\n");
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (is_ff(ctx, ci)) {
                std::unique_ptr<CellInfo> slice =
                        create_ecp5_cell(ctx, ctx->id("TRELLIS_SLICE"), ci->name.str(ctx) + "_SLICE");
                ff_to_slice(ctx, ci, slice.get(), 0, false);
                new_cells.push_back(std::move(slice));
                packed_cells.insert(ci->name);
            }
        }
        flush_cells();
    }

    int make_init_with_const_input(int init, int input, bool value)
    {
        int new_init = 0;
        for (int i = 0; i < 16; i++) {
            if (((i >> input) & 0x1) != value) {
                int other_i = (i & (~(1 << input))) | (value << input);
                if ((init >> other_i) & 0x1)
                    new_init |= (1 << i);
            } else {
                if ((init >> i) & 0x1)
                    new_init |= (1 << i);
            }
        }
        return new_init;
    }

    void set_lut_input_constant(CellInfo *cell, IdString input, bool value)
    {
        int index = std::string("ABCD").find(input.str(ctx));
        int init = int_or_default(cell->params, ctx->id("INIT"));
        int new_init = make_init_with_const_input(init, index, value);
        cell->params[ctx->id("INIT")] = std::to_string(new_init);
        cell->ports.at(input).net = nullptr;
    }

    void set_ccu2c_input_constant(CellInfo *cell, IdString input, bool value)
    {
        std::string input_str = input.str(ctx);
        int lut = std::stoi(input_str.substr(1));
        int index = std::string("ABCD").find(input_str[0]);
        int init = int_or_default(cell->params, ctx->id("INIT" + std::to_string(lut)));
        int new_init = make_init_with_const_input(init, index, value);
        cell->params[ctx->id("INIT" + std::to_string(lut))] = std::to_string(new_init);
        cell->ports.at(input).net = nullptr;
    }

    bool is_ccu2c_port_high(CellInfo *cell, IdString input)
    {
        if (!cell->ports.count(input))
            return true; // disconnected port is high
        if (cell->ports.at(input).net == nullptr || cell->ports.at(input).net->name == ctx->id("$PACKER_VCC_NET"))
            return true; // disconnected or tied-high port
        if (cell->ports.at(input).net->driver.cell != nullptr &&
            cell->ports.at(input).net->driver.cell->type == ctx->id("VCC"))
            return true; // pre-pack high
        return false;
    }

    // Merge a net into a constant net
    void set_net_constant(const Context *ctx, NetInfo *orig, NetInfo *constnet, bool constval)
    {
        orig->driver.cell = nullptr;
        for (auto user : orig->users) {
            if (user.cell != nullptr) {
                CellInfo *uc = user.cell;
                if (ctx->verbose)
                    log_info("%s user %s\n", orig->name.c_str(ctx), uc->name.c_str(ctx));
                if (is_lut(ctx, uc)) {
                    set_lut_input_constant(uc, user.port, constval);
                } else if (is_ff(ctx, uc) && user.port == ctx->id("CE")) {
                    uc->params[ctx->id("CEMUX")] = constval ? "1" : "0";
                    uc->ports[user.port].net = nullptr;
                } else if (is_carry(ctx, uc)) {
                    if (constval &&
                        (user.port == id_A0 || user.port == id_A1 || user.port == id_B0 || user.port == id_B1 ||
                         user.port == id_C0 || user.port == id_C1 || user.port == id_D0 || user.port == id_D1)) {
                        // Input tied high, nothing special to do (bitstream gen will auto-enable tie-high)
                        uc->ports[user.port].net = nullptr;
                    } else if (!constval) {
                        if (user.port == id_A0 || user.port == id_A1 || user.port == id_B0 || user.port == id_B1) {
                            // These inputs can be switched to tie-high without consequence
                            set_ccu2c_input_constant(uc, user.port, constval);
                        } else if (user.port == id_C0 && is_ccu2c_port_high(uc, id_D0)) {
                            // Partner must be tied high
                            set_ccu2c_input_constant(uc, user.port, constval);
                        } else if (user.port == id_D0 && is_ccu2c_port_high(uc, id_C0)) {
                            // Partner must be tied high
                            set_ccu2c_input_constant(uc, user.port, constval);
                        } else if (user.port == id_C1 && is_ccu2c_port_high(uc, id_D1)) {
                            // Partner must be tied high
                            set_ccu2c_input_constant(uc, user.port, constval);
                        } else if (user.port == id_D1 && is_ccu2c_port_high(uc, id_C1)) {
                            // Partner must be tied high
                            set_ccu2c_input_constant(uc, user.port, constval);
                        } else {
                            // Not allowed to change to a tie-high
                            uc->ports[user.port].net = constnet;
                            constnet->users.push_back(user);
                        }
                    } else {
                        uc->ports[user.port].net = constnet;
                        constnet->users.push_back(user);
                    }
                } else if (is_ff(ctx, uc) && user.port == ctx->id("LSR") &&
                           ((!constval && str_or_default(uc->params, ctx->id("LSRMUX"), "LSR") == "LSR") ||
                            (constval && str_or_default(uc->params, ctx->id("LSRMUX"), "LSR") == "INV"))) {
                    uc->ports[user.port].net = nullptr;
                } else if (uc->type == id_DP16KD) {
                    if (user.port == id_CLKA || user.port == id_CLKB || user.port == id_RSTA || user.port == id_RSTB ||
                        user.port == id_WEA || user.port == id_WEB || user.port == id_CEA || user.port == id_CEB ||
                        user.port == id_OCEA || user.port == id_OCEB || user.port == id_CSA0 || user.port == id_CSA1 ||
                        user.port == id_CSA2 || user.port == id_CSB0 || user.port == id_CSB1 || user.port == id_CSB2) {
                        // Connect to CIB CLK, LSR or CE. Default state is 1
                        uc->params[ctx->id(user.port.str(ctx) + "MUX")] = constval ? user.port.str(ctx) : "INV";
                    } else {
                        // Connected to CIB ABCD. Default state is bitstream configurable
                        uc->params[ctx->id(user.port.str(ctx) + "MUX")] = constval ? "1" : "0";
                    }
                    uc->ports[user.port].net = nullptr;
                } else if (uc->type == id_ALU54B || uc->type == id_MULT18X18D) {
                    if (user.port.str(ctx).substr(0, 3) == "CLK" || user.port.str(ctx).substr(0, 2) == "CE" ||
                        user.port.str(ctx).substr(0, 3) == "RST" || user.port.str(ctx).substr(0, 3) == "SRO" ||
                        user.port.str(ctx).substr(0, 3) == "SRI" || user.port.str(ctx).substr(0, 2) == "RO" ||
                        user.port.str(ctx).substr(0, 2) == "MA" || user.port.str(ctx).substr(0, 2) == "MB" ||
                        user.port.str(ctx).substr(0, 3) == "CFB" || user.port.str(ctx).substr(0, 3) == "CIN" ||
                        user.port.str(ctx).substr(0, 6) == "SOURCE" || user.port.str(ctx).substr(0, 6) == "SIGNED" ||
                        user.port.str(ctx).substr(0, 2) == "OP") {
                        uc->ports[user.port].net = constnet;
                        constnet->users.push_back(user);
                    } else {
                        // Connected to CIB ABCD. Default state is bitstream configurable
                        uc->params[ctx->id(user.port.str(ctx) + "MUX")] = constval ? "1" : "0";
                        uc->ports[user.port].net = nullptr;
                    }
                } else {
                    uc->ports[user.port].net = constnet;
                    constnet->users.push_back(user);
                }
            }
        }
        orig->users.clear();
    }

    // Pack constants (simple implementation)
    void pack_constants()
    {
        log_info("Packing constants..\n");

        std::unique_ptr<CellInfo> gnd_cell = create_ecp5_cell(ctx, ctx->id("LUT4"), "$PACKER_GND");
        gnd_cell->params[ctx->id("INIT")] = "0";
        std::unique_ptr<NetInfo> gnd_net = std::unique_ptr<NetInfo>(new NetInfo);
        gnd_net->name = ctx->id("$PACKER_GND_NET");
        gnd_net->driver.cell = gnd_cell.get();
        gnd_net->driver.port = ctx->id("Z");
        gnd_cell->ports.at(ctx->id("Z")).net = gnd_net.get();

        std::unique_ptr<CellInfo> vcc_cell = create_ecp5_cell(ctx, ctx->id("LUT4"), "$PACKER_VCC");
        vcc_cell->params[ctx->id("INIT")] = "65535";
        std::unique_ptr<NetInfo> vcc_net = std::unique_ptr<NetInfo>(new NetInfo);
        vcc_net->name = ctx->id("$PACKER_VCC_NET");
        vcc_net->driver.cell = vcc_cell.get();
        vcc_net->driver.port = ctx->id("Z");
        vcc_cell->ports.at(ctx->id("Z")).net = vcc_net.get();

        std::vector<IdString> dead_nets;

        bool gnd_used = false, vcc_used = false;

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
                vcc_used = true;
                dead_nets.push_back(net.first);
                ctx->cells.erase(drv_cell);
            }
        }

        if (gnd_used) {
            ctx->cells[gnd_cell->name] = std::move(gnd_cell);
            ctx->nets[gnd_net->name] = std::move(gnd_net);
        }
        if (vcc_used) {
            ctx->cells[vcc_cell->name] = std::move(vcc_cell);
            ctx->nets[vcc_net->name] = std::move(vcc_net);
        }

        for (auto dn : dead_nets) {
            ctx->nets.erase(dn);
        }
    }

    void autocreate_empty_port(CellInfo *cell, IdString port)
    {
        if (!cell->ports.count(port)) {
            cell->ports[port].name = port;
            cell->ports[port].net = nullptr;
            cell->ports[port].type = PORT_IN;
        }
    }

    // Pack EBR
    void pack_ebr()
    {
        // Autoincrement WID (starting from 3 seems to match vendor behaviour?)
        int wid = 3;
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == id_DP16KD) {
                // Add ports, even if disconnected, to ensure correct tie-offs
                for (int i = 0; i < 14; i++) {
                    autocreate_empty_port(ci, ctx->id("ADA" + std::to_string(i)));
                    autocreate_empty_port(ci, ctx->id("ADB" + std::to_string(i)));
                }
                for (int i = 0; i < 18; i++) {
                    autocreate_empty_port(ci, ctx->id("DIA" + std::to_string(i)));
                    autocreate_empty_port(ci, ctx->id("DIB" + std::to_string(i)));
                }
                for (int i = 0; i < 3; i++) {
                    autocreate_empty_port(ci, ctx->id("CSA" + std::to_string(i)));
                    autocreate_empty_port(ci, ctx->id("CSB" + std::to_string(i)));
                }
                for (int i = 0; i < 3; i++) {
                    autocreate_empty_port(ci, ctx->id("CSA" + std::to_string(i)));
                    autocreate_empty_port(ci, ctx->id("CSB" + std::to_string(i)));
                }

                autocreate_empty_port(ci, id_CLKA);
                autocreate_empty_port(ci, id_CEA);
                autocreate_empty_port(ci, id_OCEA);
                autocreate_empty_port(ci, id_WEA);
                autocreate_empty_port(ci, id_RSTA);

                autocreate_empty_port(ci, id_CLKB);
                autocreate_empty_port(ci, id_CEB);
                autocreate_empty_port(ci, id_OCEB);
                autocreate_empty_port(ci, id_WEB);
                autocreate_empty_port(ci, id_RSTB);

                ci->attrs[ctx->id("WID")] = std::to_string(wid++);
            }
        }
    }

    // Pack DSPs
    void pack_dsps()
    {
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == id_MULT18X18D) {
                // Add ports, even if disconnected, to ensure correct tie-offs
                for (auto sig : {"CLK", "CE", "RST"})
                    for (int i = 0; i < 4; i++)
                        autocreate_empty_port(ci, ctx->id(sig + std::to_string(i)));
                for (auto sig : {"SIGNED", "SOURCE"})
                    for (auto c : {"A", "B"})
                        autocreate_empty_port(ci, ctx->id(sig + std::string(c)));
                for (auto port : {"A", "B", "C"})
                    for (int i = 0; i < 18; i++)
                        autocreate_empty_port(ci, ctx->id(port + std::to_string(i)));
                for (auto port : {"SRIA", "SRIB"})
                    for (int i = 0; i < 18; i++)
                        autocreate_empty_port(ci, ctx->id(port + std::to_string(i)));
            } else if (ci->type == id_ALU54B) {
                for (auto sig : {"CLK", "CE", "RST"})
                    for (int i = 0; i < 4; i++)
                        autocreate_empty_port(ci, ctx->id(sig + std::to_string(i)));
                autocreate_empty_port(ci, id_SIGNEDIA);
                autocreate_empty_port(ci, id_SIGNEDIB);
                autocreate_empty_port(ci, id_SIGNEDCIN);
                for (auto port : {"A", "B", "MA", "MB"})
                    for (int i = 0; i < 36; i++)
                        autocreate_empty_port(ci, ctx->id(port + std::to_string(i)));
                for (auto port : {"C", "CFB", "CIN"})
                    for (int i = 0; i < 54; i++)
                        autocreate_empty_port(ci, ctx->id(port + std::to_string(i)));
                for (int i = 0; i < 11; i++)
                    autocreate_empty_port(ci, ctx->id("OP" + std::to_string(i)));
            }
        }
    }

    // "Pack" DCUs
    void pack_dcus()
    {
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == id_DCUA) {
                if (ci->attrs.count(ctx->id("LOC"))) {
                    std::string loc = ci->attrs.at(ctx->id("LOC"));
                    if (loc == "DCU0" &&
                        (ctx->args.type == ArchArgs::LFE5UM_25F || ctx->args.type == ArchArgs::LFE5UM5G_25F))
                        ci->attrs[ctx->id("BEL")] = "X42/Y50/DCU";
                    else if (loc == "DCU0" &&
                             (ctx->args.type == ArchArgs::LFE5UM_45F || ctx->args.type == ArchArgs::LFE5UM5G_45F))
                        ci->attrs[ctx->id("BEL")] = "X42/Y71/DCU";
                    else if (loc == "DCU1" &&
                             (ctx->args.type == ArchArgs::LFE5UM_45F || ctx->args.type == ArchArgs::LFE5UM5G_45F))
                        ci->attrs[ctx->id("BEL")] = "X69/Y71/DCU";
                    else if (loc == "DCU0" &&
                             (ctx->args.type == ArchArgs::LFE5UM_85F || ctx->args.type == ArchArgs::LFE5UM5G_85F))
                        ci->attrs[ctx->id("BEL")] = "X46/Y95/DCU";
                    else if (loc == "DCU1" &&
                             (ctx->args.type == ArchArgs::LFE5UM_85F || ctx->args.type == ArchArgs::LFE5UM5G_85F))
                        ci->attrs[ctx->id("BEL")] = "X71/Y95/DCU";
                    else
                        log_error("no DCU location '%s' in device '%s'\n", loc.c_str(), ctx->getChipName().c_str());
                }
                if (!ci->attrs.count(ctx->id("BEL")))
                    log_error("DCU must be constrained to a Bel!\n");
                // Empty port auto-creation to generate correct tie-downs
                BelId exemplar_bel;
                for (auto bel : ctx->getBels()) {
                    if (ctx->getBelType(bel) == id_DCUA) {
                        exemplar_bel = bel;
                        break;
                    }
                }
                NPNR_ASSERT(exemplar_bel != BelId());
                for (auto pin : ctx->getBelPins(exemplar_bel))
                    if (ctx->getBelPinType(exemplar_bel, pin) == PORT_IN)
                        autocreate_empty_port(ci, pin);
            }
        }
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == id_EXTREFB) {
                const NetInfo *refo = net_or_nullptr(ci, id_REFCLKO);
                CellInfo *dcu = nullptr;
                if (refo == nullptr)
                    log_error("EXTREFB REFCLKO must not be unconnected\n");
                for (auto user : refo->users) {
                    if (user.cell->type != id_DCUA)
                        continue;
                    if (dcu != nullptr && dcu != user.cell)
                        log_error("EXTREFB REFCLKO must only drive a single DCUA\n");
                    dcu = user.cell;
                }
                if (!dcu->attrs.count(ctx->id("BEL")))
                    log_error("DCU must be constrained to a Bel!\n");
                std::string bel = dcu->attrs.at(ctx->id("BEL"));
                NPNR_ASSERT(bel.substr(bel.length() - 3) == "DCU");
                bel.replace(bel.length() - 3, 3, "EXTREF");
                ci->attrs[ctx->id("BEL")] = bel;
            } else if (ci->type == id_PCSCLKDIV) {
                const NetInfo *clki = net_or_nullptr(ci, id_CLKI);
                if (clki != nullptr && clki->driver.cell != nullptr && clki->driver.cell->type == id_DCUA) {
                    CellInfo *dcu = clki->driver.cell;
                    if (!dcu->attrs.count(ctx->id("BEL")))
                        log_error("DCU must be constrained to a Bel!\n");
                    BelId bel = ctx->getBelByName(ctx->id(dcu->attrs.at(ctx->id("BEL"))));
                    if (bel == BelId())
                        log_error("Invalid DCU bel '%s'\n", dcu->attrs.at(ctx->id("BEL")).c_str());
                    Loc loc = ctx->getBelLocation(bel);
                    // DCU0 -> CLKDIV z=0; DCU1 -> CLKDIV z=1
                    ci->constr_abs_z = true;
                    ci->constr_z = (loc.x >= 69) ? 1 : 0;
                }
            }
        }
    }

    // Preplace PLL
    void preplace_plls()
    {
        std::set<BelId> available_plls;
        for (auto bel : ctx->getBels()) {
            if (ctx->getBelType(bel) == id_EHXPLLL && ctx->checkBelAvail(bel))
                available_plls.insert(bel);
        }
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == id_EHXPLLL && ci->attrs.count(ctx->id("BEL")))
                available_plls.erase(ctx->getBelByName(ctx->id(ci->attrs.at(ctx->id("BEL")))));
        }
        // Place PLL connected to fixed drivers such as IO close to their source
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == id_EHXPLLL && !ci->attrs.count(ctx->id("BEL"))) {
                const NetInfo *drivernet = net_or_nullptr(ci, id_CLKI);
                if (drivernet == nullptr || drivernet->driver.cell == nullptr)
                    continue;
                const CellInfo *drivercell = drivernet->driver.cell;
                if (!drivercell->attrs.count(ctx->id("BEL")))
                    continue;
                BelId drvbel = ctx->getBelByName(ctx->id(drivercell->attrs.at(ctx->id("BEL"))));
                Loc drvloc = ctx->getBelLocation(drvbel);
                BelId closest_pll;
                int closest_distance = std::numeric_limits<int>::max();
                for (auto bel : available_plls) {
                    Loc pllloc = ctx->getBelLocation(bel);
                    int distance = std::abs(drvloc.x - pllloc.x) + std::abs(drvloc.y - pllloc.y);
                    if (distance < closest_distance) {
                        closest_pll = bel;
                        closest_distance = distance;
                    }
                }
                if (closest_pll == BelId())
                    log_error("failed to place PLL '%s'\n", ci->name.c_str(ctx));
                available_plls.erase(closest_pll);
                ci->attrs[ctx->id("BEL")] = ctx->getBelName(closest_pll).str(ctx);
            }
        }
        // Place PLLs driven by logic, etc, randomly
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == id_EHXPLLL && !ci->attrs.count(ctx->id("BEL"))) {
                if (available_plls.empty())
                    log_error("failed to place PLL '%s'\n", ci->name.c_str(ctx));
                BelId next_pll = *(available_plls.begin());
                available_plls.erase(next_pll);
                ci->attrs[ctx->id("BEL")] = ctx->getBelName(next_pll).str(ctx);
            }
        }
    }

    // Check if two nets have identical constant drivers
    bool equal_constant(NetInfo *a, NetInfo *b)
    {
        if (a->driver.cell == nullptr || b->driver.cell == nullptr)
            return (a->driver.cell == nullptr && b->driver.cell == nullptr);
        if (a->driver.cell->type != ctx->id("GND") && a->driver.cell->type != ctx->id("VCC"))
            return false;
        return a->driver.cell->type == b->driver.cell->type;
    }

    struct EdgeClockInfo
    {
        CellInfo *buffer = nullptr;
        NetInfo *unbuf = nullptr;
        NetInfo *buf = nullptr;
    };

    std::map<std::pair<int, int>, EdgeClockInfo> eclks;

    void make_eclk(PortInfo &usr_port, CellInfo *usr_cell, BelId usr_bel, int bank)
    {
        NetInfo *ecknet = usr_port.net;
        if (ecknet == nullptr)
            log_error("Input '%s' of cell '%s' cannot be disconnected\n", usr_port.name.c_str(ctx),
                      usr_cell->name.c_str(ctx));
        int found_eclk = -1, free_eclk = -1;
        for (int i = 0; i < 2; i++) {
            if (eclks.count(std::make_pair(bank, i))) {
                if (eclks.at(std::make_pair(bank, i)).unbuf == ecknet) {
                    found_eclk = i;
                    break;
                }
            } else if (free_eclk == -1) {
                free_eclk = i;
            }
        }
        if (found_eclk == -1) {
            if (free_eclk == -1) {
                log_error("Unable to promote edge clock '%s' for bank %d. 2/2 edge clocks already used by '%s' and "
                          "'%s'.\n",
                          ecknet->name.c_str(ctx), bank, eclks.at(std::make_pair(bank, 0)).unbuf->name.c_str(ctx),
                          eclks.at(std::make_pair(bank, 1)).unbuf->name.c_str(ctx));
            } else {
                log_info("Promoted '%s' to bank %d ECLK%d.\n", ecknet->name.c_str(ctx), bank, free_eclk);
                auto &eclk = eclks[std::make_pair(bank, free_eclk)];
                eclk.unbuf = ecknet;
                IdString eckname = ctx->id(ecknet->name.str(ctx) + "$eclk" + std::to_string(bank) + "_" +
                                           std::to_string(free_eclk));

                std::unique_ptr<NetInfo> promoted_ecknet(new NetInfo);
                promoted_ecknet->name = eckname;
                promoted_ecknet->is_global = true; // Prevents router etc touching this special net
                eclk.buf = promoted_ecknet.get();
                NPNR_ASSERT(!ctx->nets.count(eckname));
                ctx->nets[eckname] = std::move(promoted_ecknet);

                // Insert TRELLIS_ECLKBUF to isolate edge clock from general routing
                std::unique_ptr<CellInfo> eclkbuf =
                        create_ecp5_cell(ctx, id_TRELLIS_ECLKBUF, eckname.str(ctx) + "$buffer");
                BelId target_bel;
                // Find the correct Bel for the ECLKBUF
                IdString eclkname = ctx->id("G_BANK" + std::to_string(bank) + "ECLK" + std::to_string(free_eclk));
                for (auto bel : ctx->getBels()) {
                    if (ctx->getBelType(bel) != id_TRELLIS_ECLKBUF)
                        continue;
                    if (ctx->getWireBasename(ctx->getBelPinWire(bel, id_ECLKO)) != eclkname)
                        continue;
                    target_bel = bel;
                    break;
                }
                NPNR_ASSERT(target_bel != BelId());

                eclkbuf->attrs[ctx->id("BEL")] = ctx->getBelName(target_bel).str(ctx);

                connect_port(ctx, ecknet, eclkbuf.get(), id_ECLKI);
                connect_port(ctx, eclk.buf, eclkbuf.get(), id_ECLKO);
                found_eclk = free_eclk;
                eclk.buffer = eclkbuf.get();
                new_cells.push_back(std::move(eclkbuf));
            }
        }

        auto &eclk = eclks[std::make_pair(bank, found_eclk)];
        disconnect_port(ctx, usr_cell, usr_port.name);
        usr_port.net = nullptr;
        connect_port(ctx, eclk.buf, usr_cell, usr_port.name);

        // Simple ECLK router
        WireId userWire = ctx->getBelPinWire(usr_bel, usr_port.name);
        IdString bnke_name = ctx->id("BNK_ECLK" + std::to_string(found_eclk));
        IdString global_name = ctx->id("G_BANK" + std::to_string(bank) + "ECLK" + std::to_string(found_eclk));

        std::queue<WireId> upstream;
        std::unordered_map<WireId, PipId> backtrace;
        upstream.push(userWire);
        WireId next;
        while (true) {
            if (upstream.empty() || upstream.size() > 30000)
                log_error("failed to route bank %d ECLK%d to %s.%s\n", bank, found_eclk,
                          ctx->getBelName(usr_bel).c_str(ctx), usr_port.name.c_str(ctx));
            next = upstream.front();
            upstream.pop();
            if (ctx->debug)
                log_info("    visited %s\n", ctx->getWireName(next).c_str(ctx));
            IdString basename = ctx->getWireBasename(next);
            if (basename == bnke_name || basename == global_name) {
                break;
            }
            if (ctx->checkWireAvail(next)) {
                for (auto pip : ctx->getPipsUphill(next)) {
                    WireId src = ctx->getPipSrcWire(pip);
                    backtrace[src] = pip;
                    upstream.push(src);
                }
            }
        }
        // Set all the pips we found along the way
        WireId cursor = next;
        while (true) {
            auto fnd = backtrace.find(cursor);
            if (fnd == backtrace.end())
                break;
            ctx->bindPip(fnd->second, eclk.buf, STRENGTH_LOCKED);
            cursor = ctx->getPipDstWire(fnd->second);
        }
    }

    void tie_zero(CellInfo *ci, IdString port)
    {

        if (!ci->ports.count(port)) {
            ci->ports[port].name = port;
            ci->ports[port].type = PORT_IN;
        }

        std::unique_ptr<CellInfo> zero_cell{new CellInfo};
        std::unique_ptr<NetInfo> zero_net{new NetInfo};
        IdString name = ctx->id(ci->name.str(ctx) + "$zero$" + port.str(ctx));
        zero_cell->type = ctx->id("GND");
        zero_cell->name = name;
        zero_net->name = name;
        zero_cell->ports[ctx->id("GND")].type = PORT_OUT;
        connect_port(ctx, zero_net.get(), zero_cell.get(), ctx->id("GND"));
        connect_port(ctx, zero_net.get(), ci, port);
        ctx->nets[name] = std::move(zero_net);
        new_cells.push_back(std::move(zero_cell));
    }

    std::unordered_map<IdString, std::pair<bool, int>> dqsbuf_dqsg;
    // Pack DQSBUFs
    void pack_dqsbuf()
    {
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == id_DQSBUFM) {
                CellInfo *pio = net_driven_by(ctx, ci->ports.at(ctx->id("DQSI")).net, is_trellis_io, id_O);
                if (pio == nullptr || ci->ports.at(ctx->id("DQSI")).net->users.size() > 1)
                    log_error("DQSBUFM '%s' DQSI input must be connected only to a top level input\n",
                              ci->name.c_str(ctx));
                if (!pio->attrs.count(ctx->id("BEL")))
                    log_error("DQSBUFM can only be used with a pin-constrained PIO connected to its DQSI input"
                              "(while processing '%s').\n",
                              ci->name.c_str(ctx));
                BelId pio_bel = ctx->getBelByName(ctx->id(pio->attrs.at(ctx->id("BEL"))));
                NPNR_ASSERT(pio_bel != BelId());
                Loc pio_loc = ctx->getBelLocation(pio_bel);
                if (pio_loc.z != 0)
                    log_error("PIO '%s' does not appear to be a DQS site (expecting an 'A' pin).\n",
                              ctx->getBelName(pio_bel).c_str(ctx));
                pio_loc.z = 8;
                BelId dqsbuf = ctx->getBelByLocation(pio_loc);
                if (dqsbuf == BelId() || ctx->getBelType(dqsbuf) != id_DQSBUFM)
                    log_error("PIO '%s' does not appear to be a DQS site (didn't find a DQSBUFM).\n",
                              ctx->getBelName(pio_bel).c_str(ctx));
                ci->attrs[ctx->id("BEL")] = ctx->getBelName(dqsbuf).str(ctx);
                bool got_dqsg = ctx->getPIODQSGroup(pio_bel, dqsbuf_dqsg[ci->name].first, dqsbuf_dqsg[ci->name].second);
                NPNR_ASSERT(got_dqsg);
                log_info("Constrained DQSBUFM '%s' to %cDQS%d\n", ci->name.c_str(ctx),
                         dqsbuf_dqsg[ci->name].first ? 'R' : 'L', dqsbuf_dqsg[ci->name].second);

                // Set all special ports, if used as 'globals' that the router won't touch
                for (auto port : {id_DQSR90, id_RDPNTR0, id_RDPNTR1, id_RDPNTR2, id_WRPNTR0, id_WRPNTR1, id_WRPNTR2,
                                  id_DQSW270, id_DQSW}) {
                    if (!ci->ports.count(port))
                        continue;
                    NetInfo *pn = ci->ports.at(port).net;
                    if (pn == nullptr)
                        continue;
                    for (auto &usr : pn->users) {
                        if (usr.port != port ||
                            (usr.cell->type != ctx->id("ODDRX2DQA") && usr.cell->type != ctx->id("ODDRX2DQSB") &&
                             usr.cell->type != ctx->id("TSHX2DQSA") && usr.cell->type != ctx->id("IDDRX2DQA") &&
                             usr.cell->type != ctx->id("TSHX2DQA") && usr.cell->type != id_IOLOGIC))
                            log_error("Port '%s' of DQSBUFM '%s' cannot drive port '%s' of cell '%s'.\n",
                                      port.c_str(ctx), ci->name.c_str(ctx), usr.port.c_str(ctx),
                                      usr.cell->name.c_str(ctx));
                    }
                    pn->is_global = true;
                }

                for (auto zport :
                     {id_RDMOVE, id_RDDIRECTION, id_WRMOVE, id_WRDIRECTION, id_READ0, id_READ1, id_READCLKSEL0,
                      id_READCLKSEL1, id_READCLKSEL2, id_DYNDELAY0, id_DYNDELAY1, id_DYNDELAY2, id_DYNDELAY3,
                      id_DYNDELAY4, id_DYNDELAY5, id_DYNDELAY6, id_DYNDELAY7}) {
                    if (net_or_nullptr(ci, zport) == nullptr)
                        tie_zero(ci, zport);
                }
            }
        }
    }

    int lookup_delay(const std::string &del_mode)
    {
        if (del_mode == "USER_DEFINED")
            return 0;
        else if (del_mode == "DQS_ALIGNED_X2")
            return 6;
        else if (del_mode == "DQS_CMD_CLK")
            return 9;
        else if (del_mode == "ECLK_ALIGNED")
            return 21;
        else if (del_mode == "ECLK_CENTERED")
            return 11;
        else if (del_mode == "ECLKBRIDGE_ALIGNED")
            return 39;
        else if (del_mode == "ECLKBRIDGE_CENTERED")
            return 29;
        else if (del_mode == "SCLK_ALIGNED")
            return 50;
        else if (del_mode == "SCLK_CENTERED")
            return 39;
        else if (del_mode == "SCLK_ZEROHOLD")
            return 59;
        else
            log_error("Unsupported DEL_MODE '%s'\n", del_mode.c_str());
    }

    // Pack IOLOGIC
    void pack_iologic()
    {
        std::unordered_map<IdString, CellInfo *> pio_iologic;

        auto set_iologic_sclk = [&](CellInfo *iol, CellInfo *prim, IdString port, bool input) {
            NetInfo *sclk = nullptr;
            if (prim->ports.count(port))
                sclk = prim->ports[port].net;
            if (sclk == nullptr) {
                iol->params[input ? ctx->id("CLKIMUX") : ctx->id("CLKOMUX")] = "0";
            } else {
                iol->params[input ? ctx->id("CLKIMUX") : ctx->id("CLKOMUX")] = "CLK";
                if (iol->ports[id_CLK].net != nullptr) {
                    if (iol->ports[id_CLK].net != sclk && !equal_constant(iol->ports[id_CLK].net, sclk))
                        log_error("IOLOGIC '%s' has conflicting clocks '%s' and '%s'\n", iol->name.c_str(ctx),
                                  iol->ports[id_CLK].net->name.c_str(ctx), sclk->name.c_str(ctx));
                } else {
                    connect_port(ctx, sclk, iol, id_CLK);
                }
            }
            if (prim->ports.count(port))
                disconnect_port(ctx, prim, port);
        };

        auto set_iologic_eclk = [&](CellInfo *iol, CellInfo *prim, IdString port) {
            NetInfo *eclk = nullptr;
            if (prim->ports.count(port))
                eclk = prim->ports[port].net;
            if (eclk == nullptr)
                log_error("%s '%s' cannot have disconnected ECLK", prim->type.c_str(ctx), prim->name.c_str(ctx));

            if (iol->ports[id_ECLK].net != nullptr) {
                if (iol->ports[id_ECLK].net != eclk)
                    log_error("IOLOGIC '%s' has conflicting ECLKs '%s' and '%s'\n", iol->name.c_str(ctx),
                              iol->ports[id_ECLK].net->name.c_str(ctx), eclk->name.c_str(ctx));
            } else {
                connect_port(ctx, eclk, iol, id_ECLK);
            }
            if (prim->ports.count(port))
                disconnect_port(ctx, prim, port);
        };

        auto set_iologic_lsr = [&](CellInfo *iol, CellInfo *prim, IdString port, bool input) {
            NetInfo *lsr = nullptr;
            if (prim->ports.count(port))
                lsr = prim->ports[port].net;
            if (lsr == nullptr) {
                iol->params[input ? ctx->id("LSRIMUX") : ctx->id("LSROMUX")] = "0";
            } else {
                iol->params[input ? ctx->id("LSRIMUX") : ctx->id("LSROMUX")] = "LSRMUX";
                if (iol->ports[id_LSR].net != nullptr && !equal_constant(iol->ports[id_LSR].net, lsr)) {
                    if (iol->ports[id_LSR].net != lsr)
                        log_error("IOLOGIC '%s' has conflicting LSR signals '%s' and '%s'\n", iol->name.c_str(ctx),
                                  iol->ports[id_LSR].net->name.c_str(ctx), lsr->name.c_str(ctx));
                } else if (iol->ports[id_LSR].net == nullptr) {
                    connect_port(ctx, lsr, iol, id_LSR);
                }
            }
            if (prim->ports.count(port))
                disconnect_port(ctx, prim, port);
        };

        auto set_iologic_mode = [&](CellInfo *iol, std::string mode) {
            auto &curr_mode = iol->params[ctx->id("MODE")];
            if (curr_mode != "NONE" && curr_mode != "IREG_OREG" && curr_mode != mode)
                log_error("IOLOGIC '%s' has conflicting modes '%s' and '%s'\n", iol->name.c_str(ctx), curr_mode.c_str(),
                          mode.c_str());
            if (iol->type == id_SIOLOGIC && mode != "IREG_OREG" && mode != "IDDRX1_ODDRX1" && mode != "NONE")
                log_error("IOLOGIC '%s' is set to mode '%s', but this is only supported for left and right IO\n",
                          iol->name.c_str(ctx), mode.c_str());
            curr_mode = mode;
        };

        auto get_pio_bel = [&](CellInfo *pio, CellInfo *curr) {
            if (!pio->attrs.count(ctx->id("BEL")))
                log_error("IOLOGIC functionality (DDR, DELAY, DQS, etc) can only be used with pin-constrained PIO "
                          "(while processing '%s').\n",
                          curr->name.c_str(ctx));
            BelId bel = ctx->getBelByName(ctx->id(pio->attrs.at(ctx->id("BEL"))));
            NPNR_ASSERT(bel != BelId());
            return bel;
        };

        auto create_pio_iologic = [&](CellInfo *pio, CellInfo *curr) {
            BelId bel = get_pio_bel(pio, curr);
            log_info("IOLOGIC component %s connected to PIO Bel %s\n", curr->name.c_str(ctx),
                     ctx->getBelName(bel).c_str(ctx));
            Loc loc = ctx->getBelLocation(bel);
            bool s = false;
            if (loc.y == 0 || loc.y == (ctx->chip_info->height - 1))
                s = true;
            std::unique_ptr<CellInfo> iol =
                    create_ecp5_cell(ctx, s ? id_SIOLOGIC : id_IOLOGIC, pio->name.str(ctx) + "$IOL");

            loc.z += s ? 2 : 4;
            iol->attrs[ctx->id("BEL")] = ctx->getBelName(ctx->getBelByLocation(loc)).str(ctx);

            CellInfo *iol_ptr = iol.get();
            pio_iologic[pio->name] = iol_ptr;
            new_cells.push_back(std::move(iol));
            return iol_ptr;
        };

        auto process_dqs_port = [&](CellInfo *prim, CellInfo *pio, CellInfo *iol, IdString port) {
            NetInfo *sig = nullptr;
            if (prim->ports.count(port))
                sig = prim->ports[port].net;
            if (sig == nullptr || sig->driver.cell == nullptr)
                log_error("Port %s of cell '%s' cannot be disconnected, it must be driven by a DQSBUFM\n",
                          port.c_str(ctx), prim->name.c_str(ctx));
            if (iol->ports.at(port).net != nullptr) {
                if (iol->ports.at(port).net != sig) {
                    log_error("IOLOGIC '%s' has conflicting %s signals '%s' and '%s'\n", iol->name.c_str(ctx),
                              port.c_str(ctx), iol->ports[port].net->name.c_str(ctx), sig->name.c_str(ctx));
                }
                disconnect_port(ctx, prim, port);
            } else {
                bool dqsr;
                int dqsgroup;
                bool has_dqs = ctx->getPIODQSGroup(get_pio_bel(pio, prim), dqsr, dqsgroup);
                if (!has_dqs)
                    log_error("Primitive '%s' cannot be connected to top level port '%s' as the associated pin is not "
                              "in any DQS group",
                              prim->name.c_str(ctx), pio->name.c_str(ctx));
                if (sig->driver.cell->type != id_DQSBUFM || sig->driver.port != port)
                    log_error("Port %s of cell '%s' must be driven by port %s of a DQSBUFM", port.c_str(ctx),
                              prim->name.c_str(ctx), port.c_str(ctx));
                auto &driver_group = dqsbuf_dqsg.at(sig->driver.cell->name);
                if (driver_group.first != dqsr || driver_group.second != dqsgroup)
                    log_error("DQS group mismatch, port %s of '%s' in group %cDQ%d is driven by DQSBUFM '%s' in group "
                              "%cDQ%d\n",
                              port.c_str(ctx), prim->name.c_str(ctx), dqsr ? 'R' : 'L', dqsgroup,
                              sig->driver.cell->name.c_str(ctx), driver_group.first ? 'R' : 'L', driver_group.second);
                replace_port(prim, port, iol, port);
            }
        };

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == ctx->id("DELAYF") || ci->type == ctx->id("DELAYG")) {
                CellInfo *i_pio = net_driven_by(ctx, ci->ports.at(ctx->id("A")).net, is_trellis_io, id_O);
                CellInfo *o_pio = net_only_drives(ctx, ci->ports.at(ctx->id("Z")).net, is_trellis_io, id_I, true);
                CellInfo *iol = nullptr;
                if (i_pio != nullptr && ci->ports.at(ctx->id("A")).net->users.size() == 1) {
                    iol = create_pio_iologic(i_pio, ci);
                    set_iologic_mode(iol, "IREG_OREG");
                    bool drives_iologic = false;
                    for (auto user : ci->ports.at(ctx->id("Z")).net->users)
                        if (is_iologic_input_cell(ctx, user.cell) && user.port == ctx->id("D"))
                            drives_iologic = true;
                    if (drives_iologic) {
                        // Reconnect to PIO which the packer expects later on
                        NetInfo *input_net = ci->ports.at(ctx->id("A")).net, *dly_net = ci->ports.at(ctx->id("Z")).net;
                        disconnect_port(ctx, i_pio, id_O);
                        i_pio->ports.at(id_O).net = nullptr;
                        disconnect_port(ctx, ci, id_A);
                        ci->ports.at(id_A).net = nullptr;
                        disconnect_port(ctx, ci, id_Z);
                        ci->ports.at(id_Z).net = nullptr;
                        connect_port(ctx, dly_net, i_pio, id_O);
                        connect_port(ctx, input_net, iol, id_INDD);
                        connect_port(ctx, input_net, iol, id_DI);
                    } else {
                        replace_port(ci, id_A, iol, id_PADDI);
                        replace_port(ci, id_Z, iol, id_INDD);
                    }
                    packed_cells.insert(cell.first);
                } else if (o_pio != nullptr) {
                    iol = create_pio_iologic(o_pio, ci);
                    iol->params[ctx->id("DELAY.OUTDEL")] = "ENABLED";
                    bool driven_by_iol = false;
                    NetInfo *input_net = ci->ports.at(ctx->id("A")).net, *dly_net = ci->ports.at(ctx->id("Z")).net;
                    if (input_net->driver.cell != nullptr && is_iologic_output_cell(ctx, input_net->driver.cell) &&
                        input_net->driver.port == ctx->id("Q"))
                        driven_by_iol = true;
                    if (driven_by_iol) {
                        disconnect_port(ctx, o_pio, id_I);
                        o_pio->ports.at(id_I).net = nullptr;
                        disconnect_port(ctx, ci, id_A);
                        ci->ports.at(id_A).net = nullptr;
                        disconnect_port(ctx, ci, id_Z);
                        ci->ports.at(id_Z).net = nullptr;
                        connect_port(ctx, input_net, o_pio, id_I);
                        ctx->nets.erase(dly_net->name);
                    } else {
                        replace_port(ci, ctx->id("A"), iol, id_TXDATA0);
                        replace_port(ci, ctx->id("Z"), iol, id_IOLDO);
                        if (!o_pio->ports.count(id_IOLDO)) {
                            o_pio->ports[id_IOLDO].name = id_IOLDO;
                            o_pio->ports[id_IOLDO].type = PORT_IN;
                        }
                        replace_port(o_pio, id_I, o_pio, id_IOLDO);
                    }
                    packed_cells.insert(cell.first);
                } else {
                    log_error("%s '%s' must be connected directly to top level input or output\n", ci->type.c_str(ctx),
                              ci->name.c_str(ctx));
                }
                iol->params[ctx->id("DELAY.DEL_VALUE")] =
                        std::to_string(lookup_delay(str_or_default(ci->params, ctx->id("DEL_MODE"), "USER_DEFINED")));
                if (ci->params.count(ctx->id("DEL_VALUE")) &&
                    ci->params.at(ctx->id("DEL_VALUE")).substr(0, 5) != "DELAY")
                    iol->params[ctx->id("DELAY.DEL_VALUE")] = ci->params.at(ctx->id("DEL_VALUE"));
                if (ci->ports.count(id_LOADN))
                    replace_port(ci, id_LOADN, iol, id_LOADN);
                else
                    tie_zero(ci, id_LOADN);
                if (ci->ports.count(id_MOVE))
                    replace_port(ci, id_MOVE, iol, id_MOVE);
                else
                    tie_zero(ci, id_MOVE);
                if (ci->ports.count(id_DIRECTION))
                    replace_port(ci, id_DIRECTION, iol, id_DIRECTION);
                else
                    tie_zero(ci, id_DIRECTION);
                if (ci->ports.count(id_CFLAG))
                    replace_port(ci, id_CFLAG, iol, id_CFLAG);
            }
        }

        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == ctx->id("IDDRX1F")) {
                CellInfo *pio = net_driven_by(ctx, ci->ports.at(ctx->id("D")).net, is_trellis_io, id_O);
                if (pio == nullptr || ci->ports.at(ctx->id("D")).net->users.size() > 1)
                    log_error("IDDRX1F '%s' D input must be connected only to a top level input\n",
                              ci->name.c_str(ctx));
                CellInfo *iol;
                if (pio_iologic.count(pio->name))
                    iol = pio_iologic.at(pio->name);
                else
                    iol = create_pio_iologic(pio, ci);
                set_iologic_mode(iol, "IDDRX1_ODDRX1");
                replace_port(ci, ctx->id("D"), iol, id_PADDI);
                set_iologic_sclk(iol, ci, ctx->id("SCLK"), true);
                set_iologic_lsr(iol, ci, ctx->id("RST"), true);
                replace_port(ci, ctx->id("Q0"), iol, id_RXDATA0);
                replace_port(ci, ctx->id("Q1"), iol, id_RXDATA1);
                iol->params[ctx->id("GSR")] = str_or_default(ci->params, ctx->id("GSR"), "DISABLED");
                packed_cells.insert(cell.first);
            } else if (ci->type == ctx->id("ODDRX1F")) {
                CellInfo *pio = net_only_drives(ctx, ci->ports.at(ctx->id("Q")).net, is_trellis_io, id_I, true);
                if (pio == nullptr)
                    log_error("ODDRX1F '%s' Q output must be connected only to a top level output\n",
                              ci->name.c_str(ctx));
                CellInfo *iol;
                if (pio_iologic.count(pio->name))
                    iol = pio_iologic.at(pio->name);
                else
                    iol = create_pio_iologic(pio, ci);
                set_iologic_mode(iol, "IDDRX1_ODDRX1");
                replace_port(ci, ctx->id("Q"), iol, id_IOLDO);
                if (!pio->ports.count(id_IOLDO)) {
                    pio->ports[id_IOLDO].name = id_IOLDO;
                    pio->ports[id_IOLDO].type = PORT_IN;
                }
                replace_port(pio, id_I, pio, id_IOLDO);
                pio->params[ctx->id("DATAMUX_ODDR")] = "IOLDO";
                set_iologic_sclk(iol, ci, ctx->id("SCLK"), false);
                set_iologic_lsr(iol, ci, ctx->id("RST"), false);
                replace_port(ci, ctx->id("D0"), iol, id_TXDATA0);
                replace_port(ci, ctx->id("D1"), iol, id_TXDATA1);
                iol->params[ctx->id("GSR")] = str_or_default(ci->params, ctx->id("GSR"), "DISABLED");
                packed_cells.insert(cell.first);
            } else if (ci->type == ctx->id("ODDRX2F")) {
                CellInfo *pio = net_only_drives(ctx, ci->ports.at(ctx->id("Q")).net, is_trellis_io, id_I, true);
                if (pio == nullptr)
                    log_error("ODDRX2F '%s' Q output must be connected only to a top level output\n",
                              ci->name.c_str(ctx));
                CellInfo *iol;
                if (pio_iologic.count(pio->name))
                    iol = pio_iologic.at(pio->name);
                else
                    iol = create_pio_iologic(pio, ci);
                set_iologic_mode(iol, "ODDRXN");
                replace_port(ci, ctx->id("Q"), iol, id_IOLDO);
                if (!pio->ports.count(id_IOLDO)) {
                    pio->ports[id_IOLDO].name = id_IOLDO;
                    pio->ports[id_IOLDO].type = PORT_IN;
                }
                replace_port(pio, id_I, pio, id_IOLDO);
                set_iologic_sclk(iol, ci, ctx->id("SCLK"), false);
                set_iologic_sclk(iol, ci, ctx->id("SCLK"), true);
                set_iologic_eclk(iol, ci, id_ECLK);
                set_iologic_lsr(iol, ci, ctx->id("RST"), false);
                set_iologic_lsr(iol, ci, ctx->id("RST"), true);
                replace_port(ci, ctx->id("D0"), iol, id_TXDATA0);
                replace_port(ci, ctx->id("D1"), iol, id_TXDATA1);
                replace_port(ci, ctx->id("D2"), iol, id_TXDATA2);
                replace_port(ci, ctx->id("D3"), iol, id_TXDATA3);
                iol->params[ctx->id("GSR")] = str_or_default(ci->params, ctx->id("GSR"), "DISABLED");
                iol->params[ctx->id("ODDRXN.MODE")] = "ODDRX2";
                pio->params[ctx->id("DATAMUX_ODDR")] = "IOLDO";
                packed_cells.insert(cell.first);
            } else if (ci->type == ctx->id("IDDRX2F")) {
                CellInfo *pio = net_driven_by(ctx, ci->ports.at(ctx->id("D")).net, is_trellis_io, id_O);
                if (pio == nullptr || ci->ports.at(ctx->id("D")).net->users.size() > 1)
                    log_error("IDDRX2F '%s' D input must be connected only to a top level input\n",
                              ci->name.c_str(ctx));
                CellInfo *iol;
                if (pio_iologic.count(pio->name))
                    iol = pio_iologic.at(pio->name);
                else
                    iol = create_pio_iologic(pio, ci);
                set_iologic_mode(iol, "IDDRXN");
                replace_port(ci, ctx->id("D"), iol, id_PADDI);
                set_iologic_sclk(iol, ci, ctx->id("SCLK"), true);
                set_iologic_eclk(iol, ci, id_ECLK);
                set_iologic_lsr(iol, ci, ctx->id("RST"), true);
                replace_port(ci, ctx->id("Q0"), iol, id_RXDATA0);
                replace_port(ci, ctx->id("Q1"), iol, id_RXDATA1);
                replace_port(ci, ctx->id("Q2"), iol, id_RXDATA2);
                replace_port(ci, ctx->id("Q3"), iol, id_RXDATA3);
                iol->params[ctx->id("GSR")] = str_or_default(ci->params, ctx->id("GSR"), "DISABLED");
                iol->params[ctx->id("IDDRXN.MODE")] = "IDDRX2";
                packed_cells.insert(cell.first);
            } else if (ci->type == ctx->id("OSHX2A")) {
                CellInfo *pio = net_only_drives(ctx, ci->ports.at(ctx->id("Q")).net, is_trellis_io, id_I, true);
                if (pio == nullptr)
                    log_error("OSHX2A '%s' Q output must be connected only to a top level output\n",
                              ci->name.c_str(ctx));
                CellInfo *iol;
                if (pio_iologic.count(pio->name))
                    iol = pio_iologic.at(pio->name);
                else
                    iol = create_pio_iologic(pio, ci);
                set_iologic_mode(iol, "MIDDRX_MODDRX");
                replace_port(ci, ctx->id("Q"), iol, id_IOLDO);
                if (!pio->ports.count(id_IOLDO)) {
                    pio->ports[id_IOLDO].name = id_IOLDO;
                    pio->ports[id_IOLDO].type = PORT_IN;
                }
                replace_port(pio, id_I, pio, id_IOLDO);
                set_iologic_sclk(iol, ci, ctx->id("SCLK"), false);
                set_iologic_eclk(iol, ci, id_ECLK);
                set_iologic_lsr(iol, ci, ctx->id("RST"), false);
                set_iologic_lsr(iol, ci, ctx->id("RST"), true);
                replace_port(ci, ctx->id("D0"), iol, id_TXDATA0);
                replace_port(ci, ctx->id("D1"), iol, id_TXDATA2);
                iol->params[ctx->id("GSR")] = str_or_default(ci->params, ctx->id("GSR"), "DISABLED");
                iol->params[ctx->id("MODDRX.MODE")] = "MOSHX2";
                pio->params[ctx->id("DATAMUX_MDDR")] = "IOLDO";
                packed_cells.insert(cell.first);
            } else if (ci->type == ctx->id("ODDRX2DQA") || ci->type == ctx->id("ODDRX2DQSB")) {
                CellInfo *pio = net_only_drives(ctx, ci->ports.at(ctx->id("Q")).net, is_trellis_io, id_I, true);
                if (pio == nullptr)
                    log_error("%s '%s' Q output must be connected only to a top level output\n", ci->type.c_str(ctx),
                              ci->name.c_str(ctx));
                CellInfo *iol;
                if (pio_iologic.count(pio->name))
                    iol = pio_iologic.at(pio->name);
                else
                    iol = create_pio_iologic(pio, ci);
                set_iologic_mode(iol, "MIDDRX_MODDRX");
                replace_port(ci, ctx->id("Q"), iol, id_IOLDO);
                if (!pio->ports.count(id_IOLDO)) {
                    pio->ports[id_IOLDO].name = id_IOLDO;
                    pio->ports[id_IOLDO].type = PORT_IN;
                }
                replace_port(pio, id_I, pio, id_IOLDO);
                set_iologic_sclk(iol, ci, ctx->id("SCLK"), false);
                set_iologic_eclk(iol, ci, id_ECLK);
                set_iologic_lsr(iol, ci, ctx->id("RST"), false);
                set_iologic_lsr(iol, ci, ctx->id("RST"), true);
                replace_port(ci, ctx->id("D0"), iol, id_TXDATA0);
                replace_port(ci, ctx->id("D1"), iol, id_TXDATA1);
                replace_port(ci, ctx->id("D2"), iol, id_TXDATA2);
                replace_port(ci, ctx->id("D3"), iol, id_TXDATA3);
                iol->params[ctx->id("GSR")] = str_or_default(ci->params, ctx->id("GSR"), "DISABLED");
                iol->params[ctx->id("MODDRX.MODE")] = "MODDRX2";
                iol->params[ctx->id("MIDDRX_MODDRX.WRCLKMUX")] = ci->type == ctx->id("ODDRX2DQSB") ? "DQSW" : "DQSW270";
                process_dqs_port(ci, pio, iol, ci->type == ctx->id("ODDRX2DQSB") ? id_DQSW : id_DQSW270);
                pio->params[ctx->id("DATAMUX_MDDR")] = "IOLDO";
                packed_cells.insert(cell.first);
            } else if (ci->type == ctx->id("IDDRX2DQA")) {
                CellInfo *pio = net_driven_by(ctx, ci->ports.at(ctx->id("D")).net, is_trellis_io, id_O);
                if (pio == nullptr || ci->ports.at(ctx->id("D")).net->users.size() > 1)
                    log_error("IDDRX2DQA '%s' D input must be connected only to a top level input\n",
                              ci->name.c_str(ctx));
                CellInfo *iol;
                if (pio_iologic.count(pio->name))
                    iol = pio_iologic.at(pio->name);
                else
                    iol = create_pio_iologic(pio, ci);
                set_iologic_mode(iol, "MIDDRX_MODDRX");
                replace_port(ci, ctx->id("D"), iol, id_PADDI);
                set_iologic_sclk(iol, ci, ctx->id("SCLK"), true);
                set_iologic_eclk(iol, ci, id_ECLK);
                set_iologic_lsr(iol, ci, ctx->id("RST"), true);
                replace_port(ci, ctx->id("Q0"), iol, id_RXDATA0);
                replace_port(ci, ctx->id("Q1"), iol, id_RXDATA1);
                replace_port(ci, ctx->id("Q2"), iol, id_RXDATA2);
                replace_port(ci, ctx->id("Q3"), iol, id_RXDATA3);
                replace_port(ci, ctx->id("QWL"), iol, id_INFF);
                iol->params[ctx->id("GSR")] = str_or_default(ci->params, ctx->id("GSR"), "DISABLED");
                iol->params[ctx->id("MIDDRX.MODE")] = "MIDDRX2";
                process_dqs_port(ci, pio, iol, id_DQSR90);
                process_dqs_port(ci, pio, iol, id_RDPNTR2);
                process_dqs_port(ci, pio, iol, id_RDPNTR1);
                process_dqs_port(ci, pio, iol, id_RDPNTR0);
                process_dqs_port(ci, pio, iol, id_WRPNTR2);
                process_dqs_port(ci, pio, iol, id_WRPNTR1);
                process_dqs_port(ci, pio, iol, id_WRPNTR0);
                packed_cells.insert(cell.first);
            } else if (ci->type == ctx->id("TSHX2DQA") || ci->type == ctx->id("TSHX2DQSA")) {
                CellInfo *pio = net_only_drives(ctx, ci->ports.at(ctx->id("Q")).net, is_trellis_io, id_T, true);
                if (pio == nullptr)
                    log_error("%s '%s' Q output must be connected only to a top level tristate\n", ci->type.c_str(ctx),
                              ci->name.c_str(ctx));
                CellInfo *iol;
                if (pio_iologic.count(pio->name))
                    iol = pio_iologic.at(pio->name);
                else
                    iol = create_pio_iologic(pio, ci);
                set_iologic_mode(iol, "MIDDRX_MODDRX");
                replace_port(ci, ctx->id("Q"), iol, id_IOLTO);
                if (!pio->ports.count(id_IOLTO)) {
                    pio->ports[id_IOLTO].name = id_IOLTO;
                    pio->ports[id_IOLTO].type = PORT_IN;
                }
                replace_port(pio, id_T, pio, id_IOLTO);
                set_iologic_sclk(iol, ci, ctx->id("SCLK"), false);
                set_iologic_eclk(iol, ci, id_ECLK);
                set_iologic_lsr(iol, ci, ctx->id("RST"), false);
                replace_port(ci, ctx->id("T0"), iol, id_TSDATA0);
                replace_port(ci, ctx->id("T1"), iol, id_TSDATA1);
                process_dqs_port(ci, pio, iol, ci->type == ctx->id("TSHX2DQSA") ? id_DQSW : id_DQSW270);
                iol->params[ctx->id("GSR")] = str_or_default(ci->params, ctx->id("GSR"), "DISABLED");
                iol->params[ctx->id("MTDDRX.MODE")] = "MTSHX2";
                iol->params[ctx->id("MTDDRX.REGSET")] = "SET";
                iol->params[ctx->id("MTDDRX.DQSW_INVERT")] = ci->type == ctx->id("TSHX2DQSA") ? "ENABLED" : "DISABLED";
                iol->params[ctx->id("MIDDRX_MODDRX.WRCLKMUX")] = ci->type == ctx->id("TSHX2DQSA") ? "DQSW" : "DQSW270";
                iol->params[ctx->id("IOLTOMUX")] = "TDDR";
                packed_cells.insert(cell.first);
            }
        }
        flush_cells();
        // Promote/route edge clocks
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == id_IOLOGIC || ci->type == id_DQSBUFM) {
                if (!ci->ports.count(id_ECLK) || ci->ports.at(id_ECLK).net == nullptr)
                    continue;
                BelId bel = ctx->getBelByName(ctx->id(str_or_default(ci->attrs, ctx->id("BEL"))));
                NPNR_ASSERT(bel != BelId());
                Loc pioLoc = ctx->getBelLocation(bel);
                if (ci->type == id_DQSBUFM)
                    pioLoc.z -= 8;
                else
                    pioLoc.z -= 4;
                BelId pioBel = ctx->getBelByLocation(pioLoc);
                NPNR_ASSERT(pioBel != BelId());
                int bank = ctx->getPioBelBank(pioBel);
                make_eclk(ci->ports.at(id_ECLK), ci, bel, bank);
            }
        }
        flush_cells();
        // Constrain ECLK-related cells
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (ci->type == id_CLKDIVF) {
                const NetInfo *clki = net_or_nullptr(ci, id_CLKI);
                for (auto &eclk : eclks) {
                    if (eclk.second.unbuf == clki) {
                        for (auto bel : ctx->getBels()) {
                            if (ctx->getBelType(bel) != id_CLKDIVF)
                                continue;
                            Loc loc = ctx->getBelLocation(bel);
                            // CLKDIVF for bank 6/7 on the left; for bank 2/3 on the right
                            if (loc.x < 10 && eclk.first.first != 6 && eclk.first.first != 7)
                                continue;
                            // z-index of CLKDIVF must match index of ECLK
                            if (loc.z != eclk.first.second)
                                continue;
                            ci->attrs[ctx->id("BEL")] = ctx->getBelName(bel).str(ctx);
                            make_eclk(ci->ports.at(id_CLKI), ci, bel, eclk.first.first);
                            goto clkdiv_done;
                        }
                    }
                }
            clkdiv_done:
                continue;
            } else if (ci->type == id_ECLKSYNCB) {
                const NetInfo *eclko = net_or_nullptr(ci, id_ECLKO);
                if (eclko == nullptr)
                    log_error("ECLKSYNCB '%s' has disconnected port ECLKO\n", ci->name.c_str(ctx));
                for (auto user : eclko->users) {
                    if (user.cell->type == id_TRELLIS_ECLKBUF) {
                        Loc eckbuf_loc =
                                ctx->getBelLocation(ctx->getBelByName(ctx->id(user.cell->attrs.at(ctx->id("BEL")))));
                        for (auto bel : ctx->getBels()) {
                            if (ctx->getBelType(bel) != id_ECLKSYNCB)
                                continue;
                            Loc loc = ctx->getBelLocation(bel);
                            if (loc.x == eckbuf_loc.x && loc.y == eckbuf_loc.y && loc.z == eckbuf_loc.z - 2) {
                                ci->attrs[ctx->id("BEL")] = ctx->getBelName(bel).str(ctx);
                                goto eclksync_done;
                            }
                        }
                    }
                }
            eclksync_done:
                continue;
            } else if (ci->type == ctx->id("DDRDLLA")) {
                ci->type = id_DDRDLL; // transform from Verilog to Bel name
                const NetInfo *clk = net_or_nullptr(ci, id_CLK);
                if (clk == nullptr)
                    log_error("DDRDLLA '%s' has disconnected port CLK\n", ci->name.c_str(ctx));
                for (auto &eclk : eclks) {
                    if (eclk.second.unbuf == clk) {
                        for (auto bel : ctx->getBels()) {
                            if (ctx->getBelType(bel) != id_DDRDLL)
                                continue;
                            Loc loc = ctx->getBelLocation(bel);
                            int ddrdll_bank = -1;
                            if (loc.x < 15 && loc.y < 15)
                                ddrdll_bank = 7;
                            else if (loc.x < 15 && loc.y > 15)
                                ddrdll_bank = 6;
                            else if (loc.x > 15 && loc.y < 15)
                                ddrdll_bank = 2;
                            else if (loc.x > 15 && loc.y > 15)
                                ddrdll_bank = 3;
                            if (eclk.first.first != ddrdll_bank)
                                continue;
                            ci->attrs[ctx->id("BEL")] = ctx->getBelName(bel).str(ctx);
                            make_eclk(ci->ports.at(id_CLK), ci, bel, eclk.first.first);
                            goto ddrdll_done;
                        }
                    }
                }
            ddrdll_done:
                continue;
            }
        }

        flush_cells();
    };

    void generate_constraints()
    {
        log_info("Generating derived timing constraints...\n");
        auto MHz = [&](delay_t a) { return 1000.0 / ctx->getDelayNS(a); };

        auto equals_epsilon = [](delay_t a, delay_t b) { return (std::abs(a - b) / std::max(double(b), 1.0)) < 1e-3; };

        std::unordered_set<IdString> user_constrained, changed_nets;
        for (auto &net : ctx->nets) {
            if (net.second->clkconstr != nullptr)
                user_constrained.insert(net.first);
            changed_nets.insert(net.first);
        }
        auto get_period = [&](CellInfo *ci, IdString port, delay_t &period) {
            if (!ci->ports.count(port))
                return false;
            NetInfo *from = ci->ports.at(port).net;
            if (from == nullptr || from->clkconstr == nullptr)
                return false;
            period = from->clkconstr->period.min_delay;
            return true;
        };

        auto set_period = [&](CellInfo *ci, IdString port, delay_t period) {
            if (!ci->ports.count(port))
                return;
            NetInfo *to = ci->ports.at(port).net;
            if (to == nullptr)
                return;
            if (to->clkconstr != nullptr) {
                if (!equals_epsilon(to->clkconstr->period.min_delay, period) && user_constrained.count(to->name))
                    log_warning(
                            "    Overriding derived constraint of %.1f MHz on net %s with user-specified constraint of "
                            "%.1f MHz.\n",
                            MHz(to->clkconstr->period.min_delay), to->name.c_str(ctx), MHz(period));
                return;
            }
            to->clkconstr = std::unique_ptr<ClockConstraint>(new ClockConstraint());
            to->clkconstr->low.min_delay = period / 2;
            to->clkconstr->low.max_delay = period / 2;
            to->clkconstr->high.min_delay = period / 2;
            to->clkconstr->high.max_delay = period / 2;
            to->clkconstr->period.min_delay = period;
            to->clkconstr->period.max_delay = period;
            log_info("    Derived frequency constraint of %.1f MHz for net %s\n", MHz(to->clkconstr->period.min_delay),
                     to->name.c_str(ctx));
            changed_nets.insert(to->name);
        };

        auto copy_constraint = [&](CellInfo *ci, IdString fromPort, IdString toPort, double ratio = 1.0) {
            if (!ci->ports.count(fromPort) || !ci->ports.count(toPort))
                return;
            NetInfo *from = ci->ports.at(fromPort).net, *to = ci->ports.at(toPort).net;
            if (from == nullptr || from->clkconstr == nullptr || to == nullptr)
                return;
            if (to->clkconstr != nullptr) {
                if (!equals_epsilon(to->clkconstr->period.min_delay,
                                    delay_t(from->clkconstr->period.min_delay / ratio)) &&
                    user_constrained.count(to->name))
                    log_warning(
                            "    Overriding derived constraint of %.1f MHz on net %s with user-specified constraint of "
                            "%.1f MHz.\n",
                            MHz(to->clkconstr->period.min_delay), to->name.c_str(ctx),
                            MHz(delay_t(from->clkconstr->period.min_delay / ratio)));
                return;
            }
            to->clkconstr = std::unique_ptr<ClockConstraint>(new ClockConstraint());
            to->clkconstr->low = ctx->getDelayFromNS(ctx->getDelayNS(from->clkconstr->low.min_delay) / ratio);
            to->clkconstr->high = ctx->getDelayFromNS(ctx->getDelayNS(from->clkconstr->high.min_delay) / ratio);
            to->clkconstr->period = ctx->getDelayFromNS(ctx->getDelayNS(from->clkconstr->period.min_delay) / ratio);
            log_info("    Derived frequency constraint of %.1f MHz for net %s\n", MHz(to->clkconstr->period.min_delay),
                     to->name.c_str(ctx));
            changed_nets.insert(to->name);
        };

        // Run in a loop while constraints are changing to deal with dependencies
        // Iteration limit avoids hanging in crazy loopback situation (self-fed PLLs or dividers, etc)
        int iter = 0;
        const int itermax = 5000;
        while (!changed_nets.empty() && iter < itermax) {
            ++iter;
            std::unordered_set<IdString> changed_cells;
            for (auto net : changed_nets)
                for (auto &user : ctx->nets.at(net)->users)
                    if (user.port == id_CLKI || user.port == id_ECLKI)
                        changed_cells.insert(user.cell->name);
            changed_nets.clear();
            for (auto cell : sorted(changed_cells)) {
                CellInfo *ci = ctx->cells.at(cell).get();
                if (ci->type == id_CLKDIVF) {
                    std::string div = str_or_default(ci->params, ctx->id("DIV"), "2.0");
                    double ratio;
                    if (div == "2.0")
                        ratio = 1 / 2.0;
                    else if (div == "3.5")
                        ratio = 1 / 3.5;
                    else
                        log_error("Unsupported divider ratio '%s' on CLKDIVF '%s'\n", div.c_str(), ci->name.c_str(ctx));
                    copy_constraint(ci, id_CLKI, id_CDIVX, ratio);
                } else if (ci->type == id_ECLKSYNCB || ci->type == id_TRELLIS_ECLKBUF) {
                    copy_constraint(ci, id_ECLKI, id_ECLKO, 1);
                } else if (ci->type == id_EHXPLLL) {
                    delay_t period_in;
                    if (!get_period(ci, id_CLKI, period_in))
                        continue;
                    log_info("    Input frequency of PLL '%s' is constrained to %.1f MHz\n", ci->name.c_str(ctx),
                             MHz(period_in));
                    double period_in_div = period_in * int_or_default(ci->params, ctx->id("CLKI_DIV"), 1);
                    std::string path = str_or_default(ci->params, ctx->id("FEEDBK_PATH"), "CLKOP");
                    int feedback_div = int_or_default(ci->params, ctx->id("CLKFB_DIV"), 1);
                    if (path == "CLKOP" || path == "INT_OP")
                        feedback_div *= int_or_default(ci->params, ctx->id("CLKOP_DIV"), 1);
                    else if (path == "CLKOS" || path == "INT_OS")
                        feedback_div *= int_or_default(ci->params, ctx->id("CLKOS_DIV"), 1);
                    else if (path == "CLKOS2" || path == "INT_OS2")
                        feedback_div *= int_or_default(ci->params, ctx->id("CLKOS2_DIV"), 1);
                    else if (path == "CLKOS3" || path == "INT_OS3")
                        feedback_div *= int_or_default(ci->params, ctx->id("CLKOS3_DIV"), 1);
                    else {
                        log_info("     Unable to determine output frequencies for PLL '%s' with FEEDBK_PATH=%s\n",
                                 ci->name.c_str(ctx), path.c_str());
                        continue;
                    }
                    double vco_period = period_in_div / feedback_div;
                    double vco_freq = MHz(vco_period);
                    if (vco_freq < 400 || vco_freq > 800)
                        log_info("    Derived VCO frequency %.1f MHz of PLL '%s' is out of legal range [400MHz, "
                                 "800MHz]\n",
                                 vco_freq, ci->name.c_str(ctx));
                    set_period(ci, id_CLKOP, vco_period * int_or_default(ci->params, ctx->id("CLKOP_DIV"), 1));
                    set_period(ci, id_CLKOS, vco_period * int_or_default(ci->params, ctx->id("CLKOS_DIV"), 1));
                    set_period(ci, id_CLKOS2, vco_period * int_or_default(ci->params, ctx->id("CLKOS2_DIV"), 1));
                    set_period(ci, id_CLKOS3, vco_period * int_or_default(ci->params, ctx->id("CLKOS3_DIV"), 1));
                } else if (ci->type == id_OSCG) {
                    int div = int_or_default(ci->params, ctx->id("DIV"), 128);
                    set_period(ci, id_OSC, delay_t((1.0e6 / (2.0 * 155)) * div));
                }
            }
        }
    }

  public:
    void pack()
    {
        pack_io();
        pack_dqsbuf();
        pack_iologic();
        pack_ebr();
        pack_dsps();
        pack_dcus();
        preplace_plls();
        pack_constants();
        pack_dram();
        pack_carries();
        find_lutff_pairs();
        pack_lut5xs();
        pair_luts();
        pack_lut_pairs();
        pack_remaining_luts();
        pack_remaining_ffs();
        generate_constraints();
        promote_ecp5_globals(ctx);
        ctx->check();
    }

  private:
    Context *ctx;

    std::unordered_set<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;

    struct SliceUsage
    {
        bool lut0_used = false, lut1_used = false;
        bool ccu2_used = false, dpram_used = false, ramw_used = false;
        bool ff0_used = false, ff1_used = false;
        bool mux5_used = false, muxx_used = false;
    };

    std::unordered_map<IdString, SliceUsage> sliceUsage;
    std::unordered_map<IdString, IdString> lutffPairs;
    std::unordered_map<IdString, IdString> fflutPairs;
    std::unordered_map<IdString, IdString> lutPairs;
};
// Main pack function
bool Arch::pack()
{
    Context *ctx = getCtx();
    try {
        log_break();
        Ecp5Packer(ctx).pack();
        log_info("Checksum: 0x%08x\n", ctx->checksum());
        assignArchInfo();
        return true;
    } catch (log_execution_error_exception) {
        assignArchInfo();
        return false;
    }
}

void Arch::assignArchInfo()
{
    for (auto cell : sorted(cells)) {
        CellInfo *ci = cell.second;
        if (ci->type == id_TRELLIS_SLICE) {

            ci->sliceInfo.using_dff = false;
            if (ci->ports.count(id_Q0) && ci->ports[id_Q0].net != nullptr)
                ci->sliceInfo.using_dff = true;
            if (ci->ports.count(id_Q1) && ci->ports[id_Q1].net != nullptr)
                ci->sliceInfo.using_dff = true;

            if (ci->ports.count(id_CLK) && ci->ports[id_CLK].net != nullptr)
                ci->sliceInfo.clk_sig = ci->ports[id_CLK].net->name;
            else
                ci->sliceInfo.clk_sig = IdString();

            if (ci->ports.count(id_LSR) && ci->ports[id_LSR].net != nullptr)
                ci->sliceInfo.lsr_sig = ci->ports[id_LSR].net->name;
            else
                ci->sliceInfo.lsr_sig = IdString();

            ci->sliceInfo.clkmux = id(str_or_default(ci->params, id_CLKMUX, "CLK"));
            ci->sliceInfo.lsrmux = id(str_or_default(ci->params, id_LSRMUX, "LSR"));
            ci->sliceInfo.srmode = id(str_or_default(ci->params, id_SRMODE, "LSR_OVER_CE"));
            ci->sliceInfo.is_carry = str_or_default(ci->params, id("MODE"), "LOGIC") == "CCU2";
            ci->sliceInfo.sd0 = int_or_default(ci->params, id("REG0_SD"), 0);
            ci->sliceInfo.sd1 = int_or_default(ci->params, id("REG1_SD"), 0);
            ci->sliceInfo.has_l6mux = false;
            if (ci->ports.count(id_FXA) && ci->ports[id_FXA].net != nullptr &&
                ci->ports[id_FXA].net->driver.port == id_OFX0)
                ci->sliceInfo.has_l6mux = true;
        }
    }
}

NEXTPNR_NAMESPACE_END
