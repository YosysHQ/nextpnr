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

#include "log.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

bool verbose_site_router(const Context *ctx) { return ctx->verbose; }

void Arch::SiteRouter::bindBel(CellInfo *cell)
{
    auto result = cells_in_site.emplace(cell);
    NPNR_ASSERT(result.second);

    dirty = true;
}

void Arch::SiteRouter::unbindBel(CellInfo *cell)
{
    NPNR_ASSERT(cells_in_site.erase(cell) == 1);

    dirty = true;
}

struct RouteNode
{
    void clear()
    {
        parent = std::list<RouteNode>::iterator();
        leafs.clear();
        pip = PipId();
        wire = WireId();
    }

    using Node = std::list<RouteNode>::iterator;

    Node parent;
    std::vector<Node> leafs;

    PipId pip;   // What pip was taken to reach this node.
    WireId wire; // What wire is this routing node located at?
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

struct SiteInformation
{
    const Context *ctx;

    const std::unordered_set<CellInfo *> &cells_in_site;

    SiteInformation(const Context *ctx, const std::unordered_set<CellInfo *> &cells_in_site)
            : ctx(ctx), cells_in_site(cells_in_site)
    {
    }

    bool check_bel_pin(CellInfo *cell, const PortInfo &port_info, BelPin bel_pin)
    {
        WireId wire = ctx->getBelPinWire(bel_pin.bel, bel_pin.pin);
        auto result = consumed_wires.emplace(wire, port_info.net);
        if (!result.second) {
            // This wire is already in use, make sure the net bound is
            // the same net, otherwise there is a net conflict.
            const NetInfo *other_net = result.first->second;
            if (other_net != port_info.net) {
                // We have a direct net conflict at the BEL pin,
                // immediately short circuit the site routing check.
                if (verbose_site_router(ctx)) {
                    log_info("Direct net conflict detected for cell %s:%s at bel %s, net %s != %s\n",
                             cell->name.c_str(ctx), cell->type.c_str(ctx), ctx->nameOfBel(cell->bel),
                             port_info.net->name.c_str(ctx), other_net->name.c_str(ctx));
                }
                return false;
            }
        }

        nets_in_site.emplace(port_info.net);

        if (port_info.type == PORT_OUT) {
            unrouted_source_wires.emplace(wire, std::unordered_set<WireId>());
        } else {
            unrouted_sink_wires.emplace(wire);
        }

        return true;
    }

    bool check_initial_wires()
    {
        // Propigate from BEL pins to first wire, checking for trival routing
        // conflicts.
        //
        // Populate initial consumed wires, and nets_in_site.
        for (CellInfo *cell : cells_in_site) {
            BelId bel = cell->bel;
            for (const auto &pin_pair : cell->cell_bel_pins) {
                const PortInfo &port = cell->ports.at(pin_pair.first);
                for (IdString bel_pin_name : pin_pair.second) {
                    BelPin bel_pin;
                    bel_pin.bel = bel;
                    bel_pin.pin = bel_pin_name;

                    if (!check_bel_pin(cell, port, bel_pin)) {
                        return false;
                    }
                }
            }
        }

        // Populate nets_fully_within_site
        for (const NetInfo *net : nets_in_site) {
            if (ctx->is_net_within_site(*net)) {
                nets_fully_within_site.emplace(net);
            }
        }

        // Remove sinks that are trivally routed.
        std::vector<WireId> trivally_routed_sinks;
        for (WireId sink_wire : unrouted_sink_wires) {
            if (unrouted_source_wires.count(sink_wire) > 0) {
                if (verbose_site_router(ctx)) {
                    log_info("Wire %s is trivally routed!\n", ctx->nameOfWire(sink_wire));
                }
                trivally_routed_sinks.push_back(sink_wire);
            }
        }

        for (WireId sink_wire : trivally_routed_sinks) {
            NPNR_ASSERT(unrouted_sink_wires.erase(sink_wire) == 1);
        }

        // Remove sources that are routed now that trivally routed sinks are
        // removed.
        std::unordered_set<WireId> trivally_routed_sources;
        for (const NetInfo *net : nets_fully_within_site) {
            std::unordered_set<WireId> sink_wires_in_net;
            bool already_routed = true;
            for (const PortRef &user : net->users) {
                for (const IdString pin : user.cell->cell_bel_pins.at(user.port)) {
                    WireId sink_wire = ctx->getBelPinWire(user.cell->bel, pin);
                    if (unrouted_sink_wires.count(sink_wire) > 0) {
                        sink_wires_in_net.emplace(sink_wire);
                        already_routed = false;
                    }
                }
            }

            if (already_routed) {
                for (const IdString pin : net->driver.cell->cell_bel_pins.at(net->driver.port)) {
                    trivally_routed_sources.emplace(ctx->getBelPinWire(net->driver.cell->bel, pin));
                }
            } else {
                for (const IdString pin : net->driver.cell->cell_bel_pins.at(net->driver.port)) {
                    WireId source_wire = ctx->getBelPinWire(net->driver.cell->bel, pin);
                    unrouted_source_wires.at(source_wire) = sink_wires_in_net;
                }
            }
        }

        for (WireId source_wire : trivally_routed_sources) {
            NPNR_ASSERT(unrouted_source_wires.erase(source_wire) == 1);
        }

        return true;
    }

