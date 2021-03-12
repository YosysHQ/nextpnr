/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Symbiflow Authors
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

#include "nextpnr.h"

#include "log.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

#include "site_arch.h"

NEXTPNR_NAMESPACE_BEGIN

bool verbose_site_router(const Context *ctx) { return ctx->debug; }

bool verbose_site_router(const SiteArch *ctx) {
    return verbose_site_router(ctx->ctx);
}

void SiteRouter::bindBel(CellInfo *cell)
{
    auto result = cells_in_site.emplace(cell);
    NPNR_ASSERT(result.second);

    dirty = true;
}

void SiteRouter::unbindBel(CellInfo *cell)
{
    NPNR_ASSERT(cells_in_site.erase(cell) == 1);

    dirty = true;
}

bool check_initial_wires(const Context *ctx, SiteInformation *site_info)
{
    // Propagate from BEL pins to first wire, checking for trivial routing
    // conflicts.
    absl::flat_hash_map<WireId, NetInfo*> wires;

    for (CellInfo *cell : site_info->cells_in_site) {
        BelId bel = cell->bel;
        for (const auto &pin_pair : cell->cell_bel_pins) {
            const PortInfo &port = cell->ports.at(pin_pair.first);
            NPNR_ASSERT(port.net != nullptr);

            for (IdString bel_pin_name : pin_pair.second) {
                BelPin bel_pin;
                bel_pin.bel = bel;
                bel_pin.pin = bel_pin_name;

                WireId wire = ctx->getBelPinWire(bel_pin.bel, bel_pin.pin);
                auto result = wires.emplace(wire, port.net);
                if(!result.second) {
                    // This wire is already in use, make sure the net bound is
                    // the same net, otherwise there is a trivial net
                    // conflict.
                    const NetInfo *other_net = result.first->second;
                    if (other_net != port.net) {
                        // We have a direct net conflict at the BEL pin,
                        // immediately short circuit the site routing check.
                        if (verbose_site_router(ctx)) {
                            log_info("Direct net conflict detected for cell %s:%s at bel %s, net %s != %s\n",
                                    cell->name.c_str(ctx), cell->type.c_str(ctx), ctx->nameOfBel(cell->bel),
                                    port.net->name.c_str(ctx), other_net->name.c_str(ctx));
                        }

                        return false;
                    }
                }
            }
        }
    }

    return true;
}

struct RouteNode
{
    void clear()
    {
        parent = std::list<RouteNode>::iterator();
        leafs.clear();
        pip = SitePip();
        wire = SiteWire();
        flags = 0;
    }

    enum Flags {
        // Has this path left the site?
        LEFT_SITE = 0,
        // Has this path entered the site?
        ENTERED_SITE = 1,
    };

    bool has_left_site() const {
        return (flags & (1 << LEFT_SITE)) != 0;
    }

    bool can_leave_site() const {
        return !has_left_site();
    }

    void mark_left_site() {
        flags |= (1 << LEFT_SITE);
    }

    bool has_entered_site() const {
        return (flags & (1 << ENTERED_SITE)) != 0;
    }

    bool can_enter_site() const {
        return !has_entered_site();
    }

    void mark_entered_site() {
        flags |= (1 << ENTERED_SITE);
    }

    using Node = std::list<RouteNode>::iterator;

    Node parent;
    std::vector<Node> leafs;

    SitePip pip;   // What pip was taken to reach this node.
    SiteWire wire; // What wire is this routing node located at?
    int32_t flags;

};

struct RouteNodeStorage
{
    // Free list of nodes.
    std::list<RouteNode> nodes;

    // Either allocate a new node if no nodes are on the free list, or return
    // an element from the free list.
    std::list<RouteNode>::iterator alloc_node(std::list<RouteNode> &new_owner)
    {
        if (nodes.empty()) {
            nodes.emplace_front(RouteNode());
        }

        auto ret = nodes.begin();
        new_owner.splice(new_owner.end(), nodes, ret);

        ret->clear();

        return ret;
    }

    // Return 1 node from the current owner to the free list.
    void free_node(std::list<RouteNode> &owner, std::list<RouteNode>::iterator node)
    {
        nodes.splice(nodes.end(), owner, node);
    }

    // Return all node from the current owner to the free list.
    void free_nodes(std::list<RouteNode> &owner)
    {
        nodes.splice(nodes.end(), owner);
        NPNR_ASSERT(owner.empty());
    }
};

