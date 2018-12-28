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
            if (tile.size() >= 8) {
                tile.clear();
            }
            if (start_of_chain) {
                tile.clear();
                chains.emplace_back();
                start_of_chain = false;
                if (cell->ports.at(ctx->id("CIN")).net) {
                    // CIN is not constant and not part of a chain. Must feed in from fabric
                    CellInfo *feedin = make_carry_feed_in(cell, cell->ports.at(ctx->id("CIN")));
                    chains.back().cells.push_back(feedin);
                    tile.push_back(feedin);
                }
            }
            tile.push_back(cell);
            chains.back().cells.push_back(cell);
            bool split_chain = (!ctx->logicCellsCompatible(tile.data(), tile.size())) ||
                               (int(chains.back().cells.size()) > max_length);
            if (split_chain) {
                CellInfo *passout = make_carry_pass_out((*(curr_cell - 1))->ports.at(ctx->id("COUT")));
                tile.pop_back();
                chains.back().cells.back() = passout;
                start_of_chain = true;
            } else {
                NetInfo *carry_net = cell->ports.at(ctx->id("COUT")).net;
                bool at_end = (curr_cell == carryc.cells.end() - 1);
                if (carry_net != nullptr && (carry_net->users.size() > 1 || at_end)) {
                    if (carry_net->users.size() > 2 ||
                        (net_only_drives(ctx, carry_net, is_lc, ctx->id("I3"), false) !=
                         net_only_drives(ctx, carry_net, is_lc, ctx->id("CIN"), false)) ||
                        (at_end && !net_only_drives(ctx, carry_net, is_lc, ctx->id("I3"), true))) {
                        CellInfo *passout = make_carry_pass_out(cell->ports.at(ctx->id("COUT")),
                                                                at_end ? nullptr : *(curr_cell + 1));
                        chains.back().cells.push_back(passout);
                        tile.push_back(passout);
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
        std::unique_ptr<CellInfo> lc = create_ice_cell(ctx, ctx->id("ICESTORM_LC"));
        lc->params[ctx->id("LUT_INIT")] = "65280"; // 0xff00: O = I3
        lc->params[ctx->id("CARRY_ENABLE")] = "1";
        lc->ports.at(id_O).net = cout_port.net;
        std::unique_ptr<NetInfo> co_i3_net(new NetInfo());
        co_i3_net->name = ctx->id(lc->name.str(ctx) + "$I3");
        co_i3_net->driver = cout_port.net->driver;
        PortRef i3_r;
        i3_r.port = id_I3;
        i3_r.cell = lc.get();
        co_i3_net->users.push_back(i3_r);
        PortRef o_r;
        o_r.port = id_O;
        o_r.cell = lc.get();
        cout_port.net->driver = o_r;
        lc->ports.at(id_I3).net = co_i3_net.get();
        cout_port.net = co_i3_net.get();

        IdString co_i3_name = co_i3_net->name;
        NPNR_ASSERT(ctx->nets.find(co_i3_name) == ctx->nets.end());
        ctx->nets[co_i3_name] = std::move(co_i3_net);

        // If COUT also connects to a CIN; preserve the carry chain
        if (cin_cell) {
            std::unique_ptr<NetInfo> co_cin_net(new NetInfo());
            co_cin_net->name = ctx->id(lc->name.str(ctx) + "$COUT");

            // Connect I1 to 1 to preserve carry chain
            NetInfo *vcc = ctx->nets.at(ctx->id("$PACKER_VCC_NET")).get();
            lc->ports.at(id_I1).net = vcc;
            PortRef i1_r;
            i1_r.port = id_I1;
            i1_r.cell = lc.get();
            vcc->users.push_back(i1_r);

            // Connect co_cin_net to the COUT of the LC
            PortRef co_r;
            co_r.port = id_COUT;
            co_r.cell = lc.get();
            co_cin_net->driver = co_r;
            lc->ports.at(id_COUT).net = co_cin_net.get();

            // Find the user corresponding to the next CIN
            int replaced_ports = 0;
            if (ctx->debug)
                log_info("cell: %s\n", cin_cell->name.c_str(ctx));
            for (auto port : {id_CIN, id_I3}) {
                auto &usr = lc->ports.at(id_O).net->users;
                if (ctx->debug)
                    for (auto user : usr)
                        log_info("%s.%s\n", user.cell->name.c_str(ctx), user.port.c_str(ctx));
                auto fnd_user = std::find_if(usr.begin(), usr.end(),
                                             [&](const PortRef &pr) { return pr.cell == cin_cell && pr.port == port; });
                if (fnd_user != usr.end()) {
                    co_cin_net->users.push_back(*fnd_user);
                    usr.erase(fnd_user);
                    cin_cell->ports.at(port).net = co_cin_net.get();
                    ++replaced_ports;
                }
            }
            NPNR_ASSERT(replaced_ports > 0);
            IdString co_cin_name = co_cin_net->name;
            NPNR_ASSERT(ctx->nets.find(co_cin_name) == ctx->nets.end());
            ctx->nets[co_cin_name] = std::move(co_cin_net);
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
        std::unique_ptr<CellInfo> lc = create_ice_cell(ctx, ctx->id("ICESTORM_LC"));
        lc->params[ctx->id("CARRY_ENABLE")] = "1";
        lc->params[ctx->id("CIN_CONST")] = "1";
        lc->params[ctx->id("CIN_SET")] = "1";
        lc->ports.at(ctx->id("I1")).net = cin_port.net;
        cin_port.net->users.erase(std::remove_if(cin_port.net->users.begin(), cin_port.net->users.end(),
                                                 [cin_cell, cin_port](const PortRef &usr) {
                                                     return usr.cell == cin_cell && usr.port == cin_port.name;
                                                 }));

        PortRef i1_ref;
        i1_ref.cell = lc.get();
        i1_ref.port = ctx->id("I1");
        lc->ports.at(ctx->id("I1")).net->users.push_back(i1_ref);

        std::unique_ptr<NetInfo> out_net(new NetInfo());
        out_net->name = ctx->id(lc->name.str(ctx) + "$O");

        PortRef drv_ref;
        drv_ref.port = ctx->id("COUT");
        drv_ref.cell = lc.get();
        out_net->driver = drv_ref;
        lc->ports.at(ctx->id("COUT")).net = out_net.get();

        PortRef usr_ref;
        usr_ref.port = cin_port.name;
        usr_ref.cell = cin_cell;
        out_net->users.push_back(usr_ref);
        cin_cell->ports.at(cin_port.name).net = out_net.get();

        IdString out_net_name = out_net->name;
        NPNR_ASSERT(ctx->nets.find(out_net_name) == ctx->nets.end());
        ctx->nets[out_net_name] = std::move(out_net);

        IdString name = lc->name;
        ctx->assignCellInfo(lc.get());
        ctx->cells[lc->name] = std::move(lc);
        return ctx->cells[name].get();
    }

    void process_carries()
    {
        std::vector<CellChain> carry_chains = find_chains(
                ctx, [](const Context *ctx, const CellInfo *cell) { return is_lc(ctx, cell); },
                [](const Context *ctx, const

                   CellInfo *cell) {
                    CellInfo *carry_prev =
                            net_driven_by(ctx, cell->ports.at(ctx->id("CIN")).net, is_lc, ctx->id("COUT"));
                    if (carry_prev != nullptr)
                        return carry_prev;
                    CellInfo *i3_prev = net_driven_by(ctx, cell->ports.at(ctx->id("I3")).net, is_lc, ctx->id("COUT"));
                    if (i3_prev != nullptr)
                        return i3_prev;
                    return (CellInfo *)nullptr;
                },
                [](const Context *ctx, const CellInfo *cell) {
                    CellInfo *carry_next =
                            net_only_drives(ctx, cell->ports.at(ctx->id("COUT")).net, is_lc, ctx->id("CIN"), false);
                    if (carry_next != nullptr)
                        return carry_next;
                    CellInfo *i3_next =
                            net_only_drives(ctx, cell->ports.at(ctx->id("COUT")).net, is_lc, ctx->id("I3"), false);
                    if (i3_next != nullptr)
                        return i3_next;
                    return (CellInfo *)nullptr;
                });
        std::unordered_set<IdString> chained;
        for (auto &base_chain : carry_chains) {
            for (auto c : base_chain.cells)
                chained.insert(c->name);
        }
        // Any cells not in chains, but with carry enabled, must also be put in a single-carry chain
        // for correct processing
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (chained.find(cell.first) == chained.end() && is_lc(ctx, ci) &&
                bool_or_default(ci->params, ctx->id("CARRY_ENABLE"))) {
                CellChain sChain;
                sChain.cells.push_back(ci);
                chained.insert(cell.first);
                carry_chains.push_back(sChain);
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
            for (int i = 1; i < int(chain.cells.size()); i++) {
                chain.cells.at(i)->constr_x = 0;
                chain.cells.at(i)->constr_y = (i / 8);
                chain.cells.at(i)->constr_z = i % 8;
                chain.cells.at(i)->constr_abs_z = true;
                chain.cells.at(i)->constr_parent = chain.cells.at(0);
                chain.cells.at(0)->constr_children.push_back(chain.cells.at(i));
            }
        }
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
