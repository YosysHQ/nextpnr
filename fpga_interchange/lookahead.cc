/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Symbiflow Authors
 *
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

#include "lookahead.h"

#include <boost/filesystem.hpp>
#include <boost/safe_numerics/safe_integer.hpp>
#include <capnp/message.h>
#include <capnp/serialize.h>
#include <kj/filesystem.h>
#include <kj/std/iostream.h>
#include <queue>
#include <sstream>
#include <zlib.h>

#include "context.h"
#include "flat_wire_map.h"
#include "log.h"
#include "sampler.h"
#include "scope_lock.h"

#if defined(NEXTPNR_USE_TBB)
#include <tbb/parallel_for_each.h>
#endif

NEXTPNR_NAMESPACE_BEGIN

static constexpr size_t kNumberSamples = 4;
static constexpr int32_t kMaxExploreDist = 20;

// Initial only explore with a depth of this.
static constexpr int32_t kInitialExploreDepth = 30;

struct RoutingNode
{
    WireId wire_to_expand;
    delay_t cost;
    int32_t depth;

    bool operator<(const RoutingNode &other) const { return cost < other.cost; }
};

struct PipAndCost
{
    PipId upstream_pip;
    delay_t cost_from_src;
    int32_t depth;
};

static void expand_input(const Context *ctx, WireId input_wire, dict<TypeWireId, delay_t> *input_costs)
{
    pool<WireId> seen;
    std::priority_queue<RoutingNode> to_expand;

    RoutingNode initial;
    initial.cost = 0;
    initial.wire_to_expand = input_wire;

    to_expand.push(initial);

    while (!to_expand.empty()) {
        RoutingNode node = to_expand.top();
        to_expand.pop();

        auto result = seen.emplace(node.wire_to_expand);
        if (!result.second) {
            // We've already done an expansion at this wire.
            continue;
        }

        for (PipId pip : ctx->getPipsUphill(node.wire_to_expand)) {
            if (ctx->is_pip_synthetic(pip)) {
                continue;
            }
            WireId new_wire = ctx->getPipSrcWire(pip);
            if (new_wire == WireId()) {
                continue;
            }

            RoutingNode next_node;
            next_node.wire_to_expand = new_wire;
            next_node.cost = node.cost + ctx->getPipDelay(pip).maxDelay() + ctx->getWireDelay(new_wire).maxDelay();

            if (ctx->is_site_port(pip)) {
                // Done with expansion, record the path if cheaper.
                // Only the first path to each wire will be the cheapest.

                // Get local tile wire at pip dest. Using getPipSrcWire may
                // return a node wire, which is not correct here.
                TypeWireId route_to;
                route_to.type = ctx->chip_info->tiles[pip.tile].type;
                route_to.index = loc_info(ctx->chip_info, pip).pip_data[pip.index].src_index;
                if (route_to.index >= 0) {
                    auto result = input_costs->emplace(route_to, next_node.cost);
                    if (!result.second && result.first->second > next_node.cost) {
                        result.first->second = next_node.cost;
                    }
                }
            } else {
                to_expand.push(next_node);
            }
        }
    }
}

static void update_site_to_site_costs(const Context *ctx, WireId first_wire, const dict<WireId, PipAndCost> &best_path,
                                      dict<TypeWirePair, delay_t> *site_to_site_cost)
{
    for (auto &best_pair : best_path) {
        WireId last_wire = best_pair.first;
        TypeWirePair pair;
        pair.dst = TypeWireId(ctx, last_wire);

        PipAndCost pip_and_cost = best_pair.second;
        delay_t cost_from_src = pip_and_cost.cost_from_src;

        WireId cursor;
        do {
            cursor = ctx->getPipSrcWire(pip_and_cost.upstream_pip);

            pair.src = TypeWireId(ctx, cursor);

            delay_t cost = cost_from_src;

            // Only use the delta cost from cursor to last_wire, not the full
            // cost from first_wire to last_wire.
            if (cursor != first_wire) {
                pip_and_cost = best_path.at(cursor);
                cost -= pip_and_cost.cost_from_src;
            }

            NPNR_ASSERT(cost >= 0);

            auto cost_result = site_to_site_cost->emplace(pair, cost);
            if (!cost_result.second) {
                // Update point to point cost if cheaper.
                if (cost_result.first->second > cost) {
                    cost_result.first->second = cost;
                }
            }
        } while (cursor != first_wire);
    }
}

static void expand_output(const Context *ctx, WireId output_wire, Lookahead::OutputSiteWireCost *output_cost,
                          dict<TypeWirePair, delay_t> *site_to_site_cost)
{
    pool<WireId> seen;
    std::priority_queue<RoutingNode> to_expand;

    RoutingNode initial;
    initial.cost = 0;
    initial.wire_to_expand = output_wire;

    to_expand.push(initial);

    dict<WireId, PipAndCost> best_path;

    while (!to_expand.empty()) {
        RoutingNode node = to_expand.top();
        to_expand.pop();

        auto result = seen.emplace(node.wire_to_expand);
        if (!result.second) {
            // We've already done an expansion at this wire.
            continue;
        }

        for (PipId pip : ctx->getPipsDownhill(node.wire_to_expand)) {
            if (ctx->is_pip_synthetic(pip)) {
                continue;
            }
            WireId new_wire = ctx->getPipDstWire(pip);
            if (new_wire == WireId()) {
                continue;
            }

            RoutingNode next_node;
            next_node.wire_to_expand = new_wire;
            next_node.cost = node.cost + ctx->getPipDelay(pip).maxDelay() + ctx->getWireDelay(new_wire).maxDelay();

            if (ctx->is_site_port(pip)) {
                // Done with expansion, record the path if cheaper.

                // Get local tile wire at pip dest. Using getPipDstWire may
                // return a node wire, which is not correct here.
                TypeWireId route_from;
                route_from.type = ctx->chip_info->tiles[pip.tile].type;
                route_from.index = loc_info(ctx->chip_info, pip).pip_data[pip.index].dst_index;
                if (route_from.index != -1 && output_cost != nullptr && next_node.cost < output_cost->cost) {
                    output_cost->cost = next_node.cost;
                    output_cost->cheapest_route_from = route_from;
                }
            } else {
                to_expand.push(next_node);

                auto result = best_path.emplace(new_wire, PipAndCost());
                PipAndCost &pip_and_cost = result.first->second;
                if (result.second || pip_and_cost.cost_from_src > next_node.cost) {
                    pip_and_cost.upstream_pip = pip;
                    pip_and_cost.cost_from_src = next_node.cost;
                }
            }
        }
    }

    update_site_to_site_costs(ctx, output_wire, best_path, site_to_site_cost);
}

