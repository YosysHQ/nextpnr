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

#ifndef NEXTPNR_H
#error Include "arch.h" via "nextpnr.h" only.
#endif

#ifdef NEXTPNR_ARCH_TOP

NEXTPNR_NAMESPACE_BEGIN

typedef int delay_t;

struct DelayInfo
{
    delay_t delay = 0;

    delay_t raiseDelay() const { return delay; }
    delay_t fallDelay() const { return delay; }
    delay_t avgDelay() const { return delay; }

    DelayInfo operator+(const DelayInfo &other) const
    {
        DelayInfo ret;
        ret.delay = this->delay + other.delay;
        return ret;
    }
};

// -----------------------------------------------------------------------

enum BelType : int32_t
{
    TYPE_NONE,
    TYPE_ICESTORM_LC,
    TYPE_ICESTORM_RAM,
    TYPE_SB_IO,
    TYPE_SB_GB
};

enum PortPin : int32_t
{
    PIN_NONE,
#define X(t) PIN_##t,
#include "portpins.inc"
#undef X
    PIN_MAXIDX
};

enum WireType : int8_t
{
    WIRE_TYPE_NONE = 0,
    WIRE_TYPE_LOCAL = 1,
    WIRE_TYPE_GLOBAL = 2,
    WIRE_TYPE_SP4_VERT = 3,
    WIRE_TYPE_SP4_HORZ = 4,
    WIRE_TYPE_SP12_HORZ = 5,
    WIRE_TYPE_SP12_VERT = 6
};

// -----------------------------------------------------------------------

/**** Everything in this section must be kept in sync with chipdb.py ****/

template <typename T> struct RelPtr
{
    int32_t offset;

    // void set(const T *ptr) {
    //     offset = reinterpret_cast<const char*>(ptr) -
    //              reinterpret_cast<const char*>(this);
    // }

    const T *get() const
    {
        return reinterpret_cast<const T *>(
                reinterpret_cast<const char *>(this) + offset);
    }

    const T &operator[](size_t index) const { return get()[index]; }

    const T &operator*() const { return *(get()); }

    const T *operator->() const { return get(); }
};

struct BelWirePOD
{
    int32_t wire_index;
    PortPin port;
} __attribute__((packed));

struct BelInfoPOD
{
    RelPtr<char> name;
    BelType type;
    int32_t num_bel_wires;
    RelPtr<BelWirePOD> bel_wires;
    int8_t x, y, z;
    int8_t padding_0;
} __attribute__((packed));

struct BelPortPOD
{
    int32_t bel_index;
    PortPin port;
} __attribute__((packed));

struct PipInfoPOD
{
    int32_t src, dst;
    int32_t delay;
    int8_t x, y;
    int16_t switch_mask;
    int32_t switch_index;
} __attribute__((packed));

struct WireInfoPOD
{
    RelPtr<char> name;
    int32_t num_uphill, num_downhill;
    RelPtr<int32_t> pips_uphill, pips_downhill;

    int32_t num_bels_downhill;
    BelPortPOD bel_uphill;
    RelPtr<BelPortPOD> bels_downhill;

    int8_t x, y;
    WireType type;
    int8_t padding_0;
} __attribute__((packed));

struct PackagePinPOD
{
    RelPtr<char> name;
    int32_t bel_index;
} __attribute__((packed));

struct PackageInfoPOD
{
    RelPtr<char> name;
    int32_t num_pins;
    RelPtr<PackagePinPOD> pins;
} __attribute__((packed));

enum TileType : uint32_t
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
} __attribute__((packed));

struct ConfigEntryPOD
{
    RelPtr<char> name;
    int32_t num_bits;
    RelPtr<ConfigBitPOD> bits;
} __attribute__((packed));

struct TileInfoPOD
{
    int8_t cols, rows;
    int16_t num_config_entries;
    RelPtr<ConfigEntryPOD> entries;
} __attribute__((packed));

static const int max_switch_bits = 5;

struct SwitchInfoPOD
{
    int32_t num_bits;
    int8_t x, y;
    ConfigBitPOD cbits[max_switch_bits];
} __attribute__((packed));

