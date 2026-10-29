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
#include <array>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
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
        // CIN is connected to COUT in hardware, make the connection explicit in the netlist
        lc->connectPort(id_CIN, co_i3_net);

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
    ChainConstrainer(Context *ctx) : ctx(ctx) {};
    void constrain_chains() { process_carries(); }
};

void constrain_chains(Context *ctx)
{
    log_info("Constraining chains...\n");
    ChainConstrainer(ctx).constrain_chains();
}

// Tile packing: group the logic cells into clusters of one tile each, every cluster with one control set (clock,
// enable, set/reset, clock polarity) and at most 32 local inputs, chosen by shared nets. Free slots of carry chain
// tiles are filled the same way. The placers then move legal tiles; placing cells one by one, they can fail to find a
// legal tile for the last cells of a design near full utilisation.
struct TilePacker
{
    Context *ctx;
    TilePacker(Context *ctx) : ctx(ctx) {};

    struct Bin
    {
        std::array<CellInfo *, 8> cells{};
        CellInfo *chain_root = nullptr; // carry chain tile: its cells stay, free slots are filled
        int dy = 0;
        bool has_ff = false, neg = false;
        const NetInfo *clk = nullptr, *cen = nullptr, *sr = nullptr;
        int locals = 0, used = 0;
        std::unordered_map<const NetInfo *, int> nets;
    };
    std::vector<Bin> bins;
    TilePacking packing;
    std::unordered_map<const CellInfo *, int> bin_of;
    std::unordered_map<const CellInfo *, std::vector<const NetInfo *>> cell_nets;

    static int ctrl_locals(const CellInfo *ci)
    {
        int n = 0;
        for (auto net : {ci->lcInfo.clk, ci->lcInfo.cen, ci->lcInfo.sr})
            if (net != nullptr && !net->is_global)
                n++;
        return n;
    }

    const std::vector<const NetInfo *> &nets_of(const CellInfo *ci)
    {
        auto found = cell_nets.find(ci);
        if (found != cell_nets.end())
            return found->second;
        auto &nets = cell_nets[ci];
        for (auto port : {id_I0, id_I1, id_I2, id_I3, id_O}) {
            const NetInfo *net = ci->getPort(port);
            // global and high fanout nets say little about which cells belong together
            if (net != nullptr && !net->is_global && net->users.entries() <= 32)
                nets.push_back(net);
        }
        return nets;
    }

    bool fits(const Bin &b, const CellInfo *ci) const
    {
        if (b.used >= 8)
            return false;
        int locals = b.locals + ci->lcInfo.inputCount;
        if (ci->lcInfo.dffEnable) {
            if (b.has_ff) {
                if (ci->lcInfo.clk != b.clk || ci->lcInfo.cen != b.cen || ci->lcInfo.sr != b.sr ||
                    ci->lcInfo.negClk != b.neg)
                    return false;
            } else {
                locals += ctrl_locals(ci);
            }
        }
        return locals <= 32;
    }

    void add(int bi, CellInfo *ci, int slot)
    {
        Bin &b = bins.at(bi);
        NPNR_ASSERT(b.cells.at(slot) == nullptr);
        b.cells.at(slot) = ci;
        b.used++;
        b.locals += ci->lcInfo.inputCount;
        if (ci->lcInfo.dffEnable && !b.has_ff) {
            b.has_ff = true;
            b.clk = ci->lcInfo.clk;
            b.cen = ci->lcInfo.cen;
            b.sr = ci->lcInfo.sr;
            b.neg = ci->lcInfo.negClk;
            b.locals += ctrl_locals(ci);
        }
        for (auto net : nets_of(ci))
            b.nets[net]++;
        bin_of[ci] = bi;
    }

    void add(int bi, CellInfo *ci)
    {
        Bin &b = bins.at(bi);
        for (int z = 0; z < 8; z++)
            if (b.cells.at(z) == nullptr) {
                add(bi, ci, z);
                return;
            }
        NPNR_ASSERT_FALSE("no free slot");
    }

