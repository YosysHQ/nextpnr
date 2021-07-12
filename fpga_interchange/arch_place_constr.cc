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

void Arch::place_constraints()
{
    std::vector<std::pair<IdString, BelId>> constrained_cells;
    for (auto &cell_pair : cells) {
        CellInfo *cell = cell_pair.second.get();
        auto loc_constr = cell->attrs.find(id("LOC"));
        auto bel_constr = cell->attrs.find(id("BEL"));

        if (loc_constr == cell->attrs.end() || bel_constr == cell->attrs.end())
            continue;

        IdString loc_name = id(loc_constr->second.as_string());
        IdString bel_name = id(bel_constr->second.as_string());

        BelId bel;
        for (size_t i = 0; i < chip_info->tiles.size(); ++i) {
            const auto &tile = chip_info->tiles[i];
            bool site_found = false;
            for (size_t j = 0; j < tile.sites.size(); ++j) {
                auto &site_data = chip_info->sites[tile.sites[j]];
                if (loc_name == id(site_data.site_name.get())) {
                    site_found = true;
                    break;
                }
            }

            if (!site_found)
                continue;

            const auto &tile_type = chip_info->tile_types[tile.type];
            bool bel_found = false;
            for (size_t j = 0; j < tile_type.bel_data.size(); ++j) {
                const BelInfoPOD &bel_data = tile_type.bel_data[j];
                if (bel_name == IdString(bel_data.name)) {
                    bel.tile = i;
                    bel.index = j;
                    bel_found = true;
                    break;
                }
            }

            if (bel_found)
                break;
            else
                log_error("No bel found for user constraint \'%s/%s\' for cell \'%s\'\n", loc_name.c_str(getCtx()),
                          bel_name.c_str(getCtx()), cell->name.c_str(getCtx()));
        }

        if (!isValidBelForCellType(cell->type, bel))
            log_error("Bel \'%s\' is invalid for cell \'%s\' (%s)\n", nameOfBel(bel), cell->name.c_str(getCtx()),
                      cell->type.c_str(getCtx()));

        auto bound_cell = getBoundBelCell(bel);
        if (bound_cell)
            log_error("Cell \'%s\' cannot be bound to bel \'%s\' "
                      "since it is already bound to cell \'%s\'\n",
                      cell->name.c_str(getCtx()), nameOfBel(bel), bound_cell->name.c_str(getCtx()));

        bindBel(bel, cell, STRENGTH_USER);

        cell->attrs.erase(id("BEL"));
        constrained_cells.emplace_back(cell->name, bel);
    }

    if (constrained_cells.empty())
        return;

    log_info("Cell placed via user constraints:\n");
    for (auto cell_bel : constrained_cells) {
        IdString cell_name = cell_bel.first;
        BelId bel = cell_bel.second;

        if (!isBelLocationValid(bel))
            log_error("  - Bel \'%s\' is not valid for cell \'%s\'\n", nameOfBel(bel), cell_name.c_str(getCtx()));

        log_info("  - %s placed at %s\n", cell_name.c_str(getCtx()), nameOfBel(cell_bel.second));
    }
}

NEXTPNR_NAMESPACE_END