bool is_invalid_site_port(const SiteArch *ctx, const SiteNetInfo *net, const SitePip &pip) {
    if(pip.type != SitePip::SITE_PORT) {
        // This isn't a site port, so its valid!
        return false;
    }

    if(ctx->ctx->is_pip_synthetic(pip.pip)) {
        // FIXME: Not all synthetic pips are for constant networks.
        // FIXME: Need to mark if synthetic site ports are for the GND or VCC
        // network, and only allow the right one.  Otherwise site router
        // could route a VCC on a GND only net (or equiv).
        IdString gnd_net_name(ctx->ctx->chip_info->constants->gnd_net_name);
        IdString vcc_net_name(ctx->ctx->chip_info->constants->vcc_net_name);
        return net->net->name != gnd_net_name && net->net->name != vcc_net_name;
    } else {
        // All non-synthetic site ports are valid
        return false;
    }
}

struct SiteExpansionLoop
{
    const SiteArch *const ctx;
    RouteNodeStorage *const node_storage;

    using Node = RouteNode::Node;

    SiteExpansionLoop(const SiteArch *ctx, RouteNodeStorage *node_storage) : ctx(ctx), node_storage(node_storage)
    {
        NPNR_ASSERT(node_storage != nullptr);
    }

    ~SiteExpansionLoop() { node_storage->free_nodes(nodes); }

    // Storage for nodes
    std::list<RouteNode> nodes;

    const SiteNetInfo *net_for_wire;
    absl::flat_hash_map<RouteNode *, Node> completed_routes;
    absl::flat_hash_map<SiteWire, std::vector<Node>> wire_to_nodes;

    Node new_node(const SiteWire & wire, const SitePip & pip, Node parent)
    {
        auto node = node_storage->alloc_node(nodes);
        node->wire = wire;
        node->pip = pip;
        node->parent = parent;
        if(parent != Node()) {
            node->flags = parent->flags;
        }

        if(pip.type == SitePip::SITE_PORT) {
            // Site ports should always have a parent!
            NPNR_ASSERT(parent != Node());
            if(wire.type == SiteWire::SITE_PORT_SINK) {
                NPNR_ASSERT(parent->wire.type == SiteWire::SITE_WIRE);
                NPNR_ASSERT(node->can_leave_site());
                node->mark_left_site();
            } else if(wire.type == SiteWire::SITE_PORT_SOURCE) {
                // This is a backward walk, so this is considered entering
                // the site.
                NPNR_ASSERT(parent->wire.type == SiteWire::SITE_WIRE);
                NPNR_ASSERT(node->can_enter_site());
                node->mark_entered_site();
            } else {
                // See if this is a forward or backward walk.
                NPNR_ASSERT(wire.type == SiteWire::SITE_WIRE);
                if(parent->wire.type == SiteWire::SITE_PORT_SINK) {
                    // This is a backward walk, so this is considered leaving
                    // the site.
                    NPNR_ASSERT(node->can_leave_site());
                    node->mark_left_site();
                } else {
                    NPNR_ASSERT(parent->wire.type == SiteWire::SITE_PORT_SOURCE);
                    NPNR_ASSERT(node->can_enter_site());
                    node->mark_entered_site();
                }
            }
        }

        return node;
    }

    void free_node(Node node) { node_storage->free_node(nodes, node); }