static void expand_input_type(const Context *ctx, DeterministicRNG *rng, const Sampler &tiles_of_type,
                              TypeWireId input_wire, std::vector<Lookahead::InputSiteWireCost> *input_costs)
{
    dict<TypeWireId, delay_t> input_costs_map;
    for (size_t region = 0; region < tiles_of_type.number_of_regions(); ++region) {
        size_t tile = tiles_of_type.get_sample_from_region(region, [rng]() -> int32_t { return rng->rng(); });

        NPNR_ASSERT(ctx->chip_info->tiles[tile].type == input_wire.type);
        WireId wire = canonical_wire(ctx->chip_info, tile, input_wire.index);

        expand_input(ctx, wire, &input_costs_map);
    }

    input_costs->clear();
    input_costs->reserve(input_costs_map.size());
    for (const auto &input_pair : input_costs_map) {
        input_costs->emplace_back();
        auto &input_cost = input_costs->back();
        input_cost.route_to = input_pair.first;
        input_cost.cost = input_pair.second;
    }
}

struct DelayStorage
{
    dict<TypeWirePair, dict<std::pair<int32_t, int32_t>, delay_t>> storage;
    int32_t max_explore_depth;
};

static bool has_multiple_inputs(const Context *ctx, WireId wire)
{
    size_t pip_count = 0;
    for (PipId pip : ctx->getPipsUphill(wire)) {
        (void)pip;
        pip_count += 1;
    }

    return pip_count > 1;
}

static void update_results(const Context *ctx, const FlatWireMap<PipAndCost> &best_path, WireId src_wire,
                           WireId sink_wire, DelayStorage *storage)
{
    TypeWireId src_wire_type(ctx, src_wire);

    int src_tile;
    if (src_wire.tile == -1) {
        src_tile = ctx->chip_info->nodes[src_wire.index].tile_wires[0].tile;
    } else {
        src_tile = src_wire.tile;
    }

    int32_t src_x, src_y;
    ctx->get_tile_x_y(src_tile, &src_x, &src_y);

    TypeWirePair wire_pair;
    wire_pair.src = src_wire_type;

    // The first couple wires from the site pip are usually boring, don't record
    // them.
    bool out_of_infeed = false;

    // Starting from end of result, walk backwards and record the path into
    // the delay storage.
    WireId cursor = sink_wire;
    pool<WireId> seen;
    while (cursor != src_wire) {
        // No loops allowed in routing!
        auto result = seen.emplace(cursor);
        NPNR_ASSERT(result.second);

        if (!out_of_infeed && has_multiple_inputs(ctx, cursor)) {
            out_of_infeed = true;
        }

        TypeWireId dst_wire_type(ctx, cursor);
        wire_pair.dst = dst_wire_type;

        int dst_tile;
        if (cursor.tile == -1) {
            dst_tile = ctx->chip_info->nodes[cursor.index].tile_wires[0].tile;
        } else {
            dst_tile = cursor.tile;
        }
        int32_t dst_x;
        int32_t dst_y;
        ctx->get_tile_x_y(dst_tile, &dst_x, &dst_y);

        std::pair<int32_t, int32_t> dx_dy;
        dx_dy.first = dst_x - src_x;
        dx_dy.second = dst_y - src_y;

        const PipAndCost &pip_and_cost = best_path.at(cursor);
        if (out_of_infeed) {
            auto &delta_data = storage->storage[wire_pair];
            auto result2 = delta_data.emplace(dx_dy, pip_and_cost.cost_from_src);
            if (!result2.second) {
                if (result2.first->second > pip_and_cost.cost_from_src) {
                    result2.first->second = pip_and_cost.cost_from_src;
                }
            }
        }

        cursor = ctx->getPipSrcWire(pip_and_cost.upstream_pip);
    }
}

static void expand_routing_graph_from_wire(const Context *ctx, WireId first_wire, FlatWireMap<PipAndCost> *best_path,
                                           DelayStorage *storage)
{
    pool<WireId> seen;
    std::priority_queue<RoutingNode> to_expand;

    int src_tile;
    if (first_wire.tile == -1) {
        src_tile = ctx->chip_info->nodes[first_wire.index].tile_wires[0].tile;
    } else {
        src_tile = first_wire.tile;
    }

    int32_t src_x, src_y;
    ctx->get_tile_x_y(src_tile, &src_x, &src_y);

    RoutingNode initial;
    initial.cost = 0;
    initial.wire_to_expand = first_wire;
    initial.depth = 0;

    to_expand.push(initial);

    best_path->clear();
    size_t skip_count = 0;

    while (!to_expand.empty()) {
        RoutingNode node = to_expand.top();
        to_expand.pop();

        auto result = seen.emplace(node.wire_to_expand);
        if (!result.second) {
            // We've already done an expansion at this wire.
            skip_count += 1;
            continue;
        }

        bool has_site_pip = false;
        for (PipId pip : ctx->getPipsDownhill(node.wire_to_expand)) {
            if (ctx->is_pip_synthetic(pip)) {
                continue;
            }

            // Don't expand edges that are site pips, but do record how we
            // got to the pip before the site pip!
            if (ctx->is_site_port(pip)) {
                has_site_pip = true;
                continue;
            }

            WireId new_wire = ctx->getPipDstWire(pip);
            if (new_wire == WireId()) {
                continue;
            }

            RoutingNode next_node;
            next_node.wire_to_expand = new_wire;
            next_node.cost = node.cost + ctx->getPipDelay(pip).maxDelay() + ctx->getWireDelay(new_wire).maxDelay();
            next_node.depth = node.depth + 1;

            // Record best path.
            PipAndCost pip_and_cost;
            pip_and_cost.upstream_pip = pip;
            pip_and_cost.cost_from_src = next_node.cost;
            pip_and_cost.depth = next_node.depth;
            auto result = best_path->emplace(new_wire, pip_and_cost);
            bool is_best_path = true;
            if (!result.second) {
                if (result.first.second->cost_from_src > next_node.cost) {
                    result.first.second->cost_from_src = next_node.cost;
                    result.first.second->upstream_pip = pip;
                    result.first.second->depth = next_node.depth;
                } else {
                    is_best_path = false;
                }
            }

            Loc dst = ctx->getPipLocation(pip);
            int32_t dst_x = dst.x;
            int32_t dst_y = dst.y;
            if (is_best_path && std::abs(dst_x - src_x) < kMaxExploreDist &&
                std::abs(dst_y - src_y) < kMaxExploreDist && next_node.depth < storage->max_explore_depth) {
                to_expand.push(next_node);
            }
        }

        if (has_site_pip) {
            update_results(ctx, *best_path, first_wire, node.wire_to_expand, storage);
        }
    }
}

