/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
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

#include "design.h"

#ifndef CHIP_H
#define CHIP_H

struct DelayInfo
{
    float delay = 0;

    float raiseDelay() const { return delay; }
    float fallDelay() const { return delay; }
    float avgDelay() const { return delay; }

    DelayInfo operator+(const DelayInfo &other) const
    {
        DelayInfo ret;
        ret.delay = this->delay + other.delay;
        return ret;
    }
};

// -----------------------------------------------------------------------

enum BelType
{
    TYPE_NIL,
    TYPE_ICESTORM_LC,
    TYPE_ICESTORM_RAM,
    TYPE_SB_IO
};

IdString belTypeToId(BelType type);
BelType belTypeFromId(IdString id);

enum PortPin
{
    PIN_NIL,
#define X(t) PIN_##t,
#include "portpins.inc"
#undef X
};

IdString portPinToId(PortPin type);
PortPin portPinFromId(IdString id);

// -----------------------------------------------------------------------

struct BelWirePOD
{
    int32_t wire_index;
    PortPin port;
};

struct BelInfoPOD
{
    const char *name;
    BelType type;
    int num_bel_wires;
    BelWirePOD *bel_wires;
    int8_t x, y, z;
};

struct BelPortPOD
{
    int32_t bel_index;
    PortPin port;
};

struct PipInfoPOD
{
    int32_t src, dst;
    float delay;
    int8_t x, y;
};

struct WireInfoPOD
{
    const char *name;
    int num_uphill, num_downhill;
    int *pips_uphill, *pips_downhill;

    int num_bels_downhill;
    BelPortPOD bel_uphill;
    BelPortPOD *bels_downhill;

    float x, y;
};

struct ChipInfoPOD
{
    int width, height;
    int num_bels, num_wires, num_pips;
    BelInfoPOD *bel_data;
    WireInfoPOD *wire_data;
    PipInfoPOD *pip_data;
};

extern ChipInfoPOD chip_info_384;
extern ChipInfoPOD chip_info_1k;
extern ChipInfoPOD chip_info_5k;
extern ChipInfoPOD chip_info_8k;

// -----------------------------------------------------------------------

struct BelId
{
    int32_t index = -1;

    bool nil() const { return index < 0; }

    bool operator==(const BelId &other) const { return index == other.index; }
    bool operator!=(const BelId &other) const { return index != other.index; }
};

struct WireId
{
    int32_t index = -1;

    bool nil() const { return index < 0; }

    bool operator==(const WireId &other) const { return index == other.index; }
    bool operator!=(const WireId &other) const { return index != other.index; }
};

struct PipId
{
    int32_t index = -1;

    bool nil() const { return index < 0; }

    bool operator==(const PipId &other) const { return index == other.index; }
    bool operator!=(const PipId &other) const { return index != other.index; }
};

struct BelPin
{
    BelId bel;
    PortPin pin;
};

namespace std {
template <> struct hash<BelId>
{
    std::size_t operator()(const BelId &bel) const noexcept
    {
        return bel.index;
    }
};

template <> struct hash<WireId>
{
    std::size_t operator()(const WireId &wire) const noexcept
    {
        return wire.index;
    }
};

template <> struct hash<PipId>
{
    std::size_t operator()(const PipId &wire) const noexcept
    {
        return wire.index;
    }
};
} // namespace std

// -----------------------------------------------------------------------

struct BelIterator
{
    int cursor;

    BelIterator operator++()
    {
        cursor++;
        return *this;
    }
    BelIterator operator++(int)
    {
        BelIterator prior(*this);
        cursor++;
        return prior;
    }

    bool operator!=(const BelIterator &other) const
    {
        return cursor != other.cursor;
    }

    bool operator==(const BelIterator &other) const
    {
        return cursor == other.cursor;
    }

    BelId operator*() const
    {
        BelId ret;
        ret.index = cursor;
        return ret;
    }
};

struct BelRange
{
    BelIterator b, e;
    BelIterator begin() const { return b; }
    BelIterator end() const { return e; }
};

// -----------------------------------------------------------------------

struct BelPinIterator
{
    BelPortPOD *ptr = nullptr;

    void operator++() { ptr++; }
    bool operator!=(const BelPinIterator &other) const
    {
        return ptr != other.ptr;
    }

