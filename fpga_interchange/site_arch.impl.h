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

#ifndef SITE_ARCH_IMPL_H
#define SITE_ARCH_IMPL_H

#include "context.h"
#include "site_arch.h"

NEXTPNR_NAMESPACE_BEGIN

inline const ChipInfoPOD &SiteInformation::chip_info() const { return *ctx->chip_info; }

inline bool SiteInformation::is_wire_in_site(WireId wire) const
{
    if (wire.tile != tile) {
        return false;
    }

    return ctx->wire_info(wire).site == site;
}

inline bool SiteInformation::is_bel_in_site(BelId bel) const
{
    if (bel.tile != tile) {
        return false;
    }

    return bel_info(ctx->chip_info, bel).site == site;
}

inline bool SiteInformation::is_pip_part_of_site(PipId pip) const
{
    if (pip.tile != tile) {
        return false;
    }

    const auto &tile_type_data = ctx->chip_info->tile_types[tile_type];
    const auto &pip_data = tile_type_data.pip_data[pip.index];
    return pip_data.site == site;
}

inline bool SiteInformation::is_site_port(PipId pip) const
{
    const auto &tile_type_data = ctx->chip_info->tile_types[tile_type];
    const auto &pip_data = tile_type_data.pip_data[pip.index];
    if (pip_data.site == -1) {
        return false;
    }
    auto &bel_data = tile_type_data.bel_data[pip_data.bel];
    return bel_data.category == BEL_CATEGORY_SITE_PORT;
}

inline SiteWire SiteWire::make(const SiteInformation *site_info, WireId site_wire)
{
    NPNR_ASSERT(site_info->is_wire_in_site(site_wire));
    SiteWire out;
    out.type = SITE_WIRE;
    out.wire = site_wire;
    return out;
}

inline SiteWire SiteWire::make_site_port(const SiteInformation *site_info, PipId pip, bool dst_wire)
{
    const auto &tile_type_data = site_info->chip_info().tile_types[site_info->tile_type];
    const auto &pip_data = tile_type_data.pip_data[pip.index];

    // This pip should definitely be part of this site
    NPNR_ASSERT(pip_data.site == site_info->site);

    SiteWire out;

    const auto &src_data = tile_type_data.wire_data[pip_data.src_index];
    const auto &dst_data = tile_type_data.wire_data[pip_data.dst_index];

    if (dst_wire) {
        if (src_data.site == site_info->site) {
            NPNR_ASSERT(dst_data.site == -1);
            out.type = SITE_PORT_SINK;
            out.pip = pip;
            out.wire = canonical_wire(&site_info->chip_info(), pip.tile, pip_data.dst_index);
        } else {
            NPNR_ASSERT(src_data.site == -1);
            NPNR_ASSERT(dst_data.site == site_info->site);
            out.type = SITE_WIRE;
            out.wire.tile = pip.tile;
            out.wire.index = pip_data.dst_index;
        }
    } else {
        if (src_data.site == site_info->site) {
            NPNR_ASSERT(dst_data.site == -1);
            out.type = SITE_WIRE;
            out.wire.tile = pip.tile;
            out.wire.index = pip_data.src_index;
        } else {
            NPNR_ASSERT(src_data.site == -1);
            NPNR_ASSERT(dst_data.site == site_info->site);
            out.type = SITE_PORT_SOURCE;
            out.pip = pip;
            out.wire = canonical_wire(&site_info->chip_info(), pip.tile, pip_data.src_index);
        }
    }

    return out;
}

inline SitePip SitePip::make(const SiteInformation *site_info, PipId pip)
{
    SitePip out;
    out.pip = pip;

    if (site_info->is_site_port(pip)) {
        out.type = SITE_PORT;
    } else {
        out.type = SITE_PIP;
    }
    return out;
}