static bool has_multiple_outputs(const Context *ctx, WireId wire)
{
    size_t pip_count = 0;
    for (PipId pip : ctx->getPipsDownhill(wire)) {
        (void)pip;
        pip_count += 1;
    }

    return pip_count > 1;
}

static void expand_routing_graph(const Context *ctx, DeterministicRNG *rng, const Sampler &tiles_of_type,
                                 TypeWireId wire_type, pool<TypeWireSet> *types_explored, DelayStorage *storage,
                                 pool<TypeWireId> *types_deferred, FlatWireMap<PipAndCost> *best_path)
{
    pool<TypeWireSet> new_types_explored;

    for (size_t region = 0; region < tiles_of_type.number_of_regions(); ++region) {
        size_t tile = tiles_of_type.get_sample_from_region(region, [rng]() -> int32_t { return rng->rng(); });

        NPNR_ASSERT(ctx->chip_info->tiles[tile].type == wire_type.type);

        WireId wire = canonical_wire(ctx->chip_info, tile, wire_type.index);
        TypeWireSet wire_set(ctx, wire);

        if (!has_multiple_outputs(ctx, wire)) {
            types_deferred->emplace(wire_type);
            continue;
        }

        new_types_explored.emplace(wire_set);

        expand_routing_graph_from_wire(ctx, wire, best_path, storage);
    }

    for (const TypeWireSet &new_wire_set : new_types_explored) {
        types_explored->emplace(new_wire_set);
    }
}

static WireId follow_pip_chain(const Context *ctx, WireId wire, delay_t *delay_out)
{
    delay_t delay = 0;
    WireId cursor = wire;
    while (true) {
        WireId next;
        size_t pip_count = 0;
        delay_t next_delay = delay;
        for (PipId pip : ctx->getPipsDownhill(cursor)) {
            pip_count += 1;
            next = ctx->getPipDstWire(pip);
            next_delay += ctx->getPipDelay(pip).maxDelay() + ctx->getWireDelay(next).maxDelay();
        }

        if (pip_count > 1) {
            *delay_out = delay;
            return cursor;
        }

        if (next == WireId()) {
            return WireId();
        }

        delay = next_delay;

        cursor = next;
    }

    // Unreachable?
    NPNR_ASSERT(false);
}

static WireId follow_pip_chain_target(const Context *ctx, WireId wire, WireId target, delay_t *delay_out)
{
    delay_t delay = 0;
    WireId cursor = wire;
    while (cursor != target) {
        WireId next;
        size_t pip_count = 0;
        delay_t next_delay = delay;
        for (PipId pip : ctx->getPipsDownhill(cursor)) {
            pip_count += 1;
            next = ctx->getPipDstWire(pip);
            next_delay += ctx->getPipDelay(pip).maxDelay() + ctx->getWireDelay(next).maxDelay();
        }

        if (pip_count > 1) {
            *delay_out = delay;
            return cursor;
        }

        if (next == WireId()) {
            return WireId();
        }

        delay = next_delay;

        cursor = next;
    }

    *delay_out = delay;
    return cursor;
}

static WireId follow_pip_chain_up(const Context *ctx, WireId wire, delay_t *delay_out)
{
    delay_t delay = 0;
    WireId cursor = wire;
    while (true) {
        WireId next;
        size_t pip_count = 0;
        delay_t next_delay = delay;
        for (PipId pip : ctx->getPipsUphill(cursor)) {
            pip_count += 1;
            next = ctx->getPipSrcWire(pip);
            next_delay += ctx->getPipDelay(pip).maxDelay() + ctx->getWireDelay(next).maxDelay();
        }

        if (pip_count > 1) {
            *delay_out = delay;
            return cursor;
        }

        if (next == WireId()) {
            return WireId();
        }

        delay = next_delay;

        cursor = next;
    }

    // Unreachable?
    NPNR_ASSERT(false);
}

static void expand_deferred_routing_graph(const Context *ctx, DeterministicRNG *rng, const Sampler &tiles_of_type,
                                          TypeWireId wire_type, pool<TypeWireSet> *types_explored,
                                          DelayStorage *storage, FlatWireMap<PipAndCost> *best_path)
{
    pool<TypeWireSet> new_types_explored;

    for (size_t region = 0; region < tiles_of_type.number_of_regions(); ++region) {
        size_t tile = tiles_of_type.get_sample_from_region(region, [rng]() -> int32_t { return rng->rng(); });

        NPNR_ASSERT(ctx->chip_info->tiles[tile].type == wire_type.type);

        WireId wire = canonical_wire(ctx->chip_info, tile, wire_type.index);
        TypeWireSet wire_set(ctx, wire);
        if (types_explored->count(wire_set)) {
            // Check if this wire set has been expanded.
            continue;
        }

        delay_t delay;
        WireId routing_wire = follow_pip_chain(ctx, wire, &delay);

        // This wire doesn't go anywhere!
        if (routing_wire == WireId()) {
            continue;
        }

        TypeWireSet routing_wire_set(ctx, routing_wire);
        if (types_explored->count(routing_wire_set)) {
            continue;
        }

        new_types_explored.emplace(wire_set);
        expand_routing_graph_from_wire(ctx, wire, best_path, storage);
    }

    for (const TypeWireSet &new_wire_set : new_types_explored) {
        types_explored->emplace(new_wire_set);
    }
}

static void expand_output_type(const Context *ctx, DeterministicRNG *rng, const Sampler &tiles_of_type,
                               TypeWireId output_wire, Lookahead::OutputSiteWireCost *output_cost,
                               dict<TypeWirePair, delay_t> *site_to_site_cost)
{
    for (size_t region = 0; region < tiles_of_type.number_of_regions(); ++region) {
        size_t tile = tiles_of_type.get_sample_from_region(region, [rng]() -> int32_t { return rng->rng(); });

        NPNR_ASSERT(ctx->chip_info->tiles[tile].type == output_wire.type);
        WireId wire = canonical_wire(ctx->chip_info, tile, output_wire.index);

        expand_output(ctx, wire, output_cost, site_to_site_cost);
    }
}

static constexpr bool kWriteLookaheadCsv = false;

