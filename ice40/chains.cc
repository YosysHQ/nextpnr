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

#include "chains.h"
#include <algorithm>
#include <vector>
#include "cells.h"
#include "chain_utils.h"
#include "design_utils.h"
#include "log.h"
#include "place_common.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

class ChainConstrainer
{
  private:
    int feedio_lcs = 0;
    Context *ctx;
    // Split a carry chain into multiple legal chains
    std::vector<CellChain> split_carry_chain(CellChain &carryc)
    {
        bool start_of_chain = true;
        std::vector<CellChain> chains;
        std::vector<const CellInfo *> tile;
        const int max_length = (ctx->chip_info->height - 2) * 8 - 2;
        auto curr_cell = carryc.cells.begin();
        while (curr_cell != carryc.cells.end()) {
            CellInfo *cell = *curr_cell;
            if (ctx->debug)
                log_info("  processing cell %s\n", ctx->nameOf(cell));
            if (tile.size() >= 8) {
                tile.clear();
            }
            if (start_of_chain) {
                tile.clear();
                chains.emplace_back();
                start_of_chain = false;
                if (cell->ports.at(id_CIN).net) {
                    // CIN is not constant and not part of a chain. Must feed in from fabric
                    CellInfo *feedin = make_carry_feed_in(cell, cell->ports.at(id_CIN));
                    chains.back().cells.push_back(feedin);
                    tile.push_back(feedin);
                    ++feedio_lcs;
                }
            }
            tile.push_back(cell);
            chains.back().cells.push_back(cell);
            bool split_chain = (!ctx->logic_cells_compatible(tile.data(), tile.size())) ||
                               (int(chains.back().cells.size()) > max_length);
            if (split_chain) {
                CellInfo *passout = make_carry_pass_out((*(curr_cell - 1))->ports.at(id_COUT));
                tile.pop_back();
                chains.back().cells.back() = passout;
                start_of_chain = true;
            } else {
                NetInfo *carry_net = cell->ports.at(id_COUT).net;
                bool at_end = (curr_cell == carryc.cells.end() - 1);
                if (carry_net != nullptr && (carry_net->users.entries() > 1 || at_end)) {
                    if (carry_net->users.entries() > 2 ||
                        (net_only_drives(ctx, carry_net, is_lc, id_I3, false) !=
                         net_only_drives(ctx, carry_net, is_lc, id_CIN, false)) ||
                        (at_end && !net_only_drives(ctx, carry_net, is_lc, id_I3, true))) {
                        if (ctx->debug)
                            log_info("      inserting feed-%s\n", at_end ? "out" : "out-in");
                        CellInfo *passout;
                        if (!at_end) {
                            // See if we need to split chain anyway
                            tile.push_back(*(curr_cell + 1));
                            bool split_chain_next = (!ctx->logic_cells_compatible(tile.data(), tile.size())) ||
                                                    (int(chains.back().cells.size()) > max_length);
                            tile.pop_back();
                            if (split_chain_next)
                                start_of_chain = true;
                            passout = make_carry_pass_out(cell->ports.at(id_COUT),
                                                          split_chain_next ? nullptr : *(curr_cell + 1));
                        } else {
                            passout = make_carry_pass_out(cell->ports.at(id_COUT), nullptr);
                        }

                        chains.back().cells.push_back(passout);
                        tile.push_back(passout);
                        ++feedio_lcs;
                    }
                }
                ++curr_cell;
            }
        }
        return chains;
    }

    // Insert a logic cell to legalise a COUT->fabric connection
    CellInfo *make_carry_pass_out(PortInfo &cout_port, CellInfo *cin_cell = nullptr)
    {
        NPNR_ASSERT(cout_port.net != nullptr);
        std::unique_ptr<CellInfo> lc = create_ice_cell(ctx, id_ICESTORM_LC);
        lc->params[id_LUT_INIT] = Property(65280, 16); // 0xff00: O = I3
        lc->params[id_CARRY_ENABLE] = Property::State::S1;
        lc->ports.at(id_O).net = cout_port.net;
        NetInfo *co_i3_net = ctx->createNet(ctx->id(lc->name.str(ctx) + "$I3"));
        co_i3_net->driver = cout_port.net->driver;
        lc->connectPort(id_I3, co_i3_net);
        PortRef o_r;
        o_r.port = id_O;
        o_r.cell = lc.get();
        cout_port.net->driver = o_r;
        cout_port.net = co_i3_net;

        // If COUT also connects to a CIN; preserve the carry chain
        if (cin_cell) {
            NetInfo *co_cin_net = ctx->createNet(ctx->id(lc->name.str(ctx) + "$COUT"));

            // Connect I1 to 1 to preserve carry chain
            NetInfo *vcc = ctx->nets.at(ctx->id("$PACKER_VCC_NET")).get();
            lc->connectPort(id_I1, vcc);

            // Connect co_cin_net to the COUT of the LC
            lc->connectPort(id_COUT, co_cin_net);

            // Find the user corresponding to the next CIN
            int replaced_ports = 0;
            if (ctx->debug)
                log_info("cell: %s\n", cin_cell->name.c_str(ctx));
            for (auto port : {id_CIN, id_I3}) {
                NetInfo *out_net = lc->ports.at(id_O).net;
                auto &cin_p = cin_cell->ports.at(port);
                if (cin_p.net == out_net) {
                    cin_cell->disconnectPort(port);
                    cin_cell->connectPort(port, co_cin_net);
                    ++replaced_ports;
                }
            }
            NPNR_ASSERT(replaced_ports > 0);
        }

        IdString name = lc->name;
        ctx->assignCellInfo(lc.get());
        ctx->cells[lc->name] = std::move(lc);
        return ctx->cells[name].get();
    }