    // the unassigned cell of the candidate list that shares the most nets with the bin and fits; nullptr if none
    // shares a net
    CellInfo *best_connected(int bi, const std::unordered_set<CellInfo *> &candidates)
    {
        const Bin &b = bins.at(bi);
        CellInfo *best = nullptr;
        int best_score = 0;
        for (auto &net_count : b.nets) {
            const NetInfo *net = net_count.first;
            auto consider = [&](CellInfo *ci) {
                if (ci == nullptr || !candidates.count(ci) || !fits(b, ci))
                    return;
                int score = 0;
                for (auto n : nets_of(ci)) {
                    auto f = b.nets.find(n);
                    if (f != b.nets.end())
                        score += f->second;
                }
                if (score > best_score || (score == best_score && best != nullptr && ci->name < best->name)) {
                    best = ci;
                    best_score = score;
                }
            };
            consider(net->driver.cell);
            for (auto &usr : net->users)
                consider(usr.cell);
        }
        return best;
    }

    // fill the bin from the candidates: connected cells first, then (if fill_any) any cell that fits
    void fill(int bi, std::unordered_set<CellInfo *> &candidates, std::vector<CellInfo *> &order, size_t &next, bool fill_any)
    {
        while (bins.at(bi).used < 8) {
            CellInfo *ci = best_connected(bi, candidates);
            if (ci == nullptr && fill_any) {
                for (; next < order.size(); next++)
                    if (candidates.count(order.at(next)) && fits(bins.at(bi), order.at(next)))
                        break;
                if (next < order.size())
                    ci = order.at(next);
            }
            if (ci == nullptr)
                return;
            candidates.erase(ci);
            add(bi, ci);
        }
    }