    // Expand from wire specified, always downhill.
    bool expand_net(const SiteNetInfo *net)
    {
        net_for_wire = net;

        if (verbose_site_router(ctx->ctx)) {
            log_info("Expanding net %s from %s\n", ctx->nameOfNet(net_for_wire), ctx->nameOfWire(net_for_wire->driver));
        }

        completed_routes.clear();
        wire_to_nodes.clear();
        node_storage->free_nodes(nodes);

        auto node = new_node(net_for_wire->driver, SitePip(), /*parent=*/Node());

        absl::flat_hash_set<SiteWire> targets;
        targets.insert(net_for_wire->users.begin(), net_for_wire->users.end());

        if (verbose_site_router(ctx)) {
            log_info("%zu targets:\n", targets.size());
            for(auto & target : targets) {
                log_info(" - %s\n", ctx->nameOfWire(target));
            }
        }

        std::vector<Node> nodes_to_expand;
        nodes_to_expand.push_back(node);

        while (!nodes_to_expand.empty()) {
            Node parent_node = nodes_to_expand.back();
            nodes_to_expand.pop_back();

            for (SitePip pip : ctx->getPipsDownhill(parent_node->wire)) {
                if(is_invalid_site_port(ctx, net_for_wire, pip)) {
                    if (verbose_site_router(ctx)) {
                        log_info("Pip %s is not a valid site port for net %s, skipping\n",
                                ctx->nameOfPip(pip),
                                ctx->nameOfNet(net_for_wire));
                    }
                    continue;
                }

                SiteWire wire = ctx->getPipDstWire(pip);

                if(pip.type == SitePip::SITE_PORT) {
                    if(wire.type == SiteWire::SITE_PORT_SINK) {
                        if(!parent_node->can_leave_site()) {
                            // This path has already left the site once, don't leave it again!
                            if (verbose_site_router(ctx)) {
                                log_info("Pip %s is not a valid for this path because it has already left the site\n",
                                        ctx->nameOfPip(pip));
                            }
                            continue;
                        }
                    } else {
                        NPNR_ASSERT(parent_node->wire.type == SiteWire::SITE_PORT_SOURCE);
                        if(!parent_node->can_enter_site()) {
                            // This path has already entered the site once,
                            // don't enter it again!
                            if (verbose_site_router(ctx)) {
                                log_info("Pip %s is not a valid for this path because it has already entered the site\n",
                                        ctx->nameOfPip(pip));
                            }
                            continue;
                        }
                    }
                }

                auto wire_iter = ctx->wire_to_nets.find(wire);
                if(wire_iter != ctx->wire_to_nets.end() && wire_iter->second.net != net_for_wire) {
                    if (verbose_site_router(ctx)) {
                        log_info("Wire %s is already tied to net %s, not exploring for net %s\n",
                                ctx->nameOfWire(wire),
                                ctx->nameOfNet(wire_iter->second.net),
                                ctx->nameOfNet(net_for_wire));
                    }
                    continue;
                }

                auto node = new_node(wire, pip, parent_node);
                if(targets.count(wire)) {
                    completed_routes.emplace(&*node, node);
                }

                nodes_to_expand.push_back(node);
            }
        }

        // Make sure expansion reached all targets, otherwise this site is
        // already unroutable!
        for(auto & route_pair : completed_routes) {
            targets.erase(route_pair.first->wire);
        }

        build_solution_lookup();

        return targets.empty();
    }

    void build_solution_lookup() {
        absl::flat_hash_set<RouteNode*> nodes_visited;
        std::vector<Node> nodes_to_process;
        nodes_to_process.reserve(completed_routes.size());

        for(auto & route_pair : completed_routes) {
            nodes_to_process.push_back(route_pair.second);
        }

        while(!nodes_to_process.empty()) {
            Node node = nodes_to_process.back();
            nodes_to_process.pop_back();;

            auto result = nodes_visited.emplace(&*node);
            if(!result.second) {
                // Already been here, don't need to process it again!
                continue;
            }

            if(node->parent != Node()) {
                node->parent->leafs.push_back(node);
            }
            wire_to_nodes[node->wire].push_back(node);
        }
    }

    // Remove any routes that use specified wire.
    void remove_wire(const SiteWire &wire)
    {
        auto iter = wire_to_nodes.find(wire);
        if (iter == wire_to_nodes.end()) {
            // This wire was not in use, done!
            return;
        }

        // We need to prune the tree of nodes starting from any node that
        // uses the specified wire. Create a queue of nodes to follow to
        // gather all nodes that need to be removed.
        std::list<RouteNode> nodes_to_follow;
        for (Node node : iter->second) {
            nodes_to_follow.splice(nodes_to_follow.end(), nodes, node);
        }

        // Follow all nodes to their end, mark that node to be eventually removed.
        std::list<RouteNode> nodes_to_remove;
        while (!nodes_to_follow.empty()) {
            Node node = nodes_to_follow.begin();
            nodes_to_remove.splice(nodes_to_remove.end(), nodes_to_follow, node);

            for (Node child_node : node->leafs) {
                nodes_to_follow.splice(nodes_to_follow.end(), nodes, child_node);
            }
        }

        // Check if any nodes being removed are a completed route.
        for (RouteNode &node : nodes_to_remove) {
            completed_routes.erase(&node);
        }

        // Move all nodes to be removed to the free list.
        node_storage->free_nodes(nodes_to_remove);
        NPNR_ASSERT(nodes_to_follow.empty());
        NPNR_ASSERT(nodes_to_remove.empty());
    }
};

