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
#include <unordered_set>
#include "cells.h"
#include "chain_utils.h"
#include "design_utils.h"
#include "globals.h"
#include "log.h"
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
                if (false) {
                paired_inlut:
                    continue;
                }
            }
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
                lut_to_slice(ctx, ci, slice.get(), 0);
                auto ff = lutffPairs.find(ci->name);

                if (ff != lutffPairs.end()) {
                    ff_to_slice(ctx, ctx->cells.at(ff->second).get(), slice.get(), 0, true);
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
                } else {
                    connect_port(ctx, lsr, iol, id_LSR);
                }
            }
            if (prim->ports.count(port))
                disconnect_port(ctx, prim, port);
        };

        auto set_iologic_mode = [&](CellInfo *iol, std::string mode) {
            auto &curr_mode = iol->params[ctx->id("MODE")];
            if (curr_mode != "NONE" && curr_mode != mode)
                log_error("IOLOGIC '%s' has conflicting modes '%s' and '%s'\n", iol->name.c_str(ctx), curr_mode.c_str(),
                          mode.c_str());
            curr_mode = mode;
        };

        auto create_pio_iologic = [&](CellInfo *pio, CellInfo *curr) {
            if (!pio->attrs.count(ctx->id("BEL")))
                log_error("IOLOGIC functionality (DDR, DELAY, DQS, etc) can only be used with pin-constrained PIO "
                          "(while processing '%s').\n",
                          curr->name.c_str(ctx));
            BelId bel = ctx->getBelByName(ctx->id(pio->attrs.at(ctx->id("BEL"))));
            NPNR_ASSERT(bel != BelId());
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
            }
        }
        flush_cells();
    };

  public:
    void pack()
    {
        pack_io();
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
