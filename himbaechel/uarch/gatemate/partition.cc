/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2024  The Project Peppercorn Authors.
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

#include <boost/algorithm/string.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include "gatemate.h"

#define HIMBAECHEL_CONSTIDS "uarch/gatemate/constids.inc"
#include "himbaechel_constids.h"

NEXTPNR_NAMESPACE_BEGIN

namespace {

struct HypergraphEdge
{
    std::vector<int> nodes;
    int weight = 1;
};

struct HypergraphNode
{
    std::vector<int> edges;
    int partition = -1;
    float area = 1;
    bool fixed = false;
};

struct Hypergraph
{
    std::vector<HypergraphEdge> edges;
    std::vector<HypergraphNode> nodes;
    void dump(std::ostream &out) const;
    void read(std::istream &in);
};

struct PartitionConstraint
{
    double min_nodes, max_nodes;
};

struct gain_store
{
    gain_store(int num_nodes) { node2element.resize(num_nodes); }

    struct gain_element
    {
        int node;
        int fwd_ptr = -1;
        int bwd_ptr = -1;
    };
    struct gain_pointer
    {
        int gain;
        int elem_ptr = -1;
    };
    struct gain_bucket
    {
        int head_ptr = -1;
        int tail_ptr = -1;
    };
    std::map<int, gain_bucket> buckets;

    void remove_elem(int gain, int elem_ptr)
    {
        auto &bucket = buckets.at(gain);
        auto &elem = elements.at(elem_ptr);
        if (elem.fwd_ptr == -1) {
            bucket.tail_ptr = elem.bwd_ptr;
        } else {
            elements.at(elem.fwd_ptr).bwd_ptr = elem.bwd_ptr;
        }
        if (elem.bwd_ptr == -1) {
            bucket.head_ptr = elem.fwd_ptr;
        } else {
            elements.at(elem.bwd_ptr).fwd_ptr = elem.fwd_ptr;
        }
        if (bucket.tail_ptr == -1) {
            NPNR_ASSERT(bucket.head_ptr == -1);
            buckets.erase(gain);
        }
        elem.fwd_ptr = next_free_elem;
        elem.bwd_ptr = -1;
        next_free_elem = elem_ptr;

        node2element.at(elem.node).elem_ptr = -1;
        elem.node = -1;
    };

    void add_elem(int gain, int node, bool at_start = true)
    {
        int elem_ptr = next_free_elem;
        if (elem_ptr == int(elements.size())) {
            elements.emplace_back();
            ++next_free_elem;
        } else {
            next_free_elem = elements.at(elem_ptr).fwd_ptr;
        }
        auto &elem = elements.at(elem_ptr);
        auto &bucket = buckets[gain];
        elem.node = node;
        if (at_start) {
            elem.fwd_ptr = bucket.head_ptr;
            elem.bwd_ptr = -1;
            if (bucket.head_ptr != -1)
                elements.at(bucket.head_ptr).bwd_ptr = elem_ptr;
            if (bucket.tail_ptr == -1)
                bucket.tail_ptr = elem_ptr;
            bucket.head_ptr = elem_ptr;
        } else {
            elem.bwd_ptr = bucket.tail_ptr;
            elem.fwd_ptr = -1;
            if (bucket.tail_ptr != -1)
                elements.at(bucket.tail_ptr).fwd_ptr = elem_ptr;
            if (bucket.head_ptr == -1)
                bucket.head_ptr = elem_ptr;
            bucket.tail_ptr = elem_ptr;
        }
        node2element.at(node).gain = gain;
        node2element.at(node).elem_ptr = elem_ptr;
    };

    std::pair<int, int> pop_node(bool lifo = true)
    {
        // Pop the node with the highest gain
        NPNR_ASSERT(!buckets.empty());
        auto highest = buckets.rbegin();
        int elem_ptr = lifo ? highest->second.head_ptr : highest->second.tail_ptr;
        int node = elements.at(elem_ptr).node;
        int gain = highest->first;
        remove_elem(gain, elem_ptr);
        return {node, gain};
    }

