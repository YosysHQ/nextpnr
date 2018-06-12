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

#ifndef CHIP_H
#define CHIP_H

#ifndef NEXTPNR_H
#error Include "chip.h" via "nextpnr.h" only.
#endif

NEXTPNR_NAMESPACE_BEGIN

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
    TYPE_NONE,
    TYPE_ICESTORM_LC,
    TYPE_ICESTORM_RAM,
    TYPE_SB_IO,
    TYPE_SB_GB
};

IdString belTypeToId(BelType type);
BelType belTypeFromId(IdString id);

enum PortPin
{
    PIN_NONE,
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
    int16_t switch_mask;
    int32_t switch_index;
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

enum TileType
{
    TILE_NONE = 0,
    TILE_LOGIC = 1,
    TILE_IO = 2,
    TILE_RAMB = 3,
    TILE_RAMT = 4,
};

struct ConfigBitPOD
{
    int8_t row, col;
};

struct ConfigEntryPOD
{
    const char *name;
    int num_bits;
    ConfigBitPOD *bits;
};

struct TileInfoPOD
{
    int8_t cols, rows;
    int num_config_entries;
    ConfigEntryPOD *entries;
};

static const int max_switch_bits = 5;

struct SwitchInfoPOD
{
    int8_t x, y;
    int num_bits;
    ConfigBitPOD cbits[max_switch_bits];
};

struct IerenInfoPOD
{
    int8_t iox, ioy, ioz;
    int8_t ierx, iery, ierz;
};

struct BitstreamInfoPOD
{
    int num_switches, num_ierens;
    TileInfoPOD *tiles_nonrouting;
    SwitchInfoPOD *switches;
    IerenInfoPOD *ierens;
};

struct ChipInfoPOD
{
    int width, height;
    int num_bels, num_wires, num_pips;
    int num_switches;
    BelInfoPOD *bel_data;
    WireInfoPOD *wire_data;
    PipInfoPOD *pip_data;
    TileType *tile_grid;
    BitstreamInfoPOD *bits_info;
};

extern ChipInfoPOD chip_info_384;
extern ChipInfoPOD chip_info_1k;
extern ChipInfoPOD chip_info_5k;
extern ChipInfoPOD chip_info_8k;

// -----------------------------------------------------------------------

struct BelId
{
    int32_t index = -1;

    bool operator==(const BelId &other) const { return index == other.index; }
    bool operator!=(const BelId &other) const { return index != other.index; }
};

struct WireId
{
    int32_t index = -1;

    bool operator==(const WireId &other) const { return index == other.index; }
    bool operator!=(const WireId &other) const { return index != other.index; }
};

struct PipId
{
    int32_t index = -1;

    bool operator==(const PipId &other) const { return index == other.index; }
    bool operator!=(const PipId &other) const { return index != other.index; }
};

struct BelPin
{
    BelId bel;
    PortPin pin;
};

NEXTPNR_NAMESPACE_END

namespace std {
template <> struct hash<NEXTPNR_NAMESPACE_PREFIX BelId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX BelId &bel) const
            noexcept
    {
        return bel.index;
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX WireId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX WireId &wire) const
            noexcept
    {
        return wire.index;
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX PipId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX PipId &wire) const
            noexcept
    {
        return wire.index;
    }
};
} // namespace std