inline SiteWire SiteArch::getPipSrcWire(const SitePip &site_pip) const
{
    SiteWire site_wire;
    switch (site_pip.type) {
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

inline SiteWire SiteArch::getPipDstWire(const SitePip &site_pip) const
{
    switch (site_pip.type) {
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

inline bool SiteArch::is_pip_synthetic(const SitePip &pip) const
{
    if (pip.type != SitePip::SITE_PORT) {
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

inline SyntheticType SiteArch::pip_synthetic_type(const SitePip &pip) const
{
    if (pip.type != SitePip::SITE_PORT) {
        // This isn't a site port, so its valid!
        return NOT_SYNTH;
    }

    auto &tile_type = ctx->chip_info->tile_types[site_info->tile_type];
    auto &pip_data = tile_type.pip_data[pip.pip.index];
    NPNR_ASSERT(pip_data.site != -1);
    auto &bel_data = tile_type.bel_data[pip_data.bel];
    return SyntheticType(bel_data.synthetic);
}

inline SitePip SitePipDownhillIterator::operator*() const
{
    switch (state) {
    case NORMAL_PIPS: {
        PipId pip;
        pip.tile = site_arch->site_info->tile;
        pip.index = (*pips_downhill)[cursor];
        return SitePip::make(site_arch->site_info, pip);
    }
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

inline const RelSlice<int32_t> *SitePipDownhillRange::init_pip_range() const
{
    NPNR_ASSERT(site_wire.type == SiteWire::SITE_WIRE);
    NPNR_ASSERT(site_wire.wire.tile == site_arch->site_info->tile);
    return &site_arch->ctx->chip_info->tile_types[site_arch->site_info->tile_type]
                    .wire_data[site_wire.wire.index]
                    .pips_downhill;
}

inline SitePipDownhillIterator SitePipDownhillRange::begin() const
{
    SitePipDownhillIterator b;
    b.state = SitePipDownhillIterator::BEGIN;
    b.site_arch = site_arch;
    b.site_wire = site_wire;
    b.cursor = 0;
    if (site_wire.type == SiteWire::SITE_WIRE) {
        b.pips_downhill = init_pip_range();
    }

    ++b;

    return b;
}

inline bool SiteArch::isInverting(const SitePip &site_pip) const
{
    if (site_pip.type != SitePip::SITE_PIP) {
        return false;
    }

    auto &tile_type = ctx->chip_info->tile_types[site_info->tile_type];
    auto &pip_data = tile_type.pip_data[site_pip.pip.index];
    NPNR_ASSERT(pip_data.site != -1);
    auto &bel_data = tile_type.bel_data[pip_data.bel];

    // Is a fixed inverter if the non_inverting_pin is another pin.
    return bel_data.non_inverting_pin != pip_data.extra_data && bel_data.inverting_pin == pip_data.extra_data;
}

inline bool SiteArch::canInvert(const SitePip &site_pip) const
{
    if (site_pip.type != SitePip::SITE_PIP) {
        return false;
    }

    auto &tile_type = ctx->chip_info->tile_types[site_info->tile_type];
    auto &pip_data = tile_type.pip_data[site_pip.pip.index];
    NPNR_ASSERT(pip_data.site != -1);
    auto &bel_data = tile_type.bel_data[pip_data.bel];

    // Can optionally invert if this pip is both the non_inverting_pin and
    // inverting pin.
    return bel_data.non_inverting_pin == pip_data.extra_data && bel_data.inverting_pin == pip_data.extra_data;
}

inline PhysicalNetlist::PhysNetlist::NetType SiteArch::prefered_constant_net_type(const SitePip &site_pip) const
{
    // FIXME: Implement site port overrides from chipdb once available.
    IdString prefered_constant_net(ctx->chip_info->constants->best_constant_net);
    IdString gnd_net_name(ctx->chip_info->constants->gnd_net_name);
    IdString vcc_net_name(ctx->chip_info->constants->vcc_net_name);

    if (prefered_constant_net == IdString()) {
        return PhysicalNetlist::PhysNetlist::NetType::SIGNAL;
    } else if (prefered_constant_net == gnd_net_name) {
        return PhysicalNetlist::PhysNetlist::NetType::GND;
    } else if (prefered_constant_net == vcc_net_name) {
        return PhysicalNetlist::PhysNetlist::NetType::VCC;
    } else {
        log_error("prefered_constant_net %s is not the GND (%s) or VCC(%s) net?\n", prefered_constant_net.c_str(ctx),
                  gnd_net_name.c_str(ctx), vcc_net_name.c_str(ctx));
    }
}

NEXTPNR_NAMESPACE_END

#endif /* SITE_ARCH_H */