    void run()
    {
        int tiles = 0;
        {
            std::set<std::pair<int, int>> lc_tiles;
            for (auto bel : ctx->getBels())
                if (ctx->getBelType(bel) == id_ICESTORM_LC) {
                    Loc l = ctx->getBelLocation(bel);
                    lc_tiles.insert(std::make_pair(l.x, l.y));
                }
            tiles = int(lc_tiles.size());
        }
        // carry chain tiles
        int chain_bins = 0;
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type != id_ICESTORM_LC || ci->cluster != ci->name)
                continue;
            packing.chains.emplace_back(ci, ci->constr_children.size());
            std::map<int, int> bin_at;
            auto place = [&](CellInfo *c, int dy, int z) {
                if (!bin_at.count(dy)) {
                    bin_at[dy] = int(bins.size());
                    bins.emplace_back();
                    bins.back().chain_root = ci;
                    bins.back().dy = dy;
                    chain_bins++;
                }
                add(bin_at.at(dy), c, z);
            };
            place(ci, 0, ci->constr_z);
            for (auto child : ci->constr_children)
                place(child, child->constr_y, child->constr_z);
        }
        // the free cells, flip-flops by control set
        std::vector<CellInfo *> comb;
        std::vector<std::vector<CellInfo *>> ff_sets;
        std::map<std::tuple<const NetInfo *, const NetInfo *, const NetInfo *, bool>, int> set_index;
        for (auto &cell : ctx->cells) {
            CellInfo *ci = cell.second.get();
            if (ci->type != id_ICESTORM_LC || ci->cluster != ClusterId() || ci->attrs.count(id_BEL))
                continue;
            // constant drivers ($PACKER_GND/VCC) are placed on their own by the placers
            if (!ci->lcInfo.dffEnable && ci->lcInfo.inputCount == 0)
                continue;
            if (!ci->lcInfo.dffEnable) {
                comb.push_back(ci);
                continue;
            }
            auto key = std::make_tuple(ci->lcInfo.clk, ci->lcInfo.cen, ci->lcInfo.sr, bool(ci->lcInfo.negClk));
            if (!set_index.count(key)) {
                set_index[key] = int(ff_sets.size());
                ff_sets.emplace_back();
            }
            ff_sets.at(set_index.at(key)).push_back(ci);
        }
        int packed = 0;
        for (auto &set : ff_sets) {
            std::unordered_set<CellInfo *> candidates(set.begin(), set.end());
            size_t next = 0;
            // chain tiles with this control set first, then new tiles
            for (int bi = 0; bi < chain_bins; bi++) {
                const Bin &b = bins.at(bi);
                if (b.has_ff && b.clk == set.front()->lcInfo.clk && b.cen == set.front()->lcInfo.cen &&
                    b.sr == set.front()->lcInfo.sr && b.neg == bool(set.front()->lcInfo.negClk))
                    fill(bi, candidates, set, next, true);
            }
            while (!candidates.empty()) {
                for (; next < set.size(); next++)
                    if (candidates.count(set.at(next)))
                        break;
                NPNR_ASSERT(next < set.size());
                CellInfo *seed = set.at(next);
                candidates.erase(seed);
                bins.emplace_back();
                add(int(bins.size()) - 1, seed);
                fill(int(bins.size()) - 1, candidates, set, next, true);
            }
            packed += int(set.size());
        }
        // combinational cells: into free slots of tiles they connect to, the rest into new tiles
        {
            std::unordered_set<CellInfo *> candidates(comb.begin(), comb.end());
            size_t next = 0;
            int nbins = int(bins.size());
            for (int bi = 0; bi < nbins; bi++)
                fill(bi, candidates, comb, next, false);
            next = 0;
            // without enough free tiles for the rest (and a few spare, which the placers need to legalise), use the free
            // slots of the other tiles too
            int spare = std::max(2, tiles / 50);
            if (nbins + (int(candidates.size()) + 7) / 8 > tiles - spare)
                for (int bi = 0; bi < nbins && !candidates.empty(); bi++)
                    fill(bi, candidates, comb, next, true);
            next = 0;
            while (!candidates.empty()) {
                for (; next < comb.size(); next++)
                    if (candidates.count(comb.at(next)))
                        break;
                NPNR_ASSERT(next < comb.size());
                CellInfo *seed = comb.at(next);
                candidates.erase(seed);
                bins.emplace_back();
                add(int(bins.size()) - 1, seed);
                fill(int(bins.size()) - 1, candidates, comb, next, true);
            }
            packed += int(comb.size());
        }
        // clusters
        int clusters = 0;
        for (auto &b : bins) {
            if (b.chain_root != nullptr) {
                for (int z = 0; z < 8; z++) {
                    CellInfo *ci = b.cells.at(z);
                    if (ci == nullptr || ci->cluster != ClusterId())
                        continue;
                    ci->cluster = b.chain_root->name;
                    ci->constr_x = 0;
                    ci->constr_y = b.dy;
                    ci->constr_z = z;
                    ci->constr_abs_z = true;
                    b.chain_root->constr_children.push_back(ci);
                    packing.cells.push_back(ci);
                }
                continue;
            }
            if (b.used == 0)
                continue;
            CellInfo *root = nullptr;
            for (int z = 0; z < 8; z++) {
                CellInfo *ci = b.cells.at(z);
                if (ci == nullptr)
                    continue;
                ci->constr_abs_z = true;
                ci->constr_z = z;
                packing.cells.push_back(ci);
                if (root == nullptr) {
                    root = ci;
                    ci->cluster = ci->name;
                } else {
                    ci->cluster = root->name;
                    ci->constr_x = 0;
                    ci->constr_y = 0;
                    root->constr_children.push_back(ci);
                }
            }
            clusters++;
        }
        int used_tiles = int(bins.size());
        log_info("Tile packing: %d LCs into %d tiles (%d with carry chains) of %d, %d control sets\n", packed,
                 used_tiles, chain_bins, tiles, int(ff_sets.size()));
        if (used_tiles > tiles)
            log_warning("Tile packing needs %d tiles, the device has %d logic tiles\n", used_tiles, tiles);
    }
};

TilePacking pack_tiles(Context *ctx)
{
    TilePacker packer(ctx);
    packer.run();
    return packer.packing;
}

void unpack_tiles(const TilePacking &packing)
{
    for (auto &chain : packing.chains)
        chain.first->constr_children.resize(chain.second);
    for (auto ci : packing.cells) {
        ci->cluster = ClusterId();
        ci->constr_x = ci->constr_y = ci->constr_z = 0;
        ci->constr_abs_z = false;
        ci->constr_children.clear();
    }
}

NEXTPNR_NAMESPACE_END
