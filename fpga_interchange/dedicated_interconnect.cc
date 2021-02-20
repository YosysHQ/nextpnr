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

#include "nextpnr.h"
#include "log.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

void DedicatedInterconnect::init(const Context *ctx) {
    this->ctx = ctx;

    if(ctx->debug) {
        log_info("Finding dedicated interconnect!\n");
    }

    find_dedicated_interconnect();
    if(ctx->debug) {
        print_dedicated_interconnect();
    }
}

bool DedicatedInterconnect::check_routing(
        BelId src_bel, IdString src_bel_pin,
        BelId dst_bel, IdString dst_bel_pin) const {
    // FIXME: Implement.
    return false;
}

bool DedicatedInterconnect::isBelLocationValid(BelId bel, const CellInfo* cell) const {
    NPNR_ASSERT(bel != BelId());

    Loc bel_loc = ctx->getBelLocation(bel);

    const auto &bel_data = bel_info(ctx->chip_info, bel);

    for(const auto &port_pair : cell->ports) {
        IdString port_name = port_pair.first;
        NetInfo *net = port_pair.second.net;
        if(net == nullptr) {
            continue;
        }

        // Only check sink BELs.
        if(net->driver.cell == cell && net->driver.port == port_name) {
            continue;
        }

        // This net doesn't have a driver, probably not valid?
        NPNR_ASSERT(net->driver.cell != nullptr);

        BelId driver_bel = net->driver.cell->bel;
        if(driver_bel == BelId()) {
            return true;
        }

        const auto &driver_bel_data = bel_info(ctx->chip_info, driver_bel);

        Loc driver_loc = ctx->getBelLocation(driver_bel);

        DeltaTileTypeBelPin driver_type_bel_pin;
        driver_type_bel_pin.delta_x = driver_loc.x - bel_loc.x;
        driver_type_bel_pin.delta_y = driver_loc.y - bel_loc.y;
        driver_type_bel_pin.type_bel_pin.tile_type = ctx->chip_info->tiles[driver_bel.tile].type;
        driver_type_bel_pin.type_bel_pin.bel_index = driver_bel.index;
        driver_type_bel_pin.type_bel_pin.bel_pin = get_only_value(ctx->getBelPinsForCellPin(net->driver.cell, net->driver.port));

        for(IdString bel_pin : ctx->getBelPinsForCellPin(cell, port_name)) {
            TileTypeBelPin type_bel_pin;
            type_bel_pin.tile_type = ctx->chip_info->tiles[bel.tile].type;
            type_bel_pin.bel_index = bel.index;
            type_bel_pin.bel_pin = bel_pin;

            auto iter = pins_with_dedicate_interconnect.find(type_bel_pin);
            if(iter == pins_with_dedicate_interconnect.end()) {
                // This BEL pin doesn't have a dedicate interconnect.
                continue;
            }

            if(bel.tile == driver_bel.tile && bel_data.site == driver_bel_data.site) {
                // This is a site local routing, even though this is a sink
                // with a dedicated interconnect.
                continue;
            }

            // Do fast routing check to see if the pair of driver and sink
            // every are valid.
            if(iter->second.count(driver_type_bel_pin) == 0) {
                if(ctx->verbose) {
                    log_info("BEL %s is not valid because pin %s cannot be driven by %s/%s\n",
                            ctx->nameOfBel(bel),
                            bel_pin.c_str(ctx),
                            ctx->nameOfBel(driver_bel),
                            driver_type_bel_pin.type_bel_pin.bel_pin.c_str(ctx));
                }
                return false;
            }

            // Do detailed routing check to ensure driver can reach sink.
            //
            // FIXME: This might be too slow, but it handles a case on
            // SLICEL.COUT -> SLICEL.CIN has delta_y = {1, 2}, but the
            // delta_y=2 case is rare.
            if(!check_routing(
                    driver_bel, driver_type_bel_pin.type_bel_pin.bel_pin,
                    bel, bel_pin)) {
                if(ctx->verbose) {
                    log_info("BEL %s is not valid because pin %s cannot be driven by %s/%s (via detailed check)\n",
                            ctx->nameOfBel(bel),
                            bel_pin.c_str(ctx),
                            ctx->nameOfBel(driver_bel),
                            driver_type_bel_pin.type_bel_pin.bel_pin.c_str(ctx));
                }
                return false;
            }
        }
    }

    return true;
}