NEXTPNR_NAMESPACE_BEGIN

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

    mutable std::unordered_map<IdString, int> bel_by_name;
    mutable std::unordered_map<IdString, int> wire_by_name;
    mutable std::unordered_map<IdString, int> pip_by_name;

    std::vector<IdString> bel_to_cell;
    std::vector<IdString> wire_to_net;
    std::vector<IdString> pip_to_net;
    std::vector<bool> switches_locked;
    Chip(ChipArgs args);

    ChipArgs args;
    // -------------------------------------------------

    std::string getChipName();

    BelId getBelByName(IdString name) const;

    IdString getBelName(BelId bel) const
    {
        assert(bel != BelId());
        return chip_info.bel_data[bel.index].name;
    }

    void bindBel(BelId bel, IdString cell)
    {
        assert(bel != BelId());
        assert(bel_to_cell[bel.index] == IdString());
        bel_to_cell[bel.index] = cell;
    }

    void unbindBel(BelId bel)
    {
        assert(bel != BelId());
        assert(bel_to_cell[bel.index] != IdString());
        bel_to_cell[bel.index] = IdString();
    }

    bool checkBelAvail(BelId bel) const
    {
        assert(bel != BelId());
        return bel_to_cell[bel.index] == IdString();
    }

    IdString getBelCell(BelId bel, bool conflicting = false) const
    {
        assert(bel != BelId());
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

    BelRange getBelsAtSameTile(BelId bel) const;

    BelType getBelType(BelId bel) const
    {
        assert(bel != BelId());
        return chip_info.bel_data[bel.index].type;
    }

    WireId getWireBelPin(BelId bel, PortPin pin) const;

    BelPin getBelPinUphill(WireId wire) const
    {
        BelPin ret;
        assert(wire != WireId());

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
        assert(wire != WireId());
        range.b.ptr = chip_info.wire_data[wire.index].bels_downhill;
        range.e.ptr =
                range.b.ptr + chip_info.wire_data[wire.index].num_bels_downhill;
        return range;
    }

    // -------------------------------------------------

    WireId getWireByName(IdString name) const;

    IdString getWireName(WireId wire) const
    {
        assert(wire != WireId());
        return chip_info.wire_data[wire.index].name;
    }

    void bindWire(WireId wire, IdString net)
    {
        assert(wire != WireId());
        assert(wire_to_net[wire.index] == IdString());
        wire_to_net[wire.index] = net;
    }

    void unbindWire(WireId wire)
    {
        assert(wire != WireId());
        assert(wire_to_net[wire.index] != IdString());
        wire_to_net[wire.index] = IdString();
    }

    bool checkWireAvail(WireId wire) const
    {
        assert(wire != WireId());
        return wire_to_net[wire.index] == IdString();
    }

    IdString getWireNet(WireId wire, bool conflicting = false) const
    {
        assert(wire != WireId());
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
        assert(pip != PipId());
        std::string src_name =
                chip_info.wire_data[chip_info.pip_data[pip.index].src].name;
        std::string dst_name =
                chip_info.wire_data[chip_info.pip_data[pip.index].dst].name;
        return src_name + "->" + dst_name;
    }

    void bindPip(PipId pip, IdString net)
    {
        assert(pip != PipId());
        assert(pip_to_net[pip.index] == IdString());
        assert(!switches_locked[chip_info.pip_data[pip.index].switch_index]);
        pip_to_net[pip.index] = net;
        switches_locked[chip_info.pip_data[pip.index].switch_index] = true;
    }

    void unbindPip(PipId pip)
    {
        assert(pip != PipId());
        assert(pip_to_net[pip.index] != IdString());
        assert(switches_locked[chip_info.pip_data[pip.index].switch_index]);
        pip_to_net[pip.index] = IdString();
        switches_locked[chip_info.pip_data[pip.index].switch_index] = false;
    }

    bool checkPipAvail(PipId pip) const
    {
        assert(pip != PipId());
        if (args.type == ChipArgs::UP5K) {
            int x = chip_info.pip_data[pip.index].x;
            if (x == 0 || x == (chip_info.width - 1))
                return false;
        }
        return !switches_locked[chip_info.pip_data[pip.index].switch_index];
    }

    IdString getPipNet(PipId pip, bool conflicting = false) const
    {
        assert(pip != PipId());
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
        assert(pip != PipId());
        wire.index = chip_info.pip_data[pip.index].src;
        return wire;
    }

    WireId getPipDstWire(PipId pip) const
    {
        WireId wire;
        assert(pip != PipId());
        wire.index = chip_info.pip_data[pip.index].dst;
        return wire;
    }

    DelayInfo getPipDelay(PipId pip) const
    {
        DelayInfo delay;
        assert(pip != PipId());
        delay.delay = chip_info.pip_data[pip.index].delay;
        return delay;
    }

    PipRange getPipsDownhill(WireId wire) const
    {
        PipRange range;
        assert(wire != WireId());
        range.b.cursor = chip_info.wire_data[wire.index].pips_downhill;
        range.e.cursor =
                range.b.cursor + chip_info.wire_data[wire.index].num_downhill;
        return range;
    }

    PipRange getPipsUphill(WireId wire) const
    {
        PipRange range;
        assert(wire != WireId());
        range.b.cursor = chip_info.wire_data[wire.index].pips_uphill;
        range.e.cursor =
                range.b.cursor + chip_info.wire_data[wire.index].num_uphill;
        return range;
    }

    PipRange getWireAliases(WireId wire) const
    {
        PipRange range;
        assert(wire != WireId());
        range.b.cursor = nullptr;
        range.e.cursor = nullptr;
        return range;
    }

    // -------------------------------------------------

    void getBelPosition(BelId bel, float &x, float &y) const;
    void getWirePosition(WireId wire, float &x, float &y) const;
    void getPipPosition(PipId pip, float &x, float &y) const;

    std::vector<GraphicElement> getBelGraphics(BelId bel) const;
    std::vector<GraphicElement> getWireGraphics(WireId wire) const;
    std::vector<GraphicElement> getPipGraphics(PipId pip) const;
    std::vector<GraphicElement> getFrameGraphics() const;
};

NEXTPNR_NAMESPACE_END

#endif
