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

#include "cost_map.h"

#include "context.h"
#include "log.h"

NEXTPNR_NAMESPACE_BEGIN

///@brief Factor to adjust the penalty calculation for deltas outside the segment bounding box:
//      factor < 1.0: penalty has less impact on the final returned delay
//      factor > 1.0: penalty has more impact on the final returned delay
static constexpr float PENALTY_FACTOR = 1.f;

///@brief Minimum penalty cost that is added when penalizing a delta outside the segment bounding box.
static constexpr delay_t PENALTY_MIN = 1;

// also known as the L1 norm
static int manhattan_distance(const std::pair<int32_t, int32_t> &a, const std::pair<int32_t, int32_t> &b)
{
    return std::abs(b.first - a.first) + std::abs(b.second - a.second);
}

static delay_t penalize(const delay_t &entry, int distance, delay_t penalty)
{
    penalty = std::max(penalty, PENALTY_MIN);
    return entry + distance * penalty * PENALTY_FACTOR;
}

delay_t CostMap::get_delay(const Context *ctx, WireId src_wire, WireId dst_wire) const
{
    TypeWirePair type_pair;
    type_pair.src = TypeWireId(ctx, src_wire);
    type_pair.dst = TypeWireId(ctx, dst_wire);

    int src_tile;
    if (src_wire.tile == -1) {
        src_tile = ctx->chip_info->nodes[src_wire.index].tile_wires[0].tile;
    } else {
        src_tile = src_wire.tile;
    }

    int32_t src_x, src_y;
    ctx->get_tile_x_y(src_tile, &src_x, &src_y);

    int dst_tile;
    if (dst_wire.tile == -1) {
        dst_tile = ctx->chip_info->nodes[dst_wire.index].tile_wires[0].tile;
    } else {
        dst_tile = dst_wire.tile;
    }

    int32_t dst_x, dst_y;
    ctx->get_tile_x_y(dst_tile, &dst_x, &dst_y);

    auto iter = cost_map_.find(type_pair);
    if (iter == cost_map_.end()) {
        auto &src_type = ctx->chip_info->tile_types[type_pair.src.type];
        IdString src_tile_type(src_type.name);
        IdString src_wire_name(src_type.wire_data[type_pair.src.index].name);

        auto &dst_type = ctx->chip_info->tile_types[type_pair.dst.type];
        IdString dst_tile_type(dst_type.name);
        IdString dst_wire_name(dst_type.wire_data[type_pair.dst.index].name);

#if 0
        log_warning("Delay matrix is missing %s/%s -> %s/%s\n",
                src_tile_type.c_str(ctx),
                src_wire_name.c_str(ctx),
                dst_tile_type.c_str(ctx),
                dst_wire_name.c_str(ctx));
#endif
        return std::numeric_limits<delay_t>::max();
    }

    const auto &delay_matrix = iter->second;

    int32_t off_x = delay_matrix.offset.first + (dst_x - src_x);
    int32_t off_y = delay_matrix.offset.second + (dst_y - src_y);

    int32_t x_dim = delay_matrix.data.shape()[0];
    int32_t y_dim = delay_matrix.data.shape()[1];
    NPNR_ASSERT(x_dim > 0);
    NPNR_ASSERT(y_dim > 0);

    // Bound closest_x/y to [0, dim)
    int32_t closest_x = std::min(std::max(off_x, 0), x_dim - 1);
    int32_t closest_y = std::min(std::max(off_y, 0), y_dim - 1);

    // Get the cost entry from the cost map at the deltas values
    auto cost = delay_matrix.data[closest_x][closest_y];
    NPNR_ASSERT(cost >= 0);

    // Get the base penalty corresponding to the current segment.
    auto penalty = delay_matrix.penalty;

    // Get the distance between the closest point in the bounding box and the original coordinates.
    // Note that if the original coordinates are within the bounding box, the distance will be equal to zero.
    auto distance = manhattan_distance(std::make_pair(off_x, off_y), std::make_pair(closest_x, closest_y));

    // Return the penalized cost (no penalty is added if the coordinates are within the bounding box).
    return penalize(cost, distance, penalty);
}

