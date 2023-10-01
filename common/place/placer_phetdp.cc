/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2023  Hannah Ravensloft <lofty@yosyshq.com>
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
 */

#include "placer_phetdp.h"
#include <algorithm>
#include <chrono>
#include <vector>
#include "hashlib.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

struct GridSpace {
    GridSpace(int x, int y) : x(x), y(y) {}
    GridSpace(Loc loc) : x(loc.x), y(loc.y) {}
    int x, y;
};

struct BinSpace {
    BinSpace(int x, int y) : x(x), y(y) {
        NPNR_ASSERT(x >= 0 && x < 12);
        NPNR_ASSERT(y >= 0 && y < 12);
    }
    BinSpace(Context *ctx, GridSpace grid) {
        x = grid.x * 12 / ctx->getGridDimX();
        y = grid.y * 12 / ctx->getGridDimY();
    }
    int x, y;
};

class Cluster {
public:
    Cluster(NetInfo* net, BinSpace bin) : nets{net}, bin{bin} {}

    size_t size() const {
        auto size = size_t{0};
        dict<IdString, int> type_count;

        for (const auto* net : nets) {
            auto result1 = type_count.insert({net->driver.cell->type, 1});
            if (!result1.second)
                result1.first->second++;
            for (const auto port : net->users) {
                auto cell = port.cell;
                auto result2 = type_count.insert({cell->type, 1});
                if (!result2.second)
                    result2.first->second++;
            }
        }

        // TODO: PFUMX, L6MX21, CCU2C, DP16KD, TRELLIS_DPR16X4
        return 10 * type_count.count(id_LUT4) +
            9 * type_count.count(id_TRELLIS_FF) +
            5 * type_count.count(id_MULT18X18D) +
            3 * type_count.count(id_DP16KD);
    }

    void insert_net(NetInfo* net) {
        nets.push_back(net);
    }

    BinSpace containing_bin() const {
        return bin;
    }

    template<typename F>
    void sort(F net_size) {
        std::sort(nets.begin(), nets.end(), [&](const NetInfo* a, const NetInfo* b) {
            return net_size(a) > net_size(b);
        });
    }

private:
    std::vector<NetInfo*> nets;
    BinSpace bin;
};

class GlobalBin {
public:
    GlobalBin(size_t capacity = 1250) : capacity{capacity}, conns{}, nets{} {}

    // The amount of available space in this bin.
    int whitespace() const {
        return int(capacity) - int(nets.size());
    }

    // The number of edges 
    // Confusingly, this term is e_uv in Formula (2), but also `c_x <- (n_i ∩ n_j)` in Formula (3).
    int edge_count(const NetInfo *candidate) const {
        auto edges = 0;
        auto result = conns.find(candidate->driver.cell->name);
        if (result != conns.end())
            edges += result->second;
        for (const auto port : candidate->users) {
            result = conns.find(port.cell->name);
            if (result != conns.end())
                edges += result->second;
        }
        return edges;
    }

    // Add a net to this bin.
    void insert_net(NetInfo* net) {
        nets.push_back(net);
        build_connectivity_for_net(net);
    }

    // Formula (3), which scores how connected this net is to the other nets in this bin.
    float gamma(const NetInfo* net) const {
        return float(1 + edge_count(net)) / float(1 + net->users.entries());
    }

    // Formula (2), which scores a net for this bin based on its connectivity or free space.
    // `(1 + edge_count(net))` is used to work around `edge_count(net) == 0` leading to whitespace
    // being ignored.
    float connectivity(const NetInfo *net) const {
        return gamma(net) * float(whitespace());
    }

    // Sort nets by their gamma score.
    void sort() {
        std::sort(nets.begin(), nets.end(), [&](const NetInfo* a, const NetInfo* b) {
            return gamma(a) > gamma(b);
        });
    }

    // Pop the lowest-gamma net from this bin.
    NetInfo* pop_least_connected() {
        if (nets.empty())
            return nullptr;
        auto net = nets.back();
        nets.pop_back();
        auto cell_name = net->driver.cell->name;
        auto result1 = conns.find(cell_name);
        if (result1 != conns.end())
            result1->second--;
        for (const auto port : net->users) {
            auto result2 = conns.find(port.cell->name);
            if (result2 != conns.end())
                result2->second--;
        }
        return net;
    }

