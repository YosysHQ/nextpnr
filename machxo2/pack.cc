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
    bool is_top_port(PortRef &port)
    {
        return false;
    }

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
                    }
                    if (ci->type == ctx->id("$nextpnr_iobuf")) {
                        NetInfo *net2 = ci->ports.at(id_I).net;
                        if (net2 != nullptr) {
                            ctx->nets.erase(net2->name);
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
        std::unique_ptr<CellInfo> feedin = create_machxo2_cell(ctx, id_CCU2C);

        feedin->params[id_INIT0] = Property(10, 16); // LUT4 = 0; LUT2 = A
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
        std::unique_ptr<CellInfo> feedout = create_machxo2_cell(ctx, id_CCU2C);

        feedout->params[id_INIT0] = Property(0, 16);
        feedout->params[id_INIT1] = Property(10, 16); // LUT4 = 0; LUT2 = A
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

    void set_ccu2c_input_constant(CellInfo *cell, IdString input, bool value)
    {
        std::string input_str = input.str(ctx);
        int lut = std::stoi(input_str.substr(1));
        int index = std::string("ABCD").find(input_str[0]);
        int init = int_or_default(cell->params, ctx->id("INIT" + std::to_string(lut)));
        int new_init = make_init_with_const_input(init, index, value);
        cell->params[ctx->id("INIT" + std::to_string(lut))] = Property(new_init, 16);
        cell->ports.at(input).net = nullptr;
    }

    bool is_ccu2c_port_high(CellInfo *cell, IdString input)
    {
        if (!cell->ports.count(input))
            return true; // disconnected port is high
        if (cell->ports.at(input).net == nullptr || cell->ports.at(input).net->name == ctx->id("$PACKER_VCC_NET"))
            return true; // disconnected or tied-high port
        if (cell->ports.at(input).net->driver.cell != nullptr && cell->ports.at(input).net->driver.cell->type == id_VCC)
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
                } else if (is_ff(ctx, uc) && user.port == id_CE) {
                    uc->params[id_CEMUX] = std::string(constval ? "1" : "0");
                    uc->ports[user.port].net = nullptr;
                } else if (is_carry(ctx, uc)) {
                    if (constval && (user.port.in(id_A0, id_A1, id_B0, id_B1, id_C0, id_C1, id_D0, id_D1))) {
                        // Input tied high, nothing special to do (bitstream gen will auto-enable tie-high)
                        uc->ports[user.port].net = nullptr;
                    } else if (!constval) {
                        if (user.port.in(id_A0, id_A1, id_B0, id_B1)) {
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

  public:
    void pack()
    {
        prepack_checks();
        print_logic_usage();
        pack_io();
        pack_constants();
        pack_dram();
        pack_carries();
        pack_luts();
        pack_ffs();
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
        Ecp5Packer(ctx).pack();
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
    }
}

void Arch::assignArchInfo()
{
    for (auto &cell : cells) {
        CellInfo *ci = cell.second.get();
        assign_arch_info_for_cell(ci);
    }
}

inline NetInfo *port_or_nullptr(const CellInfo *cell, IdString name)
{
    auto found = cell->ports.find(name);
    if (found == cell->ports.end())
        return nullptr;
    return found->second.net;
}

bool Arch::slices_compatible(LogicTileStatus *lts) const
{
    if (lts == nullptr)
        return true;
    for (int sl = 0; sl < 4; sl++) {
        if (!lts->slices[sl].dirty) {
            if (!lts->slices[sl].valid)
                return false;
            continue;
        }
        lts->slices[sl].dirty = false;
        lts->slices[sl].valid = false;
        bool found_ff = false;
        uint8_t last_ff_flags = 0;
        IdString last_ce_sig;
        bool ramw_used = false;
        if (sl == 2 && lts->cells[((sl * 2) << lc_idx_shift) | BEL_RAMW] != nullptr)
            ramw_used = true;
        for (int l = 0; l < 2; l++) {
            bool comb_m_used = false;
            CellInfo *comb = lts->cells[((sl * 2 + l) << lc_idx_shift) | BEL_COMB];
            if (comb != nullptr) {
                uint8_t flags = comb->combInfo.flags;
                if (ramw_used && !(flags & ArchCellInfo::COMB_RAMW_BLOCK))
                    return false;
                if (flags & ArchCellInfo::COMB_MUX5) {
                    // MUX5 uses M signal and must be in LC 0
                    comb_m_used = true;
                    if (l != 0)
                        return false;
                }
                if (flags & ArchCellInfo::COMB_MUX6) {
                    // MUX6+ uses M signal and must be in LC 1
                    comb_m_used = true;
                    if (l != 1)
                        return false;
                    if (comb->combInfo.mux_fxad != nullptr &&
                        (comb->combInfo.mux_fxad->combInfo.flags & ArchCellInfo::COMB_MUX5)) {
                        // LUT6 structure must be rooted at SLICE 0 or 2
                        if (sl != 0 && sl != 2)
                            return false;
                    }
                }
                // LUTRAM must be in bottom two SLICEs only
                if ((flags & ArchCellInfo::COMB_LUTRAM) && (sl > 1))
                    return false;
                if (l == 1) {
                    // Carry usage must be the same for LCs 0 and 1 in a SLICE
                    CellInfo *comb0 = lts->cells[((sl * 2 + 0) << lc_idx_shift) | BEL_COMB];
                    if (comb0 &&
                        ((comb0->combInfo.flags & ArchCellInfo::COMB_CARRY) != (flags & ArchCellInfo::COMB_CARRY)))
                        return false;
                }
            }

            CellInfo *ff = lts->cells[((sl * 2 + l) << lc_idx_shift) | BEL_FF];
            if (ff != nullptr) {
                uint8_t flags = ff->ffInfo.flags;
                if (comb_m_used && (flags & ArchCellInfo::FF_M_USED))
                    return false;
                if (found_ff) {
                    if ((flags & ArchCellInfo::FF_GSREN) != (last_ff_flags & ArchCellInfo::FF_GSREN))
                        return false;
                    if ((flags & ArchCellInfo::FF_CECONST) != (last_ff_flags & ArchCellInfo::FF_CECONST))
                        return false;
                    if ((flags & ArchCellInfo::FF_CEINV) != (last_ff_flags & ArchCellInfo::FF_CEINV))
                        return false;
                    if (ff->ffInfo.ce_sig != last_ce_sig)
                        return false;
                } else {
                    found_ff = true;
                    last_ff_flags = flags;
                    last_ce_sig = ff->ffInfo.ce_sig;
                }
            }
        }

        lts->slices[sl].valid = true;
    }
    if (lts->tile_dirty) {
        bool found_global_ff = false;
        bool found_global_dpram = false;
        bool global_lsrinv = false;
        bool global_clkinv = false;
        bool global_async = false;

        IdString clk_sig, lsr_sig;

        lts->tile_dirty = false;
        lts->tile_valid = false;

#define CHECK_EQUAL(x, y)                                                                                              \
    do {                                                                                                               \
        if ((x) != (y))                                                                                                \
            return false;                                                                                              \
    } while (0)
        for (int i = 0; i < 8; i++) {
            if (i < 4) {
                // DPRAM
                CellInfo *comb = lts->cells[(i << lc_idx_shift) | BEL_COMB];
                if (comb != nullptr && (comb->combInfo.flags & ArchCellInfo::COMB_LUTRAM)) {
                    if (found_global_dpram) {
                        CHECK_EQUAL(bool(comb->combInfo.flags & ArchCellInfo::COMB_RAM_WCKINV), global_clkinv);
                        CHECK_EQUAL(bool(comb->combInfo.flags & ArchCellInfo::COMB_RAM_WREINV), global_lsrinv);
                    } else {
                        global_clkinv = bool(comb->combInfo.flags & ArchCellInfo::COMB_RAM_WCKINV);
                        global_lsrinv = bool(comb->combInfo.flags & ArchCellInfo::COMB_RAM_WREINV);
                        found_global_dpram = true;
                    }
                }
            }
            // FF
            CellInfo *ff = lts->cells[(i << lc_idx_shift) | BEL_FF];
            if (ff != nullptr) {
                if (found_global_dpram) {
                    CHECK_EQUAL(bool(ff->ffInfo.flags & ArchCellInfo::FF_CLKINV), global_clkinv);
                    CHECK_EQUAL(bool(ff->ffInfo.flags & ArchCellInfo::FF_LSRINV), global_lsrinv);
                }
                if (found_global_ff) {
                    CHECK_EQUAL(ff->ffInfo.clk_sig, clk_sig);
                    CHECK_EQUAL(ff->ffInfo.lsr_sig, lsr_sig);
                    CHECK_EQUAL(bool(ff->ffInfo.flags & ArchCellInfo::FF_CLKINV), global_clkinv);
                    CHECK_EQUAL(bool(ff->ffInfo.flags & ArchCellInfo::FF_LSRINV), global_lsrinv);
                    CHECK_EQUAL(bool(ff->ffInfo.flags & ArchCellInfo::FF_ASYNC), global_async);

                } else {
                    clk_sig = ff->ffInfo.clk_sig;
                    lsr_sig = ff->ffInfo.lsr_sig;
                    global_clkinv = bool(ff->ffInfo.flags & ArchCellInfo::FF_CLKINV);
                    global_lsrinv = bool(ff->ffInfo.flags & ArchCellInfo::FF_LSRINV);
                    global_async = bool(ff->ffInfo.flags & ArchCellInfo::FF_ASYNC);
                    found_global_ff = true;
                }
            }
        }
#undef CHECK_EQUAL
        lts->tile_valid = true;
    } else {
        if (!lts->tile_valid)
            return false;
    }

    return true;
}

bool Arch::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    IdString bel_type = getBelType(bel);
    if (bel_type.in(id_TRELLIS_COMB, id_TRELLIS_FF, id_TRELLIS_RAMW)) {
        return slices_compatible(tile_status.at(tile_index(bel)).lts);
    }
    return true;
}

NEXTPNR_NAMESPACE_END