void write_lookahead_csv(const Context *ctx, const DelayStorage &all_tiles_storage)
{
    FILE *lookahead_data = fopen("lookahead.csv", "w");
    NPNR_ASSERT(lookahead_data != nullptr);
    fprintf(lookahead_data, "src_type,src_wire,dest_type,dest_wire,delta_x,delta_y,delay\n");
    for (const auto &type_pair : all_tiles_storage.storage) {
        auto &src_wire_type = type_pair.first.src;
        auto &src_type_data = ctx->chip_info->tile_types[src_wire_type.type];
        IdString src_type(src_type_data.name);
        IdString src_wire(src_type_data.wire_data[src_wire_type.index].name);

        auto &dst_wire_type = type_pair.first.dst;
        auto &dst_type_data = ctx->chip_info->tile_types[dst_wire_type.type];
        IdString dst_type(dst_type_data.name);
        IdString dst_wire(dst_type_data.wire_data[dst_wire_type.index].name);

        for (const auto &delta_pair : type_pair.second) {
            fprintf(lookahead_data, "%s,%s,%s,%s,%d,%d,%d\n", src_type.c_str(ctx), src_wire.c_str(ctx),
                    dst_type.c_str(ctx), dst_wire.c_str(ctx), delta_pair.first.first, delta_pair.first.second,
                    delta_pair.second);
        }
    }

    fclose(lookahead_data);
}

// Storage for tile type expansion for lookahead.
struct ExpandLocals
{
    virtual ~ExpandLocals() {}
    const std::vector<Sampler> *tiles_of_type;
    DeterministicRNG *rng;
    FlatWireMap<PipAndCost> *best_path;
    DelayStorage *storage;
    pool<TypeWireSet> *explored;
    pool<TypeWireId> *deferred;

    virtual void lock() {}
    virtual void unlock() {}
    virtual void copy_back(int32_t tile_type) {}
};

// Do tile type expansion for 1 tile.
static void expand_tile_type(const Context *ctx, int32_t tile_type, ExpandLocals *locals)
{
    auto &type_data = ctx->chip_info->tile_types[tile_type];
    if (ctx->verbose) {
        ScopeLock<ExpandLocals> lock(locals);
        log_info("Expanding all wires in type %s\n", IdString(type_data.name).c_str(ctx));
    }

    auto &tile_sampler = (*locals->tiles_of_type)[tile_type];
    for (size_t wire_index = 0; wire_index < type_data.wire_data.size(); ++wire_index) {
        auto &wire_data = type_data.wire_data[wire_index];
        if (wire_data.site != -1) {
            // Skip site wires
            continue;
        }

        if (ctx->debug) {
            ScopeLock<ExpandLocals> lock(locals);
            log_info("Expanding wire %s in type %s (%d/%zu, seen %zu types, deferred %zu types)\n",
                     IdString(wire_data.name).c_str(ctx), IdString(type_data.name).c_str(ctx), tile_type,
                     ctx->chip_info->tile_types.size(), locals->explored->size(), locals->deferred->size());
        }

        TypeWireId wire;
        wire.type = tile_type;
        wire.index = wire_index;

        expand_routing_graph(ctx, locals->rng, tile_sampler, wire, locals->explored, locals->storage, locals->deferred,
                             locals->best_path);
    }

    locals->copy_back(tile_type);
}

// Function that does all tile expansions serially.
static void expand_tile_type_serial(const Context *ctx, const std::vector<int32_t> &tile_types,
                                    const std::vector<Sampler> &tiles_of_type, DeterministicRNG *rng,
                                    FlatWireMap<PipAndCost> *best_path, DelayStorage *storage,
                                    pool<TypeWireSet> *explored, pool<TypeWireId> *deferred, pool<int32_t> *tiles_left)
{

    for (int32_t tile_type : tile_types) {
        ExpandLocals locals;

        locals.tiles_of_type = &tiles_of_type;
        locals.rng = rng;
        locals.best_path = best_path;
        locals.storage = storage;
        locals.explored = explored;
        expand_tile_type(ctx, tile_type, &locals);

        NPNR_ASSERT(tiles_left->erase(tile_type) == 1);
    }

    NPNR_ASSERT(tiles_left->empty());
}

// Additional storage for doing tile type expansion in parallel.
struct TbbExpandLocals : public ExpandLocals
{
    const Context *ctx;
    std::mutex *all_costs_mutex;

    DelayStorage *all_tiles_storage;
    pool<TypeWireSet> *types_explored;
    pool<TypeWireId> *types_deferred;
    pool<int32_t> *tiles_left;

    void lock() override { all_costs_mutex->lock(); }

    void unlock() override { all_costs_mutex->unlock(); }

    void copy_back(int32_t tile_type) override
    {
        ScopeLock<TbbExpandLocals> locker(this);

        auto &type_data = ctx->chip_info->tile_types[tile_type];

        // Copy per tile data by to over all data structures.
        if (ctx->verbose) {
            log_info("Expanded all wires in type %s, merging data back\n", IdString(type_data.name).c_str(ctx));
            log_info("Testing %zu wires, saw %zu types, deferred %zu types\n", type_data.wire_data.size(),
                     explored->size(), deferred->size());
        }

        // Copy cheapest explored paths back to all_tiles_storage.
        for (const auto &type_pair : storage->storage) {
            auto &type_pair_data = all_tiles_storage->storage[type_pair.first];
            for (const auto &delta_pair : type_pair.second) {
                // See if this dx/dy already has data.
                auto result = type_pair_data.emplace(delta_pair.first, delta_pair.second);
                if (!result.second) {
                    // This was already in the map, check if this new result is
                    // better
                    if (delta_pair.second < result.first->second) {
                        result.first->second = delta_pair.second;
                    }
                }
            }
        }

        // Update explored and deferred sets.
        for (auto &key : *explored) {
            types_explored->emplace(key);
        }
        for (auto &key : *deferred) {
            types_deferred->emplace(key);
        }

        NPNR_ASSERT(tiles_left->erase(tile_type));

        if (ctx->verbose) {
            log_info("Done merging data from type %s, %zu tiles left\n", IdString(type_data.name).c_str(ctx),
                     tiles_left->size());
        }
    }
};

// Wrapper method used if running expansion in parallel.
//
// expand_tile_type is invoked using thread local data, and then afterwards
// the data is joined with the global data.
static void expand_tile_type_parallel(const Context *ctx, int32_t tile_type, const std::vector<Sampler> &tiles_of_type,
                                      DeterministicRNG *rng, std::mutex *all_costs_mutex,
                                      DelayStorage *all_tiles_storage, pool<TypeWireSet> *types_explored,
                                      pool<TypeWireId> *types_deferred, pool<int32_t> *tiles_left)
{
    TbbExpandLocals locals;

    DeterministicRNG rng_copy = *rng;
    FlatWireMap<PipAndCost> best_path(ctx);
    pool<TypeWireSet> explored;
    pool<TypeWireId> deferred;
    DelayStorage storage;
    storage.max_explore_depth = all_tiles_storage->max_explore_depth;

    locals.tiles_of_type = &tiles_of_type;
    locals.rng = &rng_copy;
    locals.best_path = &best_path;
    locals.storage = &storage;
    locals.explored = &explored;
    locals.deferred = &deferred;

    locals.ctx = ctx;
    locals.all_costs_mutex = all_costs_mutex;
    locals.all_tiles_storage = all_tiles_storage;
    locals.types_explored = types_explored;
    locals.types_deferred = types_deferred;
    locals.tiles_left = tiles_left;

    expand_tile_type(ctx, tile_type, &locals);
}

