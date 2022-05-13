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

#include "site_lut_mapping_cache.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

// ============================================================================

SiteLutMappingKey SiteLutMappingKey::create(const SiteInformation &siteInfo)
{
    const Context *ctx = siteInfo.ctx;

    // Look for LUT cells in the site
    std::vector<CellInfo *> lutCells;
    lutCells.reserve(siteInfo.cells_in_site.size());

    for (CellInfo *cellInfo : siteInfo.cells_in_site) {

        // Not a LUT cell
        if (cellInfo->lut_cell.pins.empty()) {
            continue;
        }

        // Not bound to a LUT BEL
        BelId bel = cellInfo->bel;
        const auto &bel_data = bel_info(ctx->chip_info, bel);
        if (bel_data.lut_element == -1) {
            continue;
        }

        lutCells.push_back(cellInfo);
    }

    // Sort cells by BEL indices to maintain always the same order
    std::sort(lutCells.begin(), lutCells.end(),
              [](const CellInfo *a, const CellInfo *b) { return a->bel.index > b->bel.index; });

    // Initialize the key
    SiteLutMappingKey key;
    key.tileType = siteInfo.tile_type;
    key.siteType = ctx->chip_info->sites[siteInfo.site].site_type;
    key.numCells = 0;
    key.cells.resize(ctx->max_lut_cells);

    // Get bound nets. Store localized (to the LUT cluster) net indices only
    // to get always the same key for the same LUT port configuration even
    // when the actual global net names are different.
    dict<IdString, int32_t> netMap;
    for (CellInfo *cellInfo : lutCells) {

        NPNR_ASSERT(key.numCells < key.cells.size());
        auto &cell = key.cells[key.numCells++];

        cell.type = cellInfo->type;
        cell.belIndex = cellInfo->bel.index;

        cell.conns.resize(ctx->max_lut_pins, 0);

        size_t portId = 0;
        for (const auto &port : cellInfo->ports) {
            const auto &portInfo = port.second;

            // Consider only LUT inputs
            if (portInfo.type != PORT_IN) {
                continue;
            }

            // Assign net id if any
            int32_t netId = 0;
            if (portInfo.net != nullptr) {
                auto netInfo = portInfo.net;

                auto it = netMap.find(netInfo->name);
                if (it != netMap.end()) {
                    netId = it->second;
                } else {
                    netId = (int32_t)netMap.size() + 1;
                    netMap[netInfo->name] = netId;
                }
            }

            NPNR_ASSERT(portId < cell.conns.size());
            cell.conns[portId++] = netId;
        }
    }

    // Compute hash
    key.computeHash();

    return key;
}

// ============================================================================

bool SiteLutMappingResult::apply(const SiteInformation &siteInfo)
{

    Context *ctx = const_cast<Context *>(siteInfo.ctx);
    TileStatus &tileStatus = ctx->get_tile_status(siteInfo.tile);

    for (auto &cell : cells) {

        // Get the bound cell
        CellInfo *cellInfo = tileStatus.boundcells[cell.belIndex];
        NPNR_ASSERT(cellInfo);

        // Double check BEL binding
        NPNR_ASSERT(cellInfo->bel.tile == siteInfo.tile);
        NPNR_ASSERT(cellInfo->bel.index == cell.belIndex);

        // Cell <-> BEL pin map
        size_t numPins = cellInfo->lut_cell.pins.size();
        for (size_t pinIdx = 0; pinIdx < numPins; ++pinIdx) {
            const IdString &cellPin = cellInfo->lut_cell.pins[pinIdx];
            auto &belPins = cellInfo->cell_bel_pins[cellPin];

            // There is only one pin
            belPins.resize(1);
            belPins[0] = cell.belPins[cellPin];
        }

        // LUT data
        // FIXME: Is there any other info that is being updated than pin_connections ?
        cellInfo->lut_cell.pin_connections = std::move(cell.lutCell.pin_connections);
    }

    return true;
}

size_t SiteLutMappingResult::getSizeInBytes() const
{

    size_t size = 0;

    size += sizeof(SiteLutMappingResult);
    size += blockedWires.size() * sizeof(std::pair<IdString, IdString>);

    for (const auto &cell : cells) {
        size += sizeof(Cell);
        size += cell.belPins.size() * sizeof(decltype(cell.belPins)::value_type);
    }

    return size;
}

// ============================================================================

void SiteLutMappingCache::add(const SiteLutMappingKey &key, const SiteLutMappingResult &result)
{
    cache_[key] = result;
}

bool SiteLutMappingCache::get(const SiteLutMappingKey &key, SiteLutMappingResult *result)
{
    if (cache_.count(key) == 0) {
        numMisses++;
        return false;
    }

    numHits++;
    *result = cache_[key];
    return true;
}

void SiteLutMappingCache::clear()
{
    cache_.clear();
    clearStats();
}

void SiteLutMappingCache::clearStats()
{
    numHits = 0;
    numMisses = 0;
}

// ============================================================================

NEXTPNR_NAMESPACE_END
