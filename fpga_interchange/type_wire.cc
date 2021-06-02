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

#include "type_wire.h"
#include "nextpnr.h"

NEXTPNR_NAMESPACE_BEGIN

TypeWireId::TypeWireId(const Context *ctx, WireId wire_inst)
{
    NPNR_ASSERT(wire_inst != WireId());

    if (wire_inst.tile == -1) {
        auto &tile_wire = ctx->chip_info->nodes[wire_inst.index].tile_wires[0];
        type = ctx->chip_info->tiles[tile_wire.tile].type;
        index = tile_wire.index;
    } else {
        type = ctx->chip_info->tiles[wire_inst.tile].type;
        index = wire_inst.index;
    }
}

TypeWireSet::TypeWireSet(const Context *ctx, WireId wire)
{
    if (wire.tile == -1) {
        const auto &node_data = ctx->chip_info->nodes[wire.index];
        wire_types_.reserve(node_data.tile_wires.size());
        for (const auto &tile_wire : node_data.tile_wires) {
            wire_types_.emplace_back();
            wire_types_.back().type = ctx->chip_info->tiles[tile_wire.tile].type;
            wire_types_.back().index = tile_wire.index;
        }
    } else {
        TypeWireId wire_type(ctx, wire);
        wire_types_.push_back(wire_type);
    }

    std::sort(wire_types_.begin(), wire_types_.end());

    hash_ = wire_types_.size();
    for (const auto &wire : wire_types_) {
        hash_ = mkhash(hash_, wire.hash());
    }
}

TypeWireId::TypeWireId(lookahead_storage::TypeWireId::Reader reader) : type(reader.getType()), index(reader.getIndex())
{
}
void TypeWireId::to_builder(lookahead_storage::TypeWireId::Builder builder) const
{
    builder.setType(type);
    builder.setIndex(index);
}

TypeWirePair::TypeWirePair(lookahead_storage::TypeWirePair::Reader reader) : src(reader.getSrc()), dst(reader.getDst())
{
}

void TypeWirePair::to_builder(lookahead_storage::TypeWirePair::Builder builder) const
{
    src.to_builder(builder.getSrc());
    dst.to_builder(builder.getDst());
}

NEXTPNR_NAMESPACE_END
