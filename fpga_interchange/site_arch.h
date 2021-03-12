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

#ifndef SITE_ARCH_H
#define SITE_ARCH_H

NEXTPNR_NAMESPACE_BEGIN

struct SiteInformation {
    const Context *ctx;

    const int32_t tile;
    const int32_t tile_type;
    const int32_t site;
    const std::unordered_set<CellInfo *> &cells_in_site;

    SiteInformation(const Context *ctx, int32_t tile, int32_t site, const std::unordered_set<CellInfo *> &cells_in_site) : ctx(ctx), tile(tile), tile_type(ctx->chip_info->tiles[tile].type), site(site), cells_in_site(cells_in_site) {}

    bool is_wire_in_site(WireId wire) const {
        if(wire.tile != tile) {
            return false;
        }

        return ctx->wire_info(wire).site == site;
    }

    bool is_bel_in_site(BelId bel) const {
        if(bel.tile != tile) {
            return false;
        }

        return bel_info(ctx->chip_info, bel).site == site;

    }

    bool is_pip_part_of_site(PipId pip) const {
        if(pip.tile != tile) {
            return false;
        }

        const auto & tile_type_data = ctx->chip_info->tile_types[tile_type];
        const auto & pip_data = tile_type_data.pip_data[pip.index];
        return pip_data.site == site;
    }
};

// Site routing needs a modification of the routing graph.  Within the site,
// the arch can be consulted for edges.  However the rest of the routing graph
// needs to be reduced for analysis purposes.  Wires within the site are
// SITE_WIRE's.  4 additional nodes are introduced to model out of site
// routing:
//  - OUT_OF_SITE_SOURCE / OUT_OF_SITE_SINK
//   - These represent net sources and sinks that are only reachable via the
//     routing graph (e.g. outside of the site).
//  - SITE_PORT_SOURCE / SITE_PORT_SINK
//   - These represent the routing resources connected to other side of site
//     ports.
//
//  The non-site wire graph is connected like:
//
// ┌─────────────────┐                          ┌────────────────────┐
// │                 │                          │                    │
// │ OUT_OF_SITE_SRC │                          │  OUT_OF_SITE_SINK  │◄────┐
// │                 │                          │                    │     │
// └┬────────────────┘                          └────────────────────┘     │
//  │                                                                      │
//  │                ┌─────────────────────────────────────────────────────┤
//  │                │                                                     │
//  │                │                                                     │
//  │                │                                                     │
//  │                │                                                     │
//  │                ▼                                                     │
//  │      ┌─────────────────┐   ┌─────────────┐       ┌────────────────┐  │
//  │      │                 │   │             │       │                │  │
//  └─────►│  SITE_PORT_SRC  ├──►│    Site     ├──────►│ SITE_PORT_SINK ├──┘
//         │                 │   │             │       │                │
//         └─────────────────┘   └─────────────┘       └────────────────┘
//
struct SiteWire {
    enum Type {
        // This wire is just a plain site wire.
        SITE_WIRE = 0,
        // This wire is a source that is from outside of the site.
        OUT_OF_SITE_SOURCE = 1,
        // This wire is a sink that is from outside of the site.
        OUT_OF_SITE_SINK = 2,
        // This wire is the routing graph wire on the dst side of a site port.
        SITE_PORT_SINK = 3,
        // This wire is the routing graph wire on the src side of a site port.
        SITE_PORT_SOURCE = 4,
        NUMBER_SITE_WIRE_TYPES = 5,
    };

    static SiteWire make(const SiteInformation * site_info, WireId site_wire) {
        NPNR_ASSERT(site_info->is_wire_in_site(site_wire));
        SiteWire out;
        out.type = SITE_WIRE;
        out.wire = site_wire;
        return out;
    }

    static SiteWire make(const SiteInformation * site_info, PortType port_type, NetInfo*net) {
        SiteWire out;
        if(port_type == PORT_OUT) {
            out.type = OUT_OF_SITE_SOURCE;
            out.net = net;
        } else {
            out.type = OUT_OF_SITE_SINK;
            out.net = net;
        }
        return out;
    }


    static SiteWire make_site_port(const SiteInformation *site_info, PipId pip, WireId wire) {
        NPNR_ASSERT(site_info->ctx->is_site_port(pip));

        SiteWire out;
        out.wire = wire;

        WireId src_wire = site_info->ctx->getPipSrcWire(pip);
        WireId dst_wire = site_info->ctx->getPipDstWire(pip);
        if(wire == src_wire) {
            if(site_info->is_wire_in_site(src_wire)) {
                NPNR_ASSERT(!site_info->is_wire_in_site(dst_wire));
                out.type = SITE_WIRE;
            } else {
                NPNR_ASSERT(site_info->is_wire_in_site(dst_wire));
                out.type = SITE_PORT_SOURCE;
                out.pip = pip;
            }
        } else {
            if(site_info->is_wire_in_site(dst_wire)) {
                NPNR_ASSERT(!site_info->is_wire_in_site(src_wire));
                out.type = SITE_WIRE;
            } else {
                NPNR_ASSERT(site_info->is_wire_in_site(src_wire));
                out.type = SITE_PORT_SINK;
                out.pip = pip;
            }
        }

        return out;
    }

