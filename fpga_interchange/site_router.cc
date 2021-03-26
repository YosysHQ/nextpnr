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

#include "design_utils.h"
#include "dynamic_bitarray.h"
#include "hash_table.h"
#include "log.h"
#include "site_routing_cache.h"

#include "site_arch.h"
#include "site_arch.impl.h"

NEXTPNR_NAMESPACE_BEGIN

bool verbose_site_router(const Context *ctx) { return ctx->debug; }

bool verbose_site_router(const SiteArch *ctx) { return verbose_site_router(ctx->ctx); }

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
    HashTables::HashMap<WireId, NetInfo *> wires;

    for (CellInfo *cell : site_info->cells_in_site) {
        BelId bel = cell->bel;
        for (const auto &pin_pair : cell->cell_bel_pins) {
            if (!cell->ports.count(pin_pair.first))
                log_error("Cell %s:%s is missing expected port %s\n", ctx->nameOf(cell), cell->type.c_str(ctx),
                          pin_pair.first.c_str(ctx));
            const PortInfo &port = cell->ports.at(pin_pair.first);
            NPNR_ASSERT(port.net != nullptr);

            for (IdString bel_pin_name : pin_pair.second) {
                BelPin bel_pin;
                bel_pin.bel = bel;
                bel_pin.pin = bel_pin_name;

                WireId wire = ctx->getBelPinWire(bel_pin.bel, bel_pin.pin);
                auto result = wires.emplace(wire, port.net);
                if (!result.second) {
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

static bool is_invalid_site_port(const SiteArch *ctx, const SiteNetInfo *net, const SitePip &pip)
{
    SyntheticType type = ctx->pip_synthetic_type(pip);
    PhysicalNetlist::PhysNetlist::NetType net_type = ctx->ctx->get_net_type(net->net);
    bool is_invalid = false;
    if (type == SYNTH_GND) {
        is_invalid = net_type != PhysicalNetlist::PhysNetlist::NetType::GND;
    } else if (type == SYNTH_VCC) {
        is_invalid = net_type != PhysicalNetlist::PhysNetlist::NetType::VCC;
    }

    return is_invalid;
}

struct SiteExpansionLoop
{
    RouteNodeStorage *const node_storage;

    SiteExpansionLoop(RouteNodeStorage *node_storage) : node_storage(node_storage)
    {
        NPNR_ASSERT(node_storage != nullptr);
    }

    void clear()
    {
        node_storage->free_nodes(used_nodes);
        used_nodes.clear();
        solution.clear();
        net_driver = SiteWire();
    }

    virtual ~SiteExpansionLoop() { node_storage->free_nodes(used_nodes); }

    // Storage for nodes
    std::vector<size_t> used_nodes;

    bool expand_result;
    SiteWire net_driver;
    HashTables::HashSet<SiteWire> net_users;

    SiteRoutingSolution solution;

    Node new_node(const SiteWire &wire, const SitePip &pip, const Node *parent)
    {
        Node node = node_storage->alloc_node();
        used_nodes.push_back(node.get_index());

        node->wire = wire;
        node->pip = pip;
        if (parent != nullptr) {
            node->parent = (*parent).get_index();
            node->flags = (*parent)->flags;
            node->depth = (*parent)->depth + 1;
        }

        if (pip.type == SitePip::SITE_PORT) {
            // Site ports should always have a parent!
            NPNR_ASSERT(parent != nullptr);
            if (wire.type == SiteWire::SITE_PORT_SINK) {
                NPNR_ASSERT((*parent)->wire.type == SiteWire::SITE_WIRE);
                NPNR_ASSERT(node->can_leave_site());
                node->mark_left_site();
            } else if (wire.type == SiteWire::SITE_PORT_SOURCE) {
                // This is a backward walk, so this is considered entering
                // the site.
                NPNR_ASSERT((*parent)->wire.type == SiteWire::SITE_WIRE);
                NPNR_ASSERT(node->can_enter_site());
                node->mark_entered_site();
            } else {
                // See if this is a forward or backward walk.
                NPNR_ASSERT(wire.type == SiteWire::SITE_WIRE);
                if ((*parent)->wire.type == SiteWire::SITE_PORT_SINK) {
                    // This is a backward walk, so this is considered leaving
                    // the site.
                    NPNR_ASSERT(node->can_leave_site());
                    node->mark_left_site();
                } else {
                    NPNR_ASSERT((*parent)->wire.type == SiteWire::SITE_PORT_SOURCE);
                    NPNR_ASSERT(node->can_enter_site());
                    node->mark_entered_site();
                }
            }
        }

        return node;
    }

    // Expand from wire specified, always downhill.
    bool expand_net(const SiteArch *ctx, SiteRoutingCache *site_routing_cache, const SiteNetInfo *net)
    {
        if (net->driver == net_driver && net->users == net_users) {
            return expand_result;
        }

        clear();

        net_driver = net->driver;
        net_users = net->users;

        if (site_routing_cache->get_solution(ctx, *net, &solution)) {
            expand_result = true;
            return expand_result;
        }

        if (verbose_site_router(ctx)) {
            log_info("Expanding net %s from %s\n", ctx->nameOfNet(net), ctx->nameOfWire(net->driver));
        }

        auto node = new_node(net->driver, SitePip(), /*parent=*/nullptr);

        HashTables::HashSet<SiteWire> targets;
        targets.insert(net->users.begin(), net->users.end());

        if (verbose_site_router(ctx)) {
            log_info("%zu targets:\n", targets.size());
            for (auto &target : targets) {
                log_info(" - %s\n", ctx->nameOfWire(target));
            }
        }

        int32_t max_depth = 0;
        int32_t max_depth_seen = 0;
        std::vector<Node> nodes_to_expand;
        nodes_to_expand.push_back(node);

        std::vector<size_t> completed_routes;
        while (!nodes_to_expand.empty()) {
            Node parent_node = nodes_to_expand.back();
            nodes_to_expand.pop_back();

            max_depth_seen = std::max(max_depth_seen, parent_node->depth);

            for (SitePip pip : ctx->getPipsDownhill(parent_node->wire)) {
                if (is_invalid_site_port(ctx, net, pip)) {
                    if (verbose_site_router(ctx)) {
                        log_info("Pip %s is not a valid site port for net %s, skipping\n", ctx->nameOfPip(pip),
                                 ctx->nameOfNet(net));
                    }
                    continue;
                }

                SiteWire wire = ctx->getPipDstWire(pip);

                if (pip.type == SitePip::SITE_PORT) {
                    if (wire.type == SiteWire::SITE_PORT_SINK) {
                        if (!parent_node->can_leave_site()) {
                            // This path has already left the site once, don't leave it again!
                            if (verbose_site_router(ctx)) {
                                log_info("Pip %s is not a valid for this path because it has already left the site\n",
                                         ctx->nameOfPip(pip));
                            }
                            continue;
                        }
                    } else {
                        NPNR_ASSERT(parent_node->wire.type == SiteWire::SITE_PORT_SOURCE);

                        if (!parent_node->can_enter_site()) {
                            // This path has already entered the site once,
                            // don't enter it again!
                            if (verbose_site_router(ctx)) {
                                log_info(
                                        "Pip %s is not a valid for this path because it has already entered the site\n",
                                        ctx->nameOfPip(pip));
                            }
                            continue;
                        }
                    }
                }

                auto wire_iter = ctx->wire_to_nets.find(wire);
                if (wire_iter != ctx->wire_to_nets.end() && wire_iter->second.net != net) {
                    if (verbose_site_router(ctx)) {
                        log_info("Wire %s is already tied to net %s, not exploring for net %s\n", ctx->nameOfWire(wire),
                                 ctx->nameOfNet(wire_iter->second.net), ctx->nameOfNet(net));
                    }
                    continue;
                }

                auto node = new_node(wire, pip, &parent_node);
                if (targets.count(wire)) {
                    completed_routes.push_back(node.get_index());
                    max_depth = std::max(max_depth, node->depth);
                }

                nodes_to_expand.push_back(node);
            }
        }

        // Make sure expansion reached all targets, otherwise this site is
        // already unroutable!
        solution.clear();
        solution.store_solution(ctx, node_storage, net->driver, completed_routes);
        solution.verify(ctx, *net);
        for (size_t route : completed_routes) {
            SiteWire wire = node_storage->get_node(route)->wire;
            targets.erase(wire);
        }

        if (targets.empty()) {
            site_routing_cache->add_solutions(ctx, *net, solution);
        }

        // Return nodes back to the storage system.
        node_storage->free_nodes(used_nodes);
        used_nodes.clear();

        expand_result = targets.empty();
        return expand_result;
    }

    size_t num_solutions() const { return solution.num_solutions(); }

    const SiteWire &solution_sink(size_t idx) const { return solution.solution_sink(idx); }
    std::vector<SitePip>::const_iterator solution_begin(size_t idx) const { return solution.solution_begin(idx); }

    std::vector<SitePip>::const_iterator solution_end(size_t idx) const { return solution.solution_end(idx); }
    bool solution_inverted(size_t idx) const { return solution.solution_inverted(idx); }
    bool solution_can_invert(size_t idx) const { return solution.solution_can_invert(idx); }
};

void print_current_state(const SiteArch *site_arch)
{
    const Context *ctx = site_arch->ctx;
    auto &cells_in_site = site_arch->site_info->cells_in_site;
    const CellInfo *cell = *cells_in_site.begin();
    BelId bel = cell->bel;
    const auto &bel_data = bel_info(ctx->chip_info, bel);
    const auto &site_inst = site_inst_info(ctx->chip_info, bel.tile, bel_data.site);

    log_info("Site %s\n", site_inst.name.get());

    log_info(" Cells in site:\n");
    for (CellInfo *cell : cells_in_site) {
        log_info("  - %s (%s) => %s\n", cell->name.c_str(ctx), cell->type.c_str(ctx), ctx->nameOfBel(cell->bel));
    }

    log_info(" Nets in site:\n");
    for (auto &net_pair : site_arch->nets) {
        auto *net = net_pair.first;
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
        const SiteWire &site_wire = wire_pair.first;
        if (site_wire.type != SiteWire::SITE_WIRE) {
            continue;
        }
        WireId wire = site_wire.wire;
        const NetInfo *net = wire_pair.second.net->net;
        log_info("  - %s is bound to %s\n", ctx->nameOfWire(wire), net->name.c_str(ctx));
    }
}

struct PossibleSolutions
{
    bool tested = false;
    SiteNetInfo *net = nullptr;
    std::vector<SitePip>::const_iterator pips_begin;
    std::vector<SitePip>::const_iterator pips_end;
    bool inverted = false;
    bool can_invert = false;
    PhysicalNetlist::PhysNetlist::NetType prefered_constant_net_type = PhysicalNetlist::PhysNetlist::NetType::SIGNAL;
};

bool test_solution(SiteArch *ctx, SiteNetInfo *net, std::vector<SitePip>::const_iterator pips_begin,
                   std::vector<SitePip>::const_iterator pips_end)
{
    bool valid = true;
    std::vector<SitePip>::const_iterator good_pip_end = pips_begin;
    std::vector<SitePip>::const_iterator iter = pips_begin;
    SitePip pip;
    while (iter != pips_end) {
        pip = *iter;
        if (!ctx->bindPip(pip, net)) {
            valid = false;
            break;
        }

        ++iter;
        good_pip_end = iter;
    }

    // Unwind a bad solution
    if (!valid) {
        for (auto iter = pips_begin; iter != good_pip_end; ++iter) {
            ctx->unbindPip(*iter);
        }
    } else {
        NPNR_ASSERT(net->driver == ctx->getPipSrcWire(pip));
    }

    return valid;
}

void remove_solution(SiteArch *ctx, std::vector<SitePip>::const_iterator pips_begin,
                     std::vector<SitePip>::const_iterator pips_end)
{
    for (auto iter = pips_begin; iter != pips_end; ++iter) {
        ctx->unbindPip(*iter);
    }
}

struct SolutionPreference
{
    const SiteArch *ctx;
    const std::vector<PossibleSolutions> &solutions;

    SolutionPreference(const SiteArch *ctx, const std::vector<PossibleSolutions> &solutions)
            : ctx(ctx), solutions(solutions)
    {
    }

    bool non_inverting_preference(const PossibleSolutions &lhs, const PossibleSolutions &rhs) const
    {
        // If the LHS is non-inverting and the RHS is inverting, then put the
        // LHS first.
        if (!lhs.inverted && rhs.inverted) {
            return true;
        }

        // Better to have a path that can invert over a path that has no
        // option to invert.
        return (!lhs.can_invert) < (!rhs.can_invert);
    }

    bool inverting_preference(const PossibleSolutions &lhs, const PossibleSolutions &rhs) const
    {
        // If the LHS is inverting and the RHS is non-inverting, then put the
        // LHS first (because this is the inverting preferred case).
        if (lhs.inverted && !rhs.inverted) {
            return true;
        }

        // Better to have a path that can invert over a path that has no
        // option to invert.
        return (!lhs.can_invert) < (!rhs.can_invert);
    }

    bool operator()(size_t lhs_solution_idx, size_t rhs_solution_idx) const
    {
        const PossibleSolutions &lhs = solutions.at(lhs_solution_idx);
        const PossibleSolutions &rhs = solutions.at(rhs_solution_idx);

        NPNR_ASSERT(lhs.net == rhs.net);

        PhysicalNetlist::PhysNetlist::NetType net_type = ctx->ctx->get_net_type(lhs.net->net);
        if (net_type == PhysicalNetlist::PhysNetlist::NetType::SIGNAL) {
            return non_inverting_preference(lhs, rhs);
        }

        // All GND/VCC nets use out of site sources.  Local constant sources
        // are still connected via synthetic edges to the global GND/VCC
        // network.
        NPNR_ASSERT(lhs.net->driver.type == SiteWire::OUT_OF_SITE_SOURCE);

        bool lhs_match_preference = net_type == lhs.prefered_constant_net_type;
        bool rhs_match_preference = net_type == rhs.prefered_constant_net_type;

        if (lhs_match_preference && !rhs_match_preference) {
            // Prefer solutions where the net type already matches the
            // prefered constant type.
            return true;
        }

        if (!lhs_match_preference && rhs_match_preference) {
            // Prefer solutions where the net type already matches the
            // prefered constant type. In this case the RHS is better, which
            // means that RHS < LHS, hence false here.
            return false;
        }

        NPNR_ASSERT(lhs_match_preference == rhs_match_preference);

        if (!lhs_match_preference) {
            // If the net type does not match the preference, then prefer
            // inverted solutions.
            return inverting_preference(lhs, rhs);
        } else {
            // If the net type does match the preference, then prefer
            // non-inverted solutions.
            return non_inverting_preference(lhs, rhs);
        }
    }
};

static bool find_solution_via_backtrack(SiteArch *ctx, std::vector<PossibleSolutions> *solutions,
                                        std::vector<std::vector<size_t>> sinks_to_solutions,
                                        const std::vector<SiteWire> &sinks, bool explain)
{
    std::vector<uint8_t> routed_sinks;
    std::vector<size_t> solution_indicies;
    std::vector<std::pair<size_t, size_t>> solution_order;
    routed_sinks.resize(sinks_to_solutions.size(), 0);
    solution_indicies.resize(sinks_to_solutions.size(), 0);

    // Scan solutions, and remove any solutions that are invalid immediately
    for (size_t solution_idx = 0; solution_idx < solutions->size(); ++solution_idx) {
        PossibleSolutions &solution = (*solutions)[solution_idx];
        if (verbose_site_router(ctx) || explain) {
            log_info("Testing solution %zu\n", solution_idx);
        }
        if (test_solution(ctx, solution.net, solution.pips_begin, solution.pips_end)) {
            if (verbose_site_router(ctx) || explain) {
                log_info("Solution %zu is good\n", solution_idx);
            }
            remove_solution(ctx, solution.pips_begin, solution.pips_end);
        } else {
            if (verbose_site_router(ctx) || explain) {
                log_info("Solution %zu is not useable\n", solution_idx);
            }
            solution.tested = true;
        }
    }

    // Sort sinks_to_solutions so that preferred solutions are tested earlier
    // than less preferred solutions.
    for (size_t sink_idx = 0; sink_idx < sinks_to_solutions.size(); ++sink_idx) {
        std::vector<size_t> &solutions_for_sink = sinks_to_solutions.at(sink_idx);
        std::stable_sort(solutions_for_sink.begin(), solutions_for_sink.end(), SolutionPreference(ctx, *solutions));

        if (verbose_site_router(ctx) || explain) {
            log_info("Solutions for sink %s (%zu)\n", ctx->nameOfWire(sinks.at(sink_idx)), sink_idx);
            for (size_t solution_idx : solutions_for_sink) {
                const PossibleSolutions &solution = solutions->at(solution_idx);
                log_info("%zu: inverted = %d, can_invert = %d, tested = %d\n", solution_idx, solution.inverted,
                         solution.can_invert, solution.tested);
                for (auto iter = solution.pips_begin; iter != solution.pips_end; ++iter) {
                    log_info(" - %s\n", ctx->nameOfPip(*iter));
                }
            }
        }
    }

    for (size_t sink_idx = 0; sink_idx < sinks_to_solutions.size(); ++sink_idx) {
        size_t solution_count = 0;
        for (size_t solution_idx : sinks_to_solutions[sink_idx]) {
            if (!(*solutions)[solution_idx].tested) {
                solution_count += 1;
            }
        }

        if (solution_count == 0) {
            if (verbose_site_router(ctx) || explain) {
                log_info("Sink %s has no solution in site\n", ctx->nameOfWire(sinks.at(sink_idx)));
            }
            return false;
        }

        solution_order.emplace_back(sink_idx, solution_count);
    }

    // Sort solutions by the number of possible solutions first.  This allows
    // the backtrack to avoid the wide searches first.
    std::sort(solution_order.begin(), solution_order.end(),
              [](const std::pair<size_t, size_t> &a, const std::pair<size_t, size_t> &b) -> bool {
                  return a.second < b.second;
              });

    std::vector<size_t> solution_stack;
    solution_stack.reserve(sinks_to_solutions.size());

    if (verbose_site_router(ctx)) {
        log_info("Solving via backtrace with %zu solutions and %zu sinks\n", solutions->size(),
                 sinks_to_solutions.size());
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
    while (true) {
        // Which sink is next to be tested?
        size_t sink_idx = solution_order[solution_stack.size()].first;

        size_t next_solution_to_test = solution_indicies[sink_idx];
        if (verbose_site_router(ctx) || explain) {
            log_info("next %zu : %zu (of %zu)\n", sink_idx, next_solution_to_test, sinks_to_solutions[sink_idx].size());
        }
        if (next_solution_to_test >= sinks_to_solutions[sink_idx].size()) {
            // We have exausted all solutions at this level of the stack!
            if (solution_stack.empty()) {
                // Search is done, failed!!!
                if (verbose_site_router(ctx) || explain) {
                    log_info("No solution found via backtrace with %zu solutions and %zu sinks\n", solutions->size(),
                             sinks_to_solutions.size());
                }
                return false;
            } else {
                // This level of the stack is completely tapped out, pop back
                // to the next level up.
                size_t sink_idx = solution_order[solution_stack.size() - 1].first;
                size_t solution_idx = solution_stack.back();
                if (verbose_site_router(ctx) || explain) {
                    log_info("pop  %zu : %zu\n", sink_idx, solution_idx);
                }
                solution_stack.pop_back();

                // Remove the now tested bad solution at the previous level of
                // the stack.
                auto &solution = solutions->at(solution_idx);
                remove_solution(ctx, solution.pips_begin, solution.pips_end);

                // Because we had to pop up the stack, advance the index at
                // the level below us and start again.
                solution_indicies[sink_idx] += 1;
                continue;
            }
        }

        size_t solution_idx = sinks_to_solutions[sink_idx].at(next_solution_to_test);
        auto &solution = solutions->at(solution_idx);
        if (solution.tested) {
            // This solution was already determined to be no good, skip it.
            if (verbose_site_router(ctx) || explain) {
                log_info("skip %zu : %zu\n", sink_idx, solution_idx);
            }
            solution_indicies[sink_idx] += 1;
            continue;
        }

        if (verbose_site_router(ctx) || explain) {
            log_info("test %zu : %zu\n", sink_idx, solution_idx);
        }

        if (!test_solution(ctx, solution.net, solution.pips_begin, solution.pips_end)) {
            // This solution was no good, try the next one at this level of
            // the stack.
            solution_indicies[sink_idx] += 1;
        } else {
            // This solution was good, push onto the stack.
            if (verbose_site_router(ctx) || explain) {
                log_info("push %zu : %zu\n", sink_idx, solution_idx);
            }
            solution_stack.push_back(solution_idx);
            if (solution_stack.size() == sinks_to_solutions.size()) {
                // Found a valid solution, done!
                if (verbose_site_router(ctx)) {
                    log_info("Solved via backtrace with %zu solutions and %zu sinks\n", solutions->size(),
                             sinks_to_solutions.size());
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

bool route_site(SiteArch *ctx, SiteRoutingCache *site_routing_cache, RouteNodeStorage *node_storage, bool explain)
{
    std::vector<SiteExpansionLoop *> expansions;
    expansions.reserve(ctx->nets.size());

    for (auto &net_pair : ctx->nets) {
        SiteNetInfo *net = &net_pair.second;

        if (net->net->loop == nullptr) {
            net->net->loop = new SiteExpansionLoop(node_storage);
        }
        expansions.push_back(net->net->loop);

        SiteExpansionLoop *router = expansions.back();
        if (!router->expand_net(ctx, site_routing_cache, net)) {
            if (verbose_site_router(ctx) || explain) {
                log_info("Net %s expansion failed to reach all users, site is unroutable!\n", ctx->nameOfNet(net));
            }

            return false;
        }
    }

    // First convert remaining solutions into a flat solution set.
    std::vector<PossibleSolutions> solutions;
    std::vector<SiteWire> sinks;
    HashTables::HashMap<SiteWire, size_t> sink_map;
    std::vector<std::vector<size_t>> sinks_to_solutions;
    for (const auto *expansion : expansions) {
        for (const SiteWire &unrouted_sink : expansion->net_users) {
            auto result = sink_map.emplace(unrouted_sink, sink_map.size());
            NPNR_ASSERT(result.second);
            sinks.push_back(unrouted_sink);
        }
    }

    if (sink_map.empty()) {
        // All nets are trivially routed!
        return true;
    }

    sinks_to_solutions.resize(sink_map.size());

    for (const auto *expansion : expansions) {
        for (size_t idx = 0; idx < expansion->num_solutions(); ++idx) {
            SiteWire wire = expansion->solution_sink(idx);
            auto begin = expansion->solution_begin(idx);
            auto end = expansion->solution_end(idx);
            NPNR_ASSERT(begin != end);

            size_t sink_idx = sink_map.at(wire);
            sinks_to_solutions.at(sink_idx).push_back(solutions.size());

            solutions.emplace_back();
            auto &solution = solutions.back();
            solution.net = ctx->wire_to_nets.at(wire).net;
            solution.pips_begin = begin;
            solution.pips_end = end;
            solution.inverted = expansion->solution_inverted(idx);
            solution.can_invert = expansion->solution_can_invert(idx);

            for (auto iter = begin; iter != end; ++iter) {
                const SitePip &site_pip = *iter;
                NPNR_ASSERT(ctx->getPipDstWire(site_pip) == wire);
                wire = ctx->getPipSrcWire(site_pip);

                // If there is a input site port, mark on the solution what the
                // prefered constant net type is for this site port.
                if (site_pip.type == SitePip::SITE_PORT && wire.type == SiteWire::SITE_PORT_SOURCE) {
                    solution.prefered_constant_net_type = ctx->prefered_constant_net_type(site_pip);
                }
            }

            NPNR_ASSERT(expansion->net_driver == wire);
        }
    }

    return find_solution_via_backtrack(ctx, &solutions, sinks_to_solutions, sinks, explain);
}

void check_routing(const SiteArch &site_arch)
{
    for (auto &net_pair : site_arch.nets) {
        const NetInfo *net = net_pair.first;
        const SiteNetInfo &net_info = net_pair.second;

        for (const auto &user : net_info.users) {
            NPNR_ASSERT(site_arch.wire_to_nets.at(user).net->net == net);
            SiteWire cursor = user;
            while (cursor != net_info.driver) {
                auto iter = net_info.wires.find(cursor);
                if (iter == net_info.wires.end()) {
                    log_error("Wire %s has no pip, but didn't reach driver wire %s\n", site_arch.nameOfWire(cursor),
                              site_arch.nameOfWire(net_info.driver));
                }
                const SitePip &site_pip = iter->second.pip;
                cursor = site_arch.getPipSrcWire(site_pip);
            }

            NPNR_ASSERT(cursor == net_info.driver);
            NPNR_ASSERT(site_arch.wire_to_nets.at(cursor).net->net == net);
        }
    }
}

static void apply_simple_routing(Context *ctx, const SiteArch &site_arch, NetInfo *net, const SiteNetInfo *site_net,
                                 const SiteWire &user)
{
    SiteWire wire = user;
    while (wire != site_net->driver) {
        SitePip site_pip = site_net->wires.at(wire).pip;
        NPNR_ASSERT(site_arch.getPipDstWire(site_pip) == wire);

        if (site_pip.type == SitePip::SITE_PIP || site_pip.type == SitePip::SITE_PORT) {
            NetInfo *bound_net = ctx->getBoundPipNet(site_pip.pip);
            if (bound_net == nullptr) {
                ctx->bindPip(site_pip.pip, net, STRENGTH_PLACER);
            } else {
                NPNR_ASSERT(bound_net == net);
            }
        }

        wire = site_arch.getPipSrcWire(site_pip);
    }
}

static void apply_constant_routing(Context *ctx, const SiteArch &site_arch, NetInfo *net, const SiteNetInfo *site_net)
{
    IdString gnd_net_name(ctx->chip_info->constants->gnd_net_name);
    NetInfo *gnd_net = ctx->nets.at(gnd_net_name).get();

    IdString vcc_net_name(ctx->chip_info->constants->vcc_net_name);
    NetInfo *vcc_net = ctx->nets.at(vcc_net_name).get();

    // This function is designed to operate only on the gnd or vcc net, and
    // assumes that the GND and VCC nets have been unified.
    NPNR_ASSERT(net == vcc_net || net == gnd_net);

    for (auto &user : site_net->users) {
        // FIXME: Handle case where pip is "can_invert", and that
        // inversion helps with accomidating "best constant".
        bool is_path_inverting = false;

        SiteWire wire = user;
        PipId inverting_pip;
        while (wire != site_net->driver) {
            SitePip pip = site_net->wires.at(wire).pip;
            NPNR_ASSERT(site_arch.getPipDstWire(pip) == wire);

            if (site_arch.isInverting(pip)) {
                // FIXME: Should be able to handle the general case of
                // multiple inverters, but that is harder (and annoying). Also
                // most sites won't allow for a double inversion, so just
                // disallow for now.
                NPNR_ASSERT(!is_path_inverting);
                is_path_inverting = true;
                NPNR_ASSERT(pip.type == SitePip::SITE_PIP);
                inverting_pip = pip.pip;
            }

            wire = site_arch.getPipSrcWire(pip);
        }

        if (!is_path_inverting) {
            // This routing is boring, use base logic.
            apply_simple_routing(ctx, site_arch, net, site_net, user);
            continue;
        }

        NPNR_ASSERT(inverting_pip != PipId());

        // This net is going to become two nets.
        // The portion of the net prior to the inverter is going to be bound
        // to the opposite net.  For example, if the original net was gnd_net,
        // the portion prior to the inverter will not be the vcc_net.
        //
        // A new cell will be generated to sink the connection from the
        // opposite net.
        NetInfo *net_before_inverter;
        if (net == gnd_net) {
            net_before_inverter = vcc_net;
        } else {
            NPNR_ASSERT(net == vcc_net);
            net_before_inverter = gnd_net;
        }

        // First find a name for the new cell
        int count = 0;
        CellInfo *new_cell = nullptr;
        while (true) {
            std::string new_cell_name = stringf("%s_%s.%d", net->name.c_str(ctx), site_arch.nameOfWire(user), count);
            IdString new_cell_id = ctx->id(new_cell_name);
            if (ctx->cells.count(new_cell_id)) {
                count += 1;
            } else {
                new_cell = ctx->createCell(new_cell_id, ctx->id("$nextpnr_inv"));
                break;
            }
        }

        auto &tile_type = loc_info(ctx->chip_info, inverting_pip);
        auto &pip_data = tile_type.pip_data[inverting_pip.index];
        NPNR_ASSERT(pip_data.site != -1);
        auto &bel_data = tile_type.bel_data[pip_data.bel];

        BelId inverting_bel;
        inverting_bel.tile = inverting_pip.tile;
        inverting_bel.index = pip_data.bel;

        IdString in_port(bel_data.ports[pip_data.extra_data]);
        NPNR_ASSERT(bel_data.types[pip_data.extra_data] == PORT_IN);

        IdString id_I = ctx->id("I");
        new_cell->addInput(id_I);
        new_cell->cell_bel_pins[id_I].push_back(in_port);

        new_cell->bel = inverting_bel;
        new_cell->belStrength = STRENGTH_PLACER;
        ctx->tileStatus.at(inverting_bel.tile).boundcells[inverting_bel.index] = new_cell;

        connect_port(ctx, net_before_inverter, new_cell, id_I);

        // The original BEL pin is now routed, but only through the inverter.
        // Because the cell/net model doesn't allow for multiple source pins
        // and the fact that the portion of the net after the inverter is
        // currently routed, all BEL pins on this site wire are going to be
        // masked from the router.
        NPNR_ASSERT(user.type == SiteWire::SITE_WIRE);
        ctx->mask_bel_pins_on_site_wire(net, user.wire);

        // Bind wires and pips to the two nets.
        bool after_inverter = true;
        wire = user;
        while (wire != site_net->driver) {
            SitePip site_pip = site_net->wires.at(wire).pip;
            NPNR_ASSERT(site_arch.getPipDstWire(site_pip) == wire);

            if (site_arch.isInverting(site_pip)) {
                NPNR_ASSERT(after_inverter);
                after_inverter = false;

                // Because this wire is just after the inverter, bind it to
                // the net without the pip, as this is a "source".
                NPNR_ASSERT(wire.type == SiteWire::SITE_WIRE);
                ctx->bindWire(wire.wire, net, STRENGTH_PLACER);
            } else {
                if (site_pip.type == SitePip::SITE_PIP || site_pip.type == SitePip::SITE_PORT) {
                    if (after_inverter) {
                        ctx->bindPip(site_pip.pip, net, STRENGTH_PLACER);
                    } else {
                        ctx->bindPip(site_pip.pip, net_before_inverter, STRENGTH_PLACER);
                    }
                }
            }

            wire = site_arch.getPipSrcWire(site_pip);
        }
    }
}

static void apply_routing(Context *ctx, const SiteArch &site_arch)
{
    IdString gnd_net_name(ctx->chip_info->constants->gnd_net_name);
    NetInfo *gnd_net = ctx->nets.at(gnd_net_name).get();

    IdString vcc_net_name(ctx->chip_info->constants->vcc_net_name);
    NetInfo *vcc_net = ctx->nets.at(vcc_net_name).get();

    for (auto &net_pair : site_arch.nets) {
        NetInfo *net = net_pair.first;
        const SiteNetInfo *site_net = &net_pair.second;

        if (net == gnd_net || net == vcc_net) {
            apply_constant_routing(ctx, site_arch, net, site_net);
        } else {
            // If the driver wire is a site wire, bind it.
            if (site_net->driver.type == SiteWire::SITE_WIRE) {
                WireId driver_wire = site_net->driver.wire;
                if (ctx->getBoundWireNet(driver_wire) != net) {
                    ctx->bindWire(driver_wire, net, STRENGTH_PLACER);
                }
            }

            for (auto &wire_pair : site_net->wires) {
                const SitePip &site_pip = wire_pair.second.pip;
                if (site_pip.type != SitePip::SITE_PIP && site_pip.type != SitePip::SITE_PORT) {
                    continue;
                }

                ctx->bindPip(site_pip.pip, net, STRENGTH_PLACER);
            }
        }
    }
}

bool SiteRouter::checkSiteRouting(const Context *ctx, const TileStatus &tile_status) const
{
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
    if (!check_initial_wires(ctx, &site_info)) {
        site_ok = false;
        return site_ok;
    }

    SiteArch site_arch(&site_info);
    // site_arch.archcheck();

    site_ok = route_site(&site_arch, &ctx->site_routing_cache, &ctx->node_storage, /*explain=*/false);
    if (verbose_site_router(ctx)) {
        if (site_ok) {
            log_info("Site %s is routable\n", ctx->get_site_name(tile, site));
        } else {
            log_info("Site %s is not routable\n", ctx->get_site_name(tile, site));
        }
    }

    if (site_ok) {
        check_routing(site_arch);
    }
    return site_ok;
}

void SiteRouter::bindSiteRouting(Context *ctx)
{
    NPNR_ASSERT(!dirty);
    NPNR_ASSERT(site_ok);

    // Make sure all cells in this site belong!
    auto iter = cells_in_site.begin();
    NPNR_ASSERT((*iter)->bel != BelId());

    auto tile = (*iter)->bel.tile;
    auto &tile_type = loc_info(ctx->chip_info, (*iter)->bel);

    // Unbind all bound site wires
    WireId wire;
    wire.tile = tile;
    for (size_t wire_index = 0; wire_index < tile_type.wire_data.size(); ++wire_index) {
        const TileWireInfoPOD &wire_data = tile_type.wire_data[wire_index];

        if (wire_data.site != this->site) {
            continue;
        }

        wire.index = wire_index;

        NetInfo *net = ctx->getBoundWireNet(wire);
        if (net == nullptr) {
            continue;
        }

        auto &pip_map = net->wires.at(wire);
        if (pip_map.strength <= STRENGTH_STRONG) {
            ctx->unbindWire(wire);
        }
    }

    SiteInformation site_info(ctx, tile, site, cells_in_site);
    SiteArch site_arch(&site_info);
    NPNR_ASSERT(route_site(&site_arch, &ctx->site_routing_cache, &ctx->node_storage, /*explain=*/false));
    check_routing(site_arch);
    apply_routing(ctx, site_arch);
    if (verbose_site_router(ctx)) {
        print_current_state(&site_arch);
    }
}

void SiteRouter::explain(const Context *ctx) const
{
    NPNR_ASSERT(!dirty);
    if (site_ok) {
        return;
    }

    // Make sure all cells in this site belong!
    auto iter = cells_in_site.begin();
    NPNR_ASSERT((*iter)->bel != BelId());

    auto tile = (*iter)->bel.tile;

    SiteInformation site_info(ctx, tile, site, cells_in_site);
    SiteArch site_arch(&site_info);
    bool route_status = route_site(&site_arch, &ctx->site_routing_cache, &ctx->node_storage, /*explain=*/true);
    if (!route_status) {
        print_current_state(&site_arch);
    }
}

ArchNetInfo::~ArchNetInfo() { delete loop; }

Arch::~Arch()
{
    for (auto &net_pair : nets) {
        if (net_pair.second->loop) {
            net_pair.second->loop->clear();
        }
    }
}

NEXTPNR_NAMESPACE_END