void print_current_state(const SiteArch *site_arch)
{
    const Context *ctx = site_arch->ctx;
    auto & cells_in_site = site_arch->site_info->cells_in_site;
    const CellInfo *cell = *cells_in_site.begin();
    BelId bel = cell->bel;
    const auto &bel_data = bel_info(ctx->chip_info, bel);
    const auto &site_inst = site_inst_info(ctx->chip_info, bel.tile, bel_data.site);

    log_info("Site %s\n", site_inst.name.get());

    log_info(" Cells in site:\n");
    for (CellInfo *cell : cells_in_site) {
        log_info("  - %s (%s)\n", cell->name.c_str(ctx), cell->type.c_str(ctx));
    }

    log_info(" Nets in site:\n");
    for (auto & net_pair : site_arch->nets) {
        auto * net = net_pair.first;
        log_info("  - %s, pins in site:\n", net->name.c_str(ctx));
        if (net->driver.cell && cells_in_site.count(net->driver.cell)) {
            log_info("    - %s/%s (%s)\n", net->driver.cell->name.c_str(ctx), net->driver.port.c_str(ctx),
                     net->driver.cell->type.c_str(ctx));
        }

        for (const auto user : net->users) {
            if (user.cell && cells_in_site.count(user.cell)) {
                log_info("    - %s/%s (%s)\n", user.cell->name.c_str(ctx), user.port.c_str(ctx),
                         user.cell->type.c_str(ctx));
            }
        }
    }

    log_info(" Consumed wires:\n");
    for (auto &wire_pair : site_arch->wire_to_nets) {
        const SiteWire & site_wire = wire_pair.first;
        if(site_wire.type != SiteWire::SITE_WIRE) {
            continue;
        }
        WireId wire = site_wire.wire;
        const NetInfo *net = wire_pair.second.net->net;
        log_info("  - %s is bound to %s\n", ctx->nameOfWire(wire), net->name.c_str(ctx));
    }
}

bool apply_solution(SiteArch *ctx, RouteNode::Node cursor, std::vector<SiteWire> * wires_bound) {
    SiteNetInfo * net = ctx->wire_to_nets.at(cursor->wire).net;

    while(cursor->parent != RouteNode::Node()) {
        if(!ctx->bindPip(cursor->pip, net)) {
            return false;
        }

        wires_bound->push_back(cursor->wire);

        NPNR_ASSERT(cursor->pip.type != SitePip::Type::INVALID_TYPE);
        NPNR_ASSERT(ctx->getPipDstWire(cursor->pip) == cursor->wire);
        NPNR_ASSERT(ctx->getPipSrcWire(cursor->pip) == cursor->parent->wire);

        cursor = cursor->parent;
    }

    NPNR_ASSERT(net->driver == cursor->wire);
    wires_bound->push_back(cursor->wire);

    return true;
}

struct PossibleSolutions {
    bool tested = false;
    SiteNetInfo *net = nullptr;
    std::vector<SitePip> pips;
};

bool test_solution(SiteArch *ctx, SiteNetInfo *net, const std::vector<SitePip> & pips) {
    bool valid = true;
    size_t good_pip_end = 0;
    for(size_t i = 0; i < pips.size(); ++i) {
        if(!ctx->bindPip(pips[i], net)) {
            valid = false;
            break;
        }

        good_pip_end = i;
    }

    // Unwind a bad solution
    if(!valid) {
        for(size_t i = 0; i < good_pip_end; ++i) {
            ctx->unbindPip(pips[i]);
        }
    } else {
        NPNR_ASSERT(net->driver == ctx->getPipSrcWire(pips.back()));
    }

    return valid;
}

void remove_solution(SiteArch *ctx, const std::vector<SitePip> & pips) {
    for(size_t i = 0; i < pips.size(); ++i) {
        ctx->unbindPip(pips[i]);
    }
}