    bool has_moves() { return !buckets.empty(); }

    int node_gain(int node) const
    {
        auto &c = node2element.at(node);
        NPNR_ASSERT(c.elem_ptr != -1);
        return c.gain;
    }

    void update_node(int node, int delta)
    {
        auto &c = node2element.at(node);
        int new_gain = c.gain + delta;
        NPNR_ASSERT(c.elem_ptr != -1);
        remove_elem(c.gain, c.elem_ptr);
        add_elem(new_gain, node);
    }

    // The linked list storage (so we don't have to malloc all the time)
    std::vector<gain_element> elements;
    int next_free_elem = 0;
    // node to element map (indexed by node index)
    std::vector<gain_pointer> node2element;
};

struct FMPartitioner
{

    FMPartitioner(Context *ctx, Hypergraph &g, const std::vector<PartitionConstraint> &partitions)
            : ctx(ctx), g(g), partitions(partitions), gains(g.nodes.size())
    {
        init();
    }

    Context *ctx;
    Hypergraph &g;
    std::vector<PartitionConstraint> partitions;
    gain_store gains;
    std::vector<bool> locked;
    std::vector<double> part_area;

    void init()
    {
        locked.resize(g.nodes.size(), false);
        part_area.resize(partitions.size(), 0);
        gain_store empty(g.nodes.size());
        std::swap(gains, empty);
        for (int i = 0; i < int(g.nodes.size()); i++) {
            auto &c = g.nodes.at(i);
            if (c.fixed) {
                locked.at(i) = true;
                NPNR_ASSERT(c.partition != -1);
                part_area.at(c.partition) += c.area;
            }
        }
    }

    inline bool above_eq_target()
    {
        for (int i = 0; i < int(partitions.size()); i++)
            if (part_area.at(i) < partitions.at(i).min_nodes)
                return false;
        return true;
    }

    inline double total_area_slack(bool max_area)
    {
        double slack = 0;
        for (int i = 0; i < int(partitions.size()); i++) {
            slack += part_area_slack(i, max_area);
        }
        return slack;
    }

    inline double part_area_slack(int part, bool max_area)
    {
        if (max_area)
            return std::max<double>(0, partitions.at(part).max_nodes - part_area.at(part));
        else
            return std::max<double>(0, partitions.at(part).min_nodes - part_area.at(part));
    }

    void random_part()
    {
        /*
            Perform a random initial partitioning
            Probabilities are weighted based on relative occupancy of paritions relative to occupancy constraints
        */
        std::fill(part_area.begin(), part_area.end(), 0);
        for (int i = 0; i < int(g.nodes.size()); i++) {
            auto &c = g.nodes.at(i);
            if (c.fixed)
                part_area.at(c.partition) += c.area;
        }
        std::vector<std::pair<int, float>> sorted_by_area;

        for (int i = 0; i < int(g.nodes.size()); i++) {
            auto &c = g.nodes.at(i);
            if (c.fixed)
                continue;
            sorted_by_area.emplace_back(i, c.area);
        }
        std::sort(sorted_by_area.begin(), sorted_by_area.end(),
                  [&](std::pair<int, int> a, std::pair<int, int> b) { return a.second > b.second; });
        for (auto item : sorted_by_area) {
            auto &c = g.nodes.at(item.first);
            double r = ctx->rng() / double(0x3fffffff);
            double p = 0;
            c.partition = int(partitions.size()) - 1;

            bool use_max_area = above_eq_target();
            double total_slack = total_area_slack(use_max_area);

            for (int i = 0; i < int(partitions.size()) - 1; i++) {
                p += double(part_area_slack(i, use_max_area)) / std::max<double>(1, total_slack);
                if (r <= p) {
                    c.partition = i;
                    break;
                }
            }

            part_area.at(c.partition) += c.area;
        }
    }

