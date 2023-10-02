/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
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
#include "cells.h"
#include "chain_utils.h"
#include "design_utils.h"
#include "log.h"
#include "timing.h"
#include "util.h"
NEXTPNR_NAMESPACE_BEGIN

static bool is_nextpnr_iob(Context *ctx, CellInfo *cell)
{
    return cell->type == ctx->id("$nextpnr_ibuf") || cell->type == ctx->id("$nextpnr_obuf") ||
           cell->type == ctx->id("$nextpnr_iobuf");
}

class MachXO2Packer
{
  public:
    MachXO2Packer(Context *ctx) : ctx(ctx){};

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

    // Print logic usage
    void print_logic_usage()
    {
        int total_luts = 0, total_ffs = 0;
        int total_ramluts = 0, total_ramwluts = 0;
        for (auto bel : ctx->getBels()) {
            if (ctx->getBelType(bel) == id_TRELLIS_COMB) {
                total_luts += 1;
                Loc l = ctx->getBelLocation(bel);
                if (l.z <= 3)
                    total_ramluts += 1;
            }
            if (ctx->getBelType(bel) == id_TRELLIS_FF)
                total_ffs += 1;
            if (ctx->getBelType(bel) == id_TRELLIS_RAMW)
                total_ramwluts += 2;
        }
        int used_lgluts = 0, used_cyluts = 0, used_ramluts = 0, used_ramwluts = 0, used_ffs = 0;
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (is_lut(ctx, ci))
                ++used_lgluts;
            if (is_carry(ctx, ci))
                used_cyluts += 2;
            if (is_dpram(ctx, ci)) {
                used_ramluts += 4;
                used_ramwluts += 2;
            }
            if (is_ff(ctx, ci))
                ++used_ffs;
        }
        log_info("Logic utilisation before packing:\n");
        auto pc = [](int used, int total) { return 100 * used / total; };
        int used_luts = used_lgluts + used_cyluts + used_ramluts + used_ramwluts;
        log_info("    Total LUT4s:     %5d/%5d %5d%%\n", used_luts, total_luts, pc(used_luts, total_luts));
        log_info("        logic LUTs:  %5d/%5d %5d%%\n", used_lgluts, total_luts, pc(used_lgluts, total_luts));
        log_info("        carry LUTs:  %5d/%5d %5d%%\n", used_cyluts, total_luts, pc(used_cyluts, total_luts));
        log_info("          RAM LUTs:  %5d/%5d %5d%%\n", used_ramluts, total_ramluts, pc(used_ramluts, total_ramluts));
        log_info("         RAMW LUTs:  %5d/%5d %5d%%\n", used_ramwluts, total_ramwluts,
                 pc(used_ramwluts, total_ramwluts));
        log_break();
        log_info("     Total DFFs:     %5d/%5d %5d%%\n", used_ffs, total_ffs, pc(used_ffs, total_ffs));
        log_break();
    }

    // Pack LUTs
    void pack_luts()
    {
        log_info("Packing LUTs...\n");
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (is_lut(ctx, ci))
                lut_to_comb(ctx, ci);
        }
    }

    // Gets the z-position of a cell in a macro
    int get_macro_cell_z(const CellInfo *ci)
    {
        if (ci->constr_abs_z)
            return ci->constr_z;
        else if (ci->cluster != ClusterId() && ctx->getClusterRootCell(ci->cluster) != ci)
            return ci->constr_z + get_macro_cell_z(ctx->getClusterRootCell(ci->cluster));
        else
            return 0;
    }

    // Gets the relative xy-position of a cell in a macro
    std::pair<int, int> get_macro_cell_xy(const CellInfo *ci)
    {
        if (ci->cluster != ClusterId())
            return {ci->constr_x, ci->constr_y};
        else
            return {0, 0};
    }

    // Relatively constrain one cell to another
    void rel_constr_cells(CellInfo *a, CellInfo *b, int dz)
    {
        if (a->cluster != ClusterId() && ctx->getClusterRootCell(a->cluster) != a) {
            NPNR_ASSERT(b->cluster == ClusterId());
            NPNR_ASSERT(b->constr_children.empty());
            CellInfo *root = ctx->getClusterRootCell(a->cluster);
            root->constr_children.push_back(b);
            b->cluster = root->cluster;
            b->constr_x = a->constr_x;
            b->constr_y = a->constr_y;
            b->constr_z = get_macro_cell_z(a) + dz;
            b->constr_abs_z = a->constr_abs_z;
        } else if (b->cluster != ClusterId() && ctx->getClusterRootCell(b->cluster) != b) {
            NPNR_ASSERT(a->constr_children.empty());
            CellInfo *root = ctx->getClusterRootCell(b->cluster);
            root->constr_children.push_back(a);
            a->cluster = root->cluster;
            a->constr_x = b->constr_x;
            a->constr_y = b->constr_y;
            a->constr_z = get_macro_cell_z(b) - dz;
            a->constr_abs_z = b->constr_abs_z;
        } else if (!b->constr_children.empty()) {
            NPNR_ASSERT(a->constr_children.empty());
            b->constr_children.push_back(a);
            a->cluster = b->cluster;
            a->constr_x = 0;
            a->constr_y = 0;
            a->constr_z = get_macro_cell_z(b) - dz;
            a->constr_abs_z = b->constr_abs_z;
        } else {
            NPNR_ASSERT(a->cluster == ClusterId() || ctx->getClusterRootCell(a->cluster) == a);
            a->constr_children.push_back(b);
            a->cluster = a->name;
            b->cluster = a->name;
            b->constr_x = 0;
            b->constr_y = 0;
            b->constr_z = get_macro_cell_z(a) + dz;
            b->constr_abs_z = a->constr_abs_z;
        }
    }

    // Check if it is legal to add a FF to a macro
    // This reuses the tile validity code
    bool can_add_flipflop_to_macro(CellInfo *comb, CellInfo *ff)
    {
        Arch::LogicTileStatus lts;
        std::fill(lts.cells.begin(), lts.cells.end(), nullptr);
        lts.tile_dirty = true;
        for (auto &sl : lts.slices)
            sl.dirty = true;

        auto process_cell = [&](CellInfo *ci) {
            if (get_macro_cell_xy(ci) != get_macro_cell_xy(comb))
                return;
            int z = get_macro_cell_z(ci);
            auto &slot = lts.cells.at(z);
            NPNR_ASSERT(slot == nullptr);
            slot = ci;
            // Make sure fields needed for validity checking are set correctly
            ctx->assign_arch_info_for_cell(ci);
        };

        if (comb->cluster != ClusterId()) {
            CellInfo *root = ctx->getClusterRootCell(comb->cluster);
            process_cell(root);
            for (auto &ch : root->constr_children)
                process_cell(ch);
        } else {
            process_cell(comb);
            for (auto &ch : comb->constr_children)
                process_cell(ch);
        }
        int ff_z = get_macro_cell_z(comb) + (Arch::BEL_FF - Arch::BEL_COMB);
        if (lts.cells.at(ff_z) != nullptr)
            return false;
        ctx->assign_arch_info_for_cell(ff);
        lts.cells.at(ff_z) = ff;
        return ctx->slices_compatible(&lts);
    }

    void pack_ffs()
    {
        log_info("Packing FFs...\n");
        int pairs = 0;
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (is_ff(ctx, ci)) {
                NetInfo *di = ci->getPort(id_DI);
                if (di->driver.cell != nullptr && di->driver.cell->type == id_TRELLIS_COMB && di->driver.port == id_F) {
                    CellInfo *comb = di->driver.cell;
                    if (comb->cluster != ClusterId()) {
                        // Special procedure where the comb cell is part of an existing macro
                        // Need to make sure that CLK, CE, SR, etc are shared correctly, or
                        // the design will not be routeable
                        if (can_add_flipflop_to_macro(comb, ci)) {
                            ci->params[id_SD] = std::string("1");
                            rel_constr_cells(comb, ci, (Arch::BEL_FF - Arch::BEL_COMB));
                            // Packed successfully
                            ++pairs;
                            continue;
                        }
                    } else {
                        // LUT/COMB is not part of a macro, this is the easy case
                        // Constrain FF and LUT together, no need to rewire
                        ci->params[id_SD] = std::string("1");
                        comb->constr_children.push_back(ci);
                        ci->cluster = comb->name;
                        comb->cluster = comb->name;
                        ci->constr_x = 0;
                        ci->constr_y = 0;
                        ci->constr_z = (Arch::BEL_FF - Arch::BEL_COMB);
                        ci->constr_abs_z = false;
                        // Packed successfully
                        ++pairs;
                        continue;
                    }
                }
                {
                    // Didn't manage to pack it with a driving combinational cell
                    // Rewire to use general routing
                    ci->params[id_SD] = std::string("0");
                    ci->renamePort(id_DI, id_M);
                }
            }
        }
        log_info("    %d FFs paired with LUTs.\n", pairs);
    }

    // Return true if an port is a top level port that provides its own IOBUF
    bool is_top_port(PortRef &port) { return false; }

    // Return true if a net only drives a top port
    bool drives_top_port(NetInfo *net, PortRef &tp)
    {
        if (net == nullptr)
            return false;
        for (auto user : net->users) {
            if (is_top_port(user)) {
                if (net->users.entries() > 1)
                    log_error("   port %s.%s must be connected to (and only to) a top level pin\n",
                              user.cell->name.c_str(ctx), user.port.c_str(ctx));
                tp = user;
                return true;
            }
        }
        if (net->driver.cell != nullptr && is_top_port(net->driver)) {
            if (net->users.entries() > 1)
                log_error("   port %s.%s must be connected to (and only to) a top level pin\n",
                          net->driver.cell->name.c_str(ctx), net->driver.port.c_str(ctx));
            tp = net->driver;
            return true;
        }
        return false;
    }

    // Pass to pack LUT5s into a newly created slice
    void pack_lut5xs()
    {
        log_info("Packing LUT5-7s...\n");

        // Gets the "COMB1" side of a LUT5, where we pack a LUT[67] into
        auto get_comb1_from_lut5 = [&](CellInfo *lut5) {
            NetInfo *f1 = lut5->getPort(id_F1);
            NPNR_ASSERT(f1 != nullptr);
            NPNR_ASSERT(f1->driver.cell != nullptr);
            return f1->driver.cell;
        };

        dict<IdString, std::pair<CellInfo *, CellInfo *>> lut5_roots, lut6_roots, lut7_roots;
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (is_pfumx(ctx, ci)) {
                NetInfo *f0 = ci->ports.at(id_BLUT).net;

                if (f0 == nullptr)
                    log_error("PFUMX '%s' has disconnected port 'BLUT'\n", ci->name.c_str(ctx));
                NetInfo *f1 = ci->ports.at(id_ALUT).net;
                if (f1 == nullptr)
                    log_error("PFUMX '%s' has disconnected port 'ALUT'\n", ci->name.c_str(ctx));

                CellInfo *lut0 =
                        (f0->driver.cell && f0->driver.cell->type == id_TRELLIS_COMB && f0->driver.port == id_F)
                                ? f0->driver.cell
                                : nullptr;
                CellInfo *lut1 =
                        (f1->driver.cell && f1->driver.cell->type == id_TRELLIS_COMB && f1->driver.port == id_F)
                                ? f1->driver.cell
                                : nullptr;
                if (lut0 == nullptr || lut0->cluster != ClusterId())
                    log_error("PFUMX '%s' has BLUT driven by cell other than a LUT\n", ci->name.c_str(ctx));
                if (lut1 == nullptr || lut1->cluster != ClusterId())
                    log_error("PFUMX '%s' has ALUT driven by cell other than a LUT\n", ci->name.c_str(ctx));
                lut0->addInput(id_F1);
                lut0->addInput(id_M);
                lut0->addOutput(id_OFX);

                ci->movePortTo(id_Z, lut0, id_OFX);
                ci->movePortTo(id_ALUT, lut0, id_F1);
                ci->movePortTo(id_C0, lut0, id_M);
                ci->disconnectPort(id_BLUT);

                lut5_roots[lut0->name] = {lut0, lut1};
                packed_cells.insert(ci->name);
            }
        }
        flush_cells();
        // Pack LUT6s
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (is_l6mux(ctx, ci)) {
                NetInfo *ofx0_0 = ci->ports.at(id_D0).net;
                if (ofx0_0 == nullptr)
                    log_error("L6MUX21 '%s' has disconnected port 'D0'\n", ci->name.c_str(ctx));
                NetInfo *ofx0_1 = ci->ports.at(id_D1).net;
                if (ofx0_1 == nullptr)
                    log_error("L6MUX21 '%s' has disconnected port 'D1'\n", ci->name.c_str(ctx));
                CellInfo *comb0 = (ofx0_0->driver.cell && ofx0_0->driver.cell->type == id_TRELLIS_COMB &&
                                   ofx0_0->driver.port == id_OFX)
                                          ? ofx0_0->driver.cell
                                          : nullptr;
                CellInfo *comb1 = (ofx0_1->driver.cell && ofx0_1->driver.cell->type == id_TRELLIS_COMB &&
                                   ofx0_1->driver.port == id_OFX)
                                          ? ofx0_1->driver.cell
                                          : nullptr;
                if (comb0 == nullptr) {
                    if (!net_driven_by(ctx, ofx0_0, is_l6mux, id_Z))
                        log_error("L6MUX21 '%s' has D0 driven by cell other than a SLICE OFX0 but not a LUT7 mux "
                                  "('%s.%s')\n",
                                  ci->name.c_str(ctx), ofx0_0->driver.cell->name.c_str(ctx),
                                  ofx0_0->driver.port.c_str(ctx));
                    continue;
                }
                if (lut6_roots.count(comb0->name))
                    continue;

                if (comb1 == nullptr) {
                    if (!net_driven_by(ctx, ofx0_1, is_l6mux, id_Z))
                        log_error("L6MUX21 '%s' has D1 driven by cell other than a SLICE OFX0 but not a LUT7 mux "
                                  "('%s.%s')\n",
                                  ci->name.c_str(ctx), ofx0_0->driver.cell->name.c_str(ctx),
                                  ofx0_0->driver.port.c_str(ctx));
                    continue;
                }
                if (lut6_roots.count(comb1->name))
                    continue;
                if (ctx->verbose)
                    log_info("   mux '%s' forms part of a LUT6\n", cell.first.c_str(ctx));
                comb0 = get_comb1_from_lut5(comb0);
                comb1 = get_comb1_from_lut5(comb1);

                comb1->addInput(id_FXA);
                comb1->addInput(id_FXB);
                comb1->addInput(id_M);
                comb1->addOutput(id_OFX);
                ci->movePortTo(id_D0, comb1, id_FXA);
                ci->movePortTo(id_D1, comb1, id_FXB);
                ci->movePortTo(id_SD, comb1, id_M);
                ci->movePortTo(id_Z, comb1, id_OFX);
                lut6_roots[comb1->name] = {comb0, comb1};
                packed_cells.insert(ci->name);
            }
        }
        flush_cells();
        // Pack LUT7s
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (is_l6mux(ctx, ci)) {
                NetInfo *ofx1_0 = ci->ports.at(id_D0).net;
                if (ofx1_0 == nullptr)
                    log_error("L6MUX21 '%s' has disconnected port 'D0'\n", ci->name.c_str(ctx));
                NetInfo *ofx1_1 = ci->ports.at(id_D1).net;
                if (ofx1_1 == nullptr)
                    log_error("L6MUX21 '%s' has disconnected port 'D1'\n", ci->name.c_str(ctx));
                CellInfo *comb1 = (ofx1_0->driver.cell && ofx1_0->driver.cell->type == id_TRELLIS_COMB &&
                                   ofx1_0->driver.port == id_OFX)
                                          ? ofx1_0->driver.cell
                                          : nullptr;
                CellInfo *comb3 = (ofx1_1->driver.cell && ofx1_1->driver.cell->type == id_TRELLIS_COMB &&
                                   ofx1_1->driver.port == id_OFX)
                                          ? ofx1_1->driver.cell
                                          : nullptr;
                if (comb1 == nullptr)
                    log_error("L6MUX21 '%s' has D0 driven by cell other than a SLICE OFX ('%s.%s')\n",
                              ci->name.c_str(ctx), ofx1_0->driver.cell->name.c_str(ctx),
                              ofx1_0->driver.port.c_str(ctx));
                if (comb3 == nullptr)
                    log_error("L6MUX21 '%s' has D1 driven by cell other than a SLICE OFX ('%s.%s')\n",
                              ci->name.c_str(ctx), ofx1_1->driver.cell->name.c_str(ctx),
                              ofx1_1->driver.port.c_str(ctx));

                NetInfo *fxa_0 = comb1->ports.at(id_FXA).net;
                if (fxa_0 == nullptr)
                    log_error("SLICE '%s' has disconnected port 'FXA'\n", comb1->name.c_str(ctx));
                NetInfo *fxa_1 = comb3->ports.at(id_FXA).net;
                if (fxa_1 == nullptr)
                    log_error("SLICE '%s' has disconnected port 'FXA'\n", comb3->name.c_str(ctx));

                CellInfo *comb2 = net_driven_by(
                        ctx, fxa_1,
                        [](const Context *ctx, const CellInfo *ci) {
                            (void)ctx;
                            return ci->type == id_TRELLIS_COMB;
                        },
                        id_OFX);
                if (comb2 == nullptr)
                    log_error("SLICE '%s' has FXA driven by cell other than a SLICE OFX0 ('%s.%s')\n",
                              comb3->name.c_str(ctx), fxa_1->driver.cell->name.c_str(ctx),
                              fxa_1->driver.port.c_str(ctx));
                comb2 = get_comb1_from_lut5(comb2);
                comb2->addInput(id_FXA);
                comb2->addInput(id_FXB);
                comb2->addInput(id_M);
                comb2->addOutput(id_OFX);
                ci->movePortTo(id_D0, comb2, id_FXA);
                ci->movePortTo(id_D1, comb2, id_FXB);
                ci->movePortTo(id_SD, comb2, id_M);
                ci->movePortTo(id_Z, comb2, id_OFX);

                lut7_roots[comb2->name] = {comb1, comb3};
                packed_cells.insert(ci->name);
            }
        }

        for (auto &root : lut7_roots) {
            auto &cells = root.second;
            cells.second->cluster = cells.second->name;
            cells.second->constr_abs_z = true;
            cells.second->constr_z = (1 << Arch::lc_idx_shift) | Arch::BEL_COMB;
            rel_constr_cells(cells.second, cells.first, (4 << Arch::lc_idx_shift));
        }
        for (auto &root : lut6_roots) {
            auto &cells = root.second;
            rel_constr_cells(cells.second, cells.first, (2 << Arch::lc_idx_shift));
        }
        for (auto &root : lut5_roots) {
            auto &cells = root.second;
            rel_constr_cells(cells.first, cells.second, (1 << Arch::lc_idx_shift));
        }
        flush_cells();
    }

    // Simple "packer" to remove nextpnr IOBUFs, this assumes IOBUFs are manually instantiated
    void pack_io()
    {
        log_info("Packing IOs..\n");

        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (is_nextpnr_iob(ctx, ci)) {
                CellInfo *trio = nullptr;
                NetInfo *ionet = nullptr;
                PortRef tp;
                if (ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_iobuf")) {
                    ionet = ci->ports.at(id_O).net;
                    trio = net_only_drives(ctx, ionet, is_trellis_io, id_B, true, ci);

                } else if (ci->type == ctx->id("$nextpnr_obuf")) {
                    ionet = ci->ports.at(id_I).net;
                    trio = net_only_drives(ctx, ci->ports.at(id_I).net, is_trellis_io, id_B, true, ci);
                }
                if (bool_or_default(ctx->settings, ctx->id("arch.ooc"))) {
                    // No IO buffer insertion in out-of-context mode, just remove the nextpnr buffer
                    // and leave the top level port
                    for (auto &port : ci->ports)
                        ci->disconnectPort(port.first);
                } else if (trio != nullptr) {
                    // Trivial case, TRELLIS_IO used. Just remove the IOBUF
                    log_info("%s feeds TRELLIS_IO %s, removing %s %s.\n", ci->name.c_str(ctx), trio->name.c_str(ctx),
                             ci->type.c_str(ctx), ci->name.c_str(ctx));

                    NetInfo *net = trio->ports.at(id_B).net;
                    if (((ci->type == ctx->id("$nextpnr_ibuf") || ci->type == ctx->id("$nextpnr_iobuf")) &&
                         net->users.entries() > 1) ||
                        (ci->type == ctx->id("$nextpnr_obuf") &&
                         (net->users.entries() > 2 || net->driver.cell != nullptr)) ||
                        (ci->type == ctx->id("$nextpnr_iobuf") && ci->ports.at(id_I).net != nullptr &&
                         ci->ports.at(id_I).net->driver.cell != nullptr))
                        log_error("Pin B of %s '%s' connected to more than a single top level IO.\n",
                                  trio->type.c_str(ctx), trio->name.c_str(ctx));
                    if (net != nullptr) {
                        if (net->clkconstr != nullptr && trio->ports.count(id_O)) {
                            NetInfo *onet = trio->ports.at(id_O).net;
                            if (onet != nullptr && !onet->clkconstr) {
                                // Move clock constraint from IO pad to input buffer output
                                std::swap(net->clkconstr, onet->clkconstr);
                            }
                        }
                    }
                } else if (drives_top_port(ionet, tp)) {
                    log_info("%s feeds %s %s.%s, removing %s %s.\n", ci->name.c_str(ctx), tp.cell->type.c_str(ctx),
                             tp.cell->name.c_str(ctx), tp.port.c_str(ctx), ci->type.c_str(ctx), ci->name.c_str(ctx));
                    if (ionet != nullptr) {
                        ctx->nets.erase(ionet->name);
                        tp.cell->ports.at(tp.port).net = nullptr;
                        ci->ports.at(ci->type == ctx->id("$nextpnr_obuf") ? id_I : id_O).net = nullptr;
                    }
                    if (ci->type == ctx->id("$nextpnr_iobuf")) {
                        NetInfo *net2 = ci->ports.at(id_I).net;
                        if (net2 != nullptr) {
                            ctx->nets.erase(net2->name);
                            ci->ports.at(id_I).net = nullptr;
                        }
                    }
                } else {
                    // Create a TRELLIS_IO buffer
                    std::unique_ptr<CellInfo> tr_cell =
                            create_machxo2_cell(ctx, id_TRELLIS_IO, ci->name.str(ctx) + "$tr_io");
                    nxio_to_tr(ctx, ci, tr_cell.get(), new_cells, packed_cells);
                    new_cells.push_back(std::move(tr_cell));
                    trio = new_cells.back().get();
                }
                for (auto port : ci->ports)
                    ci->disconnectPort(port.first);
                packed_cells.insert(ci->name);
                if (trio != nullptr) {
                    for (const auto &attr : ci->attrs)
                        trio->attrs[attr.first] = attr.second;

                    auto loc_attr = trio->attrs.find(id_LOC);
                    if (loc_attr != trio->attrs.end()) {
                        std::string pin = loc_attr->second.as_string();
                        BelId pinBel = ctx->get_package_pin_bel(pin);
                        if (pinBel == BelId()) {
                            log_error("IO pin '%s' constrained to pin '%s', which does not exist for package '%s'.\n",
                                      trio->name.c_str(ctx), pin.c_str(), ctx->package_name);
                        } else {
                            log_info("pin '%s' constrained to Bel '%s'.\n", trio->name.c_str(ctx),
                                     ctx->nameOfBel(pinBel));
                        }
                        trio->attrs[id_BEL] = ctx->getBelName(pinBel).str(ctx);
                    }
                }
            }
        }
        flush_cells();
    }

    // Create a feed in to the carry chain
    CellInfo *make_carry_feed_in(NetInfo *carry, PortRef chain_in)
    {
        std::unique_ptr<CellInfo> feedin = create_machxo2_cell(ctx, id_CCU2D);

        feedin->params[id_INIT0] = Property(20480, 16);
        feedin->params[id_INIT1] = Property(65535, 16);
        feedin->params[id_INJECT1_0] = std::string("NO");
        feedin->params[id_INJECT1_1] = std::string("YES");

        carry->users.remove(chain_in.cell->ports.at(chain_in.port).user_idx);
        feedin->connectPort(id_A0, carry);

        NetInfo *new_carry = ctx->createNet(ctx->id(feedin->name.str(ctx) + "$COUT"));
        feedin->connectPort(id_COUT, new_carry);
        chain_in.cell->ports[chain_in.port].net = nullptr;
        chain_in.cell->ports[chain_in.port].user_idx = {};

        chain_in.cell->connectPort(chain_in.port, new_carry);

        CellInfo *feedin_ptr = feedin.get();
        IdString feedin_name = feedin->name;
        ctx->cells[feedin_name] = std::move(feedin);
        return feedin_ptr;
    }

    // Create a feed out and loop through from the carry chain
    CellInfo *make_carry_feed_out(NetInfo *carry, boost::optional<PortRef> chain_next = boost::optional<PortRef>())
    {
        std::unique_ptr<CellInfo> feedout = create_machxo2_cell(ctx, id_CCU2D);

        feedout->params[id_INIT0] = Property(0, 16);
        feedout->params[id_INIT1] = Property(20480, 16);
        feedout->params[id_INJECT1_0] = std::string("NO");
        feedout->params[id_INJECT1_1] = std::string("NO");

        PortRef carry_drv = carry->driver;
        carry->driver.cell = nullptr;
        feedout->connectPort(id_S0, carry);

        NetInfo *new_cin = ctx->createNet(ctx->id(feedout->name.str(ctx) + "$CIN"));
        new_cin->driver = carry_drv;
        carry_drv.cell->ports.at(carry_drv.port).net = new_cin;
        feedout->connectPort(id_CIN, new_cin);

        if (chain_next) {
            // Loop back into LUT4_1 for feedthrough
            feedout->connectPort(id_A1, carry);
            if (chain_next->cell && chain_next->cell->ports.at(chain_next->port).user_idx)
                carry->users.remove(chain_next->cell->ports.at(chain_next->port).user_idx);

            NetInfo *new_cout = ctx->createNet(ctx->id(feedout->name.str(ctx) + "$COUT"));
            feedout->connectPort(id_COUT, new_cout);

            chain_next->cell->ports[chain_next->port].net = nullptr;
            chain_next->cell->connectPort(chain_next->port, new_cout);
        }

        CellInfo *feedout_ptr = feedout.get();
        IdString feedout_name = feedout->name;
        ctx->cells[feedout_name] = std::move(feedout);

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
                if (cell->ports.at(id_CIN).net) {
                    // CIN is not constant and not part of a chain. Must feed in from fabric
                    PortRef inport;
                    inport.cell = cell;
                    inport.port = id_CIN;
                    CellInfo *feedin = make_carry_feed_in(cell->ports.at(id_CIN).net, inport);
                    chains.back().cells.push_back(feedin);
                }
            }
            chains.back().cells.push_back(cell);
            bool split_chain = int(chains.back().cells.size()) > max_length;
            if (split_chain) {
                CellInfo *passout = make_carry_feed_out(cell->ports.at(id_COUT).net);
                chains.back().cells.back() = passout;
                start_of_chain = true;
            } else {
                NetInfo *carry_net = cell->ports.at(id_COUT).net;
                bool at_end = (curr_cell == carryc.cells.end() - 1);
                if (carry_net != nullptr && (carry_net->users.entries() > 1 || at_end)) {
                    boost::optional<PortRef> nextport;
                    if (!at_end) {
                        auto next_cell = *(curr_cell + 1);
                        PortRef nextpr;
                        nextpr.cell = next_cell;
                        nextpr.port = id_CIN;
                        nextport = nextpr;
                    }
                    CellInfo *passout = make_carry_feed_out(cell->ports.at(id_COUT).net, nextport);
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
                    return net_driven_by(ctx, cell->ports.at(id_CIN).net, is_carry, id_COUT);
                },
                [](const Context *ctx, const CellInfo *cell) {
                    return net_only_drives(ctx, cell->ports.at(id_COUT).net, is_carry, id_CIN, false);
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
            std::vector<CellInfo *> packed_chain;
            for (auto &cell : chain.cells) {
                std::unique_ptr<CellInfo> comb0 =
                        create_machxo2_cell(ctx, id_TRELLIS_COMB, cell->name.str(ctx) + "$CCU2_COMB0");
                std::unique_ptr<CellInfo> comb1 =
                        create_machxo2_cell(ctx, id_TRELLIS_COMB, cell->name.str(ctx) + "$CCU2_COMB1");
                NetInfo *carry_net = ctx->createNet(ctx->id(cell->name.str(ctx) + "$CCU2_FCI_INT"));

                ccu2_to_comb(ctx, cell, comb0.get(), carry_net, 0);
                ccu2_to_comb(ctx, cell, comb1.get(), carry_net, 1);

                packed_chain.push_back(comb0.get());
                packed_chain.push_back(comb1.get());

                new_cells.push_back(std::move(comb0));
                new_cells.push_back(std::move(comb1));
                packed_cells.insert(cell->name);
                cell_count++;
            }
            packed_chains.push_back(packed_chain);
        }

        // Relative chain placement
        for (auto &chain : packed_chains) {
            chain.at(0)->constr_abs_z = true;
            chain.at(0)->constr_z = 0;
            chain.at(0)->cluster = chain.at(0)->name;
            for (int i = 1; i < int(chain.size()); i++) {
                chain.at(i)->constr_x = (i / 8);
                chain.at(i)->constr_y = 0;
                chain.at(i)->constr_z = (i % 8) << ctx->lc_idx_shift | Arch::BEL_COMB;
                chain.at(i)->constr_abs_z = true;
                chain.at(i)->cluster = chain.at(0)->name;
                chain.at(0)->constr_children.push_back(chain.at(i));
            }
        }

        flush_cells();
    }

    // Pack distributed RAM
    void pack_dram()
    {
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (is_dpram(ctx, ci)) {

                // Create RAMW slice
                std::unique_ptr<CellInfo> ramw_slice =
                        create_machxo2_cell(ctx, id_TRELLIS_RAMW, ci->name.str(ctx) + "$RAMW_SLICE");
                dram_to_ramw_split(ctx, ci, ramw_slice.get());

                // Create actual RAM slices
                std::unique_ptr<CellInfo> ram_comb[4];
                for (int i = 0; i < 4; i++) {
                    ram_comb[i] = create_machxo2_cell(ctx, id_TRELLIS_COMB,
                                                      ci->name.str(ctx) + "$DPRAM_COMB" + std::to_string(i));
                    dram_to_comb(ctx, ci, ram_comb[i].get(), ramw_slice.get(), i);
                }
                // Create 'block' SLICEs as a placement hint that these cells are mutually exclusive with the RAMW
                std::unique_ptr<CellInfo> ramw_block[2];
                for (int i = 0; i < 2; i++) {
                    ramw_block[i] = create_machxo2_cell(ctx, id_TRELLIS_COMB,
                                                        ci->name.str(ctx) + "$RAMW_BLOCK" + std::to_string(i));
                    ramw_block[i]->params[id_MODE] = std::string("RAMW_BLOCK");
                }

                // Disconnect ports of original cell after packing
                ci->disconnectPort(id_WCK);
                ci->disconnectPort(id_WRE);

                for (int i = 0; i < 4; i++)
                    ci->disconnectPort(ctx->idf("RAD[%d]", i));

                // Setup placement constraints
                // Use the 0th bit as an anchor
                ram_comb[0]->constr_abs_z = true;
                ram_comb[0]->constr_z = Arch::BEL_COMB;
                ram_comb[0]->cluster = ram_comb[0]->name;
                for (int i = 1; i < 4; i++) {
                    ram_comb[i]->cluster = ram_comb[0]->name;
                    ram_comb[i]->constr_abs_z = true;
                    ram_comb[i]->constr_x = 0;
                    ram_comb[i]->constr_y = 0;
                    ram_comb[i]->constr_z = (i << ctx->lc_idx_shift) | Arch::BEL_COMB;
                    ram_comb[0]->constr_children.push_back(ram_comb[i].get());
                }
                for (int i = 0; i < 2; i++) {
                    ramw_block[i]->cluster = ram_comb[0]->name;
                    ramw_block[i]->constr_abs_z = true;
                    ramw_block[i]->constr_x = 0;
                    ramw_block[i]->constr_y = 0;
                    ramw_block[i]->constr_z = ((i + 4) << ctx->lc_idx_shift) | Arch::BEL_COMB;
                    ram_comb[0]->constr_children.push_back(ramw_block[i].get());
                }

                ramw_slice->cluster = ram_comb[0]->name;
                ramw_slice->constr_abs_z = true;
                ramw_slice->constr_x = 0;
                ramw_slice->constr_y = 0;
                ramw_slice->constr_z = (4 << ctx->lc_idx_shift) | Arch::BEL_RAMW;
                ram_comb[0]->constr_children.push_back(ramw_slice.get());

                for (int i = 0; i < 4; i++)
                    new_cells.push_back(std::move(ram_comb[i]));
                for (int i = 0; i < 2; i++)
                    new_cells.push_back(std::move(ramw_block[i]));
                new_cells.push_back(std::move(ramw_slice));
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
        int init = int_or_default(cell->params, id_INIT);
        int new_init = make_init_with_const_input(init, index, value);
        cell->params[id_INIT] = Property(new_init, 16);
        cell->ports.at(input).net = nullptr;
    }

    void set_ccu2d_input_constant(CellInfo *cell, IdString input, bool value)
    {
        std::string input_str = input.str(ctx);
        int lut = std::stoi(input_str.substr(1));
        int index = std::string("ABCD").find(input_str[0]);
        int init = int_or_default(cell->params, ctx->id("INIT" + std::to_string(lut)));
        int new_init = make_init_with_const_input(init, index, value);
        cell->params[ctx->id("INIT" + std::to_string(lut))] = Property(new_init, 16);
        cell->ports.at(input).net = nullptr;
    }

    bool is_ccu2d_port_zero(CellInfo *cell, IdString input)
    {
        if (!cell->ports.count(input))
            return true; // disconnected port is low
        if (cell->ports.at(input).net == nullptr || cell->ports.at(input).net->name == ctx->id("$PACKER_GND_NET"))
            return true; // disconnected or tied-high low
        if (cell->ports.at(input).net->driver.cell != nullptr && cell->ports.at(input).net->driver.cell->type == id_GND)
            return true; // pre-pack low
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
                } else if (is_ff(ctx, uc) && user.port == id_CE) {
                    uc->params[id_CEMUX] = std::string(constval ? "1" : "0");
                    uc->ports[user.port].net = nullptr;
                } else if (is_carry(ctx, uc)) {
                    if (!constval && (user.port.in(id_A0, id_A1, id_B0, id_B1, id_C0, id_C1, id_D0, id_D1))) {
                        // Input tied low, nothing special to do (bitstream gen will auto-enable tie-low)
                        uc->ports[user.port].net = nullptr;
                    } else if (constval) {
                        if (user.port.in(id_A0, id_A1, id_B0, id_B1)) {
                            // These inputs can be switched to tie-low without consequence
                            set_ccu2d_input_constant(uc, user.port, constval);
                        } else if (user.port == id_C0 && is_ccu2d_port_zero(uc, id_D0)) {
                            // Partner must be tied low
                            set_ccu2d_input_constant(uc, user.port, constval);
                        } else if (user.port == id_D0 && is_ccu2d_port_zero(uc, id_C0)) {
                            // Partner must be tied low
                            set_ccu2d_input_constant(uc, user.port, constval);
                        } else if (user.port == id_C1 && is_ccu2d_port_zero(uc, id_D1)) {
                            // Partner must be tied low
                            set_ccu2d_input_constant(uc, user.port, constval);
                        } else if (user.port == id_D1 && is_ccu2d_port_zero(uc, id_C1)) {
                            // Partner must be tied low
                            set_ccu2d_input_constant(uc, user.port, constval);
                        } else {
                            // Not allowed to change to a tie-low
                            uc->ports[user.port].net = constnet;
                            uc->ports[user.port].user_idx = constnet->users.add(user);
                        }
                    } else {
                        uc->ports[user.port].net = constnet;
                        uc->ports[user.port].user_idx = constnet->users.add(user);
                    }
                } else if (is_ff(ctx, uc) && user.port == id_LSR &&
                           ((!constval && str_or_default(uc->params, id_LSRMUX, "LSR") == "LSR") ||
                            (constval && str_or_default(uc->params, id_LSRMUX, "LSR") == "INV"))) {
                    uc->ports[user.port].net = nullptr;
                } else {
                    uc->ports[user.port].net = constnet;
                    uc->ports[user.port].user_idx = constnet->users.add(user);
                }
            }
        }
        orig->users.clear();
    }

    // Pack constants (simple implementation)
    void pack_constants()
    {
        log_info("Packing constants..\n");

        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type.in(ctx->id("VLO"))) {
                ci->type = id_GND;
            } else if (ci->type.in(ctx->id("VHI"))) {
                ci->type = id_VCC;
            }
        }

        std::unique_ptr<CellInfo> gnd_cell = create_machxo2_cell(ctx, id_LUT4, "$PACKER_GND");
        gnd_cell->params[id_INIT] = Property(0, 16);
        auto gnd_net = std::make_unique<NetInfo>(ctx->id("$PACKER_GND_NET"));
        gnd_net->driver.cell = gnd_cell.get();
        gnd_net->driver.port = id_Z;
        gnd_cell->ports.at(id_Z).net = gnd_net.get();

        std::unique_ptr<CellInfo> vcc_cell = create_machxo2_cell(ctx, id_LUT4, "$PACKER_VCC");
        vcc_cell->params[id_INIT] = Property(65535, 16);
        auto vcc_net = std::make_unique<NetInfo>(ctx->id("$PACKER_VCC_NET"));
        vcc_net->driver.cell = vcc_cell.get();
        vcc_net->driver.port = id_Z;
        vcc_cell->ports.at(id_Z).net = vcc_net.get();

        std::vector<IdString> dead_nets;

        bool gnd_used = false, vcc_used = false;

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
        auto rename_bus = [&](CellInfo *c, const std::string &oldname, const std::string &newname, int width,
                              int oldoffset, int newoffset) {
            for (int i = 0; i < width; i++)
                c->renamePort(ctx->id(oldname + std::to_string(i + oldoffset)),
                              ctx->id(newname + std::to_string(i + newoffset)));
        };
        auto rename_param = [&](CellInfo *c, const std::string &oldname, const std::string &newname) {
            IdString o = ctx->id(oldname), n = ctx->id(newname);
            if (!c->params.count(o))
                return;
            c->params[n] = c->params[o];
            c->params.erase(o);
        };
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            // Convert 18-bit PDP RAMs to regular 9-bit DP ones that match the Bel
            if (ci->type == id_PDPW8KC) {
                ci->params[id_DATA_WIDTH_A] = 18; // force PDP mode
                ci->params.erase(id_DATA_WIDTH_W);
                rename_bus(ci, "BE", "ADA", 2, 0, 0);
                rename_bus(ci, "ADW", "ADA", 9, 0, 4);
                rename_bus(ci, "ADR", "ADB", 13, 0, 0);
                rename_bus(ci, "CSW", "CSA", 3, 0, 0);
                rename_bus(ci, "CSR", "CSB", 3, 0, 0);
                rename_bus(ci, "DI", "DIA", 9, 0, 0);
                rename_bus(ci, "DI", "DIB", 9, 9, 0);
                rename_bus(ci, "DO", "DOA", 9, 9, 0);
                rename_bus(ci, "DO", "DOB", 9, 0, 0);
                ci->renamePort(id_CLKW, id_CLKA);
                ci->renamePort(id_CLKR, id_CLKB);
                ci->renamePort(id_CEW, id_CEA);
                ci->renamePort(id_CER, id_CEB);
                ci->renamePort(id_OCER, id_OCEB);
                rename_param(ci, "CLKWMUX", "CLKAMUX");
                if (str_or_default(ci->params, id_CLKAMUX) == "CLKW")
                    ci->params[id_CLKAMUX] = std::string("CLKA");
                rename_param(ci, "CLKRMUX", "CLKBMUX");
                if (str_or_default(ci->params, id_CLKBMUX) == "CLKR")
                    ci->params[id_CLKBMUX] = std::string("CLKB");
                rename_param(ci, "CSDECODE_W", "CSDECODE_A");
                rename_param(ci, "CSDECODE_R", "CSDECODE_B");
                std::string outreg = str_or_default(ci->params, id_REGMODE, "NOREG");
                ci->params[id_REGMODE_A] = outreg;
                ci->params[id_REGMODE_B] = outreg;
                ci->params.erase(id_REGMODE);
                rename_param(ci, "DATA_WIDTH_R", "DATA_WIDTH_B");
                if (ci->ports.count(id_RST)) {
                    autocreate_empty_port(ci, id_RSTA);
                    autocreate_empty_port(ci, id_RSTB);
                    NetInfo *rst = ci->ports.at(id_RST).net;
                    ci->connectPort(id_RSTA, rst);
                    ci->connectPort(id_RSTB, rst);
                    ci->disconnectPort(id_RST);
                    ci->ports.erase(id_RST);
                }
                ci->type = id_DP8KC;
            }
        }
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type == id_DP8KC) {
                // Add ports, even if disconnected, to ensure correct tie-offs
                for (int i = 0; i < 13; i++) {
                    autocreate_empty_port(ci, ctx->id("ADA" + std::to_string(i)));
                    autocreate_empty_port(ci, ctx->id("ADB" + std::to_string(i)));
                }
                for (int i = 0; i < 9; i++) {
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

                ci->attrs[id_WID] = wid++;
            }
        }
    }

    // Miscellaneous packer tasks
    void pack_misc()
    {
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type.in(id_GSR, id_SGSR)) {
                ci->params[id_MODE] = std::string("ACTIVE_LOW");
                ci->params[id_SYNCMODE] = ci->type == id_SGSR ? std::string("SYNC") : std::string("ASYNC");
                ci->type = id_GSR;
                for (BelId bel : ctx->getBels()) {
                    if (ctx->getBelType(bel) != id_GSR)
                        continue;
                    ci->attrs[id_BEL] = ctx->getBelName(bel).str(ctx);
                    ctx->gsrclk_wire = ctx->getBelPinWire(bel, id_CLK);
                }
            } else if (ci->type.in(id_TSALL)) {
                ci->renamePort(id_TSALL, id_TSALLI);
            }
        }
    }

    // Check if two nets have identical constant drivers
    bool equal_constant(NetInfo *a, NetInfo *b)
    {
        if (a == nullptr && b == nullptr)
            return true;
        if ((a == nullptr) != (b == nullptr))
            return false;
        if (a->driver.cell == nullptr || b->driver.cell == nullptr)
            return (a->driver.cell == nullptr && b->driver.cell == nullptr);
        if (a->driver.cell->type != id_GND && a->driver.cell->type != id_VCC)
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
    std::map<NetInfo *, int> bridge_side_hint;

    void tie_zero(CellInfo *ci, IdString port)
    {

        if (!ci->ports.count(port)) {
            ci->ports[port].name = port;
            ci->ports[port].type = PORT_IN;
        }
        IdString name = ctx->id(ci->name.str(ctx) + "$zero$" + port.str(ctx));

        auto zero_cell = std::make_unique<CellInfo>(ctx, name, id_GND);
        NetInfo *zero_net = ctx->createNet(name);
        zero_cell->addOutput(id_GND);
        zero_cell->connectPort(id_GND, zero_net);
        ci->connectPort(port, zero_net);
        new_cells.push_back(std::move(zero_cell));
    }

    void generate_constraints()
    {
        log_info("Generating derived timing constraints...\n");
        auto MHz = [&](delay_t a) { return 1000.0 / ctx->getDelayNS(a); };

        auto equals_epsilon = [](delay_t a, delay_t b) { return (std::abs(a - b) / std::max(double(b), 1.0)) < 1e-3; };
        auto equals_epsilon_pair = [&](DelayPair &a, DelayPair &b) {
            return equals_epsilon(a.min_delay, b.min_delay) && equals_epsilon(a.max_delay, b.max_delay);
        };
        auto equals_epsilon_constr = [&](ClockConstraint &a, ClockConstraint &b) {
            return equals_epsilon_pair(a.high, b.high) && equals_epsilon_pair(a.low, b.low) &&
                   equals_epsilon_pair(a.period, b.period);
        };

        pool<IdString> user_constrained, changed_nets;
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
            period = from->clkconstr->period.minDelay();
            return true;
        };

        auto simple_clk_contraint = [&](delay_t period) {
            auto constr = std::unique_ptr<ClockConstraint>(new ClockConstraint());
            constr->low = DelayPair(period / 2);
            constr->high = DelayPair(period / 2);
            constr->period = DelayPair(period);

            return constr;
        };

        auto set_constraint = [&](CellInfo *ci, IdString port, std::unique_ptr<ClockConstraint> constr) {
            if (!ci->ports.count(port))
                return;
            NetInfo *to = ci->ports.at(port).net;
            if (to == nullptr)
                return;
            if (to->clkconstr != nullptr) {
                if (!equals_epsilon_constr(*to->clkconstr, *constr) && user_constrained.count(to->name))
                    log_warning(
                            "    Overriding derived constraint of %.2f MHz on net %s with user-specified constraint of "
                            "%.2f MHz.\n",
                            MHz(to->clkconstr->period.min_delay), to->name.c_str(ctx), MHz(constr->period.min_delay));
                return;
            }
            to->clkconstr = std::move(constr);
            log_info("    Derived frequency constraint of %.2f MHz for net %s\n", MHz(to->clkconstr->period.minDelay()),
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
                if (!equals_epsilon(to->clkconstr->period.minDelay(),
                                    delay_t(from->clkconstr->period.minDelay() / ratio)) &&
                    user_constrained.count(to->name))
                    log_warning(
                            "    Overriding derived constraint of %.2f MHz on net %s with user-specified constraint of "
                            "%.2f MHz.\n",
                            MHz(to->clkconstr->period.minDelay()), to->name.c_str(ctx),
                            MHz(delay_t(from->clkconstr->period.minDelay() / ratio)));
                return;
            }
            to->clkconstr = std::unique_ptr<ClockConstraint>(new ClockConstraint());
            to->clkconstr->low =
                    DelayPair(ctx->getDelayFromNS(ctx->getDelayNS(from->clkconstr->low.min_delay) / ratio));
            to->clkconstr->high =
                    DelayPair(ctx->getDelayFromNS(ctx->getDelayNS(from->clkconstr->high.min_delay) / ratio));
            to->clkconstr->period =
                    DelayPair(ctx->getDelayFromNS(ctx->getDelayNS(from->clkconstr->period.min_delay) / ratio));
            log_info("    Derived frequency constraint of %.2f MHz for net %s\n", MHz(to->clkconstr->period.minDelay()),
                     to->name.c_str(ctx));
            changed_nets.insert(to->name);
        };

        // Run in a loop while constraints are changing to deal with dependencies
        // Iteration limit avoids hanging in crazy loopback situation (self-fed PLLs or dividers, etc)
        int iter = 0;
        const int itermax = 5000;
        while (!changed_nets.empty() && iter < itermax) {
            ++iter;
            pool<IdString> changed_cells;
            for (auto net : changed_nets) {
                for (auto &user : ctx->nets.at(net)->users)
                    if (user.port.in(id_CLKI, id_ECLKI, id_CLK0, id_CLK1))
                        changed_cells.insert(user.cell->name);
                auto &drv = ctx->nets.at(net)->driver;
                if (iter == 1 && drv.cell != nullptr && drv.port == id_OSC)
                    changed_cells.insert(drv.cell->name);
            }
            changed_nets.clear();
            for (auto cell : changed_cells) {
                CellInfo *ci = ctx->cells.at(cell).get();
                if (ci->type == id_CLKDIVC) {
                    std::string div = str_or_default(ci->params, id_DIV, "2.0");
                    double ratio;
                    if (div == "2.0")
                        ratio = 1 / 2.0;
                    else if (div == "3.5")
                        ratio = 1 / 3.5;
                    else if (div == "4.0")
                        ratio = 1 / 4.0;
                    else
                        log_error("Unsupported divider ratio '%s' on CLKDIVC '%s'\n", div.c_str(), ci->name.c_str(ctx));
                    copy_constraint(ci, id_CLKI, id_CDIVX, ratio);
                } else if (ci->type.in(id_ECLKSYNCA /*, id_TRELLIS_ECLKBUF*/)) {
                    copy_constraint(ci, id_ECLKI, id_ECLKO, 1);
                } else if (ci->type == id_ECLKBRIDGECS) {
                    copy_constraint(ci, id_CLK0, id_ECSOUT, 1);
                    copy_constraint(ci, id_CLK1, id_ECSOUT, 1);
                } else if (ci->type == id_DCCA) {
                    copy_constraint(ci, id_CLKI, id_CLKO, 1);
                } else if (ci->type == id_EHXPLLJ) {
                    delay_t period_in;
                    if (!get_period(ci, id_CLKI, period_in))
                        continue;
                    log_info("    Input frequency of PLL '%s' is constrained to %.2f MHz\n", ci->name.c_str(ctx),
                             MHz(period_in));
                    double period_in_div = period_in * int_or_default(ci->params, id_CLKI_DIV, 1);
                    std::string path = str_or_default(ci->params, id_FEEDBK_PATH, "CLKOP");
                    int feedback_div = int_or_default(ci->params, id_CLKFB_DIV, 1);
                    if (path == "CLKOP" || path == "INT_OP")
                        feedback_div *= int_or_default(ci->params, id_CLKOP_DIV, 1);
                    else if (path == "CLKOS" || path == "INT_OS")
                        feedback_div *= int_or_default(ci->params, id_CLKOS_DIV, 1);
                    else if (path == "CLKOS2" || path == "INT_OS2")
                        feedback_div *= int_or_default(ci->params, id_CLKOS2_DIV, 1);
                    else if (path == "CLKOS3" || path == "INT_OS3")
                        feedback_div *= int_or_default(ci->params, id_CLKOS3_DIV, 1);
                    else {
                        log_info("     Unable to determine output frequencies for PLL '%s' with FEEDBK_PATH=%s\n",
                                 ci->name.c_str(ctx), path.c_str());
                        continue;
                    }
                    double vco_period = period_in_div / feedback_div;
                    double vco_freq = MHz(vco_period);
                    if (vco_freq < 400 || vco_freq > 800)
                        log_info("    Derived VCO frequency %.2f MHz of PLL '%s' is out of legal range [400MHz, "
                                 "800MHz]\n",
                                 vco_freq, ci->name.c_str(ctx));
                    set_constraint(ci, id_CLKOP,
                                   simple_clk_contraint(vco_period * int_or_default(ci->params, id_CLKOP_DIV, 1)));
                    set_constraint(ci, id_CLKOS,
                                   simple_clk_contraint(vco_period * int_or_default(ci->params, id_CLKOS_DIV, 1)));
                    set_constraint(ci, id_CLKOS2,
                                   simple_clk_contraint(vco_period * int_or_default(ci->params, id_CLKOS2_DIV, 1)));
                    set_constraint(ci, id_CLKOS3,
                                   simple_clk_contraint(vco_period * int_or_default(ci->params, id_CLKOS3_DIV, 1)));
                } else if (ci->type == id_OSCH || ci->type == id_OSCJ) {
                    static std::string const osc_freq[] = {
                            "2.08",  "2.15",  "2.22",  "2.29",  "2.38",  "2.46",  "2.56",  "2.66",  "2.77",  "2.89",
                            "3.02",  "3.17",  "3.33",  "3.50",  "3.69",  "3.91",  "4.16",  "4.29",  "4.43",  "4.59",
                            "4.75",  "4.93",  "5.12",  "5.32",  "5.54",  "5.78",  "6.05",  "6.33",  "6.65",  "7.00",
                            "7.39",  "7.82",  "8.31",  "8.58",  "8.87",  "9.17",  "9.50",  "9.85",  "10.23", "10.64",
                            "11.08", "11.57", "12.09", "12.67", "13.30", "14.00", "14.78", "15.65", "15.65", "16.63",
                            "17.73", "19.00", "20.46", "22.17", "24.18", "26.60", "29.56", "33.25", "38.00", "44.33",
                            "53.20", "66.50", "88.67", "133.00"};

                    std::string freq = str_or_default(ci->params, id_NOM_FREQ, "2.08");
                    bool found = false;
                    for (int i = 0; i < 64; i++) {
                        if (osc_freq[i] == freq) {
                            found = true;
                            set_constraint(ci, id_OSC, simple_clk_contraint(delay_t(1.0e6 / std::stof(freq))));
                            break;
                        }
                    }
                    if (!found)
                        log_error("Unsupported frequency '%s' on %s '%s'\n", freq.c_str(), ci->type.c_str(ctx),
                                  ci->name.c_str(ctx));
                }
            }
        }
    }

    void prepack_checks()
    {
        // Check for legacy-style JSON (use CEMUX as a clue) and error out, avoiding a confusing assertion failure
        // later
        for (auto &cell : ctx->cells) {
            if (is_ff(ctx, cell.second.get()) && cell.second->params.count(id_CEMUX) &&
                !cell.second->params[id_CEMUX].is_string)
                log_error("Found netlist using legacy-style JSON parameter values, please update your Yosys.\n");
        }
    }

    BelId get_bel_attr(const CellInfo *ci)
    {
        if (!ci->attrs.count(id_BEL))
            return BelId();
        return ctx->getBelByNameStr(ci->attrs.at(id_BEL).as_string());
    }

    // Using a BFS, search for bels of a given type either upstream or downstream of another cell
    void find_connected_bels(const CellInfo *cell, IdString port, IdString dest_type, IdString dest_pin, int iter_limit,
                             std::vector<BelId> &candidates)
    {
        int iter = 0;
        std::queue<WireId> visit;
        pool<WireId> seen_wires;
        pool<BelId> seen_bels;

        BelId bel = get_bel_attr(cell);
        if (bel == BelId())
            return;
        WireId start_wire = ctx->getBelPinWire(bel, port);
        NPNR_ASSERT(start_wire != WireId());
        PortType dir = ctx->getBelPinType(bel, port);

        visit.push(start_wire);

        while (!visit.empty() && (iter++ < iter_limit)) {
            WireId cursor = visit.front();
            visit.pop();
            // Check to see if we have reached a valid bel pin
            for (auto bp : ctx->getWireBelPins(cursor)) {
                if (ctx->getBelType(bp.bel) != dest_type)
                    continue;
                if (dest_pin != IdString() && bp.pin != dest_pin)
                    continue;
                if (seen_bels.count(bp.bel))
                    continue;
                seen_bels.insert(bp.bel);
                candidates.push_back(bp.bel);
            }
            // Search in the appropriate direction up/downstream of the cursor
            if (dir == PORT_OUT) {
                for (PipId p : ctx->getPipsDownhill(cursor))
                    if (ctx->checkPipAvail(p)) {
                        WireId dst = ctx->getPipDstWire(p);
                        if (seen_wires.count(dst))
                            continue;
                        seen_wires.insert(dst);
                        visit.push(dst);
                    }
            } else {
                for (PipId p : ctx->getPipsUphill(cursor))
                    if (ctx->checkPipAvail(p)) {
                        WireId src = ctx->getPipSrcWire(p);
                        if (seen_wires.count(src))
                            continue;
                        seen_wires.insert(src);
                        visit.push(src);
                    }
            }
        }
    }

    // Find the nearest bel of a given type; matching a closure predicate
    template <typename Tpred> BelId find_nearest_bel(const CellInfo *cell, IdString dest_type, Tpred predicate)
    {
        BelId origin = get_bel_attr(cell);
        if (origin == BelId())
            return BelId();
        Loc origin_loc = ctx->getBelLocation(origin);
        int best_distance = std::numeric_limits<int>::max();
        BelId best_bel = BelId();

        for (BelId bel : ctx->getBels()) {
            if (ctx->getBelType(bel) != dest_type)
                continue;
            if (!predicate(bel))
                continue;
            Loc bel_loc = ctx->getBelLocation(bel);
            int dist = std::abs(origin_loc.x - bel_loc.x) + std::abs(origin_loc.y - bel_loc.y);
            if (dist < best_distance) {
                best_distance = dist;
                best_bel = bel;
            }
        }
        return best_bel;
    }

    pool<BelId> used_bels;
    // Pre-place a primitive based on routeability first and distance second
    bool preplace_prim(CellInfo *cell, IdString pin, bool strict_routing)
    {
        std::vector<BelId> routeability_candidates;

        if (cell->attrs.count(id_BEL))
            return false;

        NetInfo *pin_net = cell->getPort(pin);
        if (pin_net == nullptr)
            return false;

        CellInfo *pin_drv = pin_net->driver.cell;
        if (pin_drv == nullptr)
            return false;

        // Check based on routeability
        find_connected_bels(pin_drv, pin_net->driver.port, cell->type, pin, 25000, routeability_candidates);

        for (BelId cand : routeability_candidates) {
            if (used_bels.count(cand))
                continue;
            log_info("    constraining %s '%s' to bel '%s' based on dedicated routing\n", ctx->nameOf(cell),
                     ctx->nameOf(cell->type), ctx->nameOfBel(cand));
            cell->attrs[id_BEL] = ctx->getBelName(cand).str(ctx);
            used_bels.insert(cand);
            return true;
        }

        // Unless in strict mode; check based on simple distance too
        BelId nearest = find_nearest_bel(pin_drv, cell->type, [&](BelId bel) { return !used_bels.count(bel); });

        if (nearest != BelId()) {
            log_info("    constraining %s '%s' to bel '%s'\n", ctx->nameOf(cell), ctx->nameOf(cell->type),
                     ctx->nameOfBel(nearest));
            cell->attrs[id_BEL] = ctx->getBelName(nearest).str(ctx);
            used_bels.insert(nearest);
            return true;
        }

        return false;
    }

    // Pre-place a singleton primitive; so decisions can be made on routeability downstream of it
    bool preplace_singleton(CellInfo *cell)
    {
        if (cell->attrs.count(id_BEL))
            return false;
        bool did_something = false;
        for (BelId bel : ctx->getBels()) {
            if (ctx->getBelType(bel) != cell->type)
                continue;
            // Check that the bel really is a singleton...
            NPNR_ASSERT(!cell->attrs.count(id_BEL));
            cell->attrs[id_BEL] = ctx->getBelName(bel).str(ctx);
            log_info("    constraining %s '%s' to bel '%s'\n", ctx->nameOf(cell), ctx->nameOf(cell->type),
                     ctx->nameOfBel(bel));
            did_something = true;
        }
        return did_something;
    }

    // Insert a buffer primitive in a signal; moving all users that match a predicate behind it
    template <typename Tpred>
    CellInfo *insert_buffer(NetInfo *net, IdString buffer_type, std::string name_postfix, IdString i, IdString o,
                            Tpred pred)
    {
        // Create the buffered net
        NetInfo *buffered_net = ctx->createNet(ctx->idf("%s$%s", ctx->nameOf(net), name_postfix.c_str()));
        // Create the buffer cell
        CellInfo *buffer = ctx->createCell(ctx->idf("%s$drv_%s", ctx->nameOf(buffered_net), ctx->nameOf(buffer_type)),
                                           buffer_type);
        buffer->addInput(i);
        buffer->addOutput(o);
        // Drive the buffered net with the buffer
        buffer->connectPort(o, buffered_net);
        // Filter users
        std::vector<PortRef> remaining_users;

        for (auto &usr : net->users) {
            if (pred(usr)) {
                usr.cell->ports[usr.port].net = buffered_net;
                usr.cell->ports[usr.port].user_idx = buffered_net->users.add(usr);
            } else {
                remaining_users.push_back(usr);
            }
        }

        net->users.clear();
        for (auto &usr : remaining_users)
            usr.cell->ports.at(usr.port).user_idx = net->users.add(usr);

        // Connect buffer input to original net
        buffer->connectPort(i, net);

        return buffer;
    }

    // Insert global buffers
    void promote_globals()
    {
        std::vector<std::pair<int, IdString>> clk_fanout;
        int available_globals = 8;
        for (auto &net : ctx->nets) {
            NetInfo *ni = net.second.get();
            // Skip undriven nets; and nets that are already global
            if (ni->driver.cell == nullptr)
                continue;
            if (ni->name.in(ctx->id("$PACKER_GND_NET"), ctx->id("$PACKER_VCC_NET")))
                continue;
            if (ni->driver.cell->type == id_DCMA) {
                continue;
            }
            if (ni->driver.cell->type == id_DCCA) {
                --available_globals;
                continue;
            }
            // Count the number of clock ports
            int clk_count = 0;
            for (const auto &usr : ni->users) {
                if (usr.cell->type == id_TRELLIS_FF && usr.port == id_CLK)
                    clk_count++;
                if (usr.cell->type == id_DP8KC && usr.port.in(id_CLKA, id_CLKB))
                    clk_count++;
            }
            if (clk_count > 0)
                clk_fanout.emplace_back(clk_count, ni->name);
        }
        if (available_globals <= 0)
            return;
        // Sort clocks by max fanout
        std::sort(clk_fanout.begin(), clk_fanout.end(), std::greater<std::pair<int, IdString>>());
        log_info("Promoting globals...\n");
        // Promote the N highest fanout clocks
        for (size_t i = 0; i < std::min<size_t>(clk_fanout.size(), available_globals); i++) {
            NetInfo *net = ctx->nets.at(clk_fanout.at(i).second).get();
            log_info("     promoting clock net '%s'\n", ctx->nameOf(net));
            insert_buffer(net, id_DCCA, "glb_clk", id_CLKI, id_CLKO,
                          [&](const PortRef &port) { return port.cell->type != id_DCCA; });
        }
    }

    // Place certain global cells
    void place_globals()
    {
        // Keep running until we reach a fixed point
        log_info("Placing globals...\n");
        bool did_something = true;
        while (did_something) {
            did_something = false;
            for (auto &cell : ctx->cells) {
                CellInfo *ci = cell.second.get();
                if (ci->type == id_OSCH)
                    did_something |= preplace_singleton(ci);
                else if (ci->type == id_DCCA)
                    did_something |= preplace_prim(ci, id_CLKI, false);
                else if (ci->type == id_EHXPLLJ)
                    did_something |= preplace_prim(ci, id_CLKI, false);
            }
        }
    }

  public:
    void pack()
    {
        prepack_checks();
        print_logic_usage();
        pack_io();
        pack_ebr();
        pack_misc();
        pack_constants();
        pack_dram();
        pack_carries();
        pack_luts();
        pack_lut5xs();
        pack_ffs();
        promote_globals();
        place_globals();
        generate_constraints();
        ctx->fixupHierarchy();
        ctx->check();
    }

  private:
    Context *ctx;

    pool<IdString> packed_cells;
    std::vector<std::unique_ptr<CellInfo>> new_cells;

    struct SliceUsage
    {
        bool lut0_used = false, lut1_used = false;
        bool ccu2_used = false, dpram_used = false, ramw_used = false;
        bool ff0_used = false, ff1_used = false;
        bool mux5_used = false, muxx_used = false;
    };

    dict<IdString, SliceUsage> sliceUsage;
    dict<IdString, IdString> lutffPairs;
    dict<IdString, IdString> fflutPairs;
    dict<IdString, IdString> lutPairs;
};
// Main pack function
bool Arch::pack()
{
    Context *ctx = getCtx();
    try {
        log_break();
        MachXO2Packer(ctx).pack();
        log_info("Checksum: 0x%08x\n", ctx->checksum());
        assignArchInfo();
        ctx->settings[id_pack] = 1;
        archInfoToAttributes();
        return true;
    } catch (log_execution_error_exception) {
        assignArchInfo();
        return false;
    }
}

