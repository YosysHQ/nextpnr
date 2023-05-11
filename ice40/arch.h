/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
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

#ifndef ICE40_ARCH_H
#define ICE40_ARCH_H

#include <cstdint>

#include "base_arch.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"
#include "relptr.h"

NEXTPNR_NAMESPACE_BEGIN

/**** Everything in this section must be kept in sync with chipdb.py ****/

NPNR_PACKED_STRUCT(struct BelWirePOD {
    int32_t port;
    int32_t type;
    int32_t wire_index;
});

NPNR_PACKED_STRUCT(struct BelInfoPOD {
    RelPtr<char> name;
    int32_t type;
    RelSlice<BelWirePOD> bel_wires;
    int8_t x, y, z;
    int8_t padding_0;
});

NPNR_PACKED_STRUCT(struct BelPortPOD {
    int32_t bel_index;
    int32_t port;
});

NPNR_PACKED_STRUCT(struct PipInfoPOD {
    enum PipFlags : uint32_t
    {
        FLAG_NONE = 0,
        FLAG_ROUTETHRU = 1,
        FLAG_NOCARRY = 2
    };

    // RelPtr<char> name;
    int32_t src, dst;
    int32_t fast_delay;
    int32_t slow_delay;
    int8_t x, y;
    int16_t src_seg, dst_seg;
    int16_t switch_mask;
    int32_t switch_index;
    PipFlags flags;
});

NPNR_PACKED_STRUCT(struct WireSegmentPOD {
    int8_t x, y;
    int16_t index;
});

NPNR_PACKED_STRUCT(struct WireInfoPOD {
    enum WireType : int8_t
    {
        WIRE_TYPE_NONE = 0,
        WIRE_TYPE_GLB2LOCAL = 1,
        WIRE_TYPE_GLB_NETWK = 2,
        WIRE_TYPE_LOCAL = 3,
        WIRE_TYPE_LUTFF_IN = 4,
        WIRE_TYPE_LUTFF_IN_LUT = 5,
        WIRE_TYPE_LUTFF_LOUT = 6,
        WIRE_TYPE_LUTFF_OUT = 7,
        WIRE_TYPE_LUTFF_COUT = 8,
        WIRE_TYPE_LUTFF_GLOBAL = 9,
        WIRE_TYPE_CARRY_IN_MUX = 10,
        WIRE_TYPE_SP4_V = 11,
        WIRE_TYPE_SP4_H = 12,
        WIRE_TYPE_SP12_V = 13,
        WIRE_TYPE_SP12_H = 14
    };

    RelPtr<char> name;
    int8_t name_x, name_y;
    int16_t padding;
    RelSlice<int32_t> pips_uphill, pips_downhill;

    RelSlice<BelPortPOD> bel_pins;

    RelSlice<WireSegmentPOD> segments;

    int32_t fast_delay;
    int32_t slow_delay;

    int8_t x, y, z;
    WireType type;
});

NPNR_PACKED_STRUCT(struct PackagePinPOD {
    RelPtr<char> name;
    int32_t bel_index;
});

NPNR_PACKED_STRUCT(struct PackageInfoPOD {
    RelPtr<char> name;
    RelSlice<PackagePinPOD> pins;
});

enum TileType : uint32_t
{
    TILE_NONE = 0,
    TILE_LOGIC = 1,
    TILE_IO = 2,
    TILE_RAMB = 3,
    TILE_RAMT = 4,
    TILE_DSP0 = 5,
    TILE_DSP1 = 6,
    TILE_DSP2 = 7,
    TILE_DSP3 = 8,
    TILE_IPCON = 9
};

NPNR_PACKED_STRUCT(struct ConfigBitPOD { int8_t row, col; });

NPNR_PACKED_STRUCT(struct ConfigEntryPOD {
    RelPtr<char> name;
    RelSlice<ConfigBitPOD> bits;
});

NPNR_PACKED_STRUCT(struct TileInfoPOD {
    int8_t cols, rows;
    int16_t padding;
    RelSlice<ConfigEntryPOD> entries;
});

static const int max_switch_bits = 5;