    int compute_cost()
    {
        int cost = 0;
        std::vector<bool> seen_parts(partitions.size(), false);
        for (int i = 0; i < int(g.edges.size()); i++) {
            std::fill(seen_parts.begin(), seen_parts.end(), false);
            auto &e = g.edges.at(i);
            for (int n : e.nodes)
                seen_parts.at(g.nodes.at(n).partition) = true;
            int num_parts = 0;
            for (bool b : seen_parts)
                if (b)
                    ++num_parts;
            if (num_parts == 0)
                continue;
            cost += e.weight * (num_parts - 1);
        }
        return cost;
    }

    double total_area()
    {
        double area = 0;
        for (int i = 0; i < int(g.nodes.size()); i++) {
            area += g.nodes.at(i).area;
        }
        return area;
    }

    void coarsen(Hypergraph &coarsened, dict<int, std::vector<int>> &new2orig)
    {
        // This is a very basic algorithm for coarsening that probably doesn't give very good results
        // Need to find a better one that
        dict<int, int> orig2new;

        double area_sum = 0;
        int area_count = 0;
        for (int i = 0; i < int(g.nodes.size()); i++) {
            auto &n = g.nodes.at(i);
            if (n.fixed || n.edges.empty())
                continue;
            ++area_count;
            area_sum += n.area;
        }

        double average_area = area_sum / area_count;

        // Merge nodes
        std::vector<int> node_indices(g.nodes.size());
        for (int i = 0; i < int(g.nodes.size()); i++)
            node_indices.at(i) = i;
        ctx->sorted_shuffle(node_indices);
        for (int i : node_indices) {
            auto &n = g.nodes.at(i);
            if (n.fixed) {
                // Locked nodes are never merged
                coarsened.nodes.emplace_back();
                auto &n2 = coarsened.nodes.back();
                n2.fixed = n.fixed;
                n2.partition = n.partition;
                n2.area = n.area;
                new2orig[int(coarsened.nodes.size()) - 1].push_back(i);
                orig2new[i] = int(coarsened.nodes.size()) - 1;
                continue;
            }
            const int max_count = 2;
            const int area_ratio = 3;
            if (!n.edges.empty()) {
                dict<int, float> neighbours;
                for (int thresh = 20; thresh < 200; thresh *= 1.2) {
                    for (int merge_edge : n.edges) {
                        auto &e = g.edges.at(merge_edge);
                        if (int(e.nodes.size()) > thresh)
                            continue;
                        for (int neighbour : e.nodes) {
                            if (neighbour == i)
                                continue;
                            auto &merge_node_data = g.nodes.at(neighbour);
                            if (merge_node_data.fixed)
                                continue;
                            if (orig2new.count(neighbour)) {
                                if (orig2new.count(i))
                                    continue; // don't merge two clusters
                                // Already a cluster
                                int n2_idx = orig2new.at(neighbour);
                                if ((coarsened.nodes.at(n2_idx).area + n.area) > (area_ratio * average_area))
                                    continue;
                                if (int(new2orig.at(n2_idx).size()) >= (max_count - 1))
                                    continue;
                                // Alias to the first node in the cluster
                                neighbours[new2orig.at(n2_idx).front()] += (1.0f / sqrt(e.nodes.size()));
                            } else if (orig2new.count(i)) {
                                int n2_idx = orig2new.at(i);
                                if ((coarsened.nodes.at(n2_idx).area + merge_node_data.area) >
                                    (area_ratio * average_area))
                                    continue;
                                if (int(new2orig.at(n2_idx).size()) >= (max_count - 1))
                                    continue;
                                neighbours[neighbour] += (1.0f / sqrt(e.nodes.size()));
                            } else {
                                neighbours[neighbour] += (1.0f / sqrt(e.nodes.size()));
                            }
                        }
                    }
                    if (!neighbours.empty())
                        break;
                }
                if (int(neighbours.size()) > 0) {
                    auto best_neighbour =
                            std::max_element(neighbours.begin(), neighbours.end(),
                                             [&](const std::pair<int, int> &a, const std::pair<int, int> &b) {
                                                 return (a.second < b.second);
                                             });
                    int merge_node = best_neighbour->first;
                    auto &merge_node_data = g.nodes.at(merge_node);
                    if (orig2new.count(i)) {
                        int n2_idx = orig2new.at(i);
                        coarsened.nodes.at(n2_idx).area += merge_node_data.area;
                        new2orig[n2_idx].push_back(merge_node);
                        orig2new[merge_node] = n2_idx;
                        goto merged;
                    } else if (orig2new.count(merge_node)) {
                        // Already a cluster
                        int n2_idx = orig2new.at(merge_node);
                        coarsened.nodes.at(n2_idx).area += n.area;
                        new2orig[n2_idx].push_back(i);
                        orig2new[i] = n2_idx;
                        goto merged;
                    } else {
                        // Create a cluster
                        coarsened.nodes.emplace_back();
                        auto &n2 = coarsened.nodes.back();
                        n2.fixed = false;
                        n2.partition = -1;
                        n2.area = n.area + merge_node_data.area;

                        new2orig[int(coarsened.nodes.size()) - 1].push_back(i);
                        new2orig[int(coarsened.nodes.size()) - 1].push_back(merge_node);
                        orig2new[i] = int(coarsened.nodes.size()) - 1;
                        orig2new[merge_node] = int(coarsened.nodes.size()) - 1;
                        goto merged;
                    }
                }
            }
            if (0) {
            merged:
                continue;
            }
            // Didn't find anything to merge with
            if (!orig2new.count(i)) {
                coarsened.nodes.emplace_back();
                auto &n2 = coarsened.nodes.back();
                n2.fixed = n.fixed;
                n2.partition = n.partition;
                n2.area = n.area;
                new2orig[int(coarsened.nodes.size()) - 1].push_back(i);
                orig2new[i] = int(coarsened.nodes.size()) - 1;
                continue;
            }
        }
        // Reconstruct edges
        std::unordered_set<int> seen_nodes;
        for (int i = 0; i < int(g.edges.size()); i++) {
            auto &e = g.edges.at(i);
            if (e.nodes.size() <= 1)
                continue;
            // Don't create a new edge if it now only connects to one node
            if (std::all_of(e.nodes.begin(), e.nodes.end(), [&](int n) { return n == e.nodes.at(0); }))
                continue;

            int e2_idx = int(coarsened.edges.size());
            coarsened.edges.emplace_back();
            auto &e2 = coarsened.edges.back();
            e2.weight = e.weight;

            seen_nodes.clear();
            for (auto n : e.nodes) {
                int n2 = orig2new.at(n);
                if (seen_nodes.count(n2))
                    continue;
                seen_nodes.insert(n2);
                coarsened.nodes.at(n2).edges.push_back(e2_idx);
                e2.nodes.push_back(n2);
            }
        }
    }

