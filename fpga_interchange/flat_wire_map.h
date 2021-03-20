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

#ifndef FLAT_WIRE_MAP_H_
#define FLAT_WIRE_MAP_H_

#include "context.h"
#include "dynamic_bitarray.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

NEXTPNR_NAMESPACE_BEGIN

template <typename Value> class FlatTileWireMap
{
  public:
    std::pair<Value *, bool> emplace(const Context *ctx, WireId wire, const Value &value)
    {
        if (values_.empty()) {
            if (wire.tile == -1) {
                resize(ctx->chip_info->nodes.size());
            } else {
                resize(loc_info(ctx->chip_info, wire).wire_data.size());
            }
        }

        if (set_.get(wire.index)) {
            return std::make_pair(&values_[wire.index], false);
        } else {
            values_[wire.index] = value;
            set_.set(wire.index, true);
            return std::make_pair(&values_[wire.index], true);
        }
    }

    const Value &at(WireId wire) const
    {
        NPNR_ASSERT(!values_.empty());
        NPNR_ASSERT(set_.get(wire.index));
        return values_.at(wire.index);
    }

    void clear()
    {
        if (!values_.empty()) {
            set_.fill(false);
        }
    }

  private:
    void resize(size_t count)
    {
        set_.resize(count);
        set_.fill(false);
        values_.resize(count);
    }

    DynamicBitarray<> set_;
    std::vector<Value> values_;
};

template <typename Value> class FlatWireMap
{
  public:
    FlatWireMap(const Context *ctx) : ctx_(ctx) { tiles_.resize(ctx->chip_info->tiles.size() + 1); }

    std::pair<std::pair<WireId, Value *>, bool> emplace(WireId wire, const Value &value)
    {
        // Tile = -1 is for node wires.
        size_t tile_index = wire.tile + 1;
        auto &tile = tiles_.at(tile_index);

        auto result = tile.emplace(ctx_, wire, value);
        if (result.second) {
            size_ += 1;
        }
        return std::make_pair(std::make_pair(wire, result.first), result.second);
    }

    const Value &at(WireId wire) const
    {
        const auto &tile = tiles_.at(wire.tile + 1);
        return tile.at(wire);
    }

    size_t size() const { return size_; }

    void clear()
    {
        for (auto &tile : tiles_) {
            tile.clear();
        }
        size_ = 0;
    }

  private:
    const Context *ctx_;
    std::vector<FlatTileWireMap<Value>> tiles_;
    size_t size_;
};

NEXTPNR_NAMESPACE_END

#endif /* FLAT_WIRE_MAP_H_ */