    std::vector<Cluster> clusterise(BinSpace bin) const {
        auto v = std::vector<Cluster>{};
        auto remaining_nets = std::vector<NetInfo*>{nets};
        while (!remaining_nets.empty()) {
            // Find the biggest single-net cluster.
            std::sort(remaining_nets.begin(), remaining_nets.end(), [&](NetInfo* a, NetInfo* b) {
                return Cluster{a, BinSpace{0, 0}}.size() > Cluster{b, BinSpace{0, 0}}.size();
            });

            // Pop it.
            auto net = remaining_nets.back();
            auto cluster = Cluster{net, bin};
            remaining_nets.pop_back();

            auto ports = pool<IdString>{};
            auto port_pair = [&](PortRef port) {
                return port.cell->name;
            };
            ports.insert(port_pair(net->driver));
            for (auto port : net->users)
                ports.insert(port_pair(port));

            // Can we attach any nets to this cluster?
            bool found_something = true;
            while (found_something) {
                auto p = std::partition(remaining_nets.begin(), remaining_nets.end(), [&](NetInfo* candidate) {
                    bool in_cluster = ports.count(port_pair(candidate->driver)) != 0;
                    for (auto port : candidate->users)
                        in_cluster |= ports.count(port_pair(port)) != 0;
                    return !in_cluster;
                });
                found_something = p != remaining_nets.end();
                for (auto it = p; it != remaining_nets.end(); it++) {
                    cluster.insert_net(*it);
                    ports.insert(port_pair((*it)->driver));
                    for (auto port : (*it)->users)
                        ports.insert(port_pair(port));
                }
                remaining_nets.erase(p, remaining_nets.end());
            }

            v.push_back(cluster);
        }
        return v;
    }

private:
    // Incrementally update conn when a new net is added.
    void build_connectivity_for_net(const NetInfo *net) {
        auto cell_name = net->driver.cell->name;
        auto result1 = conns.insert({cell_name, 1});
        if (!result1.second)
            result1.first->second++;
        for (const auto port : net->users) {
            auto result2 = conns.insert({port.cell->name, 1});
            if (!result2.second)
                result2.first->second++;
        }
    }

    size_t capacity;
    dict<IdString, int> conns;
    std::vector<NetInfo*> nets;
};

class GlobalBins {
public:
    GlobalBins(Context *ctx) : ctx{ctx}, bins{12, std::vector<GlobalBin>(12)} {}

    // Insert a net into a bin.
    void insert_net(BinSpace bin, NetInfo* net) {
        bins.at(bin.x).at(bin.y).insert_net(net);
    }

    // Return the net with the highest connectivity score.
    // TODO: can I turn this into a std::max_element call?
    BinSpace highest_connectivity(NetInfo *const net) const {
        auto best_score = bins.at(0).at(0).connectivity(net);
        auto best_x = 0;
        auto best_y = 0;
        for (int x = 0; x < 12; x++) {
            for (int y = 0; y < 12; y++) {
                if (x == 0 && y == 0)
                    continue;
                auto score = bins.at(x).at(y).connectivity(net);
                if (score > best_score) {
                    best_score = score;
                    best_x = x;
                    best_y = y;
                }
            }
        }
        return BinSpace{best_x, best_y};
    }

    // Reduce congestion by spreading cells with low connectivity into neighbouring cells.
    void spread_whitespace() {
        for (int x = 0; x < 12; x++) {
            for (int y = 0; y < 12; y++) {
                spread_bin(x, y);
            }
        }
    }

    // Display a heatmap of the whitespace in the bins.
    void print_occupancy() const {
        printf("\n");
        for (int y = 11; y >= 0; y--) {
            for (int x = 0; x < 12; x++) {
                printf("%4d,", bins.at(x).at(y).whitespace());
            }
            printf("\n");
        }
    }