struct IerenInfoPOD
{
    int8_t iox, ioy, ioz;
    int8_t ierx, iery, ierz;
} __attribute__((packed));

struct BitstreamInfoPOD
{
    int32_t num_switches, num_ierens;
    RelPtr<TileInfoPOD> tiles_nonrouting;
    RelPtr<SwitchInfoPOD> switches;
    RelPtr<IerenInfoPOD> ierens;
} __attribute__((packed));

struct ChipInfoPOD
{
    int32_t width, height;
    int32_t num_bels, num_wires, num_pips;
    int32_t num_switches, num_packages;
    RelPtr<BelInfoPOD> bel_data;
    RelPtr<WireInfoPOD> wire_data;
    RelPtr<PipInfoPOD> pip_data;
    RelPtr<TileType> tile_grid;
    RelPtr<BitstreamInfoPOD> bits_info;
    RelPtr<PackageInfoPOD> packages_data;
} __attribute__((packed));

extern const char chipdb_blob_384[];
extern const char chipdb_blob_1k[];
extern const char chipdb_blob_5k[];
extern const char chipdb_blob_8k[];

/************************ End of chipdb section. ************************/

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
        return hash<int>()(bel.index);
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX WireId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX WireId &wire) const
            noexcept
    {
        return hash<int>()(wire.index);
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX PipId>
{
    std::size_t operator()(const NEXTPNR_NAMESPACE_PREFIX PipId &pip) const
            noexcept
    {
        return hash<int>()(pip.index);
    }
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX BelType> : hash<int>
{
};

template <> struct hash<NEXTPNR_NAMESPACE_PREFIX PortPin> : hash<int>
{
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
    const BelPortPOD *ptr = nullptr;

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
    const int *cursor = nullptr;

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

NEXTPNR_NAMESPACE_END

#endif // NEXTPNR_ARCH_TOP

// -----------------------------------------------------------------------

#ifdef NEXTPNR_ARCH_BOTTOM

NEXTPNR_NAMESPACE_BEGIN

struct ArchArgs
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
    std::string package;
};

struct Arch : BaseCtx
{
    const ChipInfoPOD *chip_info;
    const PackageInfoPOD *package_info;

    mutable std::unordered_map<IdString, int> bel_by_name;
    mutable std::unordered_map<IdString, int> wire_by_name;
    mutable std::unordered_map<IdString, int> pip_by_name;

    std::vector<IdString> bel_to_cell;
    std::vector<IdString> wire_to_net;
    std::vector<IdString> pip_to_net;
    std::vector<IdString> switches_locked;

    ArchArgs args;
    Arch(ArchArgs args);

    std::string getChipName();

    IdString belTypeToId(BelType type) const;
    BelType belTypeFromId(IdString id) const;

    IdString portPinToId(PortPin type) const;
    PortPin portPinFromId(IdString id) const;

    // -------------------------------------------------

    BelId getBelByName(IdString name) const;

    IdString getBelName(BelId bel) const
    {
        assert(bel != BelId());
        return id(chip_info->bel_data[bel.index].name.get());
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
        range.e.cursor = chip_info->num_bels;
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
        return chip_info->bel_data[bel.index].type;
    }

    WireId getWireBelPin(BelId bel, PortPin pin) const;

    BelPin getBelPinUphill(WireId wire) const
    {
        BelPin ret;
        assert(wire != WireId());

        if (chip_info->wire_data[wire.index].bel_uphill.bel_index >= 0) {
            ret.bel.index =
                    chip_info->wire_data[wire.index].bel_uphill.bel_index;
            ret.pin = chip_info->wire_data[wire.index].bel_uphill.port;
        }

        return ret;
    }

    BelPinRange getBelPinsDownhill(WireId wire) const
    {
        BelPinRange range;
        assert(wire != WireId());
        range.b.ptr = chip_info->wire_data[wire.index].bels_downhill.get();
        range.e.ptr = range.b.ptr +
                      chip_info->wire_data[wire.index].num_bels_downhill;
        return range;
    }

    // -------------------------------------------------

    WireId getWireByName(IdString name) const;

    IdString getWireName(WireId wire) const
    {
        assert(wire != WireId());
        return id(chip_info->wire_data[wire.index].name.get());
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
        range.e.cursor = chip_info->num_wires;
        return range;
    }

    // -------------------------------------------------

    PipId getPipByName(IdString name) const;
    IdString getPipName(PipId pip) const;

    void bindPip(PipId pip, IdString net)
    {
        assert(pip != PipId());
        assert(pip_to_net[pip.index] == IdString());
        assert(switches_locked[chip_info->pip_data[pip.index].switch_index] ==
               IdString());
        pip_to_net[pip.index] = net;
        switches_locked[chip_info->pip_data[pip.index].switch_index] = net;
    }

    void unbindPip(PipId pip)
    {
        assert(pip != PipId());
        assert(pip_to_net[pip.index] != IdString());
        assert(switches_locked[chip_info->pip_data[pip.index].switch_index] !=
               IdString());
        pip_to_net[pip.index] = IdString();
        switches_locked[chip_info->pip_data[pip.index].switch_index] =
                IdString();
    }

    bool checkPipAvail(PipId pip) const
    {
        assert(pip != PipId());
        if (args.type == ArchArgs::UP5K) {
            int x = chip_info->pip_data[pip.index].x;
            if (x == 0 || x == (chip_info->width - 1))
                return false;
        }
        return switches_locked[chip_info->pip_data[pip.index].switch_index] ==
               IdString();
    }

    IdString getPipNet(PipId pip, bool conflicting = false) const
    {
        assert(pip != PipId());
        if (conflicting)
            return switches_locked[chip_info->pip_data[pip.index].switch_index];
        return pip_to_net[pip.index];
    }

    AllPipRange getPips() const
    {
        AllPipRange range;
        range.b.cursor = 0;
        range.e.cursor = chip_info->num_pips;
        return range;
    }

    WireId getPipSrcWire(PipId pip) const
    {
        WireId wire;
        assert(pip != PipId());
        wire.index = chip_info->pip_data[pip.index].src;
        return wire;
    }

    WireId getPipDstWire(PipId pip) const
    {
        WireId wire;
        assert(pip != PipId());
        wire.index = chip_info->pip_data[pip.index].dst;
        return wire;
    }

    DelayInfo getPipDelay(PipId pip) const
    {
        DelayInfo delay;
        assert(pip != PipId());
        delay.delay = chip_info->pip_data[pip.index].delay;
        return delay;
    }

    PipRange getPipsDownhill(WireId wire) const
    {
        PipRange range;
        assert(wire != WireId());
        range.b.cursor = chip_info->wire_data[wire.index].pips_downhill.get();
        range.e.cursor =
                range.b.cursor + chip_info->wire_data[wire.index].num_downhill;
        return range;
    }

    PipRange getPipsUphill(WireId wire) const
    {
        PipRange range;
        assert(wire != WireId());
        range.b.cursor = chip_info->wire_data[wire.index].pips_uphill.get();
        range.e.cursor =
                range.b.cursor + chip_info->wire_data[wire.index].num_uphill;
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

    BelId getPackagePinBel(const std::string &pin) const;
    std::string getBelPackagePin(BelId bel) const;

    // -------------------------------------------------

    bool estimatePosition(BelId bel, int &x, int &y) const;
    delay_t estimateDelay(WireId src, WireId dst) const;
    delay_t getDelayEpsilon() const { return 10; }
    float getDelayNS(delay_t v) const { return v * 0.001; }

    // -------------------------------------------------

    std::vector<GraphicElement> getFrameGraphics() const;
    std::vector<GraphicElement> getBelGraphics(BelId bel) const;
    std::vector<GraphicElement> getWireGraphics(WireId wire) const;
    std::vector<GraphicElement> getPipGraphics(PipId pip) const;

    bool allGraphicsReload = false;
    bool frameGraphicsReload = false;
    std::unordered_set<BelId> belGraphicsReload;
    std::unordered_set<WireId> wireGraphicsReload;
    std::unordered_set<PipId> pipGraphicsReload;
};

NEXTPNR_NAMESPACE_END

#endif // NEXTPNR_ARCH_BOTTOM
