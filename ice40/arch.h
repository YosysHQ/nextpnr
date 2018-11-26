/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
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

NEXTPNR_NAMESPACE_BEGIN

/**** Everything in this section must be kept in sync with chipdb.py ****/

template <typename T> struct RelPtr
{
    int32_t offset;

    // void set(const T *ptr) {
    //     offset = reinterpret_cast<const char*>(ptr) -
    //              reinterpret_cast<const char*>(this);
    // }

    const T *get() const { return reinterpret_cast<const T *>(reinterpret_cast<const char *>(this) + offset); }

    const T &operator[](size_t index) const { return get()[index]; }

    const T &operator*() const { return *(get()); }

    const T *operator->() const { return get(); }
};

NPNR_PACKED_STRUCT(struct BelWirePOD {
    int32_t port;
    int32_t type;
    int32_t wire_index;
});

NPNR_PACKED_STRUCT(struct BelInfoPOD {
    RelPtr<char> name;
    int32_t type;
    int32_t num_bel_wires;
    RelPtr<BelWirePOD> bel_wires;
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
    int32_t num_uphill, num_downhill;
    RelPtr<int32_t> pips_uphill, pips_downhill;

    int32_t num_bel_pins;
    RelPtr<BelPortPOD> bel_pins;

    int32_t num_segments;
    RelPtr<WireSegmentPOD> segments;

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
    int32_t num_pins;
    RelPtr<PackagePinPOD> pins;
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
    int32_t num_bits;
    RelPtr<ConfigBitPOD> bits;
});

NPNR_PACKED_STRUCT(struct TileInfoPOD {
    int8_t cols, rows;
    int16_t num_config_entries;
    RelPtr<ConfigEntryPOD> entries;
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
    int32_t num_switches, num_ierens;
    RelPtr<TileInfoPOD> tiles_nonrouting;
    RelPtr<SwitchInfoPOD> switches;
    RelPtr<IerenInfoPOD> ierens;
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
    int32_t num_entries;
    RelPtr<BelConfigEntryPOD> entries;
});

NPNR_PACKED_STRUCT(struct CellPathDelayPOD {
    int32_t from_port;
    int32_t to_port;
    int32_t fast_delay;
    int32_t slow_delay;
});

NPNR_PACKED_STRUCT(struct CellTimingPOD {
    int32_t type;
    int32_t num_paths;
    RelPtr<CellPathDelayPOD> path_delays;
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
    int32_t num_bels, num_wires, num_pips;
    int32_t num_switches, num_belcfgs, num_packages;
    int32_t num_timing_cells, num_global_networks;
    RelPtr<BelInfoPOD> bel_data;
    RelPtr<WireInfoPOD> wire_data;
    RelPtr<PipInfoPOD> pip_data;
    RelPtr<TileType> tile_grid;
    RelPtr<BitstreamInfoPOD> bits_info;
    RelPtr<BelConfigPOD> bel_config;
    RelPtr<PackageInfoPOD> packages_data;
    RelPtr<CellTimingPOD> cell_timing;
    RelPtr<GlobalNetworkInfoPOD> global_network_info;
    RelPtr<RelPtr<char>> tile_wire_names;
});

#if defined(_MSC_VER)
extern const char *chipdb_blob_384;
extern const char *chipdb_blob_1k;
extern const char *chipdb_blob_5k;
extern const char *chipdb_blob_8k;
#else
extern const char chipdb_blob_384[];
extern const char chipdb_blob_1k[];
extern const char chipdb_blob_5k[];
extern const char chipdb_blob_8k[];
#endif

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
        LP8K,
        HX1K,
        HX8K,
        UP5K
    } type = NONE;
    std::string package;
};

struct Arch : BaseCtx
{
    bool fast_part;
    const ChipInfoPOD *chip_info;
    const PackageInfoPOD *package_info;

    mutable std::unordered_map<IdString, int> bel_by_name;
    mutable std::unordered_map<IdString, int> wire_by_name;
    mutable std::unordered_map<IdString, int> pip_by_name;
    mutable std::unordered_map<Loc, int> bel_by_loc;