NPNR_PACKED_STRUCT(struct SwitchInfoPOD {
    int32_t num_bits;
    int32_t bel;
    int8_t x, y;
    ConfigBitPOD cbits[max_switch_bits];
});

NPNR_PACKED_STRUCT(struct IerenInfoPOD {
    int8_t iox, ioy, ioz;
    int8_t ierx, iery, ierz;
});

NPNR_PACKED_STRUCT(struct BitstreamInfoPOD {
    RelSlice<TileInfoPOD> tiles_nonrouting;
    RelSlice<SwitchInfoPOD> switches;
    RelSlice<IerenInfoPOD> ierens;
});

NPNR_PACKED_STRUCT(struct BelConfigEntryPOD {
    RelPtr<char> entry_name;
    RelPtr<char> cbit_name;
    int8_t x, y;
    int16_t padding;
});

// Stores mapping between bel parameters and config bits,
// for extra cells where this mapping is non-trivial
NPNR_PACKED_STRUCT(struct BelConfigPOD {
    int32_t bel_index;
    RelSlice<BelConfigEntryPOD> entries;
});

NPNR_PACKED_STRUCT(struct CellPathDelayPOD {
    int32_t from_port;
    int32_t to_port;
    int32_t fast_delay;
    int32_t slow_delay;
});

NPNR_PACKED_STRUCT(struct CellTimingPOD {
    int32_t type;
    RelSlice<CellPathDelayPOD> path_delays;
});

NPNR_PACKED_STRUCT(struct GlobalNetworkInfoPOD {
    uint8_t gb_x;
    uint8_t gb_y;

    uint8_t pi_gb_x;
    uint8_t pi_gb_y;
    uint8_t pi_gb_pio;

    uint8_t pi_eb_bank;
    uint16_t pi_eb_x;
    uint16_t pi_eb_y;

    uint16_t pad;
});

NPNR_PACKED_STRUCT(struct ChipInfoPOD {
    int32_t width, height;
    uint32_t num_switches;
    RelSlice<BelInfoPOD> bel_data;
    RelSlice<WireInfoPOD> wire_data;
    RelSlice<PipInfoPOD> pip_data;
    RelSlice<TileType> tile_grid;
    RelPtr<BitstreamInfoPOD> bits_info;
    RelSlice<BelConfigPOD> bel_config;
    RelSlice<PackageInfoPOD> packages_data;
    RelSlice<CellTimingPOD> cell_timing;
    RelSlice<GlobalNetworkInfoPOD> global_network_info;
    RelSlice<RelPtr<char>> tile_wire_names;
});

