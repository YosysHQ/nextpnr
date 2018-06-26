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

struct CellChain
{
    std::vector<CellInfo *> cells;
};

// Generic chain finder
template <typename F1, typename F2, typename F3>
std::vector<CellChain> find_chains(const Context *ctx, F1 cell_type_predicate, F2 get_previous, F3 get_next,
                                   size_t min_length = 2)
{
    std::set<IdString> chained;
    std::vector<CellChain> chains;
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
            CellChain chain;
            CellInfo *end = start;
            while (end != nullptr) {
                chain.cells.push_back(end);
                end = get_next(ctx, end);
            }
            if (chain.cells.size() >= min_length) {
                chains.push_back(chain);
                for (auto c : chain.cells)
                    chained.insert(c->name);
            }
        }
    }
    return chains;
}

static void get_chain_midpoint(const Context *ctx, const CellChain &chain, float &x, float &y)
{
    float total_x = 0, total_y = 0;
    int N = 0;
    for (auto cell : chain.cells) {
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
    void init_logic_cells()
    {
        for (auto bel : ctx->getBels()) {
            // Initialise the logic bels vector with unavailable invalid bels, dimensions [0..width][0..height[0..7]
            logic_bels.resize(ctx->chip_info->width + 1,
                              std::vector<std::vector<std::pair<BelId, bool>>>(
                                      ctx->chip_info->height + 1,
                                      std::vector<std::pair<BelId, bool>>(8, std::make_pair(BelId(), true))));
            if (ctx->getBelType(bel) == TYPE_ICESTORM_LC) {
                // Using the non-standard API here to get (x, y, z) rather than just (x, y)
                auto bi = ctx->chip_info->bel_data[bel.index];
                int x = bi.x, y = bi.y, z = bi.z;
                IdString cell = ctx->getBoundBelCell(bel);
                if (cell != IdString() && ctx->cells.at(cell)->belStrength >= STRENGTH_FIXED)
                    logic_bels.at(x).at(y).at(z) = std::make_pair(bel, true);
                else
                    logic_bels.at(x).at(y).at(z) = std::make_pair(bel, false);
            }
        }
    }

    bool legalise_carries()
    {
        std::vector<CellChain> carry_chains = find_chains(
                ctx, is_lc,
                [](const Context *ctx, const CellInfo *cell) {
                    return net_driven_by(ctx, cell->ports.at(ctx->id("CIN")).net, is_lc, ctx->id("COUT"));
                },
                [](const Context *ctx, const CellInfo *cell) {
                    return net_only_drives(ctx, cell->ports.at(ctx->id("COUT")).net, is_lc, ctx->id("CIN"), false);
                });
        int width = ctx->chip_info->width, height = ctx->chip_info->height;
        for (auto &base_chain : carry_chains) {
            std::vector<CellChain> split_chains = split_carry_chain(base_chain);
            for (auto &chain : split_chains) {
                float mid_x, mid_y;
                get_chain_midpoint(ctx, chain, mid_x, mid_y);
                float base_x = mid_x, base_y = mid_y - (chain.cells.size() / 16.0f);
                // Find Bel meeting requirements closest to the target base
            }
        }
        return true;
    }

    // Find Bel closest to a location, meeting chain requirements
    BelId find_closest_bel(float x, float y, int chain_size)
    {
        // TODO
        return BelId();
    }

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
            bool split_chain = (!ctx->logicCellsCompatible(tile)) || (int(chains.back().cells.size()) > max_length);
            if (split_chain) {
                CellInfo *passout = make_carry_pass_out(cell->ports.at(ctx->id("COUT")));
                tile.pop_back();
                chains.back().cells.back() = passout;
                start_of_chain = true;
            } else {
                NetInfo *carry_net = cell->ports.at(ctx->id("COUT")).net;
                if (carry_net != nullptr && carry_net->users.size() > 1) {
                    CellInfo *passout = make_carry_pass_out(cell->ports.at(ctx->id("COUT")));
                    chains.back().cells.push_back(passout);
                    tile.push_back(passout);
                }
                ++curr_cell;
            }
        }
        return chains;
    }

    // Insert a logic cell to legalise a COUT->fabric connection
    CellInfo *make_carry_pass_out(PortInfo &cout_port)
    {
        assert(cout_port.net != nullptr);
        std::unique_ptr<CellInfo> lc = create_ice_cell(ctx, ctx->id("ICESTORM_LC"));
        lc->params[ctx->id("LUT_INIT")] = "65280"; // 0xff00: O = I3
        lc->params[ctx->id("CARRY_ENABLE")] = "1";
        lc->ports.at(ctx->id("O")).net = cout_port.net;
        NetInfo *co_i3_net = new NetInfo();
        co_i3_net->name = ctx->id(lc->name.str(ctx) + "$I3");
        co_i3_net->driver = cout_port.net->driver;
        PortRef i3_r;
        i3_r.port = ctx->id("I3");
        i3_r.cell = lc.get();
        co_i3_net->users.push_back(i3_r);
        PortRef o_r;
        o_r.port = ctx->id("O");
        o_r.cell = lc.get();
        cout_port.net->driver = o_r;
        lc->ports.at(ctx->id("I3")).net = co_i3_net;
        // I1=1 feeds carry up the chain, so no need to actually break the chain
        lc->ports.at(ctx->id("I1")).net = ctx->nets.at(ctx->id("$PACKER_VCC_NET")).get();
        PortRef i1_r;
        i1_r.port = ctx->id("I1");
        i1_r.cell = lc.get();
        ctx->nets.at(ctx->id("$PACKER_VCC_NET"))->users.push_back(i1_r);
        IdString name = lc->name;
        ctx->cells[lc->name] = std::move(lc);
        createdCells.insert(name);
        return ctx->cells[name].get();
    }

    // Insert a logic cell to legalise a CIN->fabric connection
    CellInfo *make_carry_feed_in(CellInfo *cin_cell, PortInfo &cin_port)
    {
        assert(cin_port.net != nullptr);
        std::unique_ptr<CellInfo> lc = create_ice_cell(ctx, ctx->id("ICESTORM_LC"));
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

        IdString name = lc->name;
        ctx->cells[lc->name] = std::move(lc);
        createdCells.insert(name);
        return ctx->cells[name].get();
    }

    Context *ctx;
    std::unordered_set<IdString> rippedCells;
    std::unordered_set<IdString> createdCells;
    // Go from X and Y position to logic cells, setting occupied to true if a Bel is unavailable
    std::vector<std::vector<std::vector<std::pair<BelId, bool>>>> logic_bels;
};

bool legalise_design(Context *ctx)
{
    PlacementLegaliser lg(ctx);
    return lg.legalise();
}

NEXTPNR_NAMESPACE_END
