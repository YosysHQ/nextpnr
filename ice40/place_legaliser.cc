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
#include "place_common.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

struct CellChain
{
    std::vector<CellInfo *> cells;
    float mid_x = 0, mid_y = 0;
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
        Loc bel_loc = ctx->getBelLocation(cell->bel);
        total_x += bel_loc.x;
        total_y += bel_loc.y;
        N++;
    }
    NPNR_ASSERT(N > 0);
    x = total_x / N;
    y = total_y / N;
}

static int get_cell_evilness(const Context *ctx, const CellInfo *cell)
{
    // This returns how "evil" a logic cell is, and thus how likely it is to be ripped up
    // during logic tile legalisation
    int score = 0;
    if (get_net_or_empty(cell, ctx->id("I0")))
        ++score;
    if (get_net_or_empty(cell, ctx->id("I1")))
        ++score;
    if (get_net_or_empty(cell, ctx->id("I2")))
        ++score;
    if (get_net_or_empty(cell, ctx->id("I3")))
        ++score;
    if (bool_or_default(cell->params, ctx->id("DFF_ENABLE"))) {
        const NetInfo *cen = get_net_or_empty(cell, ctx->id("CEN")), *sr = get_net_or_empty(cell, ctx->id("SR"));
        if (cen)
            score += 10;
        if (sr)
            score += 10;
        if (bool_or_default(cell->params, ctx->id("NEG_CLK")))
            score += 5;
    }
    return score;
}

class PlacementLegaliser
{
  public:
    PlacementLegaliser(Context *ctx) : ctx(ctx){};