bool find_solution_via_backtrack(SiteArch *ctx, std::vector<PossibleSolutions> solutions, std::vector<std::vector<size_t>> sinks_to_solutions) {
    std::vector<uint8_t> routed_sinks;
    std::vector<size_t> solution_indicies;
    std::vector<std::pair<size_t, size_t>> solution_order;
    routed_sinks.resize(sinks_to_solutions.size(), 0);
    solution_indicies.resize(sinks_to_solutions.size(), 0);

    // Scan solutions, and remove any solutions that are invalid immediately
    for(auto & solution : solutions) {
        if(test_solution(ctx, solution.net, solution.pips)) {
            remove_solution(ctx, solution.pips);
        } else {
            solution.tested = true;
        }
    }

    for(size_t sink_idx = 0; sink_idx < sinks_to_solutions.size(); ++sink_idx) {
        size_t solution_count = 0;
        for(size_t solution_idx : sinks_to_solutions[sink_idx]) {
            if(!solutions[solution_idx].tested) {
                solution_count += 1;
            }
        }

        if(solution_count == 0) {
            return false;
        }

        solution_order.emplace_back(sink_idx, solution_count);
    }

    // Sort solutions by the number of possible solutions first.  This allows
    // the backtrack to avoid the wide searches first.
    std::sort(solution_order.begin(), solution_order.end(), [](
                const std::pair<size_t, size_t> &a,
                const std::pair<size_t, size_t> &b) -> bool {
            return a.second < b.second;
            });

    std::vector<size_t> solution_stack;
    solution_stack.reserve(sinks_to_solutions.size());

    if(verbose_site_router(ctx)) {
        log_info("Solving via backtrace with %zu solutions and %zu sinks\n",
                solutions.size(), sinks_to_solutions.size());
    }

    // Simple backtrack explorer:
    //  - Apply the next solution at stack index.
    //  - If solution is valid, push solution onto stack, and advance stack
    //    index at solution 0.
    //  - If solution is not valid, pop the stack.
    //    - At this level of the stack, advance to the next solution.  If
    //      there are not more solutions at this level, pop again.
    //    - If stack is now empty, mark root solution as tested and invalid.
    //      If root of stack has no more solutions, no solution is possible.
    while(true) {
        // Which sink is next to be tested?
        size_t sink_idx = solution_order[solution_stack.size()].first;

        size_t next_solution_to_test = solution_indicies[sink_idx];
        if(next_solution_to_test >= sinks_to_solutions[sink_idx].size()) {
            // We have exausted all solutions at this level of the stack!
            if(solution_stack.empty()) {
                // Search is done, failed!!!
                if(verbose_site_router(ctx)) {
                    log_info("No solution found via backtrace with %zu solutions and %zu sinks\n",
                            solutions.size(), sinks_to_solutions.size());
                }
                return false;
            } else {
                // This level of the stack is completely tapped out, pop back
                // to the next level up.
                size_t solution_idx = solution_stack.back();
                solution_stack.pop_back();

                // Remove the now tested bad solution at the previous level of
                // the stack.
                auto & solution = solutions.at(solution_idx);
                remove_solution(ctx, solution.pips);

                // Because we had to pop up the stack, advance the index at
                // the level below us and start again.
                sink_idx = solution_order[solution_stack.size()].first;
                solution_indicies[sink_idx] += 1;
                continue;
            }
        }

        size_t solution_idx = sinks_to_solutions[sink_idx].at(next_solution_to_test);
        auto & solution = solutions.at(solution_idx);
        if(solution.tested) {
            // This solution was already determined to be no good, skip it.
            solution_indicies[sink_idx] += 1;
            continue;
        }

        if(!test_solution(ctx, solution.net, solution.pips)) {
            // This solution was no good, try the next one at this level of
            // the stack.
            solution_indicies[sink_idx] += 1;
        } else {
            // This solution was good, push onto the stack.
            solution_stack.push_back(solution_idx);
            if(solution_stack.size() == sinks_to_solutions.size()) {
                // Found a valid solution, done!
                if(verbose_site_router(ctx)) {
                    log_info("Solved via backtrace with %zu solutions and %zu sinks\n",
                            solutions.size(), sinks_to_solutions.size());
                }
                return true;
            } else {
                // Because we pushing to a new level of stack, restart the
                // search at this level.
                sink_idx = solution_order[solution_stack.size()].first;
                solution_indicies[sink_idx] = 0;
            }
        }
    }

    // Unreachable!!!
    NPNR_ASSERT(false);
}