    // Checks if a source wire has been fully routed.
    //
    // Returns false if this wire is not an unrouted source wire.
    bool check_source_routed(WireId wire) const
    {
        if (unrouted_source_wires.count(wire)) {
            bool fully_routed = true;
            for (WireId sink_wire : unrouted_source_wires.at(wire)) {
                if (unrouted_sink_wires.count(sink_wire)) {
                    fully_routed = false;
                }
            }

            return fully_routed;
        } else {
            return false;
        }
    }

    // Removes an source wires that have been fully routed.
    void remove_routed_sources()
    {
        std::vector<WireId> routed_wires;
        for (auto &source_pair : unrouted_source_wires) {
            if (check_source_routed(source_pair.first)) {
                routed_wires.push_back(source_pair.first);
            }
        }

        for (WireId wire : routed_wires) {
            NPNR_ASSERT(unrouted_source_wires.erase(wire) == 1);
        }
    }

    bool is_fully_routed() const { return unrouted_sink_wires.empty() && unrouted_source_wires.empty(); }

    bool select_route(WireId first_wire, RouteNode::Node node, const NetInfo *net,
                      std::unordered_set<WireId> *newly_consumed_wires)
    {

        bool is_last_pip_site_port = ctx->is_site_port(node->pip);
        do {
            auto result = consumed_wires.emplace(node->wire, net);
            if (!result.second && result.first->second != net) {
                // Conflict, this wire is already in use and it's not
                // doesn't match!
                return false;
            }

            // By selecting a route, other sinks are potentially now routed.
            unrouted_sink_wires.erase(node->wire);

            newly_consumed_wires->emplace(node->wire);

            node = node->parent;
        } while (node != RouteNode::Node());

        if (unrouted_source_wires.count(first_wire)) {
            // By selecting a route to a site pip, this source wire is routed.
            if (is_last_pip_site_port) {
                NPNR_ASSERT(unrouted_source_wires.erase(first_wire));
            } else if (is_net_within_site(net)) {
                // For nets that are completely contained within the site, it
                // is possible that by selecting this route it is now fully
                // routed. Check now.
                if (check_source_routed(first_wire)) {
                    NPNR_ASSERT(unrouted_source_wires.erase(first_wire));
                }
            }
        }

        return true;
    }

    // Map of currently occupied wires and their paired net.
    std::unordered_map<WireId, const NetInfo *> consumed_wires;

    // Set of nets in site
    std::unordered_set<const NetInfo *> nets_in_site;

    // Map from source wire to sink wires within this site.
    // If all sink wires are routed, the source is also routed!
    std::unordered_map<WireId, std::unordered_set<WireId>> unrouted_source_wires;
    std::unordered_set<WireId> unrouted_sink_wires;

    // Set of nets are fully contained within the site.
    std::unordered_set<const NetInfo *> nets_fully_within_site;

    bool is_net_within_site(const NetInfo *net) const { return nets_fully_within_site.count(net); }
};

struct SiteExpansionLoop
{
    const Context *const ctx;
    RouteNodeStorage *const node_storage;