    // Insert a logic cell to legalise a CIN->fabric connection
    CellInfo *make_carry_feed_in(CellInfo *cin_cell, PortInfo &cin_port)
    {
        NPNR_ASSERT(cin_port.net != nullptr);
        std::unique_ptr<CellInfo> lc = create_ice_cell(ctx, id_ICESTORM_LC);
        lc->params[id_CARRY_ENABLE] = Property::State::S1;
        lc->params[id_CIN_CONST] = Property::State::S1;
        lc->params[id_CIN_SET] = Property::State::S1;

        lc->connectPort(id_I1, cin_port.net);
        cin_port.net->users.remove(cin_port.user_idx);

        NetInfo *out_net = ctx->createNet(ctx->id(lc->name.str(ctx) + "$O"));

        lc->connectPort(id_COUT, out_net);

        cin_port.net = nullptr;
        cin_cell->connectPort(cin_port.name, out_net);

        IdString name = lc->name;
        ctx->assignCellInfo(lc.get());
        ctx->cells[lc->name] = std::move(lc);
        return ctx->cells[name].get();
    }

    void process_carries()
    {
        // Find carry roots
        std::vector<CellChain> carry_chains;
        pool<IdString> processed;
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (is_lc(ctx, ci) && bool_or_default(ci->params, id_CARRY_ENABLE)) {
                // possibly a non-root if CIN or I3 driven by another cout
                NetInfo *cin = ci->getPort(id_CIN);
                if (cin && cin->driver.cell && is_lc(ctx, cin->driver.cell) && cin->driver.port == id_COUT) {
                    continue;
                }
                carry_chains.emplace_back();
                auto &cc = carry_chains.back();
                CellInfo *cursor = ci;
                while (cursor) {
                    cc.cells.push_back(cursor);
                    processed.insert(cursor->name);
                    NetInfo *cout = cursor->getPort(id_COUT);
                    if (!cout)
                        break;
                    cursor = nullptr;
                    // look for CIN connectivity
                    for (auto &usr : cout->users) {
                        if (is_lc(ctx, usr.cell) && usr.port == id_CIN && !processed.count(usr.cell->name)) {
                            cursor = usr.cell;
                            break;
                        }
                    }
                    // look for I3 connectivity - only to a top cell with no further chaining
                    if (cursor)
                        continue;
                    for (auto &usr : cout->users) {
                        if (is_lc(ctx, usr.cell) && usr.port == id_I3 && !processed.count(usr.cell->name) &&
                            !usr.cell->getPort(id_COUT)) {
                            cursor = usr.cell;
                            break;
                        }
                    }
                }
            }
        }
        // anything left behind....
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (is_lc(ctx, ci) && bool_or_default(ci->params, id_CARRY_ENABLE) && !processed.count(ci->name)) {
                carry_chains.emplace_back();
                carry_chains.back().cells.push_back(ci);
                processed.insert(ci->name);
            }
        }
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
        // Actual chain placement
        for (auto &chain : all_chains) {
            if (ctx->verbose)
                log_info("Placing carry chain starting at '%s'\n", chain.cells.front()->name.c_str(ctx));

            // Place carry chain
            chain.cells.at(0)->constr_abs_z = true;
            chain.cells.at(0)->constr_z = 0;
            chain.cells.at(0)->cluster = chain.cells.at(0)->name;

            for (int i = 1; i < int(chain.cells.size()); i++) {
                chain.cells.at(i)->constr_x = 0;
                chain.cells.at(i)->constr_y = (i / 8);
                chain.cells.at(i)->constr_z = i % 8;
                chain.cells.at(i)->constr_abs_z = true;
                chain.cells.at(i)->cluster = chain.cells.at(0)->name;
                chain.cells.at(0)->constr_children.push_back(chain.cells.at(i));
            }
        }
        log_info("    %4d LCs used to legalise carry chains.\n", feedio_lcs);
    }

  public:
    ChainConstrainer(Context *ctx) : ctx(ctx){};
    void constrain_chains() { process_carries(); }
};

void constrain_chains(Context *ctx)
{
    log_info("Constraining chains...\n");
    ChainConstrainer(ctx).constrain_chains();
}

NEXTPNR_NAMESPACE_END
