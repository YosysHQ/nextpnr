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

#ifndef ARCH_ITERATORS_H
#define ARCH_ITERATORS_H

#include "chipdb.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

NEXTPNR_NAMESPACE_BEGIN

struct BelIterator
{
    const ChipInfoPOD *chip;
    int cursor_index;
    int cursor_tile;

    BelIterator operator++()
    {
        cursor_index++;
        while (cursor_tile < chip->tiles.ssize() && cursor_index >= tile_info(chip, cursor_tile).bel_data.ssize()) {
            cursor_index = 0;
            cursor_tile++;
        }
        return *this;
    }
    BelIterator operator++(int)
    {
        BelIterator prior(*this);
        ++(*this);
        return prior;
    }

    bool operator!=(const BelIterator &other) const
    {
        return cursor_index != other.cursor_index || cursor_tile != other.cursor_tile;
    }

    bool operator==(const BelIterator &other) const
    {
        return cursor_index == other.cursor_index && cursor_tile == other.cursor_tile;
    }

    BelId operator*() const
    {
        BelId ret;
        ret.tile = cursor_tile;
        ret.index = cursor_index;
        return ret;
    }
};

struct BelRange
{
    BelIterator b, e;
    BelIterator begin() const { return b; }
    BelIterator end() const { return e; }
};

struct FilteredBelIterator
{
    std::function<bool(BelId)> filter;
    BelIterator b, e;

    FilteredBelIterator operator++()
    {
        ++b;
        while (b != e) {
            if (filter(*b)) {
                break;
            }

            ++b;
        }
        return *this;
    }

    bool operator!=(const FilteredBelIterator &other) const
    {
        NPNR_ASSERT(e == other.e);
        return b != other.b;
    }

    bool operator==(const FilteredBelIterator &other) const
    {
        NPNR_ASSERT(e == other.e);
        return b == other.b;
    }

    BelId operator*() const
    {
        BelId bel = *b;
        NPNR_ASSERT(filter(bel));
        return bel;
    }
};

struct FilteredBelRange
{
    FilteredBelRange(BelIterator bel_b, BelIterator bel_e, std::function<bool(BelId)> filter)
    {
        b.filter = filter;
        b.b = bel_b;
        b.e = bel_e;

        if (b.b != b.e && !filter(*b.b)) {
            ++b;
        }

        e.b = bel_e;
        e.e = bel_e;

        if (b != e) {
            NPNR_ASSERT(filter(*b.b));
        }
    }

    FilteredBelIterator b, e;
    FilteredBelIterator begin() const { return b; }
    FilteredBelIterator end() const { return e; }
};

// -----------------------------------------------------------------------

// Iterate over TileWires for a wire (will be more than one if nodal)
struct TileWireIterator
{
    const ChipInfoPOD *chip;
    WireId baseWire;
    int cursor = -1;

    void operator++() { cursor++; }

    bool operator==(const TileWireIterator &other) const { return cursor == other.cursor; }
    bool operator!=(const TileWireIterator &other) const { return cursor != other.cursor; }

    // Returns a *denormalised* identifier always pointing to a tile wire rather than a node
    WireId operator*() const
    {
        if (baseWire.tile == -1) {
            WireId tw;
            const auto &node_wire = chip->nodes[baseWire.index].tile_wires[cursor];
            tw.tile = node_wire.tile;
            tw.index = node_wire.index;
            return tw;
        } else {
            return baseWire;
        }
    }
};

struct TileWireRange
{
    TileWireIterator b, e;
    TileWireIterator begin() const { return b; }
    TileWireIterator end() const { return e; }
};

NPNR_ALWAYS_INLINE inline WireId canonical_wire(const ChipInfoPOD *chip_info, int32_t tile, int32_t wire)
{
    WireId id;

    if (wire >= chip_info->tiles[tile].tile_wire_to_node.ssize()) {
        // Cannot be a nodal wire
        id.tile = tile;
        id.index = wire;
    } else {
        int32_t node = chip_info->tiles[tile].tile_wire_to_node[wire];
        if (node == -1) {
            // Not a nodal wire
            id.tile = tile;
            id.index = wire;
        } else {
            // Is a nodal wire, set tile to -1
            id.tile = -1;
            id.index = node;
        }
    }

    return id;
}