    void assert_area()
    {
        std::fill(part_area.begin(), part_area.end(), 0);
        for (int i = 0; i < int(g.nodes.size()); i++) {
            auto &c = g.nodes.at(i);
            part_area.at(c.partition) += c.area;
        }
        for (int i = 0; i < int(partitions.size()); i++)
            NPNR_ASSERT((part_area.at(i) >= partitions.at(i).min_nodes) &&
                        (part_area.at(i) <= partitions.at(i).max_nodes));
    }

    void uncoarsen(const Hypergraph &coarsened, const dict<int, std::vector<int>> &new2orig)
    {
        for (const auto &item : new2orig) {
            const auto &new_node = coarsened.nodes.at(item.first);
            for (int old_node_idx : item.second) {
                auto &old_node = g.nodes.at(old_node_idx);
                if (old_node.fixed)
                    continue;
                old_node.partition = new_node.partition;
            }
        }
        std::fill(part_area.begin(), part_area.end(), 0);
        for (int i = 0; i < int(g.nodes.size()); i++) {
            auto &c = g.nodes.at(i);
            part_area.at(c.partition) += c.area;
        }
    }

    void gain_update(int node, int src_part, int dst_part)
    {
        // Figure 13 - Pseudo-code for a faster gain update that takes advantage of special cases.
        auto &n = g.nodes.at(node);
        for (int e_idx : n.edges) {
            auto &e = g.edges.at(e_idx);
            if (e.nodes.size() == 2) {
                for (int n2_idx : e.nodes) {
                    if (n2_idx == node)
                        continue;
                    if (locked.at(n2_idx))
                        break;
                    auto &n2 = g.nodes.at(n2_idx);
                    if (n2.partition == src_part)
                        gains.update_node(n2_idx, 2 * e.weight);
                    else
                        gains.update_node(n2_idx, -2 * e.weight);
                    break;
                }
                continue;
            }
            if (e.nodes.size() == 1)
                continue;
            int src_tally = 0;
            int dst_tally = 0;
            for (int n2_idx : e.nodes) {
                auto &n2 = g.nodes.at(n2_idx);
                if (n2.partition == src_part)
                    ++src_tally;
                if (n2.partition == dst_part)
                    ++dst_tally;
            }
            if (dst_tally == 0) {
                // This move is the first node on the edge to enter the dst partition
                for (int n2_idx : e.nodes) {
                    if (n2_idx == node || locked.at(n2_idx))
                        continue;
                    gains.update_node(n2_idx, e.weight);
                }
            } else if (src_tally == 1) {
                // This move is the last node on the edge to leave the src partition
                for (int n2_idx : e.nodes) {
                    if (n2_idx == node || locked.at(n2_idx))
                        continue;
                    gains.update_node(n2_idx, -e.weight);
                }
            } else {
                // None of the special cases apply
                for (int n2_idx : e.nodes) {
                    if (n2_idx == node || locked.at(n2_idx))
                        continue;
                    auto &n2 = g.nodes.at(n2_idx);
                    if (n2.partition == src_part && src_tally == 2) {
                        // This other node is the last one left in the src partition
                        // other than the one being moved
                        gains.update_node(n2_idx, e.weight);
                    }
                    if (n2.partition == dst_part && dst_tally == 1) {
                        // This other node is the only other one in the dst partition
                        // other than the one being moved
                        gains.update_node(n2_idx, -e.weight);
                    }
                }
            }
        }
    }