    std::vector<bool> bel_carry;
    std::vector<CellInfo *> bel_to_cell;
    std::vector<NetInfo *> wire_to_net;
    std::vector<NetInfo *> pip_to_net;
    std::vector<WireId> switches_locked;

    ArchArgs args;
    Arch(ArchArgs args);

    std::string getChipName() const;

    IdString archId() const { return id("ice40"); }
    ArchArgs archArgs() const { return args; }
    IdString archArgsToId(ArchArgs args) const;

    // -------------------------------------------------

    int getGridDimX() const { return chip_info->width; }
    int getGridDimY() const { return chip_info->height; }
    int getTileBelDimZ(int, int) const { return 8; }
    int getTilePipDimZ(int, int) const { return 1; }

    // -------------------------------------------------

    BelId getBelByName(IdString name) const;

    IdString getBelName(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        return id(chip_info->bel_data[bel.index].name.get());
    }

    uint32_t getBelChecksum(BelId bel) const { return bel.index; }

    void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength)
    {
        NPNR_ASSERT(bel != BelId());
        NPNR_ASSERT(bel_to_cell[bel.index] == nullptr);

        bel_to_cell[bel.index] = cell;
        bel_carry[bel.index] = (cell->type == id_ICESTORM_LC && cell->lcInfo.carryEnable);
        cell->bel = bel;
        cell->belStrength = strength;
        refreshUiBel(bel);
    }

    void unbindBel(BelId bel)
    {
        NPNR_ASSERT(bel != BelId());
        NPNR_ASSERT(bel_to_cell[bel.index] != nullptr);
        bel_to_cell[bel.index]->bel = BelId();
        bel_to_cell[bel.index]->belStrength = STRENGTH_NONE;
        bel_to_cell[bel.index] = nullptr;
        bel_carry[bel.index] = false;
        refreshUiBel(bel);
    }

    bool checkBelAvail(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        return bel_to_cell[bel.index] == nullptr;
    }

    CellInfo *getBoundBelCell(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        return bel_to_cell[bel.index];
    }

    CellInfo *getConflictingBelCell(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        return bel_to_cell[bel.index];
    }

    BelRange getBels() const
    {
        BelRange range;
        range.b.cursor = 0;
        range.e.cursor = chip_info->num_bels;
        return range;
    }

    Loc getBelLocation(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        Loc loc;
        loc.x = chip_info->bel_data[bel.index].x;
        loc.y = chip_info->bel_data[bel.index].y;
        loc.z = chip_info->bel_data[bel.index].z;
        return loc;
    }

    BelId getBelByLocation(Loc loc) const;
    BelRange getBelsByTile(int x, int y) const;

    bool getBelGlobalBuf(BelId bel) const { return chip_info->bel_data[bel.index].type == ID_SB_GB; }

    IdString getBelType(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        return IdString(chip_info->bel_data[bel.index].type);
    }

    std::vector<std::pair<IdString, std::string>> getBelAttrs(BelId bel) const;

    WireId getBelPinWire(BelId bel, IdString pin) const;
    PortType getBelPinType(BelId bel, IdString pin) const;
    std::vector<IdString> getBelPins(BelId bel) const;

    bool isBelLocked(BelId bel) const;

    // -------------------------------------------------

    WireId getWireByName(IdString name) const;

    IdString getWireName(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        return id(chip_info->wire_data[wire.index].name.get());
    }

    IdString getWireType(WireId wire) const;
    std::vector<std::pair<IdString, std::string>> getWireAttrs(WireId wire) const;

    uint32_t getWireChecksum(WireId wire) const { return wire.index; }

    void bindWire(WireId wire, NetInfo *net, PlaceStrength strength)
    {
        NPNR_ASSERT(wire != WireId());
        NPNR_ASSERT(wire_to_net[wire.index] == nullptr);
        wire_to_net[wire.index] = net;
        net->wires[wire].pip = PipId();
        net->wires[wire].strength = strength;
        refreshUiWire(wire);
    }

    void unbindWire(WireId wire)
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

    bool checkWireAvail(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        return wire_to_net[wire.index] == nullptr;
    }

    NetInfo *getBoundWireNet(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        return wire_to_net[wire.index];
    }

    WireId getConflictingWireWire(WireId wire) const { return wire; }

    NetInfo *getConflictingWireNet(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        return wire_to_net[wire.index];
    }

    DelayInfo getWireDelay(WireId wire) const
    {
        DelayInfo delay;
        NPNR_ASSERT(wire != WireId());
        if (fast_part)
            delay.delay = chip_info->wire_data[wire.index].fast_delay;
        else
            delay.delay = chip_info->wire_data[wire.index].slow_delay;
        return delay;
    }

    BelPinRange getWireBelPins(WireId wire) const
    {
        BelPinRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.ptr = chip_info->wire_data[wire.index].bel_pins.get();
        range.e.ptr = range.b.ptr + chip_info->wire_data[wire.index].num_bel_pins;
        return range;
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

    void bindPip(PipId pip, NetInfo *net, PlaceStrength strength)
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

    void unbindPip(PipId pip)
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

    bool checkPipAvail(PipId pip) const
    {
        if (ice40_pip_hard_unavail(pip))
            return false;

        auto &pi = chip_info->pip_data[pip.index];
        return switches_locked[pi.switch_index] == WireId();
    }

    NetInfo *getBoundPipNet(PipId pip) const
    {
        NPNR_ASSERT(pip != PipId());
        return pip_to_net[pip.index];
    }

    WireId getConflictingPipWire(PipId pip) const
    {
        if (ice40_pip_hard_unavail(pip))
            return WireId();

        return switches_locked[chip_info->pip_data[pip.index].switch_index];
    }

    NetInfo *getConflictingPipNet(PipId pip) const
    {
        if (ice40_pip_hard_unavail(pip))
            return nullptr;

        WireId wire = switches_locked[chip_info->pip_data[pip.index].switch_index];
        return wire == WireId() ? nullptr : wire_to_net[wire.index];
    }

    AllPipRange getPips() const
    {
        AllPipRange range;
        range.b.cursor = 0;
        range.e.cursor = chip_info->num_pips;
        return range;
    }

    Loc getPipLocation(PipId pip) const
    {
        Loc loc;
        loc.x = chip_info->pip_data[pip.index].x;
        loc.y = chip_info->pip_data[pip.index].y;
        loc.z = 0;
        return loc;
    }

    IdString getPipName(PipId pip) const;

    IdString getPipType(PipId pip) const;
    std::vector<std::pair<IdString, std::string>> getPipAttrs(PipId pip) const;

    uint32_t getPipChecksum(PipId pip) const { return pip.index; }

    WireId getPipSrcWire(PipId pip) const
    {
        WireId wire;
        NPNR_ASSERT(pip != PipId());
        wire.index = chip_info->pip_data[pip.index].src;
        return wire;
    }

    WireId getPipDstWire(PipId pip) const
    {
        WireId wire;
        NPNR_ASSERT(pip != PipId());
        wire.index = chip_info->pip_data[pip.index].dst;
        return wire;
    }

    DelayInfo getPipDelay(PipId pip) const
    {
        DelayInfo delay;
        NPNR_ASSERT(pip != PipId());
        if (fast_part)
            delay.delay = chip_info->pip_data[pip.index].fast_delay;
        else
            delay.delay = chip_info->pip_data[pip.index].slow_delay;
        return delay;
    }

    PipRange getPipsDownhill(WireId wire) const
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = chip_info->wire_data[wire.index].pips_downhill.get();
        range.e.cursor = range.b.cursor + chip_info->wire_data[wire.index].num_downhill;
        return range;
    }

    PipRange getPipsUphill(WireId wire) const
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = chip_info->wire_data[wire.index].pips_uphill.get();
        range.e.cursor = range.b.cursor + chip_info->wire_data[wire.index].num_uphill;
        return range;
    }

    PipRange getWireAliases(WireId wire) const
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = nullptr;
        range.e.cursor = nullptr;
        return range;
    }

    BelId getPackagePinBel(const std::string &pin) const;
    std::string getBelPackagePin(BelId bel) const;

    // -------------------------------------------------

    GroupId getGroupByName(IdString name) const;
    IdString getGroupName(GroupId group) const;
    std::vector<GroupId> getGroups() const;
    std::vector<BelId> getGroupBels(GroupId group) const;
    std::vector<WireId> getGroupWires(GroupId group) const;
    std::vector<PipId> getGroupPips(GroupId group) const;
    std::vector<GroupId> getGroupGroups(GroupId group) const;

    // -------------------------------------------------

    delay_t estimateDelay(WireId src, WireId dst) const;
    delay_t predictDelay(const NetInfo *net_info, const PortRef &sink) const;
    delay_t getDelayEpsilon() const { return 20; }
    delay_t getRipupDelayPenalty() const { return 200; }
    float getDelayNS(delay_t v) const { return v * 0.001; }
    DelayInfo getDelayFromNS(float ns) const
    {
        DelayInfo del;
        del.delay = delay_t(ns * 1000);
        return del;
    }
    uint32_t getDelayChecksum(delay_t v) const { return v; }
    bool getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const;

    // -------------------------------------------------

    bool pack();
    bool place();
    bool route();

    // -------------------------------------------------

    std::vector<GraphicElement> getDecalGraphics(DecalId decal) const;

    DecalXY getBelDecal(BelId bel) const;
    DecalXY getWireDecal(WireId wire) const;
    DecalXY getPipDecal(PipId pip) const;
    DecalXY getGroupDecal(GroupId group) const;

    // -------------------------------------------------

    // Get the delay through a cell from one port to another, returning false
    // if no path exists
    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const;
    // Get the port class, also setting clockInfoCount to the number of TimingClockingInfos associated with a port
    TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const;
    // Get the TimingClockingInfo of a port
    TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const;
    // Return true if a port is a net
    bool isGlobalNet(const NetInfo *net) const;

    // -------------------------------------------------

    // Perform placement validity checks, returning false on failure (all
    // implemented in arch_place.cc)

    // Whether or not a given cell can be placed at a given Bel
    // This is not intended for Bel type checks, but finer-grained constraints
    // such as conflicting set/reset signals, etc
    bool isValidBelForCell(CellInfo *cell, BelId bel) const;

    // Return true whether all Bels at a given location are valid
    bool isBelLocationValid(BelId bel) const;

    // Helper function for above
    bool logicCellsCompatible(const CellInfo **it, const size_t size) const;

    // -------------------------------------------------
    // Assign architecure-specific arguments to nets and cells, which must be
    // called between packing or further
    // netlist modifications, and validity checks
    void assignArchInfo();
    void assignCellInfo(CellInfo *cell);

    // -------------------------------------------------
    BelPin getIOBSharingPLLPin(BelId pll, IdString pll_pin) const
    {
        auto wire = getBelPinWire(pll, pll_pin);
        for (auto src_bel : getWireBelPins(wire)) {
            if (getBelType(src_bel.bel) == id_SB_IO && src_bel.pin == id_D_IN_0) {
                return src_bel;
            }
        }
        NPNR_ASSERT_FALSE("Expected PLL pin to share an output with an SB_IO D_IN_{0,1}");
    }

    int getDrivenGlobalNetwork(BelId bel) const
    {
        NPNR_ASSERT(getBelType(bel) == id_SB_GB);
        IdString glb_net = getWireName(getBelPinWire(bel, id_GLOBAL_BUFFER_OUTPUT));
        return std::stoi(std::string("") + glb_net.str(this).back());
    }
};

void ice40DelayFuzzerMain(Context *ctx);

NEXTPNR_NAMESPACE_END