    std::vector<Cluster> clusterise() {
        auto v = std::vector<Cluster>{};
        for (int x = 0; x < 12; x++) {
            for (int y = 0; y < 12; y++) {
                auto clusters = bins.at(x).at(y).clusterise(BinSpace{x, y});
                std::move(clusters.begin(), clusters.end(), std::back_inserter(v));
            }
        }
        std::sort(v.begin(), v.end(), [&](const Cluster& a, const Cluster& b) {
            return a.size() > b.size();
        });
        return v;
    }

    int edge_count_except(const NetInfo* net, const BinSpace exclude) {
        auto edges = 0;
        for (int x = 0; x < 12; x++) {
            for (int y = 0; y < 12; y++) {
                if (exclude.x == x && exclude.y == y)
                    continue;
                edges += bins.at(x).at(y).edge_count(net);
            }
        }
        return edges;
    }

private:

    // Spread a bin's least-connected cells to its neighbours to reduce peak congestion.
    bool spread_bin(int x, int y) {
        bool updated_design = false;
        bool did_something = true;
        bins.at(x).at(y).sort();
        while (did_something) {
            did_something = false;
            auto net = bins.at(x).at(y).pop_least_connected();
            auto best_x = 0;
            auto best_y = 0;
            auto best_score = 100000;
            for (int x_offset = -1; x_offset <= +1; x_offset++) {
                for (int y_offset = -1; y_offset <= +1; y_offset++) {
                    int new_x = x + x_offset;
                    int new_y = y + y_offset;
                    bool x_ok = new_x >= 0 && new_x < 12;
                    bool y_ok = new_y >= 0 && new_y < 12;
                    if (!x_ok || !y_ok || (new_x == x && new_y == y))
                        continue;
                    int score = (1250 - bins.at(new_x).at(new_y).whitespace()) + (1 - (std::abs(x_offset) + std::abs(y_offset)));
                    if (score < best_score) {
                        best_score = score;
                        best_x = new_x;
                        best_y = new_y;
                    }
                }
            }
            if (best_score < (1251 - bins.at(x).at(y).whitespace())) {
                bins.at(best_x).at(best_y).insert_net(net);
                did_something = true;
                updated_design = true;
            } else {
                bins.at(x).at(y).insert_net(net);
            }
        }
        return updated_design;
    }

    Context *ctx;
    std::vector<std::vector<GlobalBin>> bins;
};

class Phetdp {
public:
    Phetdp(Context* ctx) : ctx(ctx), g{ctx} {}

    void place() {
        using std::chrono::high_resolution_clock;
        using std::chrono::duration;
        log_info("=== PHetDP START ===\n");
        auto start_time = high_resolution_clock::now();
        // Step 1: initial placement of fixed/constrained cells in global bins
        initial_place_constraints();
        auto post_initial_constraints = high_resolution_clock::now();
        // Step 2: initial placement of unconstrained cells in global bins
        initial_place_rest();
        auto post_initial_rest = high_resolution_clock::now();
        // Step 3: spreading of whitespace to reduce congestion
        initial_spread_whitespace();
        auto post_spread_whitespace = high_resolution_clock::now();
        // Step 4: turning nets into clusters and sorting by size.
        global_clusterise();
        auto post_clusterise = high_resolution_clock::now();
        // Step 5: selecting net ordering based on logic contents.
        global_net_select();
        auto post_net_select = high_resolution_clock::now();
        log_info("=== PHetDP FINISH ===\n");
        log_info("global placement:\n");
        log_info("    initial_place_constraints(): %.02fs\n", duration<double>(post_initial_constraints - start_time).count());
        log_info("    initial_place_rest():        %.02fs\n", duration<double>(post_initial_rest - post_initial_constraints).count());
        log_info("    initial_spread_whitespace(): %.02fs\n", duration<double>(post_spread_whitespace - post_initial_rest).count());
        log_info("    global_clusterise():         %.02fs\n", duration<double>(post_clusterise - post_spread_whitespace).count());
        log_info("    global_net_select():         %.02fs\n", duration<double>(post_net_select - post_clusterise).count());
        NPNR_ASSERT_FALSE_STR("not yet implemented");
    }