    void setup_initial_gains()
    {
        // Setup the starting gains
        for (int i = 0; i < int(g.nodes.size()); i++) {
            auto &n = g.nodes.at(i);
            if (n.fixed)
                continue;
            int src_part = n.partition;
            int dst_part = 1 - n.partition;
            int gain = 0;
            for (int e_idx : n.edges) {
                auto &e = g.edges.at(e_idx);
                if (e.nodes.size() == 2) {
                    // Special-casing for two-element nodes
                    for (int n2_idx : e.nodes) {
                        if (n2_idx == i)
                            continue;
                        if (locked.at(n2_idx))
                            break;
                        auto &n2 = g.nodes.at(n2_idx);
                        if (n2.partition == src_part)
                            gain -= e.weight; // now introducing a split
                        else
                            gain += e.weight; // now removing a split
                        break;
                    }
                    continue;
                }

                if (e.nodes.size() == 1)
                    continue;

                int src_tally = 0;
                int dst_tally = 0;
                for (int n2_idx : e.nodes) {
                    auto &n2 = g.nodes.at(n2_idx);
                    if (n2.partition == src_part)
                        ++src_tally;
                    if (n2.partition == dst_part)
                        ++dst_tally;
                }

                if (src_tally == 1) {
                    gain += e.weight; // now removing a split
                    continue;
                }

                if (dst_tally == 0) {
                    gain -= e.weight; // now introducing a split
                    continue;
                }
            }
            gains.add_elem(gain, i);
        }
    }