void Lookahead::build_lookahead(const Context *ctx, DeterministicRNG *rng)
{
    auto start = std::chrono::high_resolution_clock::now();

    if (ctx->verbose) {
        log_info("Building lookahead, first gathering input and output site wires\n");
    }

    pool<TypeWireId> input_site_ports;
    for (BelId bel : ctx->getBels()) {
        const auto &bel_data = bel_info(ctx->chip_info, bel);

        for (IdString pin : ctx->getBelPins(bel)) {
            WireId pin_wire = ctx->getBelPinWire(bel, pin);
            if (pin_wire == WireId()) {
                continue;
            }

            PortType type = ctx->getBelPinType(bel, pin);

            if (type == PORT_IN && bel_data.category == BEL_CATEGORY_LOGIC) {
                input_site_wires.emplace(TypeWireId(ctx, pin_wire), std::vector<InputSiteWireCost>());
            } else if (type == PORT_OUT && bel_data.category == BEL_CATEGORY_LOGIC) {
                output_site_wires.emplace(TypeWireId(ctx, pin_wire), OutputSiteWireCost());
            } else if (type == PORT_OUT && bel_data.category == BEL_CATEGORY_SITE_PORT) {
                input_site_ports.emplace(TypeWireId(ctx, pin_wire));
            }
        }
    }

    if (ctx->verbose) {
        log_info("Have %zu input and %zu output site wire types. Creating tile type samplers\n",
                 input_site_wires.size(), output_site_wires.size());
    }

    std::vector<Sampler> tiles_of_type;
    tiles_of_type.resize(ctx->chip_info->tile_types.ssize());

    std::vector<size_t> indicies;
    std::vector<std::pair<int32_t, int32_t>> xys;
    indicies.reserve(ctx->chip_info->tiles.size());
    xys.reserve(ctx->chip_info->tiles.size());

    for (int32_t tile_type = 0; tile_type < ctx->chip_info->tile_types.ssize(); ++tile_type) {
        indicies.clear();
        xys.clear();

        for (size_t tile = 0; tile < ctx->chip_info->tiles.size(); ++tile) {
            if (ctx->chip_info->tiles[tile].type != tile_type) {
                continue;
            }

            std::pair<size_t, size_t> xy;
            ctx->get_tile_x_y(tile, &xy.first, &xy.second);

            indicies.push_back(tile);
            xys.push_back(xy);
        }

        auto &tile_sampler = tiles_of_type[tile_type];
        tile_sampler.divide_samples(kNumberSamples, xys);

        // Remap Sampler::indicies from 0 to number of tiles of type to
        // absolute tile indicies.
        for (size_t i = 0; i < indicies.size(); ++i) {
            tile_sampler.indicies[i] = indicies[tile_sampler.indicies[i]];
        }
    }

    if (ctx->verbose) {
        log_info("Expanding input site wires\n");
    }

    // Expand backwards from each input site wire to find the cheapest
    // non-site wire.
    for (auto &input_pair : input_site_wires) {
        expand_input_type(ctx, rng, tiles_of_type[input_pair.first.type], input_pair.first, &input_pair.second);
    }

    if (ctx->verbose) {
        log_info("Expanding output site wires\n");
    }

    // Expand forward from each output site wire to find the cheapest
    // non-site wire.
    for (auto &output_pair : output_site_wires) {
        output_pair.second.cost = std::numeric_limits<delay_t>::max();
        expand_output_type(ctx, rng, tiles_of_type[output_pair.first.type], output_pair.first, &output_pair.second,
                           &site_to_site_cost);
    }
    for (TypeWireId input_site_port : input_site_ports) {
        expand_output_type(ctx, rng, tiles_of_type[input_site_port.type], input_site_port, nullptr, &site_to_site_cost);
    }

    if (ctx->verbose) {
        log_info("Expanding all wire types\n");
    }

    DelayStorage all_tiles_storage;
    all_tiles_storage.max_explore_depth = kInitialExploreDepth;

    // These are wire types that have been explored.
    pool<TypeWireSet> types_explored;

    // These are wire types that have been deferred because they are trival
    // copies of another wire type.  These can be cheaply computed after the
    // graph has been explored.
    pool<TypeWireId> types_deferred;

    std::vector<int32_t> tile_types;
    pool<int32_t> tiles_left;
    tile_types.reserve(ctx->chip_info->tile_types.size());
    for (int32_t tile_type = 0; tile_type < ctx->chip_info->tile_types.ssize(); ++tile_type) {
        tile_types.push_back(tile_type);
        tiles_left.emplace(tile_type);
    }

    FlatWireMap<PipAndCost> best_path(ctx);

    // Walk each tile type, and expand all non-site wires in the tile.
    // Wires that are nodes will expand as if the node type is the first node
    // in the wire.
    //
    // Wires that only have 1 output pip are deferred until the next loop,
    // because generally those wires will get explored via another wire.
    // The deferred will be expanded if this assumption doesn't hold.
    bool expand_serially = true;
#if defined(NEXTPNR_USE_TBB) // Run parallely
    {
        std::mutex all_costs_mutex;

        expand_serially = false;
        tbb::parallel_for_each(tile_types, [&](int32_t tile_type) {
            expand_tile_type_parallel(ctx, tile_type, tiles_of_type, rng, &all_costs_mutex, &all_tiles_storage,
                                      &types_explored, &types_deferred, &tiles_left);
        });
    }
#else
    // Supress warning that expand_tile_type_parallel if not running in
    // parallel.
    (void)expand_tile_type_parallel;
#endif
    if (expand_serially) {
        expand_tile_type_serial(ctx, tile_types, tiles_of_type, rng, &best_path, &all_tiles_storage, &types_explored,
                                &types_deferred, &tiles_left);
    }

    // Check to see if deferred wire types were expanded.  If they were not
    // expanded, expand them now.  If they were expanded, copy_types is
    // populated with the wire types that can just copy the relevant data from
    // another wire type.
    for (TypeWireId wire_type : types_deferred) {
        auto &type_data = ctx->chip_info->tile_types[wire_type.type];
        auto &tile_sampler = tiles_of_type[wire_type.type];
        auto &wire_data = type_data.wire_data[wire_type.index];

        if (ctx->verbose) {
            log_info("Expanding deferred wire %s in type %s (seen %zu types)\n", IdString(wire_data.name).c_str(ctx),
                     IdString(type_data.name).c_str(ctx), types_explored.size());
        }

        expand_deferred_routing_graph(ctx, rng, tile_sampler, wire_type, &types_explored, &all_tiles_storage,
                                      &best_path);
    }

    auto end = std::chrono::high_resolution_clock::now();
    if (ctx->verbose) {
        log_info("Done with expansion, dt %02fs\n", std::chrono::duration<float>(end - start).count());
    }

    if (kWriteLookaheadCsv) {
        write_lookahead_csv(ctx, all_tiles_storage);
        end = std::chrono::high_resolution_clock::now();
        if (ctx->verbose) {
            log_info("Done writing data to disk, dt %02fs\n", std::chrono::duration<float>(end - start).count());
        }
    }

#if defined(NEXTPNR_USE_TBB) // Run parallely
    tbb::parallel_for_each(all_tiles_storage.storage,
                           [&](const std::pair<TypeWirePair, dict<std::pair<int32_t, int32_t>, delay_t>> &type_pair) {
#else
    for (const auto &type_pair : all_tiles_storage.storage) {
#endif
                               cost_map.set_cost_map(ctx, type_pair.first, type_pair.second);
#if defined(NEXTPNR_USE_TBB) // Run parallely
                           });
#else
    }
#endif

    end = std::chrono::high_resolution_clock::now();
    if (ctx->verbose) {
        log_info("build_lookahead time %.02fs\n", std::chrono::duration<float>(end - start).count());
    }
}

