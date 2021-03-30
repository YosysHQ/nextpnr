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

#include "log.h"
#include "nextpnr.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

// All legal routes involved at most 2 sites, the source site and the sink
// site.  The source site and sink sites may be the same, but that is not
// dedicated routing, that is intra site routing.
//
// Dedicated routing must leave the sink site, traverse some routing and
// terminate at another site.  Routing that "flys" over a site is expressed as
// a psuedo-pip connected the relevant site pin wires, rather than traversing
// the site.
enum WireNodeState
{
    IN_SINK_SITE = 0,
    IN_ROUTING = 1,
    IN_SOURCE_SITE = 2
};

struct WireNode
{
    WireId wire;
    WireNodeState state;
    int depth;
};

// Maximum depth that a dedicate interconnect is considered.
//
// Routing networks with depth <= kMaxDepth is considers a dedicated
// interconnect.
constexpr int kMaxDepth = 20;

void DedicatedInterconnect::init(const Context *ctx)
{
    this->ctx = ctx;

    if (ctx->debug) {
        log_info("Finding dedicated interconnect!\n");
    }

    find_dedicated_interconnect();
    if (ctx->debug) {
        print_dedicated_interconnect();
    }
}

bool DedicatedInterconnect::check_routing(BelId src_bel, IdString src_bel_pin, BelId dst_bel, IdString dst_bel_pin,
                                          bool site_only) const
{
    std::vector<WireNode> nodes_to_expand;

    WireId src_wire = ctx->getBelPinWire(src_bel, src_bel_pin);

    const auto &src_wire_data = ctx->wire_info(src_wire);
    NPNR_ASSERT(src_wire_data.site != -1);

    WireId dst_wire = ctx->getBelPinWire(dst_bel, dst_bel_pin);

    if (src_wire == dst_wire) {
        return true;
    }

    const auto &dst_wire_data = ctx->wire_info(dst_wire);
    NPNR_ASSERT(dst_wire_data.site != -1);

    WireNode wire_node;
    wire_node.wire = src_wire;
    wire_node.state = IN_SOURCE_SITE;
    wire_node.depth = 0;

    nodes_to_expand.push_back(wire_node);

    while (!nodes_to_expand.empty()) {
        WireNode node_to_expand = nodes_to_expand.back();
        nodes_to_expand.pop_back();

        for (PipId pip : ctx->getPipsDownhill(node_to_expand.wire)) {
            if (ctx->is_pip_synthetic(pip)) {
                continue;
            }

            WireId wire = ctx->getPipDstWire(pip);
            if (wire == WireId()) {
                continue;
            }

            if (ctx->debug) {
                log_info(" - At wire %s via %s\n", ctx->nameOfWire(wire), ctx->nameOfPip(pip));
            }

            WireNode next_node;
            next_node.wire = wire;
            next_node.depth = node_to_expand.depth += 1;

            if (next_node.depth > kMaxDepth) {
                // Dedicated routing should reach sources by kMaxDepth (with
                // tuning).
                //
                // FIXME: Consider removing kMaxDepth and use kMaxSources?
                return false;
            }

            auto const &wire_data = ctx->wire_info(wire);

            bool expand_node = true;
            if (ctx->is_site_port(pip)) {
                if (site_only) {
                    // When routing site only, don't allow site ports.
                    continue;
                }

                switch (node_to_expand.state) {
                case IN_SOURCE_SITE:
                    NPNR_ASSERT(wire_data.site == -1);
                    next_node.state = IN_ROUTING;
                    break;
                case IN_ROUTING:
                    NPNR_ASSERT(wire_data.site != -1);
                    if (wire.tile == src_wire.tile && wire_data.site == src_wire_data.site) {
                        // Dedicated routing won't have straight loops,
                        // general routing looks like that.
#ifdef DEBUG_EXPANSION
                        log_info(" - Not dedicated site routing because loop!");
#endif
                        return false;
                    }
                    next_node.state = IN_SINK_SITE;
                    break;
                case IN_SINK_SITE:
                    // Once entering a site, do not leave it again.
                    // This path is not a legal route!
                    expand_node = false;
                    break;
                default:
                    // Unreachable!!!
                    NPNR_ASSERT(false);
                }
            } else {
                next_node.state = node_to_expand.state;
            }

            if (expand_node) {
                nodes_to_expand.push_back(next_node);
            } else {
                continue;
            }

            if (next_node.state == IN_SINK_SITE) {
                for (BelPin bel_pin : ctx->getWireBelPins(wire)) {
                    if (bel_pin.bel == dst_bel && bel_pin.pin == dst_bel_pin) {
                        if (ctx->debug) {
                            log_info("Valid dedicated interconnect from %s/%s to %s/%s\n", ctx->nameOfBel(src_bel),
                                     src_bel_pin.c_str(ctx), ctx->nameOfBel(dst_bel), dst_bel_pin.c_str(ctx));
                        }
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

bool DedicatedInterconnect::is_driver_on_net_valid(BelId driver_bel, const CellInfo *cell, IdString driver_port,
                                                   NetInfo *net) const
{
    const auto &driver_bel_data = bel_info(ctx->chip_info, driver_bel);

    TileTypeBelPin type_bel_pin;
    type_bel_pin.tile_type = ctx->chip_info->tiles[driver_bel.tile].type;
    type_bel_pin.bel_index = driver_bel.index;

    Loc driver_loc = ctx->getBelLocation(driver_bel);

    for (IdString driver_bel_pin : ctx->getBelPinsForCellPin(cell, driver_port)) {
        type_bel_pin.bel_pin = driver_bel_pin;

        auto iter = sources.find(type_bel_pin);
        if (iter == sources.end()) {
            // This BEL pin doesn't have a dedicate interconnect.
            continue;
        }

        for (const PortRef &port_ref : net->users) {
            NPNR_ASSERT(port_ref.cell != nullptr);

            if (port_ref.cell->bel == BelId()) {
                // FIXME: This should actually return "unknown!" because the
                // sink is unplaced.  Once the sink is placed, this constraint
                // can be evaluated.
                if (ctx->debug) {
                    log_info("BEL %s is not valid because sink cell %s/%s is not placed\n", ctx->nameOfBel(driver_bel),
                             port_ref.cell->name.c_str(ctx), port_ref.port.c_str(ctx));
                }
                return false;
            }

            BelId sink_bel = port_ref.cell->bel;
            const auto &sink_bel_data = bel_info(ctx->chip_info, sink_bel);
            Loc sink_loc = ctx->getBelLocation(port_ref.cell->bel);

            if (sink_bel.tile == driver_bel.tile && sink_bel_data.site == driver_bel_data.site) {
                // This might site local routing.  See if it can be routed
                for (IdString sink_bel_pin : ctx->getBelPinsForCellPin(port_ref.cell, port_ref.port)) {
                    if (!check_routing(driver_bel, driver_bel_pin, sink_bel, sink_bel_pin, /*site_only=*/true)) {
                        return false;
                    }
                }
                continue;
            }

            DeltaTileTypeBelPin sink_type_bel_pin;
            sink_type_bel_pin.delta_x = sink_loc.x - driver_loc.x;
            sink_type_bel_pin.delta_y = sink_loc.y - driver_loc.y;
            sink_type_bel_pin.type_bel_pin.tile_type = ctx->chip_info->tiles[sink_bel.tile].type;
            sink_type_bel_pin.type_bel_pin.bel_index = sink_bel.index;

            for (IdString sink_bel_pin : ctx->getBelPinsForCellPin(port_ref.cell, port_ref.port)) {
                sink_type_bel_pin.type_bel_pin.bel_pin = sink_bel_pin;

                // Do fast routing check to see if the pair of driver and sink
                // every are valid.
                if (iter->second.count(sink_type_bel_pin) == 0) {
                    if (ctx->debug) {
                        log_info("BEL %s is not valid because pin %s cannot reach %s/%s\n", ctx->nameOfBel(driver_bel),
                                 driver_bel_pin.c_str(ctx), ctx->nameOfBel(sink_bel), sink_bel_pin.c_str(ctx));
                    }
                    return false;
                }

                // Do detailed routing check to ensure driver can reach sink.
                //
                // FIXME: This might be too slow, but it handles a case on
                // SLICEL.COUT -> SLICEL.CIN has delta_y = {1, 2}, but the
                // delta_y=2 case is rare.
                if (!check_routing(driver_bel, driver_bel_pin, sink_bel, sink_bel_pin, /*site_only=*/false)) {
                    if (ctx->debug) {
                        log_info("BEL %s is not valid because pin %s cannot be reach %s/%s (via detailed check)\n",
                                 ctx->nameOfBel(driver_bel), driver_bel_pin.c_str(ctx), ctx->nameOfBel(sink_bel),
                                 sink_bel_pin.c_str(ctx));
                    }
                    return false;
                }
            }
        }
    }

    return true;
}

bool DedicatedInterconnect::is_sink_on_net_valid(BelId bel, const CellInfo *cell, IdString port_name,
                                                 NetInfo *net) const
{
    const auto &bel_data = bel_info(ctx->chip_info, bel);
    Loc bel_loc = ctx->getBelLocation(bel);

    BelId driver_bel = net->driver.cell->bel;

    for (IdString bel_pin : ctx->getBelPinsForCellPin(cell, port_name)) {
        TileTypeBelPin type_bel_pin;
        type_bel_pin.tile_type = ctx->chip_info->tiles[bel.tile].type;
        type_bel_pin.bel_index = bel.index;
        type_bel_pin.bel_pin = bel_pin;

        auto iter = sinks.find(type_bel_pin);
        if (iter == sinks.end()) {
            // This BEL pin doesn't have a dedicate interconnect.
            continue;
        }

        if (driver_bel == BelId()) {
            // FIXME: This should actually return "unknown!" because the
            // driver is unplaced.  Once the driver is placed, this constraint
            // can be evaluated.
            if (ctx->debug) {
                log_info("BEL %s is not valid because driver cell %s/%s is not placed\n", ctx->nameOfBel(bel),
                         net->driver.cell->name.c_str(ctx), net->driver.port.c_str(ctx));
            }
            return false;
        }

        const auto &driver_bel_data = bel_info(ctx->chip_info, driver_bel);

        if (bel.tile == driver_bel.tile && bel_data.site == driver_bel_data.site) {
            // This is a site local routing, even though this is a sink
            // with a dedicated interconnect.
            continue;
        }

        Loc driver_loc = ctx->getBelLocation(driver_bel);

        DeltaTileTypeBelPin driver_type_bel_pin;
        driver_type_bel_pin.delta_x = driver_loc.x - bel_loc.x;
        driver_type_bel_pin.delta_y = driver_loc.y - bel_loc.y;
        driver_type_bel_pin.type_bel_pin.tile_type = ctx->chip_info->tiles[driver_bel.tile].type;
        driver_type_bel_pin.type_bel_pin.bel_index = driver_bel.index;
        driver_type_bel_pin.type_bel_pin.bel_pin =
                get_only_value(ctx->getBelPinsForCellPin(net->driver.cell, net->driver.port));

        // Do fast routing check to see if the pair of driver and sink
        // every are valid.
        if (iter->second.count(driver_type_bel_pin) == 0) {
            if (ctx->debug) {
                log_info("BEL %s is not valid because pin %s cannot be driven by %s/%s\n", ctx->nameOfBel(bel),
                         bel_pin.c_str(ctx), ctx->nameOfBel(driver_bel),
                         driver_type_bel_pin.type_bel_pin.bel_pin.c_str(ctx));
            }
            return false;
        }

        // Do detailed routing check to ensure driver can reach sink.
        //
        // FIXME: This might be too slow, but it handles a case on
        // SLICEL.COUT -> SLICEL.CIN has delta_y = {1, 2}, but the
        // delta_y=2 case is rare.
        if (!check_routing(driver_bel, driver_type_bel_pin.type_bel_pin.bel_pin, bel, bel_pin, /*site_only=*/false)) {
            if (ctx->debug) {
                log_info("BEL %s is not valid because pin %s cannot be driven by %s/%s (via detailed check)\n",
                         ctx->nameOfBel(bel), bel_pin.c_str(ctx), ctx->nameOfBel(driver_bel),
                         driver_type_bel_pin.type_bel_pin.bel_pin.c_str(ctx));
            }
            return false;
        }
    }

    return true;
}

bool DedicatedInterconnect::isBelLocationValid(BelId bel, const CellInfo *cell) const
{
    NPNR_ASSERT(bel != BelId());

    for (const auto &port_pair : cell->ports) {
        IdString port_name = port_pair.first;
        NetInfo *net = port_pair.second.net;
        if (net == nullptr) {
            continue;
        }

        // This net doesn't have a driver, probably not valid?
        NPNR_ASSERT(net->driver.cell != nullptr);

        // Only check sink BELs.
        if (net->driver.cell == cell && net->driver.port == port_name) {
            if (!is_driver_on_net_valid(bel, cell, port_name, net)) {
                return false;
            }
        } else {
            if (!is_sink_on_net_valid(bel, cell, port_name, net)) {
                return false;
            }
        }
    }

    return true;
}

void DedicatedInterconnect::explain_bel_status(BelId bel, const CellInfo *cell) const
{
    NPNR_ASSERT(bel != BelId());

    for (const auto &port_pair : cell->ports) {
        IdString port_name = port_pair.first;
        NetInfo *net = port_pair.second.net;
        if (net == nullptr) {
            continue;
        }

        // This net doesn't have a driver, probably not valid?
        NPNR_ASSERT(net->driver.cell != nullptr);

        // Only check sink BELs.
        if (net->driver.cell == cell && net->driver.port == port_name) {
            if (!is_driver_on_net_valid(bel, cell, port_name, net)) {
                log_info("Driver %s/%s is not valid on net '%s'", cell->name.c_str(ctx), port_name.c_str(ctx),
                         net->name.c_str(ctx));
            }
        } else {
            if (!is_sink_on_net_valid(bel, cell, port_name, net)) {
                log_info("Sink %s/%s is not valid on net '%s'", cell->name.c_str(ctx), port_name.c_str(ctx),
                         net->name.c_str(ctx));
            }
        }
    }
}

void DedicatedInterconnect::print_dedicated_interconnect() const
{
    log_info("Found %zu sinks with dedicated interconnect\n", sinks.size());
    log_info("Found %zu sources with dedicated interconnect\n", sources.size());
    std::vector<TileTypeBelPin> sorted_keys;
    for (const auto &sink_to_srcs : sinks) {
        sorted_keys.push_back(sink_to_srcs.first);
    }
    for (const auto &src_to_sinks : sources) {
        sorted_keys.push_back(src_to_sinks.first);
    }
    std::sort(sorted_keys.begin(), sorted_keys.end());

    for (const auto &key : sorted_keys) {
        auto iter = sinks.find(key);
        if (iter != sinks.end()) {
            auto dst = key;
            for (const auto &src_delta : iter->second) {
                auto src = src_delta.type_bel_pin;
                auto delta_x = src_delta.delta_x;
                auto delta_y = src_delta.delta_y;

                const TileTypeInfoPOD &src_tile_type = ctx->chip_info->tile_types[src.tile_type];
                const BelInfoPOD &src_bel_info = src_tile_type.bel_data[src.bel_index];
                IdString src_site_type = IdString(src_tile_type.site_types[src_bel_info.site]);
                IdString src_bel_pin = src.bel_pin;

                const TileTypeInfoPOD &dst_tile_type = ctx->chip_info->tile_types[dst.tile_type];
                const BelInfoPOD &dst_bel_info = dst_tile_type.bel_data[dst.bel_index];
                IdString dst_site_type = IdString(dst_tile_type.site_types[dst_bel_info.site]);
                IdString dst_bel_pin = dst.bel_pin;

                log_info("%s.%s[%d]/%s/%s (%d, %d) -> %s.%s[%d]/%s/%s\n", IdString(src_tile_type.name).c_str(ctx),
                         src_site_type.c_str(ctx), src_bel_info.site, IdString(src_bel_info.name).c_str(ctx),
                         src_bel_pin.c_str(ctx), delta_x, delta_y, IdString(dst_tile_type.name).c_str(ctx),
                         dst_site_type.c_str(ctx), dst_bel_info.site, IdString(dst_bel_info.name).c_str(ctx),
                         dst_bel_pin.c_str(ctx));
            }
        } else {
            auto src = key;
            for (const auto &dst_delta : sources.at(key)) {
                auto dst = dst_delta.type_bel_pin;
                auto delta_x = dst_delta.delta_x;
                auto delta_y = dst_delta.delta_y;

                const TileTypeInfoPOD &src_tile_type = ctx->chip_info->tile_types[src.tile_type];
                const BelInfoPOD &src_bel_info = src_tile_type.bel_data[src.bel_index];
                IdString src_site_type = IdString(src_tile_type.site_types[src_bel_info.site]);
                IdString src_bel_pin = src.bel_pin;

                const TileTypeInfoPOD &dst_tile_type = ctx->chip_info->tile_types[dst.tile_type];
                const BelInfoPOD &dst_bel_info = dst_tile_type.bel_data[dst.bel_index];
                IdString dst_site_type = IdString(dst_tile_type.site_types[dst_bel_info.site]);
                IdString dst_bel_pin = dst.bel_pin;

                log_info("%s.%s[%d]/%s/%s -> %s.%s[%d]/%s/%s  (%d, %d)\n", IdString(src_tile_type.name).c_str(ctx),
                         src_site_type.c_str(ctx), src_bel_info.site, IdString(src_bel_info.name).c_str(ctx),
                         src_bel_pin.c_str(ctx), IdString(dst_tile_type.name).c_str(ctx), dst_site_type.c_str(ctx),
                         dst_bel_info.site, IdString(dst_bel_info.name).c_str(ctx), dst_bel_pin.c_str(ctx), delta_x,
                         delta_y);
            }
        }
    }
}

void DedicatedInterconnect::find_dedicated_interconnect()
{
    for (BelId bel : ctx->getBels()) {
        const auto &bel_data = bel_info(ctx->chip_info, bel);
        if (bel_data.category != BEL_CATEGORY_LOGIC) {
            continue;
        }
        if (bel_data.synthetic) {
            continue;
        }

        for (int i = 0; i < bel_data.num_bel_wires; ++i) {
            if (bel_data.types[i] != PORT_IN) {
                continue;
            }

            WireId wire;
            wire.tile = bel.tile;
            wire.index = bel_data.wires[i];

            expand_sink_bel(bel, IdString(bel_data.ports[i]), wire);
        }
    }

    std::unordered_set<TileTypeBelPin> seen_pins;
    for (auto sink_pair : sinks) {
        for (auto src : sink_pair.second) {
            seen_pins.emplace(src.type_bel_pin);
        }
    }

    for (BelId bel : ctx->getBels()) {
        const auto &bel_data = bel_info(ctx->chip_info, bel);
        if (bel_data.category != BEL_CATEGORY_LOGIC) {
            continue;
        }
        if (bel_data.synthetic) {
            continue;
        }

        for (int i = 0; i < bel_data.num_bel_wires; ++i) {
            if (bel_data.types[i] != PORT_OUT) {
                continue;
            }

            IdString bel_pin(bel_data.ports[i]);

            TileTypeBelPin type_bel_pin;
            type_bel_pin.tile_type = ctx->chip_info->tiles[bel.tile].type;
            type_bel_pin.bel_index = bel.index;
            type_bel_pin.bel_pin = bel_pin;

            // Don't visit src pins already handled in the sink expansion!
            if (seen_pins.count(type_bel_pin)) {
                continue;
            }

            WireId wire;
            wire.tile = bel.tile;
            wire.index = bel_data.wires[i];

            expand_source_bel(bel, bel_pin, wire);
        }
    }
}

void DedicatedInterconnect::expand_sink_bel(BelId sink_bel, IdString sink_pin, WireId sink_wire)
{
    NPNR_ASSERT(sink_bel != BelId());
#ifdef DEBUG_EXPANSION
    log_info("Expanding from %s/%s\n", ctx->nameOfBel(sink_bel), pin.c_str(ctx));
#endif

    std::vector<WireNode> nodes_to_expand;

    const auto &sink_wire_data = ctx->wire_info(sink_wire);
    NPNR_ASSERT(sink_wire_data.site != -1);

    WireNode wire_node;
    wire_node.wire = sink_wire;
    wire_node.state = IN_SINK_SITE;
    wire_node.depth = 0;

    nodes_to_expand.push_back(wire_node);

    Loc sink_loc = ctx->getBelLocation(sink_bel);
    std::unordered_set<DeltaTileTypeBelPin> srcs;

    while (!nodes_to_expand.empty()) {
        WireNode node_to_expand = nodes_to_expand.back();
        nodes_to_expand.pop_back();

        for (PipId pip : ctx->getPipsUphill(node_to_expand.wire)) {
            if (ctx->is_pip_synthetic(pip)) {
                continue;
            }

            WireId wire = ctx->getPipSrcWire(pip);
            if (wire == WireId()) {
                continue;
            }

#ifdef DEBUG_EXPANSION
            log_info(" - At wire %s via %s\n", ctx->nameOfWire(wire), ctx->nameOfPip(pip));
#endif

            WireNode next_node;
            next_node.wire = wire;
            next_node.depth = node_to_expand.depth += 1;

            if (next_node.depth > kMaxDepth) {
                // Dedicated routing should reach sources by kMaxDepth (with
                // tuning).
                //
                // FIXME: Consider removing kMaxDepth and use kMaxSources?
#ifdef DEBUG_EXPANSION
                log_info(" - Exceeded max depth!\n");
#endif
                return;
            }

            auto const &wire_data = ctx->wire_info(wire);

            bool expand_node = true;
            if (ctx->is_site_port(pip)) {
                switch (node_to_expand.state) {
                case IN_SINK_SITE:
                    NPNR_ASSERT(wire_data.site == -1);
                    next_node.state = IN_ROUTING;
                    break;
                case IN_ROUTING:
                    NPNR_ASSERT(wire_data.site != -1);
                    if (wire.tile == sink_wire.tile && wire_data.site == sink_wire_data.site) {
                        // Dedicated routing won't have straight loops,
                        // general routing looks like that.
#ifdef DEBUG_EXPANSION
                        log_info(" - Not dedicated site routing because loop!");
#endif
                        return;
                    }
                    next_node.state = IN_SOURCE_SITE;
                    break;
                case IN_SOURCE_SITE:
                    // Once entering a site, do not leave it again.
                    // This path is not a legal route!
                    expand_node = false;
                    break;
                default:
                    // Unreachable!!!
                    NPNR_ASSERT(false);
                }
            } else {
                next_node.state = node_to_expand.state;
            }

            if (expand_node) {
                nodes_to_expand.push_back(next_node);
            } else {
                continue;
            }

            if (next_node.state == IN_SOURCE_SITE) {
                for (BelPin bel_pin : ctx->getWireBelPins(wire)) {
                    BelId src_bel = bel_pin.bel;
                    auto const &bel_data = bel_info(ctx->chip_info, src_bel);

                    if (bel_data.category != BEL_CATEGORY_LOGIC) {
                        continue;
                    }
                    if (bel_data.synthetic) {
                        continue;
                    }
                    if (ctx->getBelPinType(bel_pin.bel, bel_pin.pin) != PORT_OUT) {
                        continue;
                    }

#ifdef DEBUG_EXPANSION
                    log_info(" - Reached %s/%s\n", ctx->nameOfBel(bel_pin.bel), bel_pin.pin.c_str(ctx));
#endif

                    Loc src_loc = ctx->getBelLocation(src_bel);

                    DeltaTileTypeBelPin delta_type_bel_pin;
                    delta_type_bel_pin.delta_x = src_loc.x - sink_loc.x;
                    delta_type_bel_pin.delta_y = src_loc.y - sink_loc.y;
                    delta_type_bel_pin.type_bel_pin.tile_type = ctx->chip_info->tiles[src_bel.tile].type;
                    delta_type_bel_pin.type_bel_pin.bel_index = src_bel.index;
                    delta_type_bel_pin.type_bel_pin.bel_pin = bel_pin.pin;
                    srcs.emplace(delta_type_bel_pin);
                }
            }
        }
    }

    TileTypeBelPin type_bel_pin;
    type_bel_pin.tile_type = ctx->chip_info->tiles[sink_bel.tile].type;
    type_bel_pin.bel_index = sink_bel.index;
    type_bel_pin.bel_pin = sink_pin;

    auto result = sinks.emplace(type_bel_pin, srcs);
    if (!result.second) {
        // type_bel_pin was already present! Add any new sources from this
        // sink type (if any);
        for (auto src : srcs) {
            result.first->second.emplace(src);
        }
    }
}

void DedicatedInterconnect::expand_source_bel(BelId src_bel, IdString src_pin, WireId src_wire)
{
    NPNR_ASSERT(src_bel != BelId());
#ifdef DEBUG_EXPANSION
    log_info("Expanding from %s/%s\n", ctx->nameOfBel(src_bel), src_pin.c_str(ctx));
#endif

    std::vector<WireNode> nodes_to_expand;

    const auto &src_wire_data = ctx->wire_info(src_wire);
    NPNR_ASSERT(src_wire_data.site != -1);

    WireNode wire_node;
    wire_node.wire = src_wire;
    wire_node.state = IN_SOURCE_SITE;
    wire_node.depth = 0;

    nodes_to_expand.push_back(wire_node);

    Loc src_loc = ctx->getBelLocation(src_bel);
    std::unordered_set<DeltaTileTypeBelPin> dsts;

    while (!nodes_to_expand.empty()) {
        WireNode node_to_expand = nodes_to_expand.back();
        nodes_to_expand.pop_back();

        for (PipId pip : ctx->getPipsDownhill(node_to_expand.wire)) {
            if (ctx->is_pip_synthetic(pip)) {
                continue;
            }

            WireId wire = ctx->getPipDstWire(pip);
            if (wire == WireId()) {
                continue;
            }

#ifdef DEBUG_EXPANSION
            log_info(" - At wire %s via %s\n", ctx->nameOfWire(wire), ctx->nameOfPip(pip));
#endif

            WireNode next_node;
            next_node.wire = wire;
            next_node.depth = node_to_expand.depth += 1;

            if (next_node.depth > kMaxDepth) {
                // Dedicated routing should reach sources by kMaxDepth (with
                // tuning).
                //
                // FIXME: Consider removing kMaxDepth and use kMaxSources?
#ifdef DEBUG_EXPANSION
                log_info(" - Exceeded max depth!\n");
#endif
                return;
            }

            auto const &wire_data = ctx->wire_info(wire);

            bool expand_node = true;
            if (ctx->is_site_port(pip)) {
                switch (node_to_expand.state) {
                case IN_SOURCE_SITE:
                    NPNR_ASSERT(wire_data.site == -1);
                    next_node.state = IN_ROUTING;
                    break;
                case IN_ROUTING:
                    NPNR_ASSERT(wire_data.site != -1);
                    if (wire.tile == src_wire.tile && wire_data.site == src_wire_data.site) {
                        // Dedicated routing won't have straight loops,
                        // general routing looks like that.
#ifdef DEBUG_EXPANSION
                        log_info(" - Not dedicated site routing because loop!");
#endif
                        return;
                    }
                    next_node.state = IN_SINK_SITE;
                    break;
                case IN_SINK_SITE:
                    // Once entering a site, do not leave it again.
                    // This path is not a legal route!
                    expand_node = false;
                    break;
                default:
                    // Unreachable!!!
                    NPNR_ASSERT(false);
                }
            } else {
                next_node.state = node_to_expand.state;
            }

            if (expand_node) {
                nodes_to_expand.push_back(next_node);
            } else {
                continue;
            }

            if (next_node.state == IN_SINK_SITE) {
                for (BelPin bel_pin : ctx->getWireBelPins(wire)) {
                    BelId sink_bel = bel_pin.bel;
                    auto const &bel_data = bel_info(ctx->chip_info, sink_bel);

                    if (bel_data.category != BEL_CATEGORY_LOGIC) {
                        continue;
                    }
                    if (bel_data.synthetic) {
                        continue;
                    }
                    if (ctx->getBelPinType(bel_pin.bel, bel_pin.pin) != PORT_IN) {
                        continue;
                    }

#ifdef DEBUG_EXPANSION
                    log_info(" - Reached %s/%s\n", ctx->nameOfBel(bel_pin.bel), bel_pin.pin.c_str(ctx));
#endif

                    Loc sink_loc = ctx->getBelLocation(sink_bel);

                    DeltaTileTypeBelPin delta_type_bel_pin;
                    delta_type_bel_pin.delta_x = sink_loc.x - src_loc.x;
                    delta_type_bel_pin.delta_y = sink_loc.y - src_loc.y;
                    delta_type_bel_pin.type_bel_pin.tile_type = ctx->chip_info->tiles[sink_bel.tile].type;
                    delta_type_bel_pin.type_bel_pin.bel_index = sink_bel.index;
                    delta_type_bel_pin.type_bel_pin.bel_pin = bel_pin.pin;
                    dsts.emplace(delta_type_bel_pin);
                }
            }
        }
    }

    TileTypeBelPin type_bel_pin;
    type_bel_pin.tile_type = ctx->chip_info->tiles[src_bel.tile].type;
    type_bel_pin.bel_index = src_bel.index;
    type_bel_pin.bel_pin = src_pin;

    auto result = sources.emplace(type_bel_pin, dsts);
    if (!result.second) {
        // type_bel_pin was already present! Add any new sources from this
        // sink type (if any);
        for (auto dst : dsts) {
            result.first->second.emplace(dst);
        }
    }
}

NEXTPNR_NAMESPACE_END
