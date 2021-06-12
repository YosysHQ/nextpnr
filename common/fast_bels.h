/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
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

#include <cstddef>
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

// FastBels is a lookup class that provides a fast lookup for finding BELs
// that support a given cell type.
struct FastBels
{
    struct TypeData
    {
        size_t type_index;
        int number_of_possible_bels;
    };

    FastBels(Context *ctx, bool check_bel_available, int minBelsForGridPick)
            : ctx(ctx), check_bel_available(check_bel_available), minBelsForGridPick(minBelsForGridPick)
    {
    }

    void addCellType(IdString cell_type)
    {
        auto iter = cell_types.find(cell_type);
        if (iter != cell_types.end()) {
            // This cell type has already been added to the fast BEL lookup.
            return;
        }

        size_t type_idx = cell_types.size();
        auto &cell_type_data = cell_types[cell_type];
        cell_type_data.type_index = type_idx;

        fast_bels_by_cell_type.resize(type_idx + 1);
        auto &bel_data = fast_bels_by_cell_type.at(type_idx);
        NPNR_ASSERT(bel_data.get() == nullptr);
        bel_data = std::make_unique<FastBelsData>();

        for (auto bel : ctx->getBels()) {
            if (!ctx->isValidBelForCellType(cell_type, bel)) {
                continue;
            }

            cell_type_data.number_of_possible_bels += 1;
        }

        for (auto bel : ctx->getBels()) {
            if (check_bel_available && !ctx->checkBelAvail(bel)) {
                continue;
            }

            if (!ctx->isValidBelForCellType(cell_type, bel)) {
                continue;
            }

            Loc loc = ctx->getBelLocation(bel);
            if (minBelsForGridPick >= 0 && cell_type_data.number_of_possible_bels < minBelsForGridPick) {
                loc.x = loc.y = 0;
            }

            if (int(bel_data->size()) < (loc.x + 1)) {
                bel_data->resize(loc.x + 1);
            }

            if (int(bel_data->at(loc.x).size()) < (loc.y + 1)) {
                bel_data->at(loc.x).resize(loc.y + 1);
            }

            bel_data->at(loc.x).at(loc.y).push_back(bel);
        }
    }

    void addBelBucket(BelBucketId partition)
    {
        auto iter = partition_types.find(partition);
        if (iter != partition_types.end()) {
            // This partition has already been added to the fast BEL lookup.
            return;
        }

        size_t type_idx = partition_types.size();
        auto &type_data = partition_types[partition];
        type_data.type_index = type_idx;

        fast_bels_by_partition_type.resize(type_idx + 1);
        auto &bel_data = fast_bels_by_partition_type.at(type_idx);
        NPNR_ASSERT(bel_data.get() == nullptr);
        bel_data = std::make_unique<FastBelsData>();

        for (auto bel : ctx->getBels()) {
            if (ctx->getBelBucketForBel(bel) != partition) {
                continue;
            }

            type_data.number_of_possible_bels += 1;
        }

        for (auto bel : ctx->getBels()) {
            if (check_bel_available && !ctx->checkBelAvail(bel)) {
                continue;
            }

            if (ctx->getBelBucketForBel(bel) != partition) {
                continue;
            }

            Loc loc = ctx->getBelLocation(bel);
            if (minBelsForGridPick >= 0 && type_data.number_of_possible_bels < minBelsForGridPick) {
                loc.x = loc.y = 0;
            }

            if (int(bel_data->size()) < (loc.x + 1)) {
                bel_data->resize(loc.x + 1);
            }

            if (int(bel_data->at(loc.x).size()) < (loc.y + 1)) {
                bel_data->at(loc.x).resize(loc.y + 1);
            }

            bel_data->at(loc.x).at(loc.y).push_back(bel);
        }
    }

    typedef std::vector<std::vector<std::vector<BelId>>> FastBelsData;

    int getBelsForCellType(IdString cell_type, FastBelsData **data)
    {
        auto iter = cell_types.find(cell_type);
        if (iter == cell_types.end()) {
            addCellType(cell_type);
            iter = cell_types.find(cell_type);
            NPNR_ASSERT(iter != cell_types.end());
        }

        auto cell_type_data = iter->second;

        *data = fast_bels_by_cell_type.at(cell_type_data.type_index).get();
        return cell_type_data.number_of_possible_bels;
    }

    size_t getBelsForBelBucket(BelBucketId partition, FastBelsData **data)
    {
        auto iter = partition_types.find(partition);
        if (iter == partition_types.end()) {
            addBelBucket(partition);
            iter = partition_types.find(partition);
            NPNR_ASSERT(iter != partition_types.end());
        }

        auto type_data = iter->second;

        *data = fast_bels_by_partition_type.at(type_data.type_index).get();
        return type_data.number_of_possible_bels;
    }

    Context *ctx;
    const bool check_bel_available;
    const int minBelsForGridPick;

    dict<IdString, TypeData> cell_types;
    std::vector<std::unique_ptr<FastBelsData>> fast_bels_by_cell_type;

    dict<BelBucketId, TypeData> partition_types;
    std::vector<std::unique_ptr<FastBelsData>> fast_bels_by_partition_type;
};

NEXTPNR_NAMESPACE_END