    void run()
    {
        std::vector<std::pair<int, int>> moves_made;
        std::vector<std::pair<int, int>> reinsert;

        int score = 0;
        int best_score = 0;
        int best_score_idx = -1;

        setup_initial_gains();

        int start_cost = compute_cost();

        while (true) {
            int move_node = -1;
            int move_gain = 0;
            reinsert.clear();
            // Find a legal move
            while (gains.has_moves()) {
                auto move = gains.pop_node();
                int n_idx = move.first;
                auto &n = g.nodes.at(n_idx);
                int src_part = n.partition;
                int dst_part = 1 - n.partition;
                if (/*(part_area.at(src_part) >= partitions.at(src_part).min_nodes) && */ (
                        (part_area.at(src_part) - n.area) < partitions.at(src_part).min_nodes))
                    goto fail;
                if (/*(part_area.at(dst_part) <= partitions.at(dst_part).max_nodes) && */ (
                        (part_area.at(dst_part) + n.area) > partitions.at(dst_part).max_nodes))
                    goto fail;
                move_node = n_idx;
                move_gain = move.second;
                break;
            fail:
                reinsert.push_back(move);
            }
            if (move_node == -1)
                break;
            // Re-add the illegal moves we popped
            for (auto re : reinsert)
                gains.add_elem(re.second, re.first);

            auto &n = g.nodes.at(move_node);
            int src_part = n.partition;
            int dst_part = 1 - n.partition;

            // Update gains
            gain_update(move_node, src_part, dst_part);

            // Update areas
            part_area.at(src_part) -= n.area;
            part_area.at(dst_part) += n.area;
            n.partition = dst_part;

            // Update score
            score += move_gain;
            if ((best_score_idx == -1) || (score > best_score)) {
                best_score_idx = int(moves_made.size());
                best_score = score;
            }

            // Add move to list
            moves_made.emplace_back(move_node, src_part);
            locked.at(move_node) = true;
        }

        // Revert moves after the best score
        for (int i = best_score_idx + 1; i < int(moves_made.size()); i++) {
            auto &mm = moves_made.at(i);
            auto &n = g.nodes.at(mm.first);
            part_area.at(n.partition) -= n.area;
            part_area.at(mm.second) += n.area;
            n.partition = mm.second;
        }

        if (ctx->verbose)
            log_info("    start: %d end: %d incr_gain: %d moves_made: %d\n", start_cost, compute_cost(), best_score,
                     int(moves_made.size()));
    }
};

int partition_recursive(Context *ctx, Hypergraph &g, const std::vector<PartitionConstraint> &partitions, int level)
{
    int non_fixed_nodes = 0;
    for (auto &n : g.nodes)
        if (!n.fixed && !n.edges.empty() > 0)
            ++non_fixed_nodes;
    FMPartitioner fm(ctx, g, partitions);
    fm.init();
    if (ctx->verbose)
        log_info("enter level=%d, N=%d, A=%f\n", level, non_fixed_nodes, fm.total_area());
    if (non_fixed_nodes <= 200) {
        // Final level in the hierarchy
        // Initial random partioning as our seed
        fm.random_part();
    } else {
        // Coarse the hypergraph, partition the coarsened graph and use that result as our seed
        Hypergraph coarsened;
        dict<int, std::vector<int>> new2orig;
        fm.coarsen(coarsened, new2orig);
        partition_recursive(ctx, coarsened, partitions, level + 1);
        fm.uncoarsen(coarsened, new2orig);
    }
    if (ctx->verbose) {
        log_info("re-enter level=%d, N=%d, cost=%d", level, non_fixed_nodes, fm.compute_cost());
        for (int i = 0; i < int(partitions.size()); i++) {
            log(", A%d=%f", i, fm.part_area.at(i));
        }
        log("\n");
    }

    fm.assert_area();
    // The FM optimisation phase
    fm.run();
    for (int i = 0; i < 5; i++) {
        FMPartitioner fm(ctx, g, partitions);
        fm.init();
        fm.assert_area();
        fm.run();
    }
    fm.assert_area();

    // Status print

    if (ctx->verbose) {
        log_info("exit level=%d, N=%d, cost=%d", level, non_fixed_nodes, fm.compute_cost());
        for (int i = 0; i < int(partitions.size()); i++) {
            log(", A%d=%f", i, fm.part_area.at(i));
        }
        log("\n");
    }

    fm.assert_area();
    return fm.compute_cost();
}
} // namespace

NEXTPNR_NAMESPACE_END
