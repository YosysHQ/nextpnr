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

#include "site_arch.h"

#include "context.h"

NEXTPNR_NAMESPACE_BEGIN

SiteInformation::SiteInformation(const Context *ctx, int32_t tile, int32_t site, const std::unordered_set<CellInfo *> &cells_in_site) : ctx(ctx), tile(tile), tile_type(ctx->chip_info->tiles[tile].type), site(site), cells_in_site(cells_in_site) {}

bool SiteInformation::is_wire_in_site(WireId wire) const {
    if(wire.tile != tile) {
        return false;
    }

    return ctx->wire_info(wire).site == site;
}

const ChipInfoPOD & SiteInformation::chip_info() const {
    return *ctx->chip_info;
}

bool SiteInformation::is_bel_in_site(BelId bel) const {
    if(bel.tile != tile) {
        return false;
    }

    return bel_info(ctx->chip_info, bel).site == site;

}

bool SiteInformation::is_pip_part_of_site(PipId pip) const {
    if(pip.tile != tile) {
        return false;
    }

    const auto & tile_type_data = ctx->chip_info->tile_types[tile_type];
    const auto & pip_data = tile_type_data.pip_data[pip.index];
    return pip_data.site == site;
}

bool SiteInformation::is_site_port(PipId pip) const {
    const auto & tile_type_data = ctx->chip_info->tile_types[tile_type];
    const auto & pip_data = tile_type_data.pip_data[pip.index];
    if (pip_data.site == -1) {
        return false;
    }
    auto &bel_data = tile_type_data.bel_data[pip_data.bel];
    return bel_data.category == BEL_CATEGORY_SITE_PORT;
}

void SiteArch::archcheck() {
    for (SiteWire wire : getWires()) {
        for (SitePip pip : getPipsDownhill(wire)) {
            SiteWire wire2 = getPipSrcWire(pip);
            log_assert(wire == wire2);
        }

        for (SitePip pip : getPipsUphill(wire)) {
            SiteWire wire2 = getPipDstWire(pip);
            log_assert(wire == wire2);
        }
    }
}