bool route_site(SiteArch *ctx) {
    RouteNodeStorage node_storage;
    std::vector<SiteExpansionLoop> expansions;
    expansions.reserve(ctx->nets.size());

    for(auto &net_pair : ctx->nets) {
        SiteNetInfo *net = &net_pair.second;
        expansions.emplace_back(ctx, &node_storage);

        SiteExpansionLoop &router = expansions.back();
        if(!router.expand_net(net)) {
            if (verbose_site_router(ctx)) {
                log_info("Net %s expansion failed to reach all users, site is unroutable!\n",
                        ctx->nameOfNet(net));
            }

            return false;
        }
    }

    absl::flat_hash_set<SiteNetInfo*> unrouted_nets;
    absl::flat_hash_set<SiteWire> unrouted_sinks;
    for(auto &net_pair : ctx->nets) {
        unrouted_sinks.insert(net_pair.second.users.begin(), net_pair.second.users.end());
        if(!net_pair.second.users.empty()) {
            unrouted_nets.emplace(&net_pair.second);
        }
    }

    std::vector<SiteNetInfo*> newly_routed_nets;
    std::vector<RouteNode::Node> solutions_to_apply;
    std::vector<SiteWire> wires_bound;
    absl::flat_hash_map<SiteWire, std::vector<RouteNode::Node>> possible_solutions;
    absl::flat_hash_map<WireId, absl::flat_hash_set<const NetInfo *>> wire_congestion;
    while(!unrouted_sinks.empty()) {
        newly_routed_nets.clear();
        for(SiteNetInfo *net : unrouted_nets) {
            // If a net is fully routed, all users will have an entry in the
            // wires map.
            bool fully_routed = true;
            for(auto & user : net->users) {
                if(net->wires.count(user) == 0) {
                    fully_routed = false;
                    break;
                }
            }

            if(fully_routed) {
                newly_routed_nets.push_back(net);
            }
        }

        for(SiteNetInfo *routed_net : newly_routed_nets) {
            NPNR_ASSERT(unrouted_nets.erase(routed_net) == 1);
        }

        if(unrouted_nets.empty()) {
            return true;
        }

        // Populate possible_solutions with empty vectors initially.
        possible_solutions.clear();
        for(auto &sink : unrouted_sinks) {
            possible_solutions[sink].clear();
        }

        for(const auto & expansion : expansions) {
            if(expansion.completed_routes.empty() && unrouted_nets.count(expansion.net_for_wire)) {
                if(verbose_site_router(ctx)) {
                    log_info("Net %s has no more completed routes, but isn't routed!\n",
                            ctx->nameOfNet(expansion.net_for_wire));
                }
                return false;
            }

            for(const auto &sol_pair : expansion.completed_routes) {
                RouteNode::Node possible_solution = sol_pair.second;

                // All remaining solutions should only apply to unrouted sinks!
                NPNR_ASSERT(unrouted_sinks.count(possible_solution->wire));

                possible_solutions[possible_solution->wire].push_back(possible_solution);
            }
        }

        solutions_to_apply.clear();
        for(const auto &sink_pair : possible_solutions) {
            if(sink_pair.second.empty()) {
                if(verbose_site_router(ctx)) {
                    log_info("Sink %s has no possible solution!\n",
                            ctx->nameOfWire(sink_pair.first));
                }
                // If an unrouted sink has no possible solution, then the
                // site is unroutable!
                return false;
            }

            if(sink_pair.second.size() == 1) {
                // These solutions are unique, select any unique solutions.
                solutions_to_apply.push_back(sink_pair.second.front());
            }
        }

        if(!solutions_to_apply.empty()) {
            // There are some trivial solutions to apply, apply them now, and
            // then prune solution list.
            wires_bound.clear();
            for(RouteNode::Node solution : solutions_to_apply) {
                if(!apply_solution(ctx, solution, &wires_bound)) {
                    // This solution resulted in a conflict, error!
                    if(verbose_site_router(ctx)) {
                        log_info("Solution for sink %s had a conflict!\n",
                                ctx->nameOfWire(solution->wire));
                    }
                    return false;
                }

                SiteNetInfo *net = ctx->wire_to_nets.at(solution->wire).net;
                NPNR_ASSERT(net->wires.count(solution->wire) == 1);
                NPNR_ASSERT(unrouted_sinks.erase(solution->wire) == 1);
            }

            for(const SiteWire & bound_wire : wires_bound) {
                for(auto & expansion : expansions) {
                    expansion.remove_wire(bound_wire);
                }
            }

            // At least 1 solution was applied, go back to start of loop.
            continue;
        }

        break;
    }

    newly_routed_nets.clear();
    for(SiteNetInfo *net : unrouted_nets) {
        // If a net is fully routed, all users will have an entry in the
        // wires map.
        bool fully_routed = true;
        for(auto & user : net->users) {
            if(net->wires.count(user) == 0) {
                fully_routed = false;
                break;
            }
        }

        if(fully_routed) {
            newly_routed_nets.push_back(net);
        }
    }

    for(SiteNetInfo *routed_net : newly_routed_nets) {
        NPNR_ASSERT(unrouted_nets.erase(routed_net) == 1);
    }

    if(unrouted_nets.empty()) {
        return true;
    }

    for(auto &net_pair : ctx->nets) {
        SiteNetInfo & net_info = net_pair.second;
        for(auto & user : net_pair.second.users) {
            bool is_routed = net_info.wires.count(user);
            bool is_unrouted = unrouted_sinks.count(user);
            NPNR_ASSERT(is_routed != is_unrouted);
        }
    }

    // No more trival solutions, solve remaining routing problem with a simple
    // back tracking.
    //
    // First convert remaining solutions into a flat solution set.
    std::vector<PossibleSolutions> solutions;
    absl::flat_hash_map<SiteWire, size_t> sink_map;
    std::vector<std::vector<size_t>> sinks_to_solutions;
    for(const SiteWire & unrouted_sink : unrouted_sinks) {
        auto result = sink_map.emplace(unrouted_sink, sink_map.size());
        NPNR_ASSERT(result.second);
    }

    sinks_to_solutions.resize(sink_map.size());

    for(const auto & expansion : expansions) {
        if(unrouted_nets.count(expansion.net_for_wire) == 0) {
            // Only add solutions for unrouted nets
            continue;
        }

        if(expansion.completed_routes.empty()) {
            if(verbose_site_router(ctx)) {
                log_info("Net %s has no more completed routes, but isn't routed!\n",
                        ctx->nameOfNet(expansion.net_for_wire));
            }
            return false;
        }

        for(const auto &sol_pair : expansion.completed_routes) {
            RouteNode::Node cursor = sol_pair.second;

            // All remaining solutions should only apply to unrouted sinks!
            size_t sink_idx = sink_map.at(cursor->wire);

            sinks_to_solutions.at(sink_idx).push_back(solutions.size());
            solutions.emplace_back();
            auto &solution = solutions.back();
            solution.net = ctx->wire_to_nets.at(cursor->wire).net;

            while(cursor->parent != RouteNode::Node()) {
                solution.pips.push_back(cursor->pip);
                NPNR_ASSERT(ctx->getPipDstWire(cursor->pip) == cursor->wire);
                NPNR_ASSERT(ctx->getPipSrcWire(cursor->pip) == cursor->parent->wire);
                cursor = cursor->parent;
            }
            NPNR_ASSERT(solution.net->driver == cursor->wire);
        }
    }

    return find_solution_via_backtrack(ctx, solutions, sinks_to_solutions);
}