    bool operator == (const SiteWire &other) const {
        return wire == other.wire && type == other.type && pip == other.pip && net == other.net;
    }
    bool operator != (const SiteWire &other) const {
        return wire != other.wire || type != other.type || pip != other.pip || net != other.net;
    }

    Type type = NUMBER_SITE_WIRE_TYPES;
    WireId wire;
    PipId pip;
    NetInfo *net = nullptr;
};

struct SitePip {
    enum Type {
        // This is a plain regular site pip.
        SITE_PIP = 0,
        // This pip is a site port, and connects a SITE_WIRE to a SITE_PORT_SINK/SITE_PORT_SRC
        SITE_PORT = 1,
        // This pip connects a OUT_OF_SITE_SOURCE to a SITE_PORT_SRC
        SOURCE_TO_SITE_PORT = 2,
        // This pip connects a SITE_PORT_SINK to a OUT_OF_SITE_SINK
        SITE_PORT_TO_SINK = 3,
        // This pip connects a SITE_PORT_SINK to a SITE_PORT_SRC.
        SITE_PORT_TO_SITE_PORT = 4,
        INVALID_TYPE = 5,
    };

    static SitePip make(const SiteInformation *site_info, PipId pip) {
        SitePip out;
        out.pip = pip;

        if(site_info->ctx->is_site_port(pip)) {
            out.type = SITE_PORT;
        } else {
            out.type = SITE_PIP;
        }
        return out;
    }

    static SitePip make(const SiteInformation *site_info, SiteWire src, PipId dst) {
        NPNR_ASSERT(src.type == SiteWire::OUT_OF_SITE_SOURCE);
        NPNR_ASSERT(site_info->ctx->is_site_port(dst));
        NPNR_ASSERT(site_info->is_wire_in_site(site_info->ctx->getPipDstWire(dst)));

        SitePip out;
        out.type = SOURCE_TO_SITE_PORT;
        out.pip = dst;
        out.wire = src;

        return out;
    }

    static SitePip make(const SiteInformation *site_info, PipId src, SiteWire dst) {
        NPNR_ASSERT(site_info->ctx->is_site_port(src));
        NPNR_ASSERT(site_info->is_wire_in_site(site_info->ctx->getPipSrcWire(src)));
        NPNR_ASSERT(dst.type == SiteWire::OUT_OF_SITE_SINK);

        SitePip out;
        out.type = SITE_PORT_TO_SINK;
        out.pip = src;
        out.wire = dst;

        return out;
    }

    static SitePip make(const SiteInformation *site_info, PipId src_pip, PipId dst_pip) {
        SitePip out;
        out.type = SITE_PORT_TO_SITE_PORT;
        out.pip = src_pip;
        out.other_pip = dst_pip;

        NPNR_ASSERT(site_info->ctx->is_site_port(src_pip));
        NPNR_ASSERT(site_info->ctx->is_site_port(dst_pip));

        NPNR_ASSERT(site_info->is_wire_in_site(site_info->ctx->getPipSrcWire(src_pip)));
        NPNR_ASSERT(site_info->is_wire_in_site(site_info->ctx->getPipDstWire(dst_pip)));

        return out;
    }

    Type type = INVALID_TYPE;
    // For SITE_PORT_TO_SITE_PORT connections, pip is the site -> routing pip.
    PipId pip;
    SiteWire wire;
    // For SITE_PORT_TO_SITE_PORT connections, other_pip is the routing ->
    // site pip.
    PipId other_pip;

    bool operator == (const SitePip &other) const {
        return type == other.type && pip == other.pip && wire == other.wire && other_pip == other.other_pip;
    }
    bool operator != (const SitePip &other) const {
        return type != other.type || pip != other.pip || wire != other.wire || other_pip != other.other_pip;
    }
};
NEXTPNR_NAMESPACE_END

template <> struct std::hash<NEXTPNR_NAMESPACE_PREFIX SiteWire>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX SiteWire &site_wire) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX SiteWire::Type>()(site_wire.type));
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX WireId>()(site_wire.wire));
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX PipId>()(site_wire.pip));
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX NetInfo*>()(site_wire.net));
        return seed;
    }
};

template <> struct std::hash<NEXTPNR_NAMESPACE_PREFIX SitePip>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX SitePip &site_pip) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX SitePip::Type>()(site_pip.type));
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX PipId>()(site_pip.pip));
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX SiteWire>()(site_pip.wire));
        boost::hash_combine(seed, std::hash<NEXTPNR_NAMESPACE_PREFIX PipId>()(site_pip.other_pip));
        return seed;
    }
};

NEXTPNR_NAMESPACE_BEGIN


struct SitePipDownhillRange;
struct SitePipUphillRange;
struct SiteWireRange;
struct SiteNetInfo;

struct SitePipMap {
    SitePip pip;
    size_t count;
};

struct SiteNetMap {
    SiteNetInfo *net;
    size_t count;
};

struct SiteNetInfo {
    const NetInfo *net;
    SiteWire driver;
    absl::flat_hash_set<SiteWire> users;

    absl::flat_hash_map<SiteWire, SitePipMap> wires;
};