/************************ End of chipdb section. ************************/

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

    bool operator!=(const BelIterator &other) const { return cursor != other.cursor; }

    bool operator==(const BelIterator &other) const { return cursor == other.cursor; }

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
    bool operator!=(const BelPinIterator &other) const { return ptr != other.ptr; }

    BelPin operator*() const
    {
        BelPin ret;
        ret.bel.index = ptr->bel_index;
        ret.pin = IdString(ptr->port);
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
    bool operator!=(const WireIterator &other) const { return cursor != other.cursor; }

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
    bool operator!=(const AllPipIterator &other) const { return cursor != other.cursor; }

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
    bool operator!=(const PipIterator &other) const { return cursor != other.cursor; }

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

struct ArchArgs
{
    enum ArchArgsTypes
    {
        NONE,
        LP384,
        LP1K,
        LP4K,
        LP8K,
        HX1K,
        HX4K,
        HX8K,
        UP3K,
        UP5K,
        U1K,
        U2K,
        U4K
    } type = NONE;
    std::string package;
};

struct ArchRanges : BaseArchRanges
{
    using ArchArgsT = ArchArgs;
    // Bels
    using AllBelsRangeT = BelRange;
    using TileBelsRangeT = BelRange;
    using BelPinsRangeT = std::vector<IdString>;
    // Wires
    using AllWiresRangeT = WireRange;
    using DownhillPipRangeT = PipRange;
    using UphillPipRangeT = PipRange;
    using WireBelPinRangeT = BelPinRange;
    // Pips
    using AllPipsRangeT = AllPipRange;
};

struct Arch : BaseArch<ArchRanges>
{
    bool fast_part;
    const ChipInfoPOD *chip_info;
    const PackageInfoPOD *package_info;

    mutable dict<IdStringList, int> bel_by_name;
    mutable dict<IdStringList, int> wire_by_name;
    mutable dict<IdStringList, int> pip_by_name;
    mutable dict<Loc, int> bel_by_loc;

    std::vector<bool> bel_carry;
    std::vector<CellInfo *> bel_to_cell;
    std::vector<NetInfo *> wire_to_net;
    std::vector<NetInfo *> pip_to_net;
    std::vector<WireId> switches_locked;

    // fast access to  X and Y IdStrings for building object names
    std::vector<IdString> x_ids, y_ids;
    // inverse of the above for name->object mapping
    dict<IdString, int> id_to_x, id_to_y;

    ArchArgs args;
    Arch(ArchArgs args);

    static bool is_available(ArchArgs::ArchArgsTypes chip);
    static std::vector<std::string> get_supported_packages(ArchArgs::ArchArgsTypes chip);

    std::string getChipName() const override;

    ArchArgs archArgs() const override { return args; }
    IdString archArgsToId(ArchArgs args) const override;

    // -------------------------------------------------

    int getGridDimX() const override { return chip_info->width; }
    int getGridDimY() const override { return chip_info->height; }
    int getTileBelDimZ(int, int) const override { return 8; }
    int getTilePipDimZ(int, int) const override { return 1; }
    char getNameDelimiter() const override { return '/'; }

    // -------------------------------------------------

    BelId getBelByName(IdStringList name) const override;

    IdStringList getBelName(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        auto &data = chip_info->bel_data[bel.index];
        std::array<IdString, 3> ids{x_ids.at(data.x), y_ids.at(data.y), id(data.name.get())};
        return IdStringList(ids);
    }

    void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength) override
    {
        NPNR_ASSERT(bel != BelId());
        NPNR_ASSERT(bel_to_cell[bel.index] == nullptr);

        bel_to_cell[bel.index] = cell;
        bel_carry[bel.index] = (cell->type == id_ICESTORM_LC && cell->lcInfo.carryEnable);
        cell->bel = bel;
        cell->belStrength = strength;
        refreshUiBel(bel);
    }

    void unbindBel(BelId bel) override
    {
        NPNR_ASSERT(bel != BelId());
        NPNR_ASSERT(bel_to_cell[bel.index] != nullptr);
        bel_to_cell[bel.index]->bel = BelId();
        bel_to_cell[bel.index]->belStrength = STRENGTH_NONE;
        bel_to_cell[bel.index] = nullptr;
        bel_carry[bel.index] = false;
        refreshUiBel(bel);
    }

    bool checkBelAvail(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        return bel_to_cell[bel.index] == nullptr;
    }

    CellInfo *getBoundBelCell(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        return bel_to_cell[bel.index];
    }

    CellInfo *getConflictingBelCell(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        return bel_to_cell[bel.index];
    }

    BelRange getBels() const override
    {
        BelRange range;
        range.b.cursor = 0;
        range.e.cursor = chip_info->bel_data.size();
        return range;
    }

    Loc getBelLocation(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        Loc loc;
        loc.x = chip_info->bel_data[bel.index].x;
        loc.y = chip_info->bel_data[bel.index].y;
        loc.z = chip_info->bel_data[bel.index].z;
        return loc;
    }

    BelId getBelByLocation(Loc loc) const override;
    BelRange getBelsByTile(int x, int y) const override;

    bool getBelGlobalBuf(BelId bel) const override { return chip_info->bel_data[bel.index].type == ID_SB_GB; }

    IdString getBelType(BelId bel) const override
    {
        NPNR_ASSERT(bel != BelId());
        return IdString(chip_info->bel_data[bel.index].type);
    }

    std::vector<std::pair<IdString, std::string>> getBelAttrs(BelId bel) const override;

    WireId getBelPinWire(BelId bel, IdString pin) const override;
    PortType getBelPinType(BelId bel, IdString pin) const override;
    std::vector<IdString> getBelPins(BelId bel) const override;

    bool is_bel_locked(BelId bel) const;

    // -------------------------------------------------

    WireId getWireByName(IdStringList name) const override;

    IdStringList getWireName(WireId wire) const override
    {
        NPNR_ASSERT(wire != WireId());
        auto &data = chip_info->wire_data[wire.index];
        std::array<IdString, 3> ids{x_ids.at(data.name_x), y_ids.at(data.name_y), id(data.name.get())};
        return IdStringList(ids);
    }

    IdString getWireType(WireId wire) const override;
    std::vector<std::pair<IdString, std::string>> getWireAttrs(WireId wire) const override;

    void bindWire(WireId wire, NetInfo *net, PlaceStrength strength) override
    {
        NPNR_ASSERT(wire != WireId());
        NPNR_ASSERT(wire_to_net[wire.index] == nullptr);
        wire_to_net[wire.index] = net;
        net->wires[wire].pip = PipId();
        net->wires[wire].strength = strength;
        refreshUiWire(wire);
    }

    void unbindWire(WireId wire) override
    {
        NPNR_ASSERT(wire != WireId());
        NPNR_ASSERT(wire_to_net[wire.index] != nullptr);

        auto &net_wires = wire_to_net[wire.index]->wires;
        auto it = net_wires.find(wire);
        NPNR_ASSERT(it != net_wires.end());

        auto pip = it->second.pip;
        if (pip != PipId()) {
            pip_to_net[pip.index] = nullptr;
            switches_locked[chip_info->pip_data[pip.index].switch_index] = WireId();
        }

        net_wires.erase(it);
        wire_to_net[wire.index] = nullptr;
        refreshUiWire(wire);
    }

    bool checkWireAvail(WireId wire) const override
    {
        NPNR_ASSERT(wire != WireId());
        return wire_to_net[wire.index] == nullptr;
    }

    NetInfo *getBoundWireNet(WireId wire) const override
    {
        NPNR_ASSERT(wire != WireId());
        return wire_to_net[wire.index];
    }

    DelayQuad getWireDelay(WireId wire) const override
    {
        NPNR_ASSERT(wire != WireId());
        if (fast_part)
            return DelayQuad(chip_info->wire_data[wire.index].fast_delay);
        else
            return DelayQuad(chip_info->wire_data[wire.index].slow_delay);
    }

    BelPinRange getWireBelPins(WireId wire) const override
    {
        BelPinRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.ptr = chip_info->wire_data[wire.index].bel_pins.get();
        range.e.ptr = range.b.ptr + chip_info->wire_data[wire.index].bel_pins.size();
        return range;
    }

    WireRange getWires() const override
    {
        WireRange range;
        range.b.cursor = 0;
        range.e.cursor = chip_info->wire_data.size();
        return range;
    }

    // -------------------------------------------------

    PipId getPipByName(IdStringList name) const override;

    void bindPip(PipId pip, NetInfo *net, PlaceStrength strength) override
    {
        NPNR_ASSERT(pip != PipId());
        NPNR_ASSERT(pip_to_net[pip.index] == nullptr);
        NPNR_ASSERT(switches_locked[chip_info->pip_data[pip.index].switch_index] == WireId());

        WireId dst;
        dst.index = chip_info->pip_data[pip.index].dst;
        NPNR_ASSERT(wire_to_net[dst.index] == nullptr);

        pip_to_net[pip.index] = net;
        switches_locked[chip_info->pip_data[pip.index].switch_index] = dst;

        wire_to_net[dst.index] = net;
        net->wires[dst].pip = pip;
        net->wires[dst].strength = strength;
        refreshUiPip(pip);
        refreshUiWire(dst);
    }

    void unbindPip(PipId pip) override
    {
        NPNR_ASSERT(pip != PipId());
        NPNR_ASSERT(pip_to_net[pip.index] != nullptr);
        NPNR_ASSERT(switches_locked[chip_info->pip_data[pip.index].switch_index] != WireId());

        WireId dst;
        dst.index = chip_info->pip_data[pip.index].dst;
        NPNR_ASSERT(wire_to_net[dst.index] != nullptr);
        wire_to_net[dst.index] = nullptr;
        pip_to_net[pip.index]->wires.erase(dst);

        pip_to_net[pip.index] = nullptr;
        switches_locked[chip_info->pip_data[pip.index].switch_index] = WireId();
        refreshUiPip(pip);
        refreshUiWire(dst);
    }

    bool ice40_pip_hard_unavail(PipId pip) const
    {
        NPNR_ASSERT(pip != PipId());
        auto &pi = chip_info->pip_data[pip.index];
        auto &si = chip_info->bits_info->switches[pi.switch_index];

        if (pi.flags & PipInfoPOD::FLAG_ROUTETHRU) {
            NPNR_ASSERT(si.bel >= 0);
            if (bel_to_cell[si.bel] != nullptr)
                return true;
        }

        if (pi.flags & PipInfoPOD::FLAG_NOCARRY) {
            NPNR_ASSERT(si.bel >= 0);
            if (bel_carry[si.bel])
                return true;
        }

        return false;
    }

    bool checkPipAvail(PipId pip) const override
    {
        if (ice40_pip_hard_unavail(pip))
            return false;

        auto &pi = chip_info->pip_data[pip.index];
        return switches_locked[pi.switch_index] == WireId();
    }

    bool checkPipAvailForNet(PipId pip, const NetInfo *net) const override
    {
        if (ice40_pip_hard_unavail(pip))
            return false;

        auto &pi = chip_info->pip_data[pip.index];
        auto swl = switches_locked[pi.switch_index];
        return swl == WireId() || (swl == getPipDstWire(pip) && wire_to_net[swl.index] == net);
    }

    NetInfo *getBoundPipNet(PipId pip) const override
    {
        NPNR_ASSERT(pip != PipId());
        return pip_to_net[pip.index];
    }

    WireId getConflictingPipWire(PipId pip) const override
    {
        if (ice40_pip_hard_unavail(pip))
            return WireId();

        return switches_locked[chip_info->pip_data[pip.index].switch_index];
    }

    NetInfo *getConflictingPipNet(PipId pip) const override
    {
        if (ice40_pip_hard_unavail(pip))
            return nullptr;

        WireId wire = switches_locked[chip_info->pip_data[pip.index].switch_index];
        return wire == WireId() ? nullptr : wire_to_net[wire.index];
    }

    AllPipRange getPips() const override
    {
        AllPipRange range;
        range.b.cursor = 0;
        range.e.cursor = chip_info->pip_data.size();
        return range;
    }

    Loc getPipLocation(PipId pip) const override
    {
        Loc loc;
        loc.x = chip_info->pip_data[pip.index].x;
        loc.y = chip_info->pip_data[pip.index].y;
        loc.z = 0;
        return loc;
    }

    IdStringList getPipName(PipId pip) const override;

    IdString getPipType(PipId pip) const override;
    std::vector<std::pair<IdString, std::string>> getPipAttrs(PipId pip) const override;

    WireId getPipSrcWire(PipId pip) const override
    {
        WireId wire;
        NPNR_ASSERT(pip != PipId());
        wire.index = chip_info->pip_data[pip.index].src;
        return wire;
    }

    WireId getPipDstWire(PipId pip) const override
    {
        WireId wire;
        NPNR_ASSERT(pip != PipId());
        wire.index = chip_info->pip_data[pip.index].dst;
        return wire;
    }

    DelayQuad getPipDelay(PipId pip) const override
    {
        NPNR_ASSERT(pip != PipId());
        if (fast_part)
            return DelayQuad(chip_info->pip_data[pip.index].fast_delay);
        else
            return DelayQuad(chip_info->pip_data[pip.index].slow_delay);
    }

    PipRange getPipsDownhill(WireId wire) const override
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = chip_info->wire_data[wire.index].pips_downhill.get();
        range.e.cursor = range.b.cursor + chip_info->wire_data[wire.index].pips_downhill.size();
        return range;
    }

    PipRange getPipsUphill(WireId wire) const override
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = chip_info->wire_data[wire.index].pips_uphill.get();
        range.e.cursor = range.b.cursor + chip_info->wire_data[wire.index].pips_uphill.size();
        return range;
    }

    BelId get_package_pin_bel(const std::string &pin) const;
    std::string get_bel_package_pin(BelId bel) const;

    // -------------------------------------------------

    GroupId getGroupByName(IdStringList name) const override;
    IdStringList getGroupName(GroupId group) const override;
    std::vector<GroupId> getGroups() const override;
    std::vector<BelId> getGroupBels(GroupId group) const override;
    std::vector<WireId> getGroupWires(GroupId group) const override;
    std::vector<PipId> getGroupPips(GroupId group) const override;
    std::vector<GroupId> getGroupGroups(GroupId group) const override;

    // -------------------------------------------------

    delay_t estimateDelay(WireId src, WireId dst) const override;
    delay_t predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const override;
    delay_t getDelayEpsilon() const override { return 20; }
    delay_t getRipupDelayPenalty() const override { return 200; }
    float getDelayNS(delay_t v) const override { return v * 0.001; }
    delay_t getDelayFromNS(float ns) const override { return delay_t(ns * 1000); }
    uint32_t getDelayChecksum(delay_t v) const override { return v; }

    BoundingBox getRouteBoundingBox(WireId src, WireId dst) const override;

    // -------------------------------------------------

    bool pack() override;
    bool place() override;
    bool route() override;

    // -------------------------------------------------

    std::vector<GraphicElement> getDecalGraphics(DecalId decal) const override;

    DecalXY getBelDecal(BelId bel) const override;
    DecalXY getWireDecal(WireId wire) const override;
    DecalXY getPipDecal(PipId pip) const override;
    DecalXY getGroupDecal(GroupId group) const override;

    // -------------------------------------------------

    // Get the delay through a cell from one port to another, returning false
    // if no path exists. This only considers combinational delays, as required by the Arch API
    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const override;
    // get_cell_delay_internal is similar to the above, but without false path checks and including clock to out delays
    // for internal arch use only
    bool get_cell_delay_internal(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const;
    // Get the port class, also setting clockInfoCount to the number of TimingClockingInfos associated with a port
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const override;
    // Get the TimingClockingInfo of a port
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const override;
    // Return true if a port is a net
    bool is_global_net(const NetInfo *net) const;

    // -------------------------------------------------

    // Perform placement validity checks, returning false on failure (all
    // implemented in arch_place.cc)

    // Return true whether all Bels at a given location are valid
    bool isBelLocationValid(BelId bel, bool explain_invalid = false) const override;

    // Helper function for above
    bool logic_cells_compatible(const CellInfo **it, const size_t size) const;

    // -------------------------------------------------
    // Assign architecture-specific arguments to nets and cells, which must be
    // called between packing or further
    // netlist modifications, and validity checks
    void assignArchInfo() override;
    void assignCellInfo(CellInfo *cell);

    // -------------------------------------------------
    BelPin get_iob_sharing_pll_pin(BelId pll, IdString pll_pin) const
    {
        auto wire = getBelPinWire(pll, pll_pin);
        for (auto src_bel : getWireBelPins(wire)) {
            if (getBelType(src_bel.bel) == id_SB_IO && src_bel.pin == id_D_IN_0) {
                return src_bel;
            }
        }
        NPNR_ASSERT_FALSE("Expected PLL pin to share an output with an SB_IO D_IN_{0,1}");
    }

    int get_driven_glb_netwk(BelId bel) const
    {
        NPNR_ASSERT(getBelType(bel) == id_SB_GB);
        IdString glb_net = getWireName(getBelPinWire(bel, id_GLOBAL_BUFFER_OUTPUT))[2];
        return std::stoi(std::string("") + glb_net.str(this).back());
    }

    static const std::string defaultPlacer;
    static const std::vector<std::string> availablePlacers;
    static const std::string defaultRouter;
    static const std::vector<std::string> availableRouters;
};

void ice40DelayFuzzerMain(Context *ctx);

NEXTPNR_NAMESPACE_END

#endif /* ICE40_ARCH_H */
