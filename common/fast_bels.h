/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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

#pragma once

#include "nextpnr.h"
#include <cstddef>

NEXTPNR_NAMESPACE_BEGIN

// FastBels is a lookup class that provides a fast lookup for finding BELs
// that support a given cell type.
struct FastBels {
    struct CellTypeData {
        size_t cell_type_index;
        size_t number_of_possible_bels;
    };

    FastBels(Context *ctx, int minBelsForGridPick) : ctx(ctx), minBelsForGridPick(minBelsForGridPick) {}

    void addCellType(IdString cell_type) {
        auto iter = cell_types.find(cell_type);
        if(iter != cell_types.end()) {
            // This cell type has already been added to the fast BEL lookup.
            return;
        }

        size_t type_idx = cell_types.size();
        auto &cell_type_data = cell_types[cell_type];
        cell_type_data.cell_type_index = type_idx;

        fast_bels.resize(type_idx + 1);
        auto &bel_data = fast_bels.at(type_idx);

        for (auto bel : ctx->getBels()) {
            if(!ctx->isValidBelForCellType(cell_type, bel)) {
                continue;
            }

            cell_type_data.number_of_possible_bels += 1;
        }

        for (auto bel : ctx->getBels()) {
            if(!ctx->checkBelAvail(bel)) {
                continue;
            }

            Loc loc = ctx->getBelLocation(bel);
            if (minBelsForGridPick >= 0 && cell_type_data.number_of_possible_bels < minBelsForGridPick) {
                loc.x = loc.y = 0;
            }

            if (int(bel_data.size()) < (loc.x + 1)) {
                bel_data.resize(loc.x + 1);
            }

            if (int(bel_data.at(loc.x).size()) < (loc.y + 1)) {
                bel_data.at(loc.x).resize(loc.y + 1);
            }

            bel_data.at(loc.x).at(loc.y).push_back(bel);
        }
    }

    typedef std::vector<std::vector<std::vector<BelId>>> FastBelsData;

    size_t getBelsForCellType(IdString cell_type, FastBelsData **data) {
        auto iter = cell_types.find(cell_type);
        if(iter == cell_types.end()) {
            addCellType(cell_type);
            iter = cell_types.find(cell_type);
            NPNR_ASSERT(iter != cell_types.end());
        }

        auto cell_type_data = iter->second;

        *data = &fast_bels.at(cell_type_data.cell_type_index);
        return cell_type_data.number_of_possible_bels;
    }

    Context *ctx;
    int minBelsForGridPick;

    std::unordered_map<IdString, CellTypeData> cell_types;
    std::vector<FastBelsData> fast_bels;
};

NEXTPNR_NAMESPACE_END