    using Node = RouteNode::Node;

    SiteExpansionLoop(const Context *ctx, RouteNodeStorage *node_storage) : ctx(ctx), node_storage(node_storage)
    {
        NPNR_ASSERT(node_storage != nullptr);
    }

    ~SiteExpansionLoop() { node_storage->free_nodes(nodes); }

    // Storage for nodes
    std::list<RouteNode> nodes;

    WireId first_wire;
    const NetInfo *net_for_wire;
    std::unordered_map<RouteNode *, Node> completed_routes;
    std::unordered_map<WireId, std::vector<Node>> wire_to_nodes;

    Node new_node(WireId wire, PipId pip, Node parent)
    {
        auto node = node_storage->alloc_node(nodes);
        node->wire = wire;
        node->pip = pip;
        node->parent = parent;

        return node;
    }

    void free_node(Node node) { node_storage->free_node(nodes, node); }

    // Expand from wire specified, either downhill or uphill.
    //
    // Expands until it reaches another net of it's own (e.g. source to sink
    // within site) or a site port (e.g. out to routing network).
    void expand(WireId wire, const SiteInformation *site_info)
    {

        bool downhill = site_info->unrouted_source_wires.count(wire) != 0;
        if (!downhill) {
            NPNR_ASSERT(site_info->unrouted_sink_wires.count(wire) != 0);
        }

        first_wire = wire;
        net_for_wire = site_info->consumed_wires.at(first_wire);

        if (verbose_site_router(ctx)) {
            log_info("Expanding net %s from %s\n", net_for_wire->name.c_str(ctx), ctx->nameOfWire(first_wire));
        }

        completed_routes.clear();
        wire_to_nodes.clear();
        node_storage->free_nodes(nodes);

        auto node = new_node(first_wire, PipId(), /*parent=*/Node());
        wire_to_nodes[first_wire].push_back(node);

        std::vector<Node> nodes_to_expand;
        nodes_to_expand.push_back(node);

        auto do_expand = [&](Node parent_node, PipId pip, WireId wire) {
            if (wire == first_wire) {
                // No simple loops
                // FIXME: May need to detect more complicated loops!
                return;
            }

            if (ctx->is_site_port(pip)) {
                if (verbose_site_router(ctx)) {
                    log_info("Expanded net %s reaches %s\n", net_for_wire->name.c_str(ctx), ctx->nameOfPip(pip));
                }
                auto node = new_node(wire, pip, parent_node);
                completed_routes.emplace(&*node, node);
                return;
            }

            auto iter = site_info->consumed_wires.find(wire);
            if (iter != site_info->consumed_wires.end()) {
                // This wire already belongs to a net!
                if (iter->second == net_for_wire) {
                    // If this wire is the same net, this is a valid complete
                    // route.
                    if (!downhill && site_info->unrouted_source_wires.count(wire)) {
                        // This path is from a sink to a source, it is a complete route.
                        auto node = new_node(wire, pip, parent_node);
                        if (verbose_site_router(ctx)) {
                            log_info("Expanded net %s reaches source %s\n", net_for_wire->name.c_str(ctx),
                                     ctx->nameOfWire(wire));
                        }
                        completed_routes.emplace(&*node, node);
                    } else if (downhill && site_info->is_net_within_site(net_for_wire)) {
                        // This path is from a sink to a source, it is a complete route to 1 sinks.
                        auto node = new_node(wire, pip, parent_node);
                        if (verbose_site_router(ctx)) {
                            log_info("Expanded net %s reaches sink %s\n", net_for_wire->name.c_str(ctx),
                                     ctx->nameOfWire(wire));
                        }
                        completed_routes.emplace(&*node, node);
                    }
                } else {
                    // Net conflict, do not expand further.
                    return;
                }
            }

            // This wire is not a destination, and is not directly occupied,
            // put it on the expansion list.
            nodes_to_expand.push_back(new_node(wire, pip, parent_node));
        };

        while (!nodes_to_expand.empty()) {
            Node node_to_expand = nodes_to_expand.back();
            nodes_to_expand.pop_back();

            if (downhill) {
                for (PipId pip : ctx->getPipsDownhill(node_to_expand->wire)) {
                    WireId wire = ctx->getPipDstWire(pip);
                    do_expand(node_to_expand, pip, wire);
                }
            } else {
                for (PipId pip : ctx->getPipsUphill(node_to_expand->wire)) {
                    WireId wire = ctx->getPipSrcWire(pip);
                    do_expand(node_to_expand, pip, wire);
                }
            }
        }
    }