// -----------------------------------------------------------------------

struct WireIterator
{
    const ChipInfoPOD *chip;
    int cursor_index = 0;
    int cursor_tile = -1;

    WireIterator operator++()
    {
        // Iterate over nodes first, then tile wires that aren't nodes
        do {
            cursor_index++;
            if (cursor_tile == -1 && cursor_index >= chip->nodes.ssize()) {
                cursor_tile = 0;
                cursor_index = 0;
            }
            while (cursor_tile != -1 && cursor_tile < chip->tiles.ssize() &&
                   cursor_index >= chip->tile_types[chip->tiles[cursor_tile].type].wire_data.ssize()) {
                cursor_index = 0;
                cursor_tile++;
            }

        } while ((cursor_tile != -1 && cursor_tile < chip->tiles.ssize() &&
                  cursor_index < chip->tiles[cursor_tile].tile_wire_to_node.ssize() &&
                  chip->tiles[cursor_tile].tile_wire_to_node[cursor_index] != -1));

        return *this;
    }
    WireIterator operator++(int)
    {
        WireIterator prior(*this);
        ++(*this);
        return prior;
    }

    bool operator!=(const WireIterator &other) const
    {
        return cursor_index != other.cursor_index || cursor_tile != other.cursor_tile;
    }

    bool operator==(const WireIterator &other) const
    {
        return cursor_index == other.cursor_index && cursor_tile == other.cursor_tile;
    }

    WireId operator*() const
    {
        WireId ret;
        ret.tile = cursor_tile;
        ret.index = cursor_index;
        return ret;
    }
};

struct WireRange
{
    WireIterator b, e;
    WireIterator begin() const { return b; }
    WireIterator end() const { return e; }
};

// -----------------------------------------------------------------------
struct AllPipIterator
{
    const ChipInfoPOD *chip;
    int cursor_index;
    int cursor_tile;

    AllPipIterator operator++()
    {
        cursor_index++;
        while (cursor_tile < chip->tiles.ssize() &&
               cursor_index >= chip->tile_types[chip->tiles[cursor_tile].type].pip_data.ssize()) {
            cursor_index = 0;
            cursor_tile++;
        }
        return *this;
    }
    AllPipIterator operator++(int)
    {
        AllPipIterator prior(*this);
        ++(*this);
        return prior;
    }

    bool operator!=(const AllPipIterator &other) const
    {
        return cursor_index != other.cursor_index || cursor_tile != other.cursor_tile;
    }

    bool operator==(const AllPipIterator &other) const
    {
        return cursor_index == other.cursor_index && cursor_tile == other.cursor_tile;
    }

    PipId operator*() const
    {
        PipId ret;
        ret.tile = cursor_tile;
        ret.index = cursor_index;
        return ret;
    }
};

struct AllPipRange
{
    AllPipIterator b, e;
    AllPipIterator begin() const { return b; }
    AllPipIterator end() const { return e; }
};

// -----------------------------------------------------------------------

struct UphillPipIterator
{
    const ChipInfoPOD *chip;
    TileWireIterator twi, twi_end;
    int cursor = -1;

    void operator++()
    {
        cursor++;
        while (true) {
            if (!(twi != twi_end))
                break;
            WireId w = *twi;
            auto &tile = chip->tile_types[chip->tiles[w.tile].type];
            if (cursor < tile.wire_data[w.index].pips_uphill.ssize())
                break;
            ++twi;
            cursor = 0;
        }
    }
    bool operator!=(const UphillPipIterator &other) const { return twi != other.twi || cursor != other.cursor; }

    PipId operator*() const
    {
        PipId ret;
        WireId w = *twi;
        ret.tile = w.tile;
        ret.index = chip->tile_types[chip->tiles[w.tile].type].wire_data[w.index].pips_uphill[cursor];
        return ret;
    }
};