void check_routing(const SiteArch &site_arch) {
    for(auto &net_pair : site_arch.nets) {
        const NetInfo *net = net_pair.first;
        const SiteNetInfo &net_info = net_pair.second;

        for(const auto & user : net_info.users) {
            NPNR_ASSERT(site_arch.wire_to_nets.at(user).net->net == net);
            SiteWire cursor = user;
            while(cursor != net_info.driver) {
                auto iter = net_info.wires.find(cursor);
                if(iter == net_info.wires.end()) {
                    log_error("Wire %s has no pip, but didn't reach driver wire %s\n",
                            site_arch.nameOfWire(cursor),
                            site_arch.nameOfWire(net_info.driver));
                }
                const SitePip & site_pip = iter->second.pip;
                cursor = site_arch.getPipSrcWire(site_pip);
            }
        }
    }
}

void apply_routing(Context *ctx, const SiteArch &site_arch) {
    for(auto &net_pair : site_arch.nets) {
        NetInfo *net = net_pair.first;
        for(auto &wire_pair : net_pair.second.wires) {
            const SitePip &site_pip = wire_pair.second.pip;
            if(site_pip.type != SitePip::SITE_PIP && site_pip.type != SitePip::SITE_PORT) {
                continue;
            }

            ctx->bindPip(site_pip.pip, net, STRENGTH_PLACER);
        }
    }
}