struct SiteArch {
    const Context *const ctx;
    const SiteInformation *const site_info;

    absl::flat_hash_map<NetInfo *, SiteNetInfo> nets;
    absl::flat_hash_map<SiteWire, SiteNetMap> wire_to_nets;

    std::vector<PipId> input_site_ports;
    std::vector<PipId> output_site_ports;

    std::vector<SiteWire> out_of_site_sources;
    std::vector<SiteWire> out_of_site_sinks;

    SiteArch(const SiteInformation *site_info) : ctx(site_info->ctx), site_info(site_info) {
        // Build list of input and output site ports
        //
        // FIXME: This doesn't need to be computed over and over, move to
        // arch/chip db.
        const TileTypeInfoPOD & tile_type = loc_info(site_info->ctx->chip_info, *site_info);
        PipId pip;
        pip.tile = site_info->tile;
        for(size_t pip_index = 0; pip_index < tile_type.pip_data.size(); ++pip_index) {
            if(tile_type.pip_data[pip_index].site != site_info->site) {
                continue;
            }

            pip.index = pip_index;

            if(!ctx->is_site_port(pip)) {
                continue;
            }

            WireId src_wire = ctx->getPipSrcWire(pip);
            if(site_info->is_wire_in_site(src_wire)) {
                output_site_ports.push_back(pip);
            } else {
                input_site_ports.push_back(pip);
            }
        }

        // Create list of out of site sources and sinks.

        for (CellInfo *cell : site_info->cells_in_site) {
            for (const auto &pin_pair : cell->cell_bel_pins) {
                const PortInfo &port = cell->ports.at(pin_pair.first);
                if(port.net != nullptr) {
                    nets.emplace(port.net, SiteNetInfo{port.net});
                }
            }
        }

        for(auto &net_pair : nets) {
            NetInfo *net = net_pair.first;
            SiteNetInfo &net_info = net_pair.second;

            // All nets require drivers
            NPNR_ASSERT(net->driver.cell != nullptr);

            bool net_driven_out_of_site = false;
            if(net->driver.cell->bel == BelId()) {
                // The driver of this site hasn't been placed, so treat it as
                // out of site.
                out_of_site_sources.push_back(SiteWire::make(site_info, PORT_OUT, net));
                net_info.driver = out_of_site_sources.back();
                net_driven_out_of_site = true;
            } else {
                if(!site_info->is_bel_in_site(net->driver.cell->bel)) {

                    // The driver of this site has been placed, it is an out
                    // of site source.
                    out_of_site_sources.push_back(
                            SiteWire::make(site_info, PORT_OUT,
                                net));
                    out_of_site_sources.back().wire = ctx->getNetinfoSourceWire(net);
                    net_info.driver = out_of_site_sources.back();

                    net_driven_out_of_site = true;
                } else {
                    net_info.driver = SiteWire::make(site_info, ctx->getNetinfoSourceWire(net));
                }
            }

            if(net_driven_out_of_site) {
                // Because this net is driven from a source out of the site,
                // no out of site sink is required.
                continue;
            }

            // Examine net to determine if it has any users not in this site.
            bool net_used_out_of_site = false;
            WireId out_of_site_wire;
            for(const PortRef & user : net->users) {
                if(user.cell == nullptr) {
                    // This is pretty weird!
                    continue;
                }

                if(user.cell->bel == BelId()) {
                    // Because this net has a user that has not been placed,
                    // and this net is being driven from this site, make sure
                    // this net can be routed from this site.
                    net_used_out_of_site = true;
                    continue;
                }

                if(!site_info->is_bel_in_site(user.cell->bel)) {
                    net_used_out_of_site = true;
                    for(IdString bel_pin : ctx->getBelPinsForCellPin(user.cell, user.port)) {
                        out_of_site_wire = ctx->getBelPinWire(user.cell->bel, bel_pin);
                        break;
                    }
                }
            }

            if(net_used_out_of_site) {
                out_of_site_sinks.push_back(
                        SiteWire::make(site_info, PORT_IN,
                            net));
                if(out_of_site_wire != WireId()) {
                    out_of_site_sinks.back().wire = out_of_site_wire;
                }

                net_info.users.emplace(out_of_site_sinks.back());
            }
        }

        // At this point all nets have a driver SiteWire, but user SiteWire's
        // within the site are not present.  Add them now.
        for(auto &net_pair : nets) {
            NetInfo *net = net_pair.first;
            SiteNetInfo &net_info = net_pair.second;

            for(const PortRef & user : net->users) {
                if(!site_info->is_bel_in_site(user.cell->bel)) {
                    // Only care about BELs within the site at this point.
                    continue;
                }

                for(IdString bel_pin : ctx->getBelPinsForCellPin(user.cell, user.port)) {
                    SiteWire wire = getBelPinWire(user.cell->bel, bel_pin);
                    // Don't add users that are trivially routable!
                    if(wire != net_info.driver) {
                        if(ctx->debug) {
                            log_info("Add user %s because it isn't driver %s\n",
                                    nameOfWire(wire), nameOfWire(net_info.driver));
                        }
                        net_info.users.emplace(wire);
                    }
                }
            }
        }

        for(auto &net_pair : nets) {
            SiteNetInfo *net_info = &net_pair.second;
            auto result = wire_to_nets.emplace(net_info->driver, SiteNetMap{net_info, 1});
            // By this point, trivial congestion at sources should already by
            // avoided, and there should be no duplicates in the
            // driver/users data.
            NPNR_ASSERT(result.second);

            for(const auto &user : net_info->users) {
                result = wire_to_nets.emplace(user, SiteNetMap{net_info, 1});
                NPNR_ASSERT(result.second);
            }
        }
    }