constexpr static bool kUseGzipForLookahead = false;

static void write_message(::capnp::MallocMessageBuilder &message, const std::string &filename)
{
    kj::Array<capnp::word> words = messageToFlatArray(message);
    kj::ArrayPtr<kj::byte> bytes = words.asBytes();

    boost::filesystem::path temp = boost::filesystem::unique_path();
    log_info("Writing tempfile to %s\n", temp.c_str());

    if (kUseGzipForLookahead) {
        gzFile file = gzopen(temp.c_str(), "w");
        NPNR_ASSERT(file != Z_NULL);

        size_t bytes_written = 0;
        int result;
        while (bytes_written < bytes.size()) {
            size_t bytes_remaining = bytes.size() - bytes_written;
            size_t bytes_to_write = bytes_remaining;
            if (bytes_to_write >= std::numeric_limits<int>::max()) {
                bytes_to_write = std::numeric_limits<int>::max();
            }
            result = gzwrite(file, &bytes[0] + bytes_written, bytes_to_write);
            if (result < 0) {
                break;
            }

            bytes_written += result;
        }

        int error;
        std::string error_str;
        if (result < 0) {
            error_str.assign(gzerror(file, &error));
        }
        NPNR_ASSERT(gzclose(file) == Z_OK);
        if (bytes_written != bytes.size()) {
            // Remove failed writes before reporting error.
            boost::filesystem::remove(temp);
        }

        if (result < 0) {
            log_error("Failed to write lookahead, error from gzip %s\n", error_str.c_str());
        } else {
            if (bytes_written != bytes.size()) {
                log_error("Failed to write lookahead, wrote %d bytes, had %zu bytes\n", result, bytes.size());
            } else {
                // Written, move file into place
                boost::filesystem::rename(temp, filename);
            }
        }
    } else {
        {
            kj::Own<kj::Filesystem> fs = kj::newDiskFilesystem();

            auto path = kj::Path::parse(temp);
            auto file = fs->getCurrent().openFile(path, kj::WriteMode::CREATE);
            file->writeAll(bytes);
        }

        boost::filesystem::rename(temp, filename);
    }
}

bool Lookahead::read_lookahead(const std::string &chipdb_hash, const std::string &filename)
{
    capnp::ReaderOptions reader_options;
    reader_options.traversalLimitInWords = 32llu * 1024llu * 1024llu * 1024llu;

    if (kUseGzipForLookahead) {
        gzFile file = gzopen(filename.c_str(), "r");
        if (file == Z_NULL) {
            return false;
        }

        std::vector<uint8_t> output_data;
        output_data.resize(4096);
        std::stringstream sstream(std::ios_base::in | std::ios_base::out | std::ios_base::binary);
        while (true) {
            int ret = gzread(file, output_data.data(), output_data.size());
            NPNR_ASSERT(ret >= 0);
            if (ret > 0) {
                sstream.write((const char *)output_data.data(), ret);
                NPNR_ASSERT(sstream);
            } else {
                NPNR_ASSERT(ret == 0);
                int error;
                gzerror(file, &error);
                NPNR_ASSERT(error == Z_OK);
                break;
            }
        }

        NPNR_ASSERT(gzclose(file) == Z_OK);

        sstream.seekg(0);
        kj::std::StdInputStream istream(sstream);
        capnp::InputStreamMessageReader message_reader(istream, reader_options);

        lookahead_storage::Lookahead::Reader lookahead = message_reader.getRoot<lookahead_storage::Lookahead>();
        return from_reader(chipdb_hash, lookahead);
    } else {
        boost::iostreams::mapped_file_source file;
        try {
            file.open(filename.c_str());
        } catch (std::ios_base::failure &fail) {
            return false;
        }

        if (!file.is_open()) {
            return false;
        }

        const char *data = reinterpret_cast<const char *>(file.data());
        const kj::ArrayPtr<const ::capnp::word> words =
                kj::arrayPtr(reinterpret_cast<const ::capnp::word *>(data), file.size() / sizeof(::capnp::word));
        ::capnp::FlatArrayMessageReader reader(words, reader_options);
        lookahead_storage::Lookahead::Reader lookahead = reader.getRoot<lookahead_storage::Lookahead>();
        return from_reader(chipdb_hash, lookahead);
    }
}

void Lookahead::write_lookahead(const std::string &chipdb_hash, const std::string &file) const
{
    ::capnp::MallocMessageBuilder message;

    lookahead_storage::Lookahead::Builder lookahead = message.initRoot<lookahead_storage::Lookahead>();
    to_builder(chipdb_hash, lookahead);
    write_message(message, file);
}

void Lookahead::init(const Context *ctx, DeterministicRNG *rng)
{
    std::string lookahead_filename;
    if (kUseGzipForLookahead) {
        lookahead_filename = ctx->args.chipdb + ".lookahead.tgz";
    } else {
        lookahead_filename = ctx->args.chipdb + ".lookahead";
    }

    std::string chipdb_hash = ctx->get_chipdb_hash();

    if (ctx->args.rebuild_lookahead || !read_lookahead(chipdb_hash, lookahead_filename)) {
        build_lookahead(ctx, rng);
        if (!ctx->args.dont_write_lookahead) {
            write_lookahead(chipdb_hash, lookahead_filename);
        }
    }
}