SiteArch::SiteArch(const SiteInformation *site_info) : ctx(site_info->ctx), site_info(site_info) {
    // Build list of input and output site ports
    //
    // FIXME: This doesn't need to be computed over and over, move to
    // arch/chip db.
    const TileTypeInfoPOD & tile_type = loc_info(&site_info->chip_info(), *site_info);
    PipId pip;
    pip.tile = site_info->tile;
    for(size_t pip_index = 0; pip_index < tile_type.pip_data.size(); ++pip_index) {
        if(tile_type.pip_data[pip_index].site != site_info->site) {
            continue;
        }

        pip.index = pip_index;

        if(!site_info->is_site_port(pip)) {
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
                //out_of_site_sources.back().wire = ctx->getNetinfoSourceWire(net);
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
                break;
            }

            if(!site_info->is_bel_in_site(user.cell->bel)) {
                net_used_out_of_site = true;
                break;
            }
        }

        if(net_used_out_of_site) {
            out_of_site_sinks.push_back(
                    SiteWire::make(site_info, PORT_IN,
                        net));
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

SiteWire SiteArch::getPipSrcWire(const SitePip &site_pip) const {
    SiteWire site_wire;
    switch(site_pip.type) {
    case SitePip::Type::SITE_PIP:
        return SiteWire::make(site_info, ctx->getPipSrcWire(site_pip.pip));
    case SitePip::Type::SITE_PORT:
        return SiteWire::make_site_port(site_info, site_pip.pip, /*dst_wire=*/false);
    case SitePip::Type::SOURCE_TO_SITE_PORT:
        NPNR_ASSERT(site_pip.wire.type == SiteWire::OUT_OF_SITE_SOURCE);
        return site_pip.wire;
    case SitePip::Type::SITE_PORT_TO_SINK:
        site_wire = SiteWire::make_site_port(site_info, site_pip.pip, /*dst_wire=*/true);
        NPNR_ASSERT(site_wire.type == SiteWire::SITE_PORT_SINK);
        return site_wire;
    case SitePip::Type::SITE_PORT_TO_SITE_PORT:
        site_wire = SiteWire::make_site_port(site_info, site_pip.pip, /*dst_wire=*/true);
        NPNR_ASSERT(site_wire.type == SiteWire::SITE_PORT_SINK);
        return site_wire;
    default:
        // Unreachable!
        NPNR_ASSERT(false);
    }
}

SiteWire SiteArch::getPipDstWire(const SitePip &site_pip) const {
    switch(site_pip.type) {
    case SitePip::Type::SITE_PIP:
        return SiteWire::make(site_info, ctx->getPipDstWire(site_pip.pip));
    case SitePip::Type::SITE_PORT:
        return SiteWire::make_site_port(site_info, site_pip.pip, /*dst_wire=*/true);
    case SitePip::Type::SOURCE_TO_SITE_PORT: {
        SiteWire site_wire = SiteWire::make_site_port(site_info, site_pip.pip, /*dst_wire=*/false);
        NPNR_ASSERT(site_wire.type == SiteWire::SITE_PORT_SOURCE);
        return site_wire;
    }
    case SitePip::Type::SITE_PORT_TO_SINK:
        NPNR_ASSERT(site_pip.wire.type == SiteWire::OUT_OF_SITE_SINK);
        return site_pip.wire;
    case SitePip::Type::SITE_PORT_TO_SITE_PORT: {
        SiteWire site_wire = SiteWire::make_site_port(site_info, site_pip.other_pip, /*dst_wire=*/false);
        NPNR_ASSERT(site_wire.type == SiteWire::SITE_PORT_SOURCE);
        return site_wire;
    }
    default:
        // Unreachable!
        NPNR_ASSERT(false);
    }
}

SiteWire SiteArch::getBelPinWire(BelId bel, IdString pin) const {
    WireId wire = ctx->getBelPinWire(bel, pin);
    return SiteWire::make(site_info, wire);
}

PortType SiteArch::getBelPinType(BelId bel, IdString pin) const {
    return ctx->getBelPinType(bel, pin);
}

const char * SiteArch::nameOfWire(const SiteWire &wire) const {
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

const char * SiteArch::nameOfPip(const SitePip &pip) const {
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

const char * SiteArch::nameOfNet(const SiteNetInfo *net) const {
    return net->net->name.c_str(ctx);
}

bool SiteArch::debug() const {
    return ctx->debug;
}


const RelSlice<int32_t> * SitePipDownhillRange::init_pip_range() const {
    NPNR_ASSERT(site_wire.type == SiteWire::SITE_WIRE);
    NPNR_ASSERT(site_wire.wire.tile == site_arch->site_info->tile);
    return &site_arch->ctx->chip_info->tile_types[site_arch->site_info->tile_type].wire_data[site_wire.wire.index].pips_downhill;
}

SitePipUphillRange::SitePipUphillRange(const SiteArch *site_arch, SiteWire site_wire) : site_arch(site_arch), site_wire(site_wire) {
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

SiteWire SiteWireIterator::operator*() const
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
        site_wire = SiteWire::make_site_port(site_arch->site_info, pip, /*dst_wire=*/false);
        NPNR_ASSERT(site_wire.type == SiteWire::SITE_PORT_SOURCE);
        return site_wire;
    case OUTPUT_SITE_PORTS:
        pip = site_arch->output_site_ports.at(cursor);
        site_wire = SiteWire::make_site_port(site_arch->site_info, pip, /*dst_wire=*/true);
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

bool SiteArch::is_pip_synthetic(const SitePip &pip) const {
    if(pip.type != SitePip::SITE_PORT) {
        // This isn't a site port, so its valid!
        return false;
    }

    auto &tile_type = ctx->chip_info->tile_types[site_info->tile_type];
    auto &pip_data = tile_type.pip_data[pip.pip.index];
    if (pip_data.site == -1) {
        return pip_data.extra_data == -1;
    } else {
        auto &bel_data = tile_type.bel_data[pip_data.bel];
        return bel_data.synthetic != 0;
    }
}

NEXTPNR_NAMESPACE_END
