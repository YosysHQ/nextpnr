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
#include "site_arch.impl.h"

NEXTPNR_NAMESPACE_BEGIN

SiteInformation::SiteInformation(const Context *ctx, int32_t tile, int32_t site,
                                 const std::unordered_set<CellInfo *> &cells_in_site)
        : ctx(ctx), tile(tile), tile_type(ctx->chip_info->tiles[tile].type), site(site), cells_in_site(cells_in_site)
{
}

bool SiteArch::bindPip(const SitePip &pip, SiteNetInfo *net)
{
    SiteWire src = getPipSrcWire(pip);
    SiteWire dst = getPipDstWire(pip);

    if (!bindWire(src, net)) {
        return false;
    }
    if (!bindWire(dst, net)) {
        unbindWire(src);
        return false;
    }

    auto result = net->wires.emplace(dst, SitePipMap{pip, 1});
    if (!result.second) {
        if (result.first->second.pip != pip) {
            // Pip conflict!
            if (debug()) {
                log_info("Pip conflict binding pip %s to wire %s, conflicts with pip %s\n", nameOfPip(pip),
                         nameOfWire(dst), nameOfPip(result.first->second.pip));
            }

            unbindWire(src);
            unbindWire(dst);
            return false;
        }

        result.first->second.count += 1;
    }

    if (debug()) {
        log_info("Bound pip %s to wire %s\n", nameOfPip(pip), nameOfWire(dst));
    }

    return true;
}

void SiteArch::unbindPip(const SitePip &pip)
{
    SiteWire src = getPipSrcWire(pip);
    SiteWire dst = getPipDstWire(pip);

    if (debug()) {
        log_info("Unbinding pip %s from wire %s\n", nameOfPip(pip), nameOfWire(dst));
    }

    SiteNetInfo *src_net = unbindWire(src);
    SiteNetInfo *dst_net = unbindWire(dst);
    NPNR_ASSERT(src_net == dst_net);
    auto iter = dst_net->wires.find(dst);
    NPNR_ASSERT(iter != dst_net->wires.end());
    NPNR_ASSERT(iter->second.count >= 1);
    iter->second.count -= 1;

    if (iter->second.count == 0) {
        dst_net->wires.erase(iter);
    }
}

