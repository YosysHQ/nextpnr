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

#include "place_legaliser.h"
#include <algorithm>
#include <vector>
#include "cells.h"
#include "design_utils.h"
#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

// Generic chain finder
template <typename F1, typename F2, typename F3>
std::vector<std::vector<CellInfo *>> find_chains(const Context *ctx, F1 cell_type_predicate, F2 get_previous,
                                                 F3 get_next, size_t min_length = 2)
{
    std::set<IdString> chained;
    std::vector<std::vector<CellInfo *>> chains;
    for (auto cell : sorted(ctx->cells)) {
        if (chained.find(cell.first) != chained.end())
            continue;
        CellInfo *ci = cell.second;
        if (cell_type_predicate(ctx, ci)) {
            CellInfo *start = ci;
            CellInfo *prev_start = ci;
            while (prev_start != nullptr) {
                start = prev_start;
                prev_start = get_previous(ctx, start);
            }
            std::vector<CellInfo *> chain;
            CellInfo *end = start;
            while (end != nullptr) {
                chain.push_back(end);
                end = get_next(ctx, end);
            }
            if (chain.size() >= min_length) {
                chains.push_back(chain);
                for (auto c : chain)
                    chained.insert(c->name);
            }
        }
    }
    return chains;
}

static void get_chain_midpoint(const Context *ctx, const std::vector<CellInfo *> &chain, float &x, float &y)
{
    float total_x = 0, total_y = 0;
    int N = 0;
    for (auto cell : chain) {
        if (cell->bel == BelId())
            continue;
        int bel_x, bel_y;
        bool bel_gb;
        ctx->estimatePosition(cell->bel, bel_x, bel_y, bel_gb);
        total_x += bel_x;
        total_y += bel_y;
        N++;
    }
    assert(N > 0);
    x = total_x / N;
    y = total_y / N;
}

class PlacementLegaliser
{
  public:
    PlacementLegaliser(Context *ctx) : ctx(ctx){};
    bool legalise()
    {
        bool legalised_carries = legalise_carries();
        if (!legalised_carries && !ctx->force)
            return false;
        return legalised_carries;
    }

  private:
    bool legalise_carries()
    {
        std::vector<std::vector<CellInfo *>> carry_chains = find_chains(
                ctx, is_lc,
                [](const Context *ctx, const CellInfo *cell) {
                    return net_driven_by(ctx, cell->ports.at(ctx->id("CIN")).net, is_lc, ctx->id("COUT"));
                },
                [](const Context *ctx, const CellInfo *cell) {
                    return net_only_drives(ctx, cell->ports.at(ctx->id("COUT")).net, is_lc, ctx->id("CIN"), false);
                });
        // TODO
        return true;
    }

    CellInfo *make_carry_pass_out(PortInfo &cout_port)
    {
        assert(cout_port.net != nullptr);
        CellInfo *lc = create_ice_cell(ctx, ctx->id("ICESTORM_LC"));
        lc->params[ctx->id("LUT_INIT")] = "65280"; // 0xff00: O = I3
        lc->ports.at(ctx->id("O")).net = cout_port.net;
        NetInfo *co_i3_net = new NetInfo();
        co_i3_net->name = ctx->id(lc->name.str(ctx) + "$I3");
        co_i3_net->driver = cout_port.net->driver;
        PortRef i3_r;
        i3_r.port = ctx->id("I3");
        i3_r.cell = lc;
        co_i3_net->users.push_back(i3_r);
        PortRef o_r;
        o_r.port = ctx->id("O");
        o_r.cell = lc;
        cout_port.net->driver = o_r;
        lc->ports.at(ctx->id("I3")).net = co_i3_net;
        ctx->cells[lc->name] = lc;
        createdCells.insert(lc->name);
        return lc;
    }

    CellInfo *make_carry_feed_in(CellInfo *cin_cell, PortInfo &cin_port)
    {
        assert(cin_port.net != nullptr);
        CellInfo *lc = create_ice_cell(ctx, ctx->id("ICESTORM_LC"));
        lc->params[ctx->id("CARRY_ENABLE")] = "1";
        lc->params[ctx->id("CIN_CONST")] = "1";
        lc->params[ctx->id("CIN_SET")] = "1";
        lc->ports.at(ctx->id("I1")).net = cin_port.net;
        cin_port.net->users.erase(std::remove_if(cin_port.net->users.begin(), cin_port.net->users.end(),
                                                 [cin_cell, cin_port](const PortRef &usr) {
                                                     return usr.cell == cin_cell && usr.port == cin_port.name;
                                                 }));
        NetInfo *out_net = new NetInfo();
        out_net->name = ctx->id(lc->name.str(ctx) + "$O");

        ctx->cells[lc->name] = lc;
        createdCells.insert(lc->name);
        return lc;
    }

    Context *ctx;
    std::unordered_set<IdString> rippedCells;
    std::unordered_set<IdString> createdCells;
};

bool legalise_design(Context *ctx)
{
    PlacementLegaliser lg(ctx);
    return lg.legalise();
}

NEXTPNR_NAMESPACE_END