    BelPin operator*() const
    {
        BelPin ret;
        ret.bel.index = ptr->bel_index;
        ret.pin = ptr->port;
        return ret;
    }
};

struct BelPinRange
{
    BelPinIterator b, e;
    BelPinIterator begin() const { return b; }
    BelPinIterator end() const { return e; }
};

// -----------------------------------------------------------------------

struct WireIterator
{
    int cursor = -1;

    void operator++() { cursor++; }
    bool operator!=(const WireIterator &other) const
    {
        return cursor != other.cursor;
    }

    WireId operator*() const
    {
        WireId ret;
        ret.index = cursor;
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
    int cursor = -1;

    void operator++() { cursor++; }
    bool operator!=(const AllPipIterator &other) const
    {
        return cursor != other.cursor;
    }

    PipId operator*() const
    {
        PipId ret;
        ret.index = cursor;
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

struct PipIterator
{
    int *cursor = nullptr;

    void operator++() { cursor++; }
    bool operator!=(const PipIterator &other) const
    {
        return cursor != other.cursor;
    }

    PipId operator*() const
    {
        PipId ret;
        ret.index = *cursor;
        return ret;
    }
};

struct PipRange
{
    PipIterator b, e;
    PipIterator begin() const { return b; }
    PipIterator end() const { return e; }
};

// -----------------------------------------------------------------------

struct ChipArgs
{
    enum
    {
        NONE,
        LP384,
        LP1K,
        LP8K,
        HX1K,
        HX8K,
        UP5K
    } type = NONE;
};

struct Chip
{
    ChipInfoPOD chip_info;

    mutable dict<IdString, int> bel_by_name;
    mutable dict<IdString, int> wire_by_name;
    mutable dict<IdString, int> pip_by_name;

    vector<IdString> bel_to_cell;
    vector<IdString> wire_to_net;
    vector<IdString> pip_to_net;

    Chip(ChipArgs args);

    // -------------------------------------------------

    BelId getBelByName(IdString name) const;

    IdString getBelName(BelId bel) const
    {
        assert(!bel.nil());
        return chip_info.bel_data[bel.index].name;
    }

    void bindBel(BelId bel, IdString cell)
    {
        assert(!bel.nil());
        assert(bel_to_cell[bel.index] == IdString());
        bel_to_cell[bel.index] = cell;
    }

    void unbindBel(BelId bel)
    {
        assert(!bel.nil());
        assert(bel_to_cell[bel.index] != IdString());
        bel_to_cell[bel.index] = IdString();
    }

    bool checkBelAvail(BelId bel) const
    {
        assert(!bel.nil());
        return bel_to_cell[bel.index] == IdString();
    }

    IdString getBelCell(BelId bel) const
    {
        assert(!bel.nil());
        return bel_to_cell[bel.index];
    }

    BelRange getBels() const
    {
        BelRange range;
        range.b.cursor = 0;
        range.e.cursor = chip_info.num_bels;
        return range;
    }

    BelRange getBelsByType(BelType type) const
    {
        BelRange range;
// FIXME
#if 0
		if (type == "TYPE_A") {
			range.b.cursor = bels_type_a_begin;
			range.e.cursor = bels_type_a_end;
		}
		...
#endif
        return range;
    }

    BelType getBelType(BelId bel) const
    {
        assert(!bel.nil());
        return chip_info.bel_data[bel.index].type;
    }

    WireId getWireBelPin(BelId bel, PortPin pin) const;

    BelPin getBelPinUphill(WireId wire) const
    {
        BelPin ret;
        assert(!wire.nil());

        if (chip_info.wire_data[wire.index].bel_uphill.bel_index >= 0) {
            ret.bel.index =
                    chip_info.wire_data[wire.index].bel_uphill.bel_index;
            ret.pin = chip_info.wire_data[wire.index].bel_uphill.port;
        }

        return ret;
    }

    BelPinRange getBelPinsDownhill(WireId wire) const
    {
        BelPinRange range;
        assert(!wire.nil());
        range.b.ptr = chip_info.wire_data[wire.index].bels_downhill;
        range.e.ptr =
                range.b.ptr + chip_info.wire_data[wire.index].num_bels_downhill;
        return range;
    }

    // -------------------------------------------------

    WireId getWireByName(IdString name) const;

    IdString getWireName(WireId wire) const
    {
        assert(!wire.nil());
        return chip_info.wire_data[wire.index].name;
    }

    void bindWire(WireId wire, IdString net)
    {
        assert(!wire.nil());
        assert(wire_to_net[wire.index] == IdString());
        wire_to_net[wire.index] = net;
    }

    void unbindWire(WireId wire)
    {
        assert(!wire.nil());
        assert(wire_to_net[wire.index] != IdString());
        wire_to_net[wire.index] = IdString();
    }

    bool checkWireAvail(WireId wire) const
    {
        assert(!wire.nil());
        return wire_to_net[wire.index] == IdString();
    }

    IdString getWireNet(WireId wire) const
    {
        assert(!wire.nil());
        return wire_to_net[wire.index];
    }

    WireRange getWires() const
    {
        WireRange range;
        range.b.cursor = 0;
        range.e.cursor = chip_info.num_wires;
        return range;
    }

    // -------------------------------------------------

    PipId getPipByName(IdString name) const;

    IdString getPipName(PipId pip) const
    {
        assert(!pip.nil());
        std::string src_name =
                chip_info.wire_data[chip_info.pip_data[pip.index].src].name;
        std::string dst_name =
                chip_info.wire_data[chip_info.pip_data[pip.index].dst].name;
        return src_name + "->" + dst_name;
    }

    void bindPip(PipId pip, IdString net)
    {
        assert(!pip.nil());
        assert(pip_to_net[pip.index] == IdString());
        pip_to_net[pip.index] = net;
    }

    void unbindPip(PipId pip)
    {
        assert(!pip.nil());
        assert(pip_to_net[pip.index] != IdString());
        pip_to_net[pip.index] = IdString();
    }

    bool checkPipAvail(PipId pip) const
    {
        assert(!pip.nil());
        return pip_to_net[pip.index] == IdString();
    }

    IdString getPipNet(PipId pip) const
    {
        assert(!pip.nil());
        return pip_to_net[pip.index];
    }

    AllPipRange getPips() const
    {
        AllPipRange range;
        range.b.cursor = 0;
        range.e.cursor = chip_info.num_pips;
        return range;
    }

    WireId getPipSrcWire(PipId pip) const
    {
        WireId wire;
        assert(!pip.nil());
        wire.index = chip_info.pip_data[pip.index].src;
        return wire;
    }

    WireId getPipDstWire(PipId pip) const
    {
        WireId wire;
        assert(!pip.nil());
        wire.index = chip_info.pip_data[pip.index].dst;
        return wire;
    }

    DelayInfo getPipDelay(PipId pip) const
    {
        DelayInfo delay;
        assert(!pip.nil());
        delay.delay = chip_info.pip_data[pip.index].delay;
        return delay;
    }

    PipRange getPipsDownhill(WireId wire) const
    {
        PipRange range;
        assert(!wire.nil());
        range.b.cursor = chip_info.wire_data[wire.index].pips_downhill;
        range.e.cursor =
                range.b.cursor + chip_info.wire_data[wire.index].num_downhill;
        return range;
    }

    PipRange getPipsUphill(WireId wire) const
    {
        PipRange range;
        assert(!wire.nil());
        range.b.cursor = chip_info.wire_data[wire.index].pips_uphill;
        range.e.cursor =
                range.b.cursor + chip_info.wire_data[wire.index].num_uphill;
        return range;
    }

    PipRange getWireAliases(WireId wire) const
    {
        PipRange range;
        assert(!wire.nil());
        range.b.cursor = nullptr;
        range.e.cursor = nullptr;
        return range;
    }

    // -------------------------------------------------

    void getBelPosition(BelId bel, float &x, float &y) const;
    void getWirePosition(WireId wire, float &x, float &y) const;
    void getPipPosition(PipId pip, float &x, float &y) const;
    vector<GraphicElement> getBelGraphics(BelId bel) const;
    vector<GraphicElement> getWireGraphics(WireId wire) const;
    vector<GraphicElement> getPipGraphics(PipId pip) const;
    vector<GraphicElement> getFrameGraphics() const;
};

#endif