    // Remove any routes that use specified wire.
    void remove_wire(WireId wire)
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

bool route_site(const Context *ctx, SiteInformation *site_info)
{
    // All nets need to route:
    //  - From sources to an output site pin or sink wire.
    //  - From sink to an input site pin.

    std::unordered_set<WireId> unrouted_wires;

    for (auto wire_pair : site_info->unrouted_source_wires) {
        auto result = unrouted_wires.emplace(wire_pair.first);
        NPNR_ASSERT(result.second);
    }
    for (WireId wire : site_info->unrouted_sink_wires) {
        auto result = unrouted_wires.emplace(wire);
        if (!result.second) {
            log_error("Found sink wire %s already in unrouted_wires set. unrouted_source_wires.count() == %zu\n",
                      ctx->nameOfWire(wire), site_info->unrouted_source_wires.count(wire));
        }
    }

    // All done!
    if (unrouted_wires.empty()) {
        return true;
    }

    // Expand from first wires to all pontential routes (either net pair or
    // site pin).
    RouteNodeStorage node_storage;
    std::vector<SiteExpansionLoop> expansions;
    expansions.reserve(unrouted_wires.size());

    for (WireId wire : unrouted_wires) {
        expansions.emplace_back(SiteExpansionLoop(ctx, &node_storage));

        SiteExpansionLoop &wire_router = expansions.back();
        wire_router.expand(wire, site_info);

        // It is not possible to route this wire at all, fail early.
        if (wire_router.completed_routes.empty()) {
            return false;
        }
    }

    std::unordered_set<WireId> newly_consumed_wires;
    std::unordered_map<WireId, SiteExpansionLoop *> wire_to_expansion;
    for (auto &expansion : expansions) {
        // This is a special case, where the expansion found exactly 1 solution.
        // That solution must be conflict free, or the site is unroutable.
        if (expansion.completed_routes.size() == 1) {
            auto node = expansion.completed_routes.begin()->second;
            if (!site_info->select_route(expansion.first_wire, node, expansion.net_for_wire, &newly_consumed_wires)) {
                // Conflict!
                return false;
            }
        } else {
            auto result = wire_to_expansion.emplace(expansion.first_wire, &expansion);
            NPNR_ASSERT(result.second);
        }
    }

    if (wire_to_expansion.empty()) {
        // All routes have been assigned with congestion!
        return true;
    }

    // At this point some expansions have multiple results.  Build congestion
    // information, and pick non-conflicted routes for remaining expansions.
    std::vector<WireId> completed_wires;
    do {
        // Before anything, remove routes that have been consumed in previous
        // iteration.
        for (auto &expansion_wire : wire_to_expansion) {
            auto &expansion = *expansion_wire.second;
            for (WireId consumed_wire : newly_consumed_wires) {
                const NetInfo *net_for_wire = site_info->consumed_wires.at(consumed_wire);
                if (net_for_wire != expansion.net_for_wire) {
                    expansion.remove_wire(consumed_wire);
                }

                // By removing that wire, this expansion now has no solutions!
                if (expansion.completed_routes.empty()) {
                    return false;
                }
            }
        }

        // Check if there are any more trival solutions.
        completed_wires.clear();
        newly_consumed_wires.clear();

        for (auto &expansion_wire : wire_to_expansion) {
            auto &expansion = *expansion_wire.second;
            if (expansion.completed_routes.size() == 1) {
                auto node = expansion.completed_routes.begin()->second;
                if (!site_info->select_route(expansion.first_wire, node, expansion.net_for_wire,
                                             &newly_consumed_wires)) {
                    // Conflict!
                    return false;
                }

                // Mark this expansion as done!
                completed_wires.push_back(expansion_wire.first);
            }
        }

        // Remove trival solutions from unsolved routing.
        for (WireId wire : completed_wires) {
            NPNR_ASSERT(wire_to_expansion.erase(wire) == 1);
        }

        // All expansions have been selected for!
        if (wire_to_expansion.empty()) {
            break;
        }

        // At least 1 trival solution was selected, re-prune.
        if (!newly_consumed_wires.empty()) {
            // Prune remaining solutions.
            continue;
        }

        std::unordered_map<WireId, std::unordered_set<const NetInfo *>> wire_congestion;

        for (auto &expansion_wire : wire_to_expansion) {
            auto &expansion = *expansion_wire.second;

            for (auto pair : expansion.completed_routes) {
                auto node = pair.second;

                do {
                    wire_congestion[node->wire].emplace(expansion.net_for_wire);
                    node = node->parent;
                } while (node != RouteNode::Node());
            }
        }

        for (auto &expansion_wire : wire_to_expansion) {
            auto &expansion = *expansion_wire.second;

            RouteNode::Node uncongestion_route;

            for (auto pair : expansion.completed_routes) {
                auto node = pair.second;
                uncongestion_route = node;

                do {
                    if (wire_congestion[node->wire].size() > 1) {
                        uncongestion_route = RouteNode::Node();
                        break;
                    }
                    node = node->parent;
                } while (node != RouteNode::Node());

                if (uncongestion_route != RouteNode::Node()) {
                    break;
                }
            }

            if (uncongestion_route != RouteNode::Node()) {
                // Select a trivally uncongestion route if possible.
                NPNR_ASSERT(site_info->select_route(expansion.first_wire, uncongestion_route, expansion.net_for_wire,
                                                    &newly_consumed_wires));
                completed_wires.push_back(expansion.first_wire);
            }
        }

        // Remove trival solutions from unsolved routing.
        for (WireId wire : completed_wires) {
            NPNR_ASSERT(wire_to_expansion.erase(wire) == 1);
        }

        // All expansions have been selected for!
        if (wire_to_expansion.empty()) {
            break;
        }

        // At least 1 trival solution was selected, re-prune.
        if (!newly_consumed_wires.empty()) {
            // Prune remaining solutions.
            continue;
        }

        // FIXME: Actually de-congest non-trival site routing.
        //
        // The simplistic solution (only select when 1 solution is available)
        // will likely solve initial problems.  Once that is show to be wrong,
        // come back with something more general.
        NPNR_ASSERT(false);

    } while (!wire_to_expansion.empty());

    return true;
}

bool Arch::SiteRouter::checkSiteRouting(const Context *ctx, const Arch::TileStatus &tile_status) const
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
        log_info("Checking site routing for site %s\n",
                 ctx->chip_info->sites[ctx->chip_info->tiles[tile].sites[site]].name.get());
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
    //
    // FIXME: Populate "consumed_wires" with all VCC/GND tied in the site.
    // This will allow route_site to leverage site local constant sources.
    //
    // FIXME: Handle case where a constant is requested, but use of an
    // inverter is possible. This is the place to handle "bestConstant"
    // (e.g. route VCC's over GND's, etc).
    //
    // FIXME: Enable some LUT rotation!
    // Default cell/bel pin map always uses high pins, which will generate
    // conflicts where there are none!!!

    SiteInformation site_info(ctx, cells_in_site);

    // Push from cell pins to the first WireId from each cell pin.
    if (!site_info.check_initial_wires()) {
        site_ok = false;
        return site_ok;
    }

    site_ok = route_site(ctx, &site_info);
    if (verbose_site_router(ctx)) {
        if (site_ok) {
            site_info.remove_routed_sources();
            NPNR_ASSERT(site_info.is_fully_routed());
            log_info("Site %s is routable\n",
                     ctx->chip_info->sites[ctx->chip_info->tiles[tile].sites[site]].name.get());
        } else {
            log_info("Site %s is not routable\n",
                     ctx->chip_info->sites[ctx->chip_info->tiles[tile].sites[site]].name.get());
        }
    }

    return site_ok;
}

NEXTPNR_NAMESPACE_END