    SiteWire getPipSrcWire(const SitePip &site_pip) const {
        SiteWire site_wire;
        switch(site_pip.type) {
        case SitePip::Type::SITE_PIP:
            return SiteWire::make(site_info, ctx->getPipSrcWire(site_pip.pip));
        case SitePip::Type::SITE_PORT:
            return SiteWire::make_site_port(site_info, site_pip.pip, ctx->getPipSrcWire(site_pip.pip));
        case SitePip::Type::SOURCE_TO_SITE_PORT:
            NPNR_ASSERT(site_pip.wire.type == SiteWire::OUT_OF_SITE_SOURCE);
            return site_pip.wire;
        case SitePip::Type::SITE_PORT_TO_SINK:
            site_wire = SiteWire::make_site_port(site_info, site_pip.pip, ctx->getPipDstWire(site_pip.pip));
            NPNR_ASSERT(site_wire.type == SiteWire::SITE_PORT_SINK);
            return site_wire;
        case SitePip::Type::SITE_PORT_TO_SITE_PORT:
            site_wire = SiteWire::make_site_port(site_info, site_pip.pip, ctx->getPipDstWire(site_pip.pip));
            NPNR_ASSERT(site_wire.type == SiteWire::SITE_PORT_SINK);
            return site_wire;
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    SiteWire getPipDstWire(const SitePip &site_pip) const {
        SiteWire site_wire;
        switch(site_pip.type) {
        case SitePip::Type::SITE_PIP:
            return SiteWire::make(site_info, ctx->getPipDstWire(site_pip.pip));
        case SitePip::Type::SITE_PORT:
            return SiteWire::make_site_port(site_info, site_pip.pip, ctx->getPipDstWire(site_pip.pip));
        case SitePip::Type::SOURCE_TO_SITE_PORT:
            site_wire = SiteWire::make_site_port(site_info, site_pip.pip, ctx->getPipSrcWire(site_pip.pip));
            NPNR_ASSERT(site_wire.type == SiteWire::SITE_PORT_SOURCE);
            return site_wire;
        case SitePip::Type::SITE_PORT_TO_SINK:
            NPNR_ASSERT(site_pip.wire.type == SiteWire::OUT_OF_SITE_SINK);
            return site_pip.wire;
        case SitePip::Type::SITE_PORT_TO_SITE_PORT:
            site_wire = SiteWire::make_site_port(site_info, site_pip.other_pip, ctx->getPipSrcWire(site_pip.other_pip));
            NPNR_ASSERT(site_wire.type == SiteWire::SITE_PORT_SOURCE);
            return site_wire;
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    SitePipDownhillRange getPipsDownhill(const SiteWire &site_wire) const;
    SitePipUphillRange getPipsUphill(const SiteWire &site_wire) const;
    SiteWireRange getWires() const;

    SiteWire getBelPinWire(BelId bel, IdString pin) const {
        WireId wire = ctx->getBelPinWire(bel, pin);
        return SiteWire::make(site_info, wire);
    }

    PortType getBelPinType(BelId bel, IdString pin) const {
        return ctx->getBelPinType(bel, pin);
    }

    const char * nameOfWire(const SiteWire &wire) const {
        switch(wire.type) {
        case SiteWire::SITE_WIRE:
            return ctx->nameOfWire(wire.wire);
        case SiteWire::SITE_PORT_SINK:
            return ctx->nameOfWire(wire.wire);
        case SiteWire::SITE_PORT_SOURCE:
            return ctx->nameOfWire(wire.wire);
        case SiteWire::OUT_OF_SITE_SOURCE:
            return "out of site source, implement me!";
        case SiteWire::OUT_OF_SITE_SINK:
            return "out of site sink, implement me!";
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    const char * nameOfPip(const SitePip &pip) const {
        switch(pip.type) {
        case SitePip::SITE_PIP:
            return ctx->nameOfPip(pip.pip);
        case SitePip::SITE_PORT:
            return ctx->nameOfPip(pip.pip);
        case SitePip::SOURCE_TO_SITE_PORT:
            return "source to site port, implement me!";
        case SitePip::SITE_PORT_TO_SINK:
            return "site port to sink, implement me!";
        case SitePip::SITE_PORT_TO_SITE_PORT:
            return "site port to site port, implement me!";
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    const char * nameOfNet(const SiteNetInfo *net) const {
        return net->net->name.c_str(ctx);
    }

    bool bindWire(const SiteWire &wire, SiteNetInfo *net) {
        auto result = wire_to_nets.emplace(wire, SiteNetMap{net, 1});
        if(result.first->second.net != net) {
            if(ctx->debug) {
                log_info("Net conflict binding wire %s to net %s, conflicts with net %s\n",
                        nameOfWire(wire), nameOfNet(net), nameOfNet(result.first->second.net));
            }
            return false;
        }

        if(!result.second) {
            result.first->second.count += 1;
        }

        return true;
    }

    SiteNetInfo * unbindWire(const SiteWire &wire) {
        auto iter = wire_to_nets.find(wire);
        NPNR_ASSERT(iter != wire_to_nets.end());
        NPNR_ASSERT(iter->second.count >= 1);
        SiteNetInfo *net = iter->second.net;
        iter->second.count -= 1;

        if(iter->second.count == 0) {
            wire_to_nets.erase(iter);
        }

        return net;
    }

    bool bindPip(const SitePip &pip, SiteNetInfo *net) {
        SiteWire src = getPipSrcWire(pip);
        SiteWire dst = getPipDstWire(pip);

        if(!bindWire(src, net)) {
            return false;
        }
        if(!bindWire(dst, net)) {
            unbindWire(src);
            return false;
        }

        auto result = net->wires.emplace(dst, SitePipMap{pip, 1});
        if(!result.second) {
            if(result.first->second.pip != pip) {
                // Pip conflict!
                if(ctx->debug) {
                    log_info("Pip conflict binding pip %s to wire %s, conflicts with pip %s\n",
                            nameOfPip(pip), nameOfWire(dst), nameOfPip(result.first->second.pip));
                }

                unbindWire(src);
                unbindWire(dst);
                return false;
            }

            result.first->second.count += 1;
        }

        return true;
    }

    void unbindPip(const SitePip &pip) {
        SiteWire src = getPipSrcWire(pip);
        SiteWire dst = getPipDstWire(pip);

        SiteNetInfo *src_net = unbindWire(src);
        SiteNetInfo *dst_net = unbindWire(dst);
        NPNR_ASSERT(src_net == dst_net);
        auto iter = dst_net->wires.find(dst);
        NPNR_ASSERT(iter != dst_net->wires.end());
        NPNR_ASSERT(iter->second.count >= 1);
        iter->second.count -= 1;

        if(iter->second.count == 0) {
            dst_net->wires.erase(iter);
        }
    }

    void archcheck();
};

struct SitePipDownhillIterator {
    enum DownhillIteratorState {
        // Initial state
        BEGIN = 0,
        // Iterating over normal pips.
        NORMAL_PIPS = 1,
        // Iterating off all site port sources.
        PORT_SINK_TO_PORT_SRC = 2,
        // Iterating over out of site sinks.
        OUT_OF_SITE_SINKS = 3,
        // Iterating off all site port sources.
        OUT_OF_SITE_SOURCE_TO_PORT_SRC = 4,
        SITE_PORT = 5,
        END = 6,
        NUMBER_STATES = 7,
    };

    DownhillIteratorState state = BEGIN;
    const SiteArch *site_arch;
    SiteWire site_wire;
    size_t cursor;
    DownhillPipIterator iter;
    DownhillPipIterator downhill_end;

    bool advance_in_state() {
        switch(state) {
        case BEGIN:
            return false;
        case NORMAL_PIPS:
            while(iter != downhill_end) {
                ++iter;
                if(!(iter != downhill_end)) {
                    break;
                }

                return true;
            }

            return false;
        case PORT_SINK_TO_PORT_SRC:
            ++cursor;
            return (cursor < site_arch->input_site_ports.size());
        case OUT_OF_SITE_SINKS:
            ++cursor;
            return (cursor < site_arch->out_of_site_sinks.size());
        case OUT_OF_SITE_SOURCE_TO_PORT_SRC:
            ++cursor;
            return (cursor < site_arch->input_site_ports.size());
        case SITE_PORT:
            ++cursor;
            return false;
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    bool check_first() const {
        switch(state) {
        case BEGIN:
            return false;
        case NORMAL_PIPS:
            if(!(iter != downhill_end)) {
                return false;
            } else {
                return true;
            }
        case PORT_SINK_TO_PORT_SRC:
            return (cursor < site_arch->input_site_ports.size());
        case OUT_OF_SITE_SINKS:
            return (cursor < site_arch->out_of_site_sinks.size());
        case OUT_OF_SITE_SOURCE_TO_PORT_SRC:
            return (cursor < site_arch->input_site_ports.size());
        case SITE_PORT:
            return true;
        case END:
            return true;
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    const std::array<std::array<DownhillIteratorState, NUMBER_STATES>, SiteWire::NUMBER_SITE_WIRE_TYPES> get_state_table() const {
        std::array<std::array<DownhillIteratorState, NUMBER_STATES>, SiteWire::NUMBER_SITE_WIRE_TYPES> state_table;
        for(size_t j = 0; j < SiteWire::NUMBER_SITE_WIRE_TYPES; ++j) {
            for(size_t i = 0; i < NUMBER_STATES; ++i) {
                state_table[j][i] = NUMBER_STATES;
            }
        }

        state_table[SiteWire::SITE_WIRE][BEGIN] = NORMAL_PIPS;
        state_table[SiteWire::SITE_WIRE][NORMAL_PIPS] = END;

        state_table[SiteWire::OUT_OF_SITE_SOURCE][BEGIN] = OUT_OF_SITE_SOURCE_TO_PORT_SRC;
        state_table[SiteWire::OUT_OF_SITE_SOURCE][OUT_OF_SITE_SOURCE_TO_PORT_SRC] = END;

        state_table[SiteWire::OUT_OF_SITE_SINK][BEGIN] = END;

        state_table[SiteWire::SITE_PORT_SINK][BEGIN] = PORT_SINK_TO_PORT_SRC;
        state_table[SiteWire::SITE_PORT_SINK][PORT_SINK_TO_PORT_SRC] = OUT_OF_SITE_SINKS;
        state_table[SiteWire::SITE_PORT_SINK][OUT_OF_SITE_SINKS] = END;

        state_table[SiteWire::SITE_PORT_SOURCE][BEGIN] = SITE_PORT;
        state_table[SiteWire::SITE_PORT_SOURCE][SITE_PORT] = END;

        return state_table;
    }

    void advance_state() {
        state = get_state_table().at(site_wire.type).at(state);
        cursor = 0;
        NPNR_ASSERT(state >= BEGIN && state <= END);
    }

    void operator++()
    {
        NPNR_ASSERT(state != END);
        while(state != END) {
            if(advance_in_state()) {
                break;
            } else {
                advance_state();
                if(check_first()) {
                    break;
                }
            }
        }
    }

    bool operator !=(const SitePipDownhillIterator &other) const {
        return state != other.state || cursor != other.cursor || iter != other.iter;
    }

    SitePip operator*() const
    {
        switch(state) {
        case NORMAL_PIPS:
            return SitePip::make(site_arch->site_info, *iter);
        case PORT_SINK_TO_PORT_SRC:
            return SitePip::make(site_arch->site_info, site_wire.pip, site_arch->input_site_ports.at(cursor));
        case OUT_OF_SITE_SINKS:
            return SitePip::make(site_arch->site_info, site_wire.pip, site_arch->out_of_site_sinks.at(cursor));
        case OUT_OF_SITE_SOURCE_TO_PORT_SRC:
            return SitePip::make(site_arch->site_info, site_wire, site_arch->input_site_ports.at(cursor));
        case SITE_PORT:
            return SitePip::make(site_arch->site_info, site_wire.pip);
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }
};

struct SitePipDownhillRange {
    const SiteArch *site_arch;
    SiteWire site_wire;
    DownhillPipRange pip_range;

    SitePipDownhillRange(const SiteArch *site_arch, SiteWire site_wire) : site_arch(site_arch), site_wire(site_wire) {
        switch(site_wire.type) {
        case SiteWire::SITE_WIRE:
            pip_range = site_arch->ctx->getPipsDownhill(site_wire.wire);
            break;
        case SiteWire::OUT_OF_SITE_SOURCE:
            // No normal pips!
            break;
        case SiteWire::OUT_OF_SITE_SINK:
            // No normal pips!
            break;
        case SiteWire::SITE_PORT_SINK:
            // No normal pips!
            break;
        case SiteWire::SITE_PORT_SOURCE:
            // No normal pips!
            break;
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    SitePipDownhillIterator begin() const {
        SitePipDownhillIterator b;
        b.state = SitePipDownhillIterator::BEGIN;
        b.site_arch = site_arch;
        b.site_wire = site_wire;
        b.cursor = 0;
        b.iter = pip_range.b;
        b.downhill_end = pip_range.e;

        ++b;

        return b;
    }

    SitePipDownhillIterator end() const {
        SitePipDownhillIterator e;
        e.state = SitePipDownhillIterator::END;
        e.site_arch = site_arch;
        e.site_wire = site_wire;
        e.cursor = 0;
        e.iter = pip_range.e;
        e.downhill_end = pip_range.e;

        return e;
    }
};

struct SitePipUphillIterator {
    enum UphillIteratorState {
        // Initial state
        BEGIN = 0,
        // Iterating over normal pips.
        NORMAL_PIPS = 1,
        // Iterating off all site port sources.
        PORT_SRC_TO_PORT_SINK = 2,
        // Iterating over out of site sinks.
        OUT_OF_SITE_SOURCES = 3,
        // Iterating off all site port sources.
        OUT_OF_SITE_SINK_TO_PORT_SINK = 4,
        SITE_PORT = 5,
        END = 6,
        NUMBER_STATES = 7,
    };

    UphillIteratorState state = BEGIN;
    const SiteArch *site_arch;
    SiteWire site_wire;
    size_t cursor;
    UphillPipIterator iter;
    UphillPipIterator uphill_end;

    bool advance_in_state() {
        switch(state) {
        case BEGIN:
            return false;
        case NORMAL_PIPS:
            while(iter != uphill_end) {
                ++iter;
                if(!(iter != uphill_end)) {
                    break;
                }
            }

            return false;
        case PORT_SRC_TO_PORT_SINK:
            ++cursor;
            return (cursor < site_arch->output_site_ports.size());
        case OUT_OF_SITE_SOURCES:
            ++cursor;
            return (cursor < site_arch->out_of_site_sources.size());
        case OUT_OF_SITE_SINK_TO_PORT_SINK:
            ++cursor;
            return (cursor < site_arch->output_site_ports.size());
        case SITE_PORT:
            ++cursor;
            return false;
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    bool check_first() const {
        switch(state) {
        case BEGIN:
            return false;
        case NORMAL_PIPS:
            if(!(iter != uphill_end)) {
                return false;
            } else {
                return true;
            }
        case PORT_SRC_TO_PORT_SINK:
            return (cursor < site_arch->output_site_ports.size());
        case OUT_OF_SITE_SOURCES:
            return (cursor < site_arch->out_of_site_sources.size());
        case OUT_OF_SITE_SINK_TO_PORT_SINK:
            return (cursor < site_arch->output_site_ports.size());
        case SITE_PORT:
            return true;
        case END:
            return true;
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    const std::array<std::array<UphillIteratorState, NUMBER_STATES>, SiteWire::NUMBER_SITE_WIRE_TYPES> get_state_table() const {
        std::array<std::array<UphillIteratorState, NUMBER_STATES>, SiteWire::NUMBER_SITE_WIRE_TYPES> state_table;
        for(size_t j = 0; j < SiteWire::NUMBER_SITE_WIRE_TYPES; ++j) {
            for(size_t i = 0; i < NUMBER_STATES; ++i) {
                state_table[j][i] = NUMBER_STATES;
            }
        }

        state_table[SiteWire::SITE_WIRE][BEGIN] = NORMAL_PIPS;
        state_table[SiteWire::SITE_WIRE][NORMAL_PIPS] = END;

        state_table[SiteWire::OUT_OF_SITE_SOURCE][BEGIN] = END;

        state_table[SiteWire::OUT_OF_SITE_SINK][BEGIN] = OUT_OF_SITE_SINK_TO_PORT_SINK;
        state_table[SiteWire::OUT_OF_SITE_SINK][OUT_OF_SITE_SINK_TO_PORT_SINK] = END;

        state_table[SiteWire::SITE_PORT_SINK][BEGIN] = SITE_PORT;
        state_table[SiteWire::SITE_PORT_SINK][SITE_PORT] = END;

        state_table[SiteWire::SITE_PORT_SOURCE][BEGIN] = PORT_SRC_TO_PORT_SINK;
        state_table[SiteWire::SITE_PORT_SOURCE][PORT_SRC_TO_PORT_SINK] = OUT_OF_SITE_SOURCES;
        state_table[SiteWire::SITE_PORT_SOURCE][OUT_OF_SITE_SOURCES] = END;

        return state_table;
    }

    void advance_state() {
        state = get_state_table().at(site_wire.type).at(state);
        cursor = 0;
        NPNR_ASSERT(state >= BEGIN && state <= END);
    }

    void operator++()
    {
        NPNR_ASSERT(state != END);
        while(state != END) {
            if(advance_in_state()) {
                break;
            } else {
                advance_state();
                if(check_first()) {
                    break;
                }
            }
        }
    }

    bool operator !=(const SitePipUphillIterator &other) const {
        return state != other.state || cursor != other.cursor || iter != other.iter;
    }

    SitePip operator*() const
    {
        switch(state) {
        case NORMAL_PIPS:
            return SitePip::make(site_arch->site_info, *iter);
        case PORT_SRC_TO_PORT_SINK:
            return SitePip::make(site_arch->site_info, site_arch->output_site_ports.at(cursor), site_wire.pip);
        case OUT_OF_SITE_SOURCES:
            return SitePip::make(site_arch->site_info, site_arch->out_of_site_sources.at(cursor), site_wire.pip);
        case OUT_OF_SITE_SINK_TO_PORT_SINK:
            return SitePip::make(site_arch->site_info, site_arch->output_site_ports.at(cursor), site_wire);
        case SITE_PORT:
            return SitePip::make(site_arch->site_info, site_wire.pip);
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }
};

struct SitePipUphillRange {
    const SiteArch *site_arch;
    SiteWire site_wire;
    UphillPipRange pip_range;

    SitePipUphillRange(const SiteArch *site_arch, SiteWire site_wire) : site_arch(site_arch), site_wire(site_wire) {
        switch(site_wire.type) {
        case SiteWire::SITE_WIRE:
            pip_range = site_arch->ctx->getPipsUphill(site_wire.wire);
            break;
        case SiteWire::OUT_OF_SITE_SOURCE:
            // No normal pips!
            break;
        case SiteWire::OUT_OF_SITE_SINK:
            // No normal pips!
            break;
        case SiteWire::SITE_PORT_SINK:
            // No normal pips!
            break;
        case SiteWire::SITE_PORT_SOURCE:
            // No normal pips!
            break;
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    SitePipUphillIterator begin() const {
        SitePipUphillIterator b;
        b.state = SitePipUphillIterator::BEGIN;
        b.site_arch = site_arch;
        b.site_wire = site_wire;
        b.cursor = 0;
        b.iter = pip_range.b;
        b.uphill_end = pip_range.e;

        ++b;

        return b;
    }

    SitePipUphillIterator end() const {
        SitePipUphillIterator e;
        e.state = SitePipUphillIterator::END;
        e.site_arch = site_arch;
        e.site_wire = site_wire;
        e.cursor = 0;
        e.iter = pip_range.e;
        e.uphill_end = pip_range.e;

        return e;
    }
};

inline SitePipDownhillRange SiteArch::getPipsDownhill(const SiteWire &site_wire) const {
    return SitePipDownhillRange(this, site_wire);
}

inline SitePipUphillRange SiteArch::getPipsUphill(const SiteWire &site_wire) const {
    return SitePipUphillRange(this, site_wire);
}

struct SiteWireIterator {
    enum SiteWireIteratorState {
        // Initial state
        BEGIN = 0,
        NORMAL_WIRES = 1,
        INPUT_SITE_PORTS = 2,
        OUTPUT_SITE_PORTS = 3,
        OUT_OF_SITE_SOURCES = 4,
        OUT_OF_SITE_SINKS = 5,
        END = 6,
    };

    SiteWireIteratorState state = BEGIN;
    const SiteArch *site_arch;
    const TileTypeInfoPOD *tile_type;
    size_t cursor = 0;

    bool advance_in_state() {
        switch(state) {
        case BEGIN:
            return false;
        case NORMAL_WIRES:
            while(true) {
                ++cursor;
                if(cursor >= tile_type->wire_data.size()) {
                    return false;
                }
                if(tile_type->wire_data[cursor].site == site_arch->site_info->site) {
                    return true;
                }
            }
        case INPUT_SITE_PORTS:
            ++cursor;
            return (cursor < site_arch->input_site_ports.size());
        case OUTPUT_SITE_PORTS:
            ++cursor;
            return (cursor < site_arch->output_site_ports.size());
        case OUT_OF_SITE_SOURCES:
            ++cursor;
            return (cursor < site_arch->out_of_site_sources.size());
        case OUT_OF_SITE_SINKS:
            ++cursor;
            return (cursor < site_arch->out_of_site_sinks.size());
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    // See if initial value in state is good.
    bool check_first() const {
        switch(state) {
        case BEGIN:
            return false;
        case NORMAL_WIRES:
            if(cursor >= tile_type->wire_data.size()) {
                return false;
            }
            return tile_type->wire_data[cursor].site == site_arch->site_info->site;
        case INPUT_SITE_PORTS:
            return (cursor < site_arch->input_site_ports.size());
        case OUTPUT_SITE_PORTS:
            return (cursor < site_arch->output_site_ports.size());
        case OUT_OF_SITE_SOURCES:
            return (cursor < site_arch->out_of_site_sources.size());
        case OUT_OF_SITE_SINKS:
            return (cursor < site_arch->out_of_site_sinks.size());
        case END:
            return true;
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }

    void advance_state() {
        NPNR_ASSERT(state >= BEGIN && state < END);
        state = static_cast<SiteWireIteratorState>(state+1);
        cursor = 0;
        NPNR_ASSERT(state >= BEGIN && state <= END);
    }

    void operator++()
    {
        NPNR_ASSERT(state != END);
        while(state != END) {
            if(advance_in_state()) {
                break;
            } else {
                advance_state();
                if(check_first()) {
                    break;
                }
            }
        }
    }

    bool operator !=(const SiteWireIterator &other) const {
        return state != other.state || cursor != other.cursor;
    }

    SiteWire operator*() const
    {
        WireId wire;
        PipId pip;
        SiteWire site_wire;
        switch(state) {
        case NORMAL_WIRES:
            wire.tile = site_arch->site_info->tile;
            wire.index = cursor;
            return SiteWire::make(site_arch->site_info, wire);
        case INPUT_SITE_PORTS:
            pip = site_arch->input_site_ports.at(cursor);
            site_wire = SiteWire::make_site_port(site_arch->site_info, pip, site_arch->ctx->getPipSrcWire(pip));
            NPNR_ASSERT(site_wire.type == SiteWire::SITE_PORT_SOURCE);
            return site_wire;
        case OUTPUT_SITE_PORTS:
            pip = site_arch->output_site_ports.at(cursor);
            site_wire = SiteWire::make_site_port(site_arch->site_info, pip, site_arch->ctx->getPipDstWire(pip));
            NPNR_ASSERT(site_wire.type == SiteWire::SITE_PORT_SINK);
            return site_wire;
        case OUT_OF_SITE_SOURCES:
            return site_arch->out_of_site_sources.at(cursor);
        case OUT_OF_SITE_SINKS:
            return site_arch->out_of_site_sinks.at(cursor);
        default:
            // Unreachable!
            NPNR_ASSERT(false);
        }
    }
};

struct SiteWireRange {
    const SiteArch *site_arch;
    SiteWireRange(const SiteArch *site_arch) : site_arch(site_arch) {}
    SiteWireIterator begin() const {
        SiteWireIterator b;

        b.state = SiteWireIterator::BEGIN;
        b.site_arch = site_arch;
        b.tile_type = &loc_info(site_arch->ctx->chip_info, *site_arch->site_info);

        ++b;
        return b;
    }

    SiteWireIterator end() const {
        SiteWireIterator e;

        e.state = SiteWireIterator::END;

        return e;
    }
};

inline SiteWireRange SiteArch::getWires() const {
    return SiteWireRange(this);
}


NEXTPNR_NAMESPACE_END

#endif /* SITE_ARCH_H */