void SiteArch::archcheck()
{
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

SiteArch::SiteArch(const SiteInformation *site_info) : ctx(site_info->ctx), site_info(site_info)
{
    // Build list of input and output site ports
    //
    // FIXME: This doesn't need to be computed over and over, move to
    // arch/chip db.
    const TileTypeInfoPOD &tile_type = loc_info(&site_info->chip_info(), *site_info);
    PipId pip;
    pip.tile = site_info->tile;
    for (size_t pip_index = 0; pip_index < tile_type.pip_data.size(); ++pip_index) {
        if (tile_type.pip_data[pip_index].site != site_info->site) {
            continue;
        }

        pip.index = pip_index;

        if (!site_info->is_site_port(pip)) {
            continue;
        }

        WireId src_wire = ctx->getPipSrcWire(pip);
        if (site_info->is_wire_in_site(src_wire)) {
            output_site_ports.push_back(pip);
        } else {
            input_site_ports.push_back(pip);
        }
    }

    // Create list of out of site sources and sinks.

    bool have_vcc_pins = false;
    for (CellInfo *cell : site_info->cells_in_site) {
        for (const auto &pin_pair : cell->cell_bel_pins) {
            const PortInfo &port = cell->ports.at(pin_pair.first);
            if (port.net != nullptr) {
                nets.emplace(port.net, SiteNetInfo{port.net});
            }
        }

        if (!cell->lut_cell.vcc_pins.empty()) {
            have_vcc_pins = true;
        }
    }

    for (auto &net_pair : nets) {
        NetInfo *net = net_pair.first;
        SiteNetInfo &net_info = net_pair.second;

        // All nets require drivers
        NPNR_ASSERT(net->driver.cell != nullptr);

        bool net_driven_out_of_site = false;
        if (net->driver.cell->bel == BelId()) {
            // The driver of this site hasn't been placed, so treat it as
            // out of site.
            out_of_site_sources.push_back(SiteWire::make(site_info, PORT_OUT, net));
            net_info.driver = out_of_site_sources.back();
            net_driven_out_of_site = true;
        } else {
            if (!site_info->is_bel_in_site(net->driver.cell->bel)) {

                // The driver of this site has been placed, it is an out
                // of site source.
                out_of_site_sources.push_back(SiteWire::make(site_info, PORT_OUT, net));
                // out_of_site_sources.back().wire = ctx->getNetinfoSourceWire(net);
                net_info.driver = out_of_site_sources.back();

                net_driven_out_of_site = true;
            } else {
                net_info.driver = SiteWire::make(site_info, ctx->getNetinfoSourceWire(net));
            }
        }

        if (net_driven_out_of_site) {
            // Because this net is driven from a source out of the site,
            // no out of site sink is required.
            continue;
        }

        // Examine net to determine if it has any users not in this site.
        bool net_used_out_of_site = false;
        for (const PortRef &user : net->users) {
            NPNR_ASSERT(user.cell != nullptr);

            if (user.cell->bel == BelId()) {
                // Because this net has a user that has not been placed,
                // and this net is being driven from this site, make sure
                // this net can be routed from this site.
                net_used_out_of_site = true;
                break;
            }

            if (!site_info->is_bel_in_site(user.cell->bel)) {
                net_used_out_of_site = true;
                break;
            }
        }

        if (net_used_out_of_site) {
            out_of_site_sinks.push_back(SiteWire::make(site_info, PORT_IN, net));
            net_info.users.emplace(out_of_site_sinks.back());
        }
    }

    // At this point all nets have a driver SiteWire, but user SiteWire's
    // within the site are not present.  Add them now.
    for (auto &net_pair : nets) {
        NetInfo *net = net_pair.first;
        SiteNetInfo &net_info = net_pair.second;

        for (const PortRef &user : net->users) {
            if (!site_info->is_bel_in_site(user.cell->bel)) {
                // Only care about BELs within the site at this point.
                continue;
            }

            for (IdString bel_pin : ctx->getBelPinsForCellPin(user.cell, user.port)) {
                SiteWire wire = getBelPinWire(user.cell->bel, bel_pin);
                // Don't add users that are trivially routable!
                if (wire != net_info.driver) {
#ifdef DEBUG_SITE_ARCH
                    if (ctx->debug) {
                        log_info("Add user %s because it isn't driver %s\n", nameOfWire(wire),
                                 nameOfWire(net_info.driver));
                    }
#endif
                    net_info.users.emplace(wire);
                }
            }
        }
    }

    IdString vcc_net_name(ctx->chip_info->constants->vcc_net_name);
    NetInfo *vcc_net = ctx->nets.at(vcc_net_name).get();
    auto iter = nets.find(vcc_net);
    if (iter == nets.end() && have_vcc_pins) {
        // VCC net isn't present, add it.
        SiteNetInfo net_info;
        net_info.net = vcc_net;
        net_info.driver.type = SiteWire::OUT_OF_SITE_SOURCE;
        net_info.driver.net = vcc_net;
        auto result = nets.emplace(vcc_net, net_info);
        NPNR_ASSERT(result.second);
        iter = result.first;
    }

    for (CellInfo *cell : site_info->cells_in_site) {
        for (IdString vcc_pin : cell->lut_cell.vcc_pins) {
            SiteWire wire = getBelPinWire(cell->bel, vcc_pin);
            iter->second.users.emplace(wire);
        }
    }

    for (auto &net_pair : nets) {
        SiteNetInfo *net_info = &net_pair.second;
        auto result = wire_to_nets.emplace(net_info->driver, SiteNetMap{net_info, 1});
        // By this point, trivial congestion at sources should already by
        // avoided, and there should be no duplicates in the
        // driver/users data.
        NPNR_ASSERT(result.second);

        for (const auto &user : net_info->users) {
            result = wire_to_nets.emplace(user, SiteNetMap{net_info, 1});
            NPNR_ASSERT(result.second);
        }
    }
}

SiteWire SiteArch::getBelPinWire(BelId bel, IdString pin) const
{
    WireId wire = ctx->getBelPinWire(bel, pin);
    return SiteWire::make(site_info, wire);
}

PortType SiteArch::getBelPinType(BelId bel, IdString pin) const { return ctx->getBelPinType(bel, pin); }

const char *SiteArch::nameOfWire(const SiteWire &wire) const
{
    switch (wire.type) {
    case SiteWire::SITE_WIRE:
        return ctx->nameOfWire(wire.wire);
    case SiteWire::SITE_PORT_SINK:
        return ctx->nameOfWire(wire.wire);
    case SiteWire::SITE_PORT_SOURCE:
        return ctx->nameOfWire(wire.wire);
    case SiteWire::OUT_OF_SITE_SOURCE: {
        std::string &str = ctx->log_strs.next();
        str = stringf("Out of site source for net %s", wire.net->name.c_str(ctx));
        return str.c_str();
    }
    case SiteWire::OUT_OF_SITE_SINK: {
        std::string &str = ctx->log_strs.next();
        str = stringf("Out of sink source for net %s", wire.net->name.c_str(ctx));
        return str.c_str();
    }
    default:
        // Unreachable!
        NPNR_ASSERT(false);
    }
}

const char *SiteArch::nameOfPip(const SitePip &pip) const
{
    switch (pip.type) {
    case SitePip::SITE_PIP:
        return ctx->nameOfPip(pip.pip);
    case SitePip::SITE_PORT:
        return ctx->nameOfPip(pip.pip);
    case SitePip::SOURCE_TO_SITE_PORT: {
        std::string &str = ctx->log_strs.next();
        str = stringf("Out of site source for net %s => %s", pip.wire.net->name.c_str(ctx),
                      ctx->nameOfWire(ctx->getPipSrcWire(pip.pip)));
        return str.c_str();
    }
    case SitePip::SITE_PORT_TO_SINK: {
        std::string &str = ctx->log_strs.next();
        str = stringf("%s => Out of site sink for net %s", ctx->nameOfWire(ctx->getPipDstWire(pip.pip)),
                      pip.wire.net->name.c_str(ctx));
        return str.c_str();
    }
    case SitePip::SITE_PORT_TO_SITE_PORT: {
        std::string &str = ctx->log_strs.next();
        str = stringf("%s => %s", ctx->nameOfWire(ctx->getPipSrcWire(pip.pip)),
                      ctx->nameOfWire(ctx->getPipDstWire(pip.other_pip)));
        return str.c_str();
    }
    default:
        // Unreachable!
        NPNR_ASSERT(false);
    }
}

const char *SiteArch::nameOfNet(const SiteNetInfo *net) const { return net->net->name.c_str(ctx); }

bool SiteArch::debug() const { return ctx->debug; }

SitePipUphillRange::SitePipUphillRange(const SiteArch *site_arch, SiteWire site_wire)
        : site_arch(site_arch), site_wire(site_wire)
{
    switch (site_wire.type) {
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

SitePip SitePipUphillIterator::operator*() const
{
    switch (state) {
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

SiteWire SiteWireIterator::operator*() const
{
    WireId wire;
    PipId pip;
    SiteWire site_wire;
    switch (state) {
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

SiteWireIterator SiteWireRange::begin() const
{
    SiteWireIterator b;

    b.state = SiteWireIterator::BEGIN;
    b.site_arch = site_arch;
    b.tile_type = &loc_info(&site_arch->site_info->chip_info(), *site_arch->site_info);

    ++b;
    return b;
}

NEXTPNR_NAMESPACE_END
