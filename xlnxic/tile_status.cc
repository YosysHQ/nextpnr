/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  gatecat <gatecat@ds0.me>
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

#include "tile_status.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

LogicSiteStatus::LogicSiteStatus(ArchFamily family) : family(family)
{
    bound.resize(family == ArchFamily::XC7 ? 64 : 128);
}

void LogicSiteStatus::update_bel(uint32_t place_idx, CellInfo *cell)
{
    LogicBelIdx bel(place_idx);
    // Always mark the eighth the bel is in as dirty
    eighth_status.at(bel.eighth()).dirty = true;
    switch (bel.bel()) {
    case LogicBelIdx::LUT5:
    case LogicBelIdx::LUT6:
        // If RAM or SRLs are involved, trigger a whole tile update
        if (cell->lutInfo.is_srl || cell->lutInfo.is_memory)
            tile_dirty = true;
        if (family == ArchFamily::XC7 && cell->lutInfo.is_srl && cell->lutInfo.out_casc && bel.eighth() == 0)
            eighth_status.at(3).dirty = true; // might be used for route-out of cascade
        // Write address MSBs
        if (cell->lutInfo.is_memory && cell->lutInfo.input_count == 6 &&
            bel.eighth() == (family == ArchFamily::XC7 ? 3 : 7))
            for (int i = 0; i < 8; i++)
                eighth_status.at(i).dirty = true;
        break;
    case LogicBelIdx::FF:
    case LogicBelIdx::FF2:
        // Flipflops update control sets
        tile_dirty = true;
        half_status.at(bel.eighth() / 4).dirty = true;
        break;
    case LogicBelIdx::F7MUX:
        // Also mark other side as dirty for muxes
        eighth_status.at(bel.eighth() + 1).dirty = true;
        break;
    case LogicBelIdx::F8MUX:
        eighth_status.at(bel.eighth() + 1).dirty = true;
        eighth_status.at(bel.eighth() + 2).dirty = true;
        break;
    case LogicBelIdx::F9MUX:
        eighth_status.at(3).dirty = true;
        eighth_status.at(4).dirty = true;
        break;
    case LogicBelIdx::CARRY:
        for (int i = 0; i < 8; i++)
            eighth_status.at(i).dirty = true;
        break;
    default:
        // TODO: Versal IMI/IMR/etc
        break;
    }
}

void LogicSiteStatus::bind_bel(uint32_t place_idx, CellInfo *cell)
{
    auto &entry = bound.at(place_idx);
    NPNR_ASSERT(entry == nullptr);
    entry = cell;
    update_bel(place_idx, cell);
}

void LogicSiteStatus::unbind_bel(uint32_t place_idx)
{
    auto &entry = bound.at(place_idx);
    NPNR_ASSERT(entry != nullptr);
    update_bel(place_idx, entry);
    entry = nullptr;
}

void LogicSiteStatus::update_lut_inputs()
{
    for (int i = 0; i < 8; i++) {
        auto &status = lut_status.at(i);
        status.is_fractured = false;
        status.net2input.clear();
        const CellInfo *lut6 = get_cell(i, LogicBelIdx::LUT6);
        const CellInfo *lut5 = get_cell(i, LogicBelIdx::LUT5);
        if (!lut5 || !lut6)
            continue; // only applies to fractured LUTs
        if (lut5->lutInfo.is_memory || lut5->lutInfo.is_srl)
            continue; // only applies to pure LUTs
        if (lut6->lutInfo.is_memory || lut6->lutInfo.is_srl)
            continue; // only applies to pure LUTs
        status.is_fractured = true;
        for (const CellInfo *cell : {lut5, lut6}) {
            for (auto &port : cell->ports) {
                if (port.second.type == PORT_OUT || (port.first == id__TIED_0 || port.first == id__TIED_1))
                    continue;
                const NetInfo *ni = port.second.net;
                if (!ni)
                    continue;
                if (status.net2input.count(ni->name))
                    continue;
                int input_idx = int(status.net2input.size());
                status.net2input[ni->name] = input_idx;
            }
        }
    }
}

TileStatus::TileStatus(Context *ctx, int tile_idx) : ctx(ctx), tile_idx(tile_idx)
{
    const auto &tile_data = chip_tile_info(ctx->chip_info, tile_idx);
    bound_cells.resize(tile_data.bels.size());
    sites.resize(tile_data.sites.size());
    for (int i = 0; i < tile_data.sites.ssize(); i++) {
        if (ctx->is_logic_site(tile_idx, i)) {
            sites.at(i).logic = std::make_unique<LogicSiteStatus>(ctx->family);
        }
    }
}

void TileStatus::bind_bel(BelId bel, CellInfo *cell, PlaceStrength strength)
{
    const auto &bel_data = chip_bel_info(ctx->chip_info, bel);
    auto &bel_entry = bound_cells.at(bel.index);
    NPNR_ASSERT(bel_entry == nullptr);
    bel_entry = cell;
    cell->bel = bel;
    cell->belStrength = strength;

    if (bel_data.site != -1) {
        auto &site_status = sites.at(bel_data.site);
        ++site_status.bound_count;
        NPNR_ASSERT(site_status.variant == -1 || site_status.variant == bel_data.site_variant);
        site_status.variant = bel_data.site_variant;
        if (site_status.logic)
            site_status.logic->bind_bel(bel_data.place_idx, cell);
    }
}

void TileStatus::unbind_bel(BelId bel)
{
    const auto &bel_data = chip_bel_info(ctx->chip_info, bel);
    auto &bel_entry = bound_cells.at(bel.index);
    NPNR_ASSERT(bel_entry != nullptr);
    bel_entry->bel = BelId();
    bel_entry->belStrength = STRENGTH_NONE;

    if (bel_data.site != -1) {
        auto &site_status = sites.at(bel_data.site);
        --site_status.bound_count;
        // No placed bels remaining at site means the variant constraint can be dropped
        if (site_status.bound_count == 0) {
            site_status.variant = -1;
        }
        if (site_status.logic)
            site_status.logic->unbind_bel(bel_data.place_idx);
    }

    bel_entry = nullptr;
}

bool TileStatus::is_bel_avail(BelId bel) const
{
    const auto &bel_data = chip_bel_info(ctx->chip_info, bel);
    const auto &bel_entry = bound_cells.at(bel.index);
    if (bel_entry != nullptr)
        return false; // bel is in use
    if (bel_data.site != -1) {
        auto &site_status = sites.at(bel_data.site);
        // Check that a different variant of the site isn't in use, we can't mix and match variants
        if (site_status.variant != -1 && site_status.variant != bel_data.site_variant)
            return false;
    }

    return true;
}

CellInfo *TileStatus::get_bound_bel_cell(BelId bel) const { return bound_cells.at(bel.index); }

const LUTStatus &TileStatus::get_lut_status(BelId bel) const
{
    const auto &bel_data = chip_bel_info(ctx->chip_info, bel);
    NPNR_ASSERT(bel_data.site != -1);
    LogicBelIdx bel_idx(bel_data.place_idx);
    NPNR_ASSERT(bel_idx.bel() == LogicBelIdx::LUT5 || bel_idx.bel() == LogicBelIdx::LUT6);
    return sites.at(bel_data.site).logic->lut_status.at(bel_idx.eighth());
}

NEXTPNR_NAMESPACE_END