void DedicatedInterconnect::print_dedicated_interconnect() const {
    log_info("Found %zu sinks with dedicated interconnect\n", pins_with_dedicate_interconnect.size());
    std::vector<TileTypeBelPin> sorted_keys;
    for(const auto & sink_to_srcs : pins_with_dedicate_interconnect) {
        sorted_keys.push_back(sink_to_srcs.first);
    }
    std::sort(sorted_keys.begin(), sorted_keys.end());

    for(const auto & dst : sorted_keys) {
        for(const auto & src : pins_with_dedicate_interconnect.at(dst)) {
            const TileTypeInfoPOD & src_tile_type = ctx->chip_info->tile_types[src.type_bel_pin.tile_type];
            const BelInfoPOD & src_bel_info = src_tile_type.bel_data[src.type_bel_pin.bel_index];
            IdString src_site_type = IdString(src_tile_type.site_types[src_bel_info.site]);
            IdString src_bel_pin = src.type_bel_pin.bel_pin;

            const TileTypeInfoPOD & dst_tile_type = ctx->chip_info->tile_types[dst.tile_type];
            const BelInfoPOD & dst_bel_info = dst_tile_type.bel_data[dst.bel_index];
            IdString dst_site_type = IdString(dst_tile_type.site_types[dst_bel_info.site]);
            IdString dst_bel_pin = dst.bel_pin;

            log_info("%s.%s/%s/%s (%d, %d) -> %s.%s/%s/%s\n",
                    IdString(src_tile_type.name).c_str(ctx),
                    src_site_type.c_str(ctx),
                    IdString(src_bel_info.name).c_str(ctx),
                    src_bel_pin.c_str(ctx),
                    src.delta_x,
                    src.delta_y,
                    IdString(dst_tile_type.name).c_str(ctx),
                    dst_site_type.c_str(ctx),
                    IdString(dst_bel_info.name).c_str(ctx),
                    dst_bel_pin.c_str(ctx));

        }
    }
}

void DedicatedInterconnect::find_dedicated_interconnect() {
    for(BelId bel : ctx->getBels()) {
        const auto & bel_data = bel_info(ctx->chip_info, bel);
        if(bel_data.category != BEL_CATEGORY_LOGIC) {
            continue;
        }
        if(bel_data.synthetic) {
            continue;
        }

        for(size_t i = 0; i < bel_data.num_bel_wires; ++i) {
            if(bel_data.types[i] != PORT_IN) {
                continue;
            }

            WireId wire;
            wire.tile = bel.tile;
            wire.index = bel_data.wires[i];

            expand_bel(bel, IdString(bel_data.ports[i]), wire);
        }
    }
}

// All legal routes involved at most 2 sites, the source site and the sink
// site.  The source site and sink sites may be the same, but that is not
// dedicated routing, that is intra site routing.
//
// Dedicated routing must leave the sink site, traverse some routing and
// terminate at another site.  Routing that "flys" over a site is expressed as
// a psuedo-pip connected the relevant site pin wires, rather than traversing
// the site.
enum WireNodeState {
    IN_SINK_SITE = 0,
    IN_ROUTING = 1,
    IN_SOURCE_SITE = 2
};

struct WireNode {
    WireId wire;
    WireNodeState state;
    int depth;
};

// Maximum depth that a dedicate interconnect is considered.
//
// Routing networks with depth <= kMaxDepth is considers a dedicated
// interconnect.
constexpr int kMaxDepth = 20;