static bool safe_add_i32(int32_t a, int32_t b, int32_t *out)
{
#if defined(__GNUG__) || defined(__clang__)
    // GCC and clang have had __builtin_add_overflow for a while.
    return !__builtin_add_overflow(a, b, out);
#else
    // MSVC and other don't have an intrinsic.  Emit some more code.
    bool safe_to_add;
    if (b < 0) {
        safe_to_add = a >= std::numeric_limits<int32_t>::min() - b;
    } else {
        safe_to_add = a <= std::numeric_limits<int32_t>::max() - b;
    }
    if (!safe_to_add) {
        return false;
    }
    *out = a + b;
    return true;
#endif
}

static void saturating_incr(int32_t *acc, int32_t value)
{
    if (!safe_add_i32(*acc, value, acc)) {
        if (value > 0) {
            *acc = std::numeric_limits<int32_t>::max();
        } else {
            *acc = std::numeric_limits<int32_t>::min();
        }
    }
}

#define DEBUG_LOOKUP

delay_t Lookahead::estimateDelay(const Context *ctx, WireId src, WireId dst) const
{
#ifdef DEBUG_LOOKUP
    if (ctx->debug) {
        log_info("Looking up %s to %s\n", ctx->nameOfWire(src), ctx->nameOfWire(dst));
    }
#endif
    delay_t delay = 0;

    // Follow chain down, chasing wires with only 1 pip.  Stop if dst is
    // reached.
    WireId orig_src = src;
    src = follow_pip_chain_target(ctx, src, dst, &delay);
    NPNR_ASSERT(delay >= 0);
    if (src == WireId()) {
        // This src wire is a dead end, tell router to avoid it!
#ifdef DEBUG_LOOKUP
        if (ctx->debug) {
            log_info("Source %s is a dead end!\n", ctx->nameOfWire(orig_src));
        }
#endif
        return std::numeric_limits<delay_t>::max();
    }

#ifdef DEBUG_LOOKUP
    if (ctx->debug && src != orig_src) {
        log_info("Moving src from %s to %s, delay = %d\n", ctx->nameOfWire(orig_src), ctx->nameOfWire(src), delay);
    }
#endif

    if (src == dst) {
        // Reached target already, done!
        return delay;
    }

    if (ctx->is_same_site(src, dst)) {
        // Check for site to site direct path.
        TypeWirePair pair;

        TypeWireId src_type(ctx, src);
        pair.src = src_type;

        TypeWireId dst_type(ctx, dst);
        pair.dst = dst_type;

        auto iter = site_to_site_cost.find(pair);
        if (iter != site_to_site_cost.end()) {
            NPNR_ASSERT(iter->second >= 0);
            saturating_incr(&delay, iter->second);
#ifdef DEBUG_LOOKUP
            if (ctx->debug) {
                log_info("Found site to site direct path %s -> %s = %d\n", ctx->nameOfWire(src), ctx->nameOfWire(dst),
                         delay);
            }
#endif
            return delay;
        }
    }

    // At this point we know that the routing interconnect is needed, or
    // the pair is unreachable.
    orig_src = src;
    TypeWireId src_type(ctx, src);

    // Find the first routing wire from the src_type.
    auto src_iter = output_site_wires.find(src_type);
    if (src_iter != output_site_wires.end()) {
        NPNR_ASSERT(src_iter->second.cost >= 0);
        saturating_incr(&delay, src_iter->second.cost);
        src_type = src_iter->second.cheapest_route_from;

        src = canonical_wire(ctx->chip_info, src.tile, src_type.index);
#ifdef DEBUG_LOOKUP
        if (ctx->debug) {
            log_info("Moving src from %s to %s, delay = %d\n", ctx->nameOfWire(orig_src), ctx->nameOfWire(src), delay);
        }
#endif
    }

    // Make sure that the new wire is in the routing graph.
    if (ctx->is_wire_in_site(src)) {
#ifdef DEBUG_LOOKUP
        // We've already tested for direct site to site routing, if src cannot
        // reach outside of the routing network, this path is impossible.
        if (ctx->debug) {
            log_warning("Failed to reach routing network for src %s, got to %s\n", ctx->nameOfWire(orig_src),
                        ctx->nameOfWire(src));
        }
#endif
        return std::numeric_limits<delay_t>::max();
    }

    if (src == dst) {
        // Reached target already, done!
        return delay;
    }

    // Find the first routing wire that reaches dst_type.
    WireId orig_dst = dst;
    TypeWireId dst_type(ctx, dst);

    auto dst_iter = input_site_wires.find(dst_type);
    if (dst_iter == input_site_wires.end()) {
        // dst_type isn't an input site wire, just add point to point delay.
        auto &dst_data = ctx->wire_info(dst);
        if (dst_data.site != -1) {
#ifdef DEBUG_LOOKUP
            // We've already tested for direct site to site routing, if dst cannot
            // be reached from the routing network, this path is impossible.
            if (ctx->debug) {
                log_warning("Failed to reach routing network for dst %s, got to %s\n", ctx->nameOfWire(orig_dst),
                            ctx->nameOfWire(dst));
            }
#endif
            return std::numeric_limits<delay_t>::max();
        }

        // Follow chain up
        WireId orig_dst = dst;
        (void)orig_dst;

        delay_t chain_delay;
        dst = follow_pip_chain_up(ctx, dst, &chain_delay);
        NPNR_ASSERT(chain_delay >= 0);
        saturating_incr(&delay, chain_delay);
#ifdef DEBUG_LOOKUP
        if (ctx->debug && dst != orig_dst) {
            log_info("Moving dst from %s to %s, delay = %d\n", ctx->nameOfWire(orig_dst), ctx->nameOfWire(dst), delay);
        }
#endif

        if (src == dst) {
            // Reached target already, done!
            return delay;
        }

        // Both src and dst are in the routing graph, lookup approx cost to go
        // from src to dst.
        int32_t delay_from_map = cost_map.get_delay(ctx, src, dst);
        NPNR_ASSERT(delay_from_map >= 0);
        saturating_incr(&delay, delay_from_map);

#ifdef DEBUG_LOOKUP
        if (ctx->debug) {
            log_info("Final delay = %d\n", delay);
        }
#endif

        return delay;
    } else {
        // dst_type is an input site wire, try each possible routing path.
        delay_t base_delay = delay;
        delay_t cheapest_path = std::numeric_limits<delay_t>::max();

        for (const InputSiteWireCost &input_cost : dst_iter->second) {
            dst = orig_dst;
            delay = base_delay;

            NPNR_ASSERT(input_cost.cost >= 0);
            saturating_incr(&delay, input_cost.cost);
            dst_type = input_cost.route_to;

            NPNR_ASSERT(dst_type.index != -1);
            dst = canonical_wire(ctx->chip_info, dst.tile, dst_type.index);
            NPNR_ASSERT(dst != WireId());

#ifdef DEBUG_LOOKUP
            if (ctx->debug) {
                log_info("Moving dst from %s to %s, delay = %d\n", ctx->nameOfWire(orig_dst), ctx->nameOfWire(dst),
                         delay);
            }
#endif

            if (dst == src) {
#ifdef DEBUG_LOOKUP
                if (ctx->debug) {
                    log_info("Possible delay = %d\n", delay);
                }
#endif
                // Reached target already, done!
                cheapest_path = std::min(delay, cheapest_path);
                continue;
            }

            auto &dst_data = ctx->wire_info(dst);
            if (dst_data.site != -1) {
#ifdef DEBUG_LOOKUP
                // We've already tested for direct site to site routing, if dst cannot
                // be reached from the routing network, this path is impossible.
                if (ctx->debug) {
                    log_warning("Failed to reach routing network for dst %s, got to %s\n", ctx->nameOfWire(orig_dst),
                                ctx->nameOfWire(dst));
                }
#endif
                continue;
            }

            // Follow chain up
            WireId orig_dst = dst;
            (void)orig_dst;

            delay_t chain_delay;
            dst = follow_pip_chain_up(ctx, dst, &chain_delay);
            NPNR_ASSERT(chain_delay >= 0);
            saturating_incr(&delay, chain_delay);
#ifdef DEBUG_LOOKUP
            if (ctx->debug && dst != orig_dst) {
                log_info("Moving dst from %s to %s, delay = %d\n", ctx->nameOfWire(orig_dst), ctx->nameOfWire(dst),
                         delay);
            }
#endif

            if (dst == WireId()) {
                // This dst wire is a dead end, don't examine it!
#ifdef DEBUG_LOOKUP
                if (ctx->debug) {
                    log_info("Dest %s is a dead end!\n", ctx->nameOfWire(dst));
                }
#endif
                continue;
            }

            if (src == dst) {
#ifdef DEBUG_LOOKUP
                if (ctx->debug) {
                    log_info("Possible delay = %d\n", delay);
                }
#endif
                // Reached target already, done!
                cheapest_path = std::min(delay, cheapest_path);
                continue;
            }

            // Both src and dst are in the routing graph, lookup approx cost to go
            // from src to dst.
            int32_t delay_from_map = cost_map.get_delay(ctx, src, dst);
            NPNR_ASSERT(delay_from_map >= 0);
            saturating_incr(&delay, delay_from_map);
            cheapest_path = std::min(delay, cheapest_path);
#ifdef DEBUG_LOOKUP
            if (ctx->debug) {
                log_info("Possible delay = %d\n", delay);
            }
#endif
        }

#ifdef DEBUG_LOOKUP
        if (ctx->debug) {
            log_info("Final delay = %d\n", delay);
        }
#endif

        return cheapest_path;
    }
}