bool SiteRouter::checkSiteRouting(const Context *ctx, const TileStatus &tile_status) const {
    if (!dirty) {
        return site_ok;
    }

    dirty = false;

    if (cells_in_site.size() == 0) {
        site_ok = true;
        return site_ok;
    }

    site_ok = false;

    // Make sure all cells in this site belong!
    auto iter = cells_in_site.begin();
    NPNR_ASSERT((*iter)->bel != BelId());
    auto tile = (*iter)->bel.tile;

    if (verbose_site_router(ctx)) {
        log_info("Checking site routing for site %s\n", ctx->get_site_name(tile, site));
    }

    for (CellInfo *cell : cells_in_site) {
        // All cells in the site must be placed.
        NPNR_ASSERT(cell->bel != BelId());

        // Sanity check that all cells in this site are part of the same site.
        NPNR_ASSERT(tile == cell->bel.tile);
        NPNR_ASSERT(site == bel_info(ctx->chip_info, cell->bel).site);

        // As a first pass make sure each assigned cell in site is valid by
        // constraints.
        if (!ctx->is_cell_valid_constraints(cell, tile_status, verbose_site_router(ctx))) {
            if (verbose_site_router(ctx)) {
                log_info("Sanity check failed, cell_type %s at %s has an invalid constraints, so site is not good\n",
                         cell->type.c_str(ctx), ctx->nameOfBel(cell->bel));
            }
            site_ok = false;
            return site_ok;
        }
    }

    // FIXME: Populate "consumed_wires" with all VCC/GND tied in the site.
    // This will allow route_site to leverage site local constant sources.
    //
    // FIXME: Handle case where a constant is requested, but use of an
    // inverter is possible. This is the place to handle "bestConstant"
    // (e.g. route VCC's over GND's, etc).
    auto tile_type_idx = ctx->chip_info->tiles[tile].type;
    const std::vector<LutElement> &lut_elements = ctx->lut_elements.at(tile_type_idx);
    std::vector<LutMapper> lut_mappers;
    lut_mappers.reserve(lut_elements.size());
    for (size_t i = 0; i < lut_elements.size(); ++i) {
        lut_mappers.push_back(LutMapper(lut_elements[i]));
    }

    for (CellInfo *cell : cells_in_site) {
        if (cell->lut_cell.pins.empty()) {
            continue;
        }

        BelId bel = cell->bel;
        const auto &bel_data = bel_info(ctx->chip_info, bel);
        if (bel_data.lut_element != -1) {
            lut_mappers[bel_data.lut_element].cells.push_back(cell);
        }
    }

    for (LutMapper lut_mapper : lut_mappers) {
        if (lut_mapper.cells.empty()) {
            continue;
        }

        if (!lut_mapper.remap_luts(ctx)) {
            site_ok = false;
            return site_ok;
        }
    }

    SiteInformation site_info(ctx, tile, site, cells_in_site);

    // Push from cell pins to the first WireId from each cell pin.
    if(!check_initial_wires(ctx, &site_info)) {
        site_ok = false;
        return site_ok;
    }

    SiteArch site_arch(&site_info);
    site_arch.archcheck();

    site_ok = route_site(&site_arch);
    if(verbose_site_router(ctx)) {
        if(site_ok) {
            log_info("Site %s is routable\n", ctx->get_site_name(tile, site));
        } else {
            log_info("Site %s is not routable\n", ctx->get_site_name(tile, site));
        }
    }

    if(site_ok) {
        check_routing(site_arch);
    }
    return site_ok;
}

void SiteRouter::bindSiteRouting(Context *ctx) {
    NPNR_ASSERT(!dirty);
    NPNR_ASSERT(site_ok);

    // Make sure all cells in this site belong!
    auto iter = cells_in_site.begin();
    NPNR_ASSERT((*iter)->bel != BelId());

    auto tile = (*iter)->bel.tile;
    auto & tile_type = loc_info(ctx->chip_info, (*iter)->bel);

    // Unbind all bound site wires
    WireId wire;
    wire.tile = tile;
    for(size_t wire_index = 0; wire_index < tile_type.wire_data.size(); ++wire_index) {
        const TileWireInfoPOD & wire_data = tile_type.wire_data[wire_index];

        if(wire_data.site != this->site) {
            continue;
        }

        wire.index = wire_index;

        NetInfo * net = ctx->getBoundWireNet(wire);
        if(net == nullptr) {
            continue;
        }

        auto & pip_map = net->wires.at(wire);
        if(pip_map.strength <= STRENGTH_STRONG) {
            ctx->unbindWire(wire);
        }
    }

    SiteInformation site_info(ctx, tile, site, cells_in_site);
    SiteArch site_arch(&site_info);
    NPNR_ASSERT(route_site(&site_arch));
    check_routing(site_arch);
    apply_routing(ctx, site_arch);
}

NEXTPNR_NAMESPACE_END