void DedicatedInterconnect::expand_bel(BelId bel, IdString pin, WireId wire) {
    NPNR_ASSERT(bel != BelId());

    std::vector<WireNode> nodes_to_expand;

    const auto & src_wire_data = ctx->wire_info(wire);
    NPNR_ASSERT(src_wire_data.site != -1);

    WireNode wire_node;
    wire_node.wire = wire;
    wire_node.state = IN_SINK_SITE;
    wire_node.depth = 0;

    nodes_to_expand.push_back(wire_node);

    Loc sink_loc = ctx->getBelLocation(bel);
    std::unordered_set<DeltaTileTypeBelPin> srcs;

    while(!nodes_to_expand.empty()) {
        WireNode node_to_expand = nodes_to_expand.back();
        nodes_to_expand.pop_back();

        for(PipId pip : ctx->getPipsUphill(node_to_expand.wire)) {
            if(ctx->is_pip_synthetic(pip)) {
                continue;
            }

            WireId wire = ctx->getPipSrcWire(pip);
            if(wire == WireId()) {
                continue;
            }

            WireNode next_node;
            next_node.wire = wire;
            next_node.depth = node_to_expand.depth += 1;

            if(next_node.depth > kMaxDepth) {
                // Dedicated routing should reach sources by kMaxDepth (with
                // tuning).
                //
                // FIXME: Consider removing kMaxDepth and use kMaxSources?
                return;
            }

            auto const & wire_data = ctx->wire_info(wire);

            bool expand_node = true;
            if(ctx->is_site_port(pip)) {
                switch(node_to_expand.state) {
                    case IN_SINK_SITE:
                        NPNR_ASSERT(wire_data.site == -1);
                        next_node.state = IN_ROUTING;
                        break;
                    case IN_ROUTING:
                        NPNR_ASSERT(wire_data.site != -1);
                        if(wire_data.site == src_wire_data.site) {
                            // Dedicated routing won't have straight loops,
                            // general routing looks like that.
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

            if(expand_node) {
                nodes_to_expand.push_back(next_node);
            } else {
                continue;
            }

            if(next_node.state == IN_SOURCE_SITE) {
                for(BelPin bel_pin : ctx->getWireBelPins(wire)) {
                    BelId src_bel = bel_pin.bel;
                    auto const & bel_data = bel_info(ctx->chip_info, src_bel);
                    NPNR_ASSERT(bel_data.site != src_wire_data.site);

                    if(bel_data.category != BEL_CATEGORY_LOGIC) {
                        continue;
                    }
                    if(bel_data.synthetic) {
                        continue;
                    }
                    if(ctx->getBelPinType(bel_pin.bel, bel_pin.pin) != PORT_OUT) {
                        continue;
                    }

                    Loc src_loc = ctx->getBelLocation(src_bel);

                    DeltaTileTypeBelPin delta_type_bel_pin;
                    delta_type_bel_pin.delta_x = src_loc.x - sink_loc.x;
                    delta_type_bel_pin.delta_x = src_loc.y - sink_loc.y;
                    delta_type_bel_pin.type_bel_pin.tile_type = ctx->chip_info->tiles[src_bel.tile].type;
                    delta_type_bel_pin.type_bel_pin.bel_index = src_bel.index;
                    delta_type_bel_pin.type_bel_pin.bel_pin = bel_pin.pin;
                    srcs.emplace(delta_type_bel_pin);
                }
            }
        }
    }

    TileTypeBelPin type_bel_pin;
    type_bel_pin.tile_type = ctx->chip_info->tiles[bel.tile].type;
    type_bel_pin.bel_index = bel.index;
    type_bel_pin.bel_pin = pin;

    auto result = pins_with_dedicate_interconnect.emplace(type_bel_pin, srcs);
    if(!result.second) {
        // type_bel_pin was already present! Add any new sources from this
        // sink type (if any);
        for(auto src : srcs) {
            result.first->second.emplace(src);
        }
    }
}

NEXTPNR_NAMESPACE_END