void Arch::assign_arch_info_for_cell(CellInfo *ci)
{
    auto get_port_net = [&](CellInfo *ci, IdString p) {
        NetInfo *n = ci->getPort(p);
        return n ? n->name : IdString();
    };
    if (ci->type == id_TRELLIS_COMB) {
        std::string mode = str_or_default(ci->params, id_MODE, "LOGIC");
        ci->combInfo.flags = ArchCellInfo::COMB_NONE;
        if (mode == "CCU2")
            ci->combInfo.flags |= ArchCellInfo::COMB_CARRY;
        if (mode == "DPRAM") {
            ci->combInfo.flags |= ArchCellInfo::COMB_LUTRAM;
            std::string wckmux = str_or_default(ci->params, id_WCKMUX, "WCK");
            if (wckmux == "INV")
                ci->combInfo.flags |= ArchCellInfo::COMB_RAM_WCKINV;
            std::string wremux = str_or_default(ci->params, id_WREMUX, "WRE");
            if (wremux == "INV" || wremux == "0")
                ci->combInfo.flags |= ArchCellInfo::COMB_RAM_WREINV;
            ci->combInfo.ram_wck = get_port_net(ci, id_WCK);
            ci->combInfo.ram_wre = get_port_net(ci, id_WRE);
        }
        if (mode == "RAMW_BLOCK")
            ci->combInfo.flags |= ArchCellInfo::COMB_RAMW_BLOCK;
        if (ci->getPort(id_F1) != nullptr)
            ci->combInfo.flags |= ArchCellInfo::COMB_MUX5;
        if (ci->getPort(id_FXA) != nullptr || ci->getPort(id_FXB) != nullptr) {
            ci->combInfo.flags |= ArchCellInfo::COMB_MUX6;
            NetInfo *fxa = ci->getPort(id_FXA);
            if (fxa != nullptr)
                ci->combInfo.mux_fxad = fxa->driver.cell;
        }
    } else if (ci->type == id_TRELLIS_FF) {
        ci->ffInfo.flags = ArchCellInfo::FF_NONE;
        if (str_or_default(ci->params, id_GSR, "ENABLED") == "ENABLED")
            ci->ffInfo.flags |= ArchCellInfo::FF_GSREN;
        if (str_or_default(ci->params, id_SRMODE, "LSR_OVER_CE") == "ASYNC")
            ci->ffInfo.flags |= ArchCellInfo::FF_ASYNC;
        if (ci->getPort(id_M) != nullptr)
            ci->ffInfo.flags |= ArchCellInfo::FF_M_USED;
        std::string clkmux = str_or_default(ci->params, id_CLKMUX, "CLK");
        std::string cemux = str_or_default(ci->params, id_CEMUX, "CE");
        std::string lsrmux = str_or_default(ci->params, id_LSRMUX, "LSR");
        if (clkmux == "INV" || clkmux == "0")
            ci->ffInfo.flags |= ArchCellInfo::FF_CLKINV;
        if (cemux == "INV" || cemux == "0")
            ci->ffInfo.flags |= ArchCellInfo::FF_CEINV;
        if (cemux == "1" || cemux == "0")
            ci->ffInfo.flags |= ArchCellInfo::FF_CECONST;
        if (lsrmux == "INV")
            ci->ffInfo.flags |= ArchCellInfo::FF_LSRINV;
        ci->ffInfo.clk_sig = get_port_net(ci, id_CLK);
        ci->ffInfo.ce_sig = get_port_net(ci, id_CE);
        ci->ffInfo.lsr_sig = get_port_net(ci, id_LSR);
    } else if (ci->type == id_DP8KC) {
        ci->ramInfo.is_pdp = (int_or_default(ci->params, id_DATA_WIDTH_A, 0) == 18);

        // Output register mode (REGMODE_{A,B}). Valid options are 'NOREG' and 'OUTREG'.
        std::string regmode_a = str_or_default(ci->params, id_REGMODE_A, "NOREG");
        if (regmode_a != "NOREG" && regmode_a != "OUTREG")
            log_error("DP8KC %s has invalid REGMODE_A configuration '%s'\n", ci->name.c_str(this), regmode_a.c_str());
        std::string regmode_b = str_or_default(ci->params, id_REGMODE_B, "NOREG");
        if (regmode_b != "NOREG" && regmode_b != "OUTREG")
            log_error("DP8KC %s has invalid REGMODE_B configuration '%s'\n", ci->name.c_str(this), regmode_b.c_str());
        ci->ramInfo.is_output_a_registered = regmode_a == "OUTREG";
        ci->ramInfo.is_output_b_registered = regmode_b == "OUTREG";

        // Based on the REGMODE, we have different timing lookup tables.
        if (!ci->ramInfo.is_output_a_registered && !ci->ramInfo.is_output_b_registered) {
            ci->ramInfo.regmode_timing_id = id_DP8KC_REGMODE_A_NOREG_REGMODE_B_NOREG;
        } else if (!ci->ramInfo.is_output_a_registered && ci->ramInfo.is_output_b_registered) {
            ci->ramInfo.regmode_timing_id = id_DP8KC_REGMODE_A_NOREG_REGMODE_B_OUTREG;
        } else if (ci->ramInfo.is_output_a_registered && !ci->ramInfo.is_output_b_registered) {
            ci->ramInfo.regmode_timing_id = id_DP8KC_REGMODE_A_OUTREG_REGMODE_B_NOREG;
        } else if (ci->ramInfo.is_output_a_registered && ci->ramInfo.is_output_b_registered) {
            ci->ramInfo.regmode_timing_id = id_DP8KC_REGMODE_A_OUTREG_REGMODE_B_OUTREG;
        }
    }
}

void Arch::assignArchInfo()
{
    for (auto &cell : cells) {
        CellInfo *ci = cell.second.get();
        assign_arch_info_for_cell(ci);
    }
}

NEXTPNR_NAMESPACE_END