    void initial_place_constraints() {
        size_t placed_cells = 0;
        for (auto &net_entry : ctx->nets) {
            NetInfo *net = net_entry.second.get();
            CellInfo *cell = net->driver.cell;
            if (!cell)
                continue; // maybe?
            if (cell->isPseudo())
                continue;
            auto loc = cell->attrs.find(ctx->id("BEL"));
            if (loc != cell->attrs.end()) {
                std::string loc_name = loc->second.as_string();
                BelId bel = ctx->getBelByNameStr(loc_name);
                if (bel == BelId()) {
                    log_error("No Bel named \'%s\' located for "
                              "this chip (processing BEL attribute on \'%s\')\n",
                              loc_name.c_str(), cell->name.c_str(ctx));
                }

                if (!ctx->isValidBelForCellType(cell->type, bel)) {
                    IdString bel_type = ctx->getBelType(bel);
                    log_error("Bel \'%s\' of type \'%s\' does not match cell "
                              "\'%s\' of type \'%s\'\n",
                              loc_name.c_str(), bel_type.c_str(ctx), cell->name.c_str(ctx), cell->type.c_str(ctx));
                }
                auto bound_cell = ctx->getBoundBelCell(bel);
                if (bound_cell) {
                    if (cell != bound_cell) {
                        log_error("Cell \'%s\' cannot be bound to bel \'%s\' since it is already bound to cell \'%s\'\n",
                                cell->name.c_str(ctx), loc_name.c_str(), bound_cell->name.c_str(ctx));
                    }
                    continue;
                }

                ctx->bindBel(bel, cell, STRENGTH_USER);
                auto bel_loc = BinSpace{ctx, GridSpace{ctx->getBelLocation(bel)}};
                g.insert_net(bel_loc, net);

                if (!ctx->isBelLocationValid(bel, /* explain_invalid */ true)) {
                    IdString bel_type = ctx->getBelType(bel);
                    log_error("Bel \'%s\' of type \'%s\' is not valid for cell "
                              "\'%s\' of type \'%s\'\n",
                              loc_name.c_str(), bel_type.c_str(ctx), cell->name.c_str(ctx), cell->type.c_str(ctx));
                }
                placed_cells++;
            }
        }
        log_info("Placed %d cells based on constraints.\n", int(placed_cells));
        log_info("after fixed initial placement:");
        g.print_occupancy();
    }

    void initial_place_rest() {
        size_t placed_cells = 0;
        for (auto &net_entry : ctx->nets) {
            NetInfo *net = net_entry.second.get();
            CellInfo *cell = net->driver.cell;
            if (!cell)
                continue; // maybe?
            if (cell->isPseudo())
                continue;
            
            // Fixed constraints are handled in initial_place_constraints().
            auto loc = cell->attrs.find(ctx->id("BEL"));
            if (loc != cell->attrs.end())
                continue;

            g.insert_net(g.highest_connectivity(net), net);
            placed_cells++;
        }
        log_info("Binned %d cells.\n", int(placed_cells));
        log_info("after connectivity-based initial placement:");
        g.print_occupancy();
    }

    void initial_spread_whitespace() {
        g.spread_whitespace();
        log_info("after whitespace spreading:");
        g.print_occupancy();
    }

    void global_clusterise() {
        clusters = std::move(g.clusterise());
        log_info("found %zu clusters\n", clusters.size());
        log_info("largest cluster is %zu\n", clusters.front().size());
    }

    void global_net_select() {
        for (auto& cluster : clusters) {
            cluster.sort([&](const NetInfo* net) {
                pool<IdString> cell_types;
                cell_types.insert(net->driver.cell->type);
                for (auto port : net->users)
                    cell_types.insert(port.cell->type);
                
                auto lut_ffs = cell_types.count(id_LUT4) + cell_types.count(id_TRELLIS_FF);

                return float(g.edge_count_except(net, cluster.containing_bin())) * float(lut_ffs) / float(1 + net->users.entries());
            });
        }
    }
private:
    Context* ctx;

    std::vector<Cluster> clusters;
    GlobalBins g;
};

void placer_phetdp(Context* ctx) {
    Phetdp{ctx}.place();
}

NEXTPNR_NAMESPACE_END