    bool legalise()
    {
        log_info("Legalising design..\n");
        init_logic_cells();
        bool legalised_carries = legalise_carries();
        if (!legalised_carries && !ctx->force)
            return false;
        legalise_others();
        legalise_logic_tiles();
        bool replaced_cells = replace_cells();
        ctx->assignArchInfo();
        return legalised_carries && replaced_cells;
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
                    logic_bels.at(x).at(y).at(z) = std::make_pair(bel, true); // locked out of use
                else
                    logic_bels.at(x).at(y).at(z) = std::make_pair(bel, false); // available
            }
        }
    }

    bool legalise_carries()
    {
        std::vector<CellChain> carry_chains =
                find_chains(ctx, [](const Context *ctx, const CellInfo *cell) { return is_lc(ctx, cell); },
                            [](const Context *ctx, const

                               CellInfo *cell) {
                                CellInfo *carry_prev =
                                        net_driven_by(ctx, cell->ports.at(ctx->id("CIN")).net, is_lc, ctx->id("COUT"));
                                if (carry_prev != nullptr)
                                    return carry_prev;
                                /*CellInfo *i3_prev = net_driven_by(ctx, cell->ports.at(ctx->id("I3")).net, is_lc,
                                ctx->id("COUT")); if (i3_prev != nullptr) return i3_prev;*/
                                return (CellInfo *)nullptr;
                            },
                            [](const Context *ctx, const CellInfo *cell) {
                                CellInfo *carry_next = net_only_drives(ctx, cell->ports.at(ctx->id("COUT")).net, is_lc,
                                                                       ctx->id("CIN"), false);
                                if (carry_next != nullptr)
                                    return carry_next;
                                /*CellInfo *i3_next =
                                        net_only_drives(ctx, cell->ports.at(ctx->id("COUT")).net, is_lc, ctx->id("I3"),
                                false); if (i3_next != nullptr) return i3_next;*/
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
        bool success = true;
        // Find midpoints for all chains, before we start tearing them up
        std::vector<CellChain> all_chains;
        for (auto &base_chain : carry_chains) {
            if (ctx->verbose) {
                log_info("Found carry chain: \n");
                for (auto entry : base_chain.cells)
                    log_info("     %s\n", entry->name.c_str(ctx));
                log_info("\n");
            }
            std::vector<CellChain> split_chains = split_carry_chain(base_chain);
            for (auto &chain : split_chains) {
                get_chain_midpoint(ctx, chain, chain.mid_x, chain.mid_y);
                all_chains.push_back(chain);
            }
        }
        // Actual chain placement
        for (auto &chain : all_chains) {
            if (ctx->verbose)
                log_info("Placing carry chain starting at '%s'\n", chain.cells.front()->name.c_str(ctx));
            float base_x = chain.mid_x, base_y = chain.mid_y - (chain.cells.size() / 16.0f);
            // Find Bel meeting requirements closest to the target base, returning location as <x, y, z>
            auto chain_origin_bel = find_closest_bel(base_x, base_y, chain);
            int place_x = std::get<0>(chain_origin_bel), place_y = std::get<1>(chain_origin_bel),
                place_z = std::get<2>(chain_origin_bel);
            if (place_x == -1) {
                if (ctx->force) {
                    log_warning("failed to place carry chain, starting with cell '%s', length %d\n",
                                chain.cells.front()->name.c_str(ctx), int(chain.cells.size()));
                    success = false;
                    continue;
                } else {
                    log_error("failed to place carry chain, starting with cell '%s', length %d\n",
                              chain.cells.front()->name.c_str(ctx), int(chain.cells.size()));
                }
            }
            // Place carry chain
            for (int i = 0; i < int(chain.cells.size()); i++) {
                int target_z = place_y * 8 + place_z + i;
                place_lc(chain.cells.at(i), place_x, target_z / 8, target_z % 8);
                if (ctx->verbose)
                    log_info("    Cell '%s' placed at (%d, %d, %d)\n", chain.cells.at(i)->name.c_str(ctx), place_x,
                             target_z / 8, target_z % 8);
            }
        }
        return success;
    }

    // Find Bel closest to a location, meeting chain requirements
    std::tuple<int, int, int> find_closest_bel(float target_x, float target_y, CellChain &chain)
    {
        std::tuple<int, int, int> best_origin = std::make_tuple(-1, -1, -1);
        wirelen_t best_metric = std::numeric_limits<wirelen_t>::max();
        int width = ctx->chip_info->width, height = ctx->chip_info->height;
        // Slow, should radiate outwards from target position - TODO
        int chain_size = int(chain.cells.size());
        for (int x = 1; x < width; x++) {
            for (int y = 1; y < (height - (chain_size / 8)); y++) {
                bool valid = true;
                wirelen_t wirelen = 0;
                for (int k = 0; k < chain_size; k++) {
                    auto &lb = logic_bels.at(x).at(y + k / 8).at(k % 8);
                    if (lb.second) {
                        valid = false;
                        break;
                    } else {
                        wirelen += get_cell_metric_at_bel(ctx, chain.cells.at(k), lb.first, MetricType::COST);
                    }
                }
                if (valid && wirelen < best_metric) {
                    best_metric = wirelen;
                    best_origin = std::make_tuple(x, y, 0);
                }
            }
        }
        return best_origin;
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
                bool at_end = (curr_cell == carryc.cells.end() - 1);
                if (carry_net != nullptr && (carry_net->users.size() > 1 || at_end)) {
                    if (carry_net->users.size() > 2 ||
                        (net_only_drives(ctx, carry_net, is_lc, ctx->id("I3"), false) !=
                         net_only_drives(ctx, carry_net, is_lc, ctx->id("CIN"), false)) ||
                        (at_end && !net_only_drives(ctx, carry_net, is_lc, ctx->id("I3"), true))) {
                        CellInfo *passout = make_carry_pass_out(cell->ports.at(ctx->id("COUT")));
                        chains.back().cells.push_back(passout);
                        tile.push_back(passout);
                        start_of_chain = true;
                    }
                }
                ++curr_cell;
            }
        }
        return chains;
    }

    // Place a logic cell at a given grid location, handling rip-up etc
    void place_lc(CellInfo *cell, int x, int y, int z)
    {
        auto &loc = logic_bels.at(x).at(y).at(z);
        NPNR_ASSERT(!loc.second);
        BelId bel = loc.first;
        // Check if there is a cell presently at the location, which we will need to rip up
        IdString existing = ctx->getBoundBelCell(bel);
        if (existing != IdString()) {
            // TODO: keep track of the previous position of the ripped up cell, as a hint
            rippedCells.insert(existing);
            ctx->unbindBel(bel);
        }
        if (cell->bel != BelId()) {
            ctx->unbindBel(cell->bel);
        }
        ctx->bindBel(bel, cell->name, STRENGTH_LOCKED);
        rippedCells.erase(cell->name); // If cell was ripped up previously, no need to re-place
        loc.second = true;             // Bel is now unavailable for further use
    }

    // Insert a logic cell to legalise a COUT->fabric connection
    CellInfo *make_carry_pass_out(PortInfo &cout_port)
    {
        NPNR_ASSERT(cout_port.net != nullptr);
        std::unique_ptr<CellInfo> lc = create_ice_cell(ctx, ctx->id("ICESTORM_LC"));
        lc->params[ctx->id("LUT_INIT")] = "65280"; // 0xff00: O = I3
        lc->params[ctx->id("CARRY_ENABLE")] = "1";
        lc->ports.at(ctx->id("O")).net = cout_port.net;
        std::unique_ptr<NetInfo> co_i3_net(new NetInfo());
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
        lc->ports.at(ctx->id("I3")).net = co_i3_net.get();
        cout_port.net = co_i3_net.get();

        IdString co_i3_name = co_i3_net->name;
        NPNR_ASSERT(ctx->nets.find(co_i3_name) == ctx->nets.end());
        ctx->nets[co_i3_name] = std::move(co_i3_net);
        IdString name = lc->name;
        ctx->assignCellInfo(lc.get());
        ctx->cells[lc->name] = std::move(lc);
        createdCells.insert(name);
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
        createdCells.insert(name);
        return ctx->cells[name].get();
    }

    // Legalise logic tiles
    void legalise_logic_tiles()
    {
        int width = ctx->chip_info->width, height = ctx->chip_info->height;
        for (int x = 1; x < width; x++) {
            for (int y = 1; y < height; y++) {
                BelId tileBel = logic_bels.at(x).at(y).at(0).first;
                if (tileBel != BelId()) {
                    bool changed = true;
                    while (!ctx->isBelLocationValid(tileBel) && changed) {
                        changed = false;
                        int max_score = 0;
                        CellInfo *target = nullptr;
                        for (int z = 0; z < 8; z++) {
                            BelId bel = logic_bels.at(x).at(y).at(z).first;
                            IdString cell = ctx->getBoundBelCell(bel);
                            if (cell != IdString()) {
                                CellInfo *ci = ctx->cells.at(cell).get();
                                if (ci->belStrength >= STRENGTH_STRONG)
                                    continue;
                                int score = get_cell_evilness(ctx, ci);
                                if (score > max_score) {
                                    max_score = score;
                                    target = ci;
                                }
                            }
                        }
                        if (target != nullptr) {
                            ctx->unbindBel(target->bel);
                            rippedCells.insert(target->name);
                            changed = true;
                        }
                    }
                }
            }
        }
    }

    // Legalise other tiles
    void legalise_others()
    {
        std::vector<CellInfo *> legalised_others;
        for (auto cell : sorted(ctx->cells)) {
            CellInfo *ci = cell.second;
            if (!is_lc(ctx, ci)) {
                if (ci->belStrength < STRENGTH_STRONG && ci->bel != BelId()) {
                    if (!ctx->isValidBelForCell(ci, ci->bel)) {
                        place_single_cell(ctx, ci, true);
                    }
                    legalised_others.push_back(ci);
                }
            }
        }
        // Lock all these cells now, we don't need to move them in SA (don't lock during legalise placement
        // so legalise placement can rip up in case of gbuf contention etc)
        for (auto cell : legalised_others)
            cell->belStrength = STRENGTH_STRONG;
    }

    // Replace ripped-up cells
    bool replace_cells()
    {
        bool success = true;
        for (auto cell : sorted(rippedCells)) {
            CellInfo *ci = ctx->cells.at(cell).get();
            bool placed = place_single_cell(ctx, ci, true);
            if (!placed) {
                if (ctx->force) {
                    log_warning("failed to place cell '%s' of type '%s'\n", cell.c_str(ctx), ci->type.c_str(ctx));
                    success = false;
                } else {
                    log_error("failed to place cell '%s' of type '%s'\n", cell.c_str(ctx), ci->type.c_str(ctx));
                }
            }
        }
        return success;
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