bool Lookahead::from_reader(const std::string &chipdb_hash, lookahead_storage::Lookahead::Reader reader)
{
    std::string expected_hash = reader.getChipdbHash();
    if (chipdb_hash != expected_hash) {
        return false;
    }

    input_site_wires.clear();
    output_site_wires.clear();
    site_to_site_cost.clear();

    for (auto input_reader : reader.getInputSiteWires()) {
        TypeWireId key(input_reader.getKey());

        auto result = input_site_wires.emplace(key, std::vector<InputSiteWireCost>());
        NPNR_ASSERT(result.second);
        std::vector<InputSiteWireCost> &costs = result.first->second;
        auto value = input_reader.getValue();
        costs.reserve(value.size());
        for (auto cost : value) {
            costs.emplace_back(InputSiteWireCost{TypeWireId(cost.getRouteTo()), cost.getCost()});
        }
    }

    for (auto output_reader : reader.getOutputSiteWires()) {
        TypeWireId key(output_reader.getKey());

        auto result = output_site_wires.emplace(
                key, OutputSiteWireCost{TypeWireId(output_reader.getCheapestRouteFrom()), output_reader.getCost()});
        NPNR_ASSERT(result.second);
    }

    for (auto site_to_site_reader : reader.getSiteToSiteCost()) {
        TypeWirePair key(site_to_site_reader.getKey());
        auto result = site_to_site_cost.emplace(key, site_to_site_reader.getCost());
        NPNR_ASSERT(result.second);
    }

    cost_map.from_reader(reader.getCostMap());

    return true;
}

void Lookahead::to_builder(const std::string &chipdb_hash, lookahead_storage::Lookahead::Builder builder) const
{
    builder.setChipdbHash(chipdb_hash);

    auto input_out = builder.initInputSiteWires(input_site_wires.size());
    auto in = input_site_wires.begin();
    for (auto out = input_out.begin(); out != input_out.end(); ++out, ++in) {
        NPNR_ASSERT(in != input_site_wires.end());

        const TypeWireId &key = in->first;
        key.to_builder(out->getKey());

        const std::vector<InputSiteWireCost> &costs = in->second;
        auto value = out->initValue(costs.size());

        auto value_in = costs.begin();
        for (auto value_out = value.begin(); value_out != value.end(); ++value_out, ++value_in) {
            value_in->route_to.to_builder(value_out->getRouteTo());
            value_out->setCost(value_in->cost);
        }
    }

    auto output_out = builder.initOutputSiteWires(output_site_wires.size());
    auto out = output_site_wires.begin();
    for (auto out2 = output_out.begin(); out2 != output_out.end(); ++out, ++out2) {
        NPNR_ASSERT(out != output_site_wires.end());

        const TypeWireId &key = out->first;
        key.to_builder(out2->getKey());

        const TypeWireId &cheapest_route_from = out->second.cheapest_route_from;
        cheapest_route_from.to_builder(out2->getCheapestRouteFrom());

        out2->setCost(out->second.cost);
    }

    auto site_out = builder.initSiteToSiteCost(site_to_site_cost.size());
    auto site = site_to_site_cost.begin();
    for (auto out2 = site_out.begin(); out2 != site_out.end(); ++out2, ++site) {
        NPNR_ASSERT(site != site_to_site_cost.end());

        const TypeWirePair &key = site->first;
        key.to_builder(out2->getKey());
        out2->setCost(site->second);
    }

    cost_map.to_builder(builder.getCostMap());
}

NEXTPNR_NAMESPACE_END