void CostMap::set_cost_map(const Context *ctx, const TypeWirePair &wire_pair,
                           const dict<std::pair<int32_t, int32_t>, delay_t> &delays)
{
    CostMapEntry delay_matrix;

    auto &offset = delay_matrix.offset;
    offset.first = 0;
    offset.second = 0;

    int32_t max_x_offset = 0;
    int32_t max_y_offset = 0;

    for (const auto &delay_pair : delays) {
        auto &dx_dy = delay_pair.first;
        offset.first = std::max(-dx_dy.first, offset.first);
        offset.second = std::max(-dx_dy.second, offset.second);
        max_x_offset = std::max(dx_dy.first, max_x_offset);
        max_y_offset = std::max(dx_dy.second, max_y_offset);
    }

    int32_t x_dim = offset.first + max_x_offset + 1;
    int32_t y_dim = offset.second + max_y_offset + 1;

    delay_matrix.data.resize(boost::extents[x_dim][y_dim]);

    // Fill matrix with sentinel of -1 to know where the holes in the matrix
    // are.
    std::fill_n(delay_matrix.data.data(), delay_matrix.data.num_elements(), -1);

    for (const auto &delay_pair : delays) {
        auto &dx_dy = delay_pair.first;
        int32_t off_x = dx_dy.first + offset.first;
        int32_t off_y = dx_dy.second + offset.second;
        NPNR_ASSERT(off_x >= 0);
        NPNR_ASSERT(off_x < x_dim);
        NPNR_ASSERT(off_y >= 0);
        NPNR_ASSERT(off_y < y_dim);

        delay_matrix.data[off_x][off_y] = delay_pair.second;
    }

    delay_matrix.penalty = get_penalty(delay_matrix.data);
    fill_holes(ctx, wire_pair, delay_matrix.data, delay_matrix.penalty);

    {
        cost_map_mutex_.lock();
        auto result = cost_map_.emplace(wire_pair, delay_matrix);
        NPNR_ASSERT(result.second);
        cost_map_mutex_.unlock();
    }
}

static void assign_min_entry(delay_t *dst, const delay_t &src)
{
    if (src >= 0) {
        if (*dst < 0) {
            *dst = src;
        } else if (src < *dst) {
            *dst = src;
        }
    }
}

std::pair<delay_t, int> CostMap::get_nearby_cost_entry(const boost::multi_array<delay_t, 2> &matrix, int cx, int cy,
                                                       const BoundingBox &bounds)
{
#ifdef DEBUG_FILL
    log_info("Filling %d, %d within (%d, %d, %d, %d)\n", cx, cy, bounds.x0, bounds.y0, bounds.x1, bounds.y1);
#endif

    // spiral around (cx, cy) looking for a nearby entry
    bool in_bounds = bounds.contains(cx, cy);
    if (!in_bounds) {
#ifdef DEBUG_FILL
        log_info("Already out of bounds, return!\n");
#endif
        return std::make_pair(-1, 0);
    }
    int n = 0;
    delay_t fill(matrix[cx][cy]);

    while (in_bounds && (fill < 0)) {
        n++;
#ifdef DEBUG_FILL
        log_info("At n = %d\n", n);
#endif
        in_bounds = false;
        delay_t min_entry = -1;
        for (int ox = -n; ox <= n; ox++) {
            int x = cx + ox;
            int oy = n - abs(ox);
            int yp = cy + oy;
            int yn = cy - oy;
#ifdef DEBUG_FILL
            log_info("Testing %d, %d\n", x, yp);
#endif
            if (bounds.contains(x, yp)) {
                assign_min_entry(&min_entry, matrix[x][yp]);
                in_bounds = true;
#ifdef DEBUG_FILL
                log_info("matrix[%d, %d] = %d, min_entry = %d\n", x, yp, matrix[x][yp], min_entry);
#endif
            }
#ifdef DEBUG_FILL
            log_info("Testing %d, %d\n", x, yn);
#endif
            if (bounds.contains(x, yn)) {
                assign_min_entry(&min_entry, matrix[x][yn]);
                in_bounds = true;
#ifdef DEBUG_FILL
                log_info("matrix[%d, %d] = %d, min_entry = %d\n", x, yn, matrix[x][yn], min_entry);
#endif
            }
        }

        if (fill < 0 && min_entry >= 0) {
            fill = min_entry;
        }
    }

    return std::make_pair(fill, n);
}