struct UphillPipRange
{
    UphillPipIterator b, e;
    UphillPipIterator begin() const { return b; }
    UphillPipIterator end() const { return e; }
};

struct DownhillPipIterator
{
    const ChipInfoPOD *chip;
    TileWireIterator twi, twi_end;
    int cursor = -1;

    int32_t tile;
    int32_t tile_type;
    const RelSlice<int32_t> *pips_downhill = nullptr;

    void operator++()
    {
        cursor++;
        while (true) {
            if (!(twi != twi_end))
                break;

            if (pips_downhill == nullptr) {
                WireId w = *twi;
                tile_type = chip->tiles[w.tile].type;
                const TileTypeInfoPOD &type = chip->tile_types[tile_type];

                tile = w.tile;
                pips_downhill = &type.wire_data[w.index].pips_downhill;
            }

            if (cursor < pips_downhill->ssize())
                break;

            ++twi;
            cursor = 0;
            pips_downhill = nullptr;
        }
    }
    bool operator!=(const DownhillPipIterator &other) const { return twi != other.twi || cursor != other.cursor; }

    PipId operator*() const
    {
        PipId ret;
        ret.tile = tile;
        ret.index = (*pips_downhill)[cursor];
        return ret;
    }
};

struct DownhillPipRange
{
    DownhillPipIterator b, e;
    DownhillPipIterator begin() const { return b; }
    DownhillPipIterator end() const { return e; }
};

struct BelPinIterator
{
    const ChipInfoPOD *chip;
    TileWireIterator twi, twi_end;
    int cursor = -1;

    void operator++()
    {
        cursor++;

        while (twi != twi_end) {
            WireId w = *twi;
            auto &tile = tile_info(chip, w.tile);
            if (cursor < tile.wire_data[w.index].bel_pins.ssize())
                break;

            ++twi;
            cursor = 0;
        }
    }
    bool operator!=(const BelPinIterator &other) const { return twi != other.twi || cursor != other.cursor; }

    BelPin operator*() const
    {
        BelPin ret;
        WireId w = *twi;
        ret.bel.tile = w.tile;
        ret.bel.index = tile_info(chip, w.tile).wire_data[w.index].bel_pins[cursor].bel_index;
        ret.pin.index = tile_info(chip, w.tile).wire_data[w.index].bel_pins[cursor].port;
        return ret;
    }
};

struct BelPinRange
{
    BelPinIterator b, e;
    BelPinIterator begin() const { return b; }
    BelPinIterator end() const { return e; }
};

struct IdStringIterator : std::iterator<std::forward_iterator_tag,
                                        /*T=*/IdString,
                                        /*Distance=*/ptrdiff_t,
                                        /*pointer=*/IdString *,
                                        /*reference=*/IdString>
{
    const int32_t *cursor;

    void operator++() { cursor += 1; }

    bool operator!=(const IdStringIterator &other) const { return cursor != other.cursor; }

    bool operator==(const IdStringIterator &other) const { return cursor == other.cursor; }

    IdString operator*() const { return IdString(*cursor); }
};

struct IdStringRange
{
    IdStringIterator b, e;
    IdStringIterator begin() const { return b; }
    IdStringIterator end() const { return e; }
};

struct BelBucketIterator
{
    IdStringIterator cursor;

    void operator++() { ++cursor; }

    bool operator!=(const BelBucketIterator &other) const { return cursor != other.cursor; }

    bool operator==(const BelBucketIterator &other) const { return cursor == other.cursor; }

    BelBucketId operator*() const
    {
        BelBucketId bucket;
        bucket.name = IdString(*cursor);
        return bucket;
    }
};

struct BelBucketRange
{
    BelBucketIterator b, e;
    BelBucketIterator begin() const { return b; }
    BelBucketIterator end() const { return e; }
};

NEXTPNR_NAMESPACE_END

#endif /* ARCH_ITERATORS_H */