void CostMap::fill_holes(const Context *ctx, const TypeWirePair &type_pair, boost::multi_array<delay_t, 2> &matrix,
                         delay_t delay_penalty)
{
    // find missing cost entries and fill them in by copying a nearby cost entry
    std::vector<std::tuple<unsigned, unsigned, delay_t>> missing;
    bool couldnt_fill = false;
    auto shifted_bounds = BoundingBox(0, 0, matrix.shape()[0] - 1, matrix.shape()[1] - 1);
    int max_fill = 0;
    for (unsigned ix = 0; ix < matrix.shape()[0]; ix++) {
        for (unsigned iy = 0; iy < matrix.shape()[1]; iy++) {
            delay_t &cost_entry = matrix[ix][iy];
            if (cost_entry < 0) {
                // maximum search radius
                delay_t filler;
                int distance;
                std::tie(filler, distance) = get_nearby_cost_entry(matrix, ix, iy, shifted_bounds);
                if (filler >= 0) {
                    missing.push_back(std::make_tuple(ix, iy, penalize(filler, distance, delay_penalty)));
                    max_fill = std::max(max_fill, distance);
                } else {
                    couldnt_fill = true;
                }
            }
        }
        if (couldnt_fill) {
            // give up trying to fill an empty matrix
            break;
        }
    }

    if (!couldnt_fill && max_fill > 0) {
        if (ctx->verbose) {
            auto &src_type_data = ctx->chip_info->tile_types[type_pair.src.type];
            IdString src_type(src_type_data.name);
            IdString src_wire(src_type_data.wire_data[type_pair.src.index].name);

            auto &dst_type_data = ctx->chip_info->tile_types[type_pair.dst.type];
            IdString dst_type(dst_type_data.name);
            IdString dst_wire(dst_type_data.wire_data[type_pair.dst.index].name);

#ifdef DEBUG_FILL
            log_info("At %s/%s -> %s/%s: max_fill = %d, delay_penalty = %d\n", src_type.c_str(ctx), src_wire.c_str(ctx),
                     dst_type.c_str(ctx), dst_wire.c_str(ctx), max_fill, delay_penalty);
#endif
        }
    }

    // write back the missing entries
    for (auto &xy_entry : missing) {
        matrix[std::get<0>(xy_entry)][std::get<1>(xy_entry)] = std::get<2>(xy_entry);
    }

    if (couldnt_fill) {
        auto &src_type_data = ctx->chip_info->tile_types[type_pair.src.type];
        IdString src_type(src_type_data.name);
        IdString src_wire(src_type_data.wire_data[type_pair.src.index].name);

        auto &dst_type_data = ctx->chip_info->tile_types[type_pair.dst.type];
        IdString dst_type(dst_type_data.name);
        IdString dst_wire(dst_type_data.wire_data[type_pair.dst.index].name);

        log_warning("Couldn't fill holes in the cost matrix %s/%s -> %s/%s %d x %d bounding box\n", src_type.c_str(ctx),
                    src_wire.c_str(ctx), dst_type.c_str(ctx), dst_wire.c_str(ctx), shifted_bounds.x1,
                    shifted_bounds.y1);
        for (unsigned y = 0; y < matrix.shape()[1]; y++) {
            for (unsigned x = 0; x < matrix.shape()[0]; x++) {
                NPNR_ASSERT(matrix[x][y] >= 0);
            }
        }
    }
}

delay_t CostMap::get_penalty(const boost::multi_array<delay_t, 2> &matrix) const
{
    delay_t min_delay = std::numeric_limits<delay_t>::max();
    delay_t max_delay = std::numeric_limits<delay_t>::lowest();

    std::pair<int32_t, int32_t> min_location(0, 0), max_location(0, 0);
    for (unsigned ix = 0; ix < matrix.shape()[0]; ix++) {
        for (unsigned iy = 0; iy < matrix.shape()[1]; iy++) {
            const delay_t &cost_entry = matrix[ix][iy];
            if (cost_entry >= 0) {
                if (cost_entry < min_delay) {
                    min_delay = cost_entry;
                    min_location = std::make_pair(ix, iy);
                }
                if (cost_entry > max_delay) {
                    max_delay = cost_entry;
                    max_location = std::make_pair(ix, iy);
                }
            }
        }
    }

    delay_t delay_penalty =
            (max_delay - min_delay) / static_cast<float>(std::max(1, manhattan_distance(max_location, min_location)));

    return delay_penalty;
}

void CostMap::from_reader(lookahead_storage::CostMap::Reader reader)
{
    for (auto cost_entry : reader.getCostMap()) {
        TypeWirePair key(cost_entry.getKey());

        auto result = cost_map_.emplace(key, CostMapEntry());
        NPNR_ASSERT(result.second);

        CostMapEntry &entry = result.first->second;
        auto data = cost_entry.getData();
        auto in_iter = data.begin();

        entry.data.resize(boost::extents[cost_entry.getXDim()][cost_entry.getYDim()]);
        if (entry.data.num_elements() != data.size()) {
            log_error("entry.data.num_elements() %zu != data.size() %u", entry.data.num_elements(), data.size());
        }

        delay_t *out = entry.data.origin();
        for (; in_iter != data.end(); ++in_iter, ++out) {
            *out = *in_iter;
        }

        entry.penalty = cost_entry.getPenalty();

        entry.offset.first = cost_entry.getXOffset();
        entry.offset.second = cost_entry.getYOffset();
    }
}

void CostMap::to_builder(lookahead_storage::CostMap::Builder builder) const
{
    auto cost_map = builder.initCostMap(cost_map_.size());
    auto entry_iter = cost_map.begin();
    auto in = cost_map_.begin();
    for (; entry_iter != cost_map.end(); ++entry_iter, ++in) {
        NPNR_ASSERT(in != cost_map_.end());

        in->first.to_builder(entry_iter->getKey());
        const CostMapEntry &entry = in->second;

        auto data = entry_iter->initData(entry.data.num_elements());
        const delay_t *data_in = entry.data.origin();
        for (size_t i = 0; i < entry.data.num_elements(); ++i) {
            data.set(i, data_in[i]);
        }

        entry_iter->setXDim(entry.data.shape()[0]);
        entry_iter->setYDim(entry.data.shape()[1]);
        entry_iter->setXOffset(entry.offset.first);
        entry_iter->setYOffset(entry.offset.second);
        entry_iter->setPenalty(entry.penalty);
    }
}

NEXTPNR_NAMESPACE_END
