/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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

#include <sstream>

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
    LocationPOD rel_wire_loc;
    int32_t wire_index;
    PortPin port;
});

NPNR_PACKED_STRUCT(struct BelInfoPOD {
    RelPtr<char> name;
    BelType type;
    int32_t num_bel_wires;
    RelPtr<BelWirePOD> bel_wires;
});

NPNR_PACKED_STRUCT(struct BelPortPOD {
    LocationPOD rel_bel_loc;
    int32_t bel_index;
    PortPin port;
});

NPNR_PACKED_STRUCT(struct PipInfoPOD {
    LocationPOD rel_src_loc, rel_dst_loc;
    int32_t src_idx, dst_idx;
    int32_t delay;
    int16_t tile_type;
    int8_t pip_type;
    int8_t padding_0;
});

NPNR_PACKED_STRUCT(struct PipLocatorPOD {
    LocationPOD rel_loc;
    int32_t index;
});

NPNR_PACKED_STRUCT(struct WireInfoPOD {
    RelPtr<char> name;
    int32_t num_uphill, num_downhill;
    RelPtr<PipLocatorPOD> pips_uphill, pips_downhill;

    int32_t num_bels_downhill;
    BelPortPOD bel_uphill;
    RelPtr<BelPortPOD> bels_downhill;
});

NPNR_PACKED_STRUCT(struct LocationTypePOD {
    int32_t num_bels, num_wires, num_pips;
    RelPtr<BelInfoPOD> bel_data;
    RelPtr<WireInfoPOD> wire_data;
    RelPtr<PipInfoPOD> pip_data;
});

NPNR_PACKED_STRUCT(struct ChipInfoPOD {
    int32_t width, height;
    int32_t num_tiles;
    int32_t num_location_types;
    RelPtr<LocationTypePOD> locations;
    RelPtr<int32_t> location_type;
    RelPtr<RelPtr<char>> tiletype_names;
});

#if defined(_MSC_VER)
extern const char *chipdb_blob_25k;
extern const char *chipdb_blob_45k;
extern const char *chipdb_blob_85k;
#else
extern const char chipdb_blob_25k[];
extern const char chipdb_blob_45k[];
extern const char chipdb_blob_85k[];
#endif

/************************ End of chipdb section. ************************/

struct BelIterator
{
    const ChipInfoPOD *chip;
    int cursor_index;
    int cursor_tile;

    BelIterator operator++()
    {
        cursor_index++;
        while (cursor_tile < chip->num_tiles &&
               cursor_index >= chip->locations[chip->location_type[cursor_tile]].num_bels) {
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
        ret.location.x = cursor_tile % chip->width;
        ret.location.y = cursor_tile / chip->width;
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

// -----------------------------------------------------------------------

struct BelPinIterator
{
    const BelPortPOD *ptr = nullptr;
    Location wire_loc;
    void operator++() { ptr++; }
    bool operator!=(const BelPinIterator &other) const { return ptr != other.ptr; }

    BelPin operator*() const
    {
        BelPin ret;
        ret.bel.index = ptr->bel_index;
        ret.bel.location = wire_loc + ptr->rel_bel_loc;
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
    const ChipInfoPOD *chip;
    int cursor_index;
    int cursor_tile;

    WireIterator operator++()
    {
        cursor_index++;
        while (cursor_tile < chip->num_tiles &&
               cursor_index >= chip->locations[chip->location_type[cursor_tile]].num_wires) {
            cursor_index = 0;
            cursor_tile++;
        }
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
        ret.location.x = cursor_tile % chip->width;
        ret.location.y = cursor_tile / chip->width;
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
        while (cursor_tile < chip->num_tiles &&
               cursor_index >= chip->locations[chip->location_type[cursor_tile]].num_pips) {
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
        ret.location.x = cursor_tile % chip->width;
        ret.location.y = cursor_tile / chip->width;
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

struct PipIterator
{

    const PipLocatorPOD *cursor = nullptr;
    Location wire_loc;

    void operator++() { cursor++; }
    bool operator!=(const PipIterator &other) const { return cursor != other.cursor; }

    PipId operator*() const
    {
        PipId ret;
        ret.index = cursor->index;
        ret.location = wire_loc + cursor->rel_loc;
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
    enum
    {
        NONE,
        LFE5U_25F,
        LFE5U_45F,
        LFE5U_85F,
    } type = NONE;
    std::string package;
    int speed = 6;
};

struct Arch : BaseCtx
{
    // We let proxy methods access our state.
    friend class ArchMutateMethods;
    friend class ArchReadMethods;
private:
    mutable std::unordered_map<IdString, BelId> bel_by_name;
    mutable std::unordered_map<IdString, WireId> wire_by_name;
    mutable std::unordered_map<IdString, PipId> pip_by_name;

    std::unordered_map<BelId, IdString> bel_to_cell;
    std::unordered_map<WireId, IdString> wire_to_net;
    std::unordered_map<PipId, IdString> pip_to_net;
    std::unordered_map<PipId, IdString> switches_locked;

public:
    const ChipInfoPOD *chip_info;

    ArchArgs args;
    Arch(ArchArgs args);

    std::string getChipName();

    IdString archId() const { return id("ecp5"); }
    IdString archArgsToId(ArchArgs args) const;

    IdString belTypeToId(BelType type) const;
    BelType belTypeFromId(IdString id) const;

    IdString portPinToId(PortPin type) const;
    PortPin portPinFromId(IdString id) const;

    // -------------------------------------------------

    template <typename Id> const LocationTypePOD *locInfo(Id &id) const
    {
        return &(chip_info->locations[chip_info->location_type[id.location.y * chip_info->width + id.location.x]]);
    }

    IdString getBelName(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        std::stringstream name;
        name << "X" << bel.location.x << "/Y" << bel.location.y << "/" << locInfo(bel)->bel_data[bel.index].name.get();
        return id(name.str());
    }

    uint32_t getBelChecksum(BelId bel) const { return bel.index; }

    BelRange getBels() const
    {
        BelRange range;
        range.b.cursor_tile = 0;
        range.b.cursor_index = -1;
        range.b.chip = chip_info;
        ++range.b; //-1 and then ++ deals with the case of no Bels in the first tile
        range.e.cursor_tile = chip_info->width * chip_info->height;
        range.e.cursor_index = 0;
        range.e.chip = chip_info;
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
        NPNR_ASSERT(bel != BelId());
        return locInfo(bel)->bel_data[bel.index].type;
    }

    BelPin getBelPinUphill(WireId wire) const
    {
        BelPin ret;
        NPNR_ASSERT(wire != WireId());

        if (locInfo(wire)->wire_data[wire.index].bel_uphill.bel_index >= 0) {
            ret.bel.index = locInfo(wire)->wire_data[wire.index].bel_uphill.bel_index;
            ret.bel.location = wire.location + locInfo(wire)->wire_data[wire.index].bel_uphill.rel_bel_loc;
            ret.pin = locInfo(wire)->wire_data[wire.index].bel_uphill.port;
        }

        return ret;
    }

    BelPinRange getBelPinsDownhill(WireId wire) const
    {
        BelPinRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.ptr = locInfo(wire)->wire_data[wire.index].bels_downhill.get();
        range.b.wire_loc = wire.location;
        range.e.ptr = range.b.ptr + locInfo(wire)->wire_data[wire.index].num_bels_downhill;
        range.e.wire_loc = wire.location;
        return range;
    }

    // -------------------------------------------------

    IdString getWireName(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());

        std::stringstream name;
        name << "X" << wire.location.x << "/Y" << wire.location.y << "/"
             << locInfo(wire)->wire_data[wire.index].name.get();
        return id(name.str());
    }

    uint32_t getWireChecksum(WireId wire) const { return wire.index; }

    WireRange getWires() const
    {
        WireRange range;
        range.b.cursor_tile = 0;
        range.b.cursor_index = -1;
        range.b.chip = chip_info;
        ++range.b; //-1 and then ++ deals with the case of no wries in the first tile
        range.e.cursor_tile = chip_info->width * chip_info->height;
        range.e.cursor_index = 0;
        range.e.chip = chip_info;
        return range;
    }

    // -------------------------------------------------

    IdString getPipName(PipId pip) const;

    uint32_t getPipChecksum(PipId pip) const { return pip.index; }

    AllPipRange getPips() const
    {
        AllPipRange range;
        range.b.cursor_tile = 0;
        range.b.cursor_index = -1;
        range.b.chip = chip_info;
        ++range.b; //-1 and then ++ deals with the case of no wries in the first tile
        range.e.cursor_tile = chip_info->width * chip_info->height;
        range.e.cursor_index = 0;
        range.e.chip = chip_info;
        return range;
    }

    WireId getPipSrcWire(PipId pip) const
    {
        WireId wire;
        NPNR_ASSERT(pip != PipId());
        wire.index = locInfo(pip)->pip_data[pip.index].src_idx;
        wire.location = pip.location + locInfo(pip)->pip_data[pip.index].rel_src_loc;
        return wire;
    }

    WireId getPipDstWire(PipId pip) const
    {
        WireId wire;
        NPNR_ASSERT(pip != PipId());
        wire.index = locInfo(pip)->pip_data[pip.index].dst_idx;
        wire.location = pip.location + locInfo(pip)->pip_data[pip.index].rel_dst_loc;
        return wire;
    }

    DelayInfo getPipDelay(PipId pip) const
    {
        DelayInfo delay;
        NPNR_ASSERT(pip != PipId());
        delay.delay = locInfo(pip)->pip_data[pip.index].delay;
        return delay;
    }

    PipRange getPipsDownhill(WireId wire) const
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = locInfo(wire)->wire_data[wire.index].pips_downhill.get();
        range.b.wire_loc = wire.location;
        range.e.cursor = range.b.cursor + locInfo(wire)->wire_data[wire.index].num_downhill;
        range.e.wire_loc = wire.location;
        return range;
    }

    PipRange getPipsUphill(WireId wire) const
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = locInfo(wire)->wire_data[wire.index].pips_uphill.get();
        range.b.wire_loc = wire.location;
        range.e.cursor = range.b.cursor + locInfo(wire)->wire_data[wire.index].num_uphill;
        range.e.wire_loc = wire.location;
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

    std::string getPipTiletype(PipId pip) const
    {
        return chip_info->tiletype_names[locInfo(pip)->pip_data[pip.index].tile_type].get();
    }

    int8_t getPipType(PipId pip) const { return locInfo(pip)->pip_data[pip.index].pip_type; }

    BelId getPackagePinBel(const std::string &pin) const;
    std::string getBelPackagePin(BelId bel) const;

    // -------------------------------------------------

    // TODO(q3k) move this to archproxies?
    GroupId getGroupByName(IdString name) const { return GroupId(); }
    IdString getGroupName(GroupId group) const { return IdString(); }
    std::vector<GroupId> getGroups() const { return std::vector<GroupId>(); }
    std::vector<BelId> getGroupBels(GroupId group) const { return std::vector<BelId>(); }
    std::vector<WireId> getGroupWires(GroupId group) const { return std::vector<WireId>(); }
    std::vector<PipId> getGroupPips(GroupId group) const { return std::vector<PipId>(); }
    std::vector<GroupId> getGroupGroups(GroupId group) const { return std::vector<GroupId>(); }

    // -------------------------------------------------

    // These are also specific to the chip and not state, so they're available
    // on arch directly.
    void estimatePosition(BelId bel, int &x, int &y, bool &gb) const;
    delay_t estimateDelay(WireId src, WireId dst) const;
    delay_t getDelayEpsilon() const { return 20; }
    delay_t getRipupDelayPenalty() const { return 200; }
    float getDelayNS(delay_t v) const { return v * 0.001; }
    uint32_t getDelayChecksum(delay_t v) const { return v; }

    // -------------------------------------------------

    bool pack();
    bool place();
    bool route();

    // -------------------------------------------------

    // TODO(q3k) move this to archproxies?
    DecalXY getFrameDecal() const;
    DecalXY getBelDecal(BelId bel) const;
    DecalXY getWireDecal(WireId wire) const;
    DecalXY getPipDecal(PipId pip) const;
    DecalXY getGroupDecal(GroupId group) const;

    // -------------------------------------------------

    // Get the delay through a cell from one port to another, returning false
    // if no path exists
    bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, delay_t &delay) const;
    // Get the associated clock to a port, or empty if the port is combinational
    IdString getPortClock(const CellInfo *cell, IdString port) const;
    // Return true if a port is a clock
    bool isClockPort(const CellInfo *cell, IdString port) const;
    // Return true if a port is a net
    bool isGlobalNet(const NetInfo *net) const;
};

class ArchReadMethods : public BaseReadCtx
{
  private:
    const Arch *parent_;
    const ChipInfoPOD *chip_info;

    const std::unordered_map<BelId, IdString> &bel_to_cell;
    const std::unordered_map<WireId, IdString> &wire_to_net;
    const std::unordered_map<PipId, IdString> &pip_to_net;
    const std::unordered_map<PipId, IdString> &switches_locked;
    std::unordered_map<IdString, BelId> &bel_by_name;
    std::unordered_map<IdString, WireId> &wire_by_name;
    std::unordered_map<IdString, PipId> &pip_by_name;

  public:
    ~ArchReadMethods() noexcept {}
    ArchReadMethods(const Arch *parent)
            : BaseReadCtx(parent), parent_(parent), chip_info(parent->chip_info), bel_to_cell(parent->bel_to_cell),
              wire_to_net(parent->wire_to_net), pip_to_net(parent->pip_to_net),
              switches_locked(parent->switches_locked), bel_by_name(parent->bel_by_name),
              wire_by_name(parent->wire_by_name), pip_by_name(parent->pip_by_name)
    {
    }
    ArchReadMethods(ArchReadMethods &&other) noexcept : ArchReadMethods(other.parent_) {}
    ArchReadMethods(const ArchReadMethods &other) : ArchReadMethods(other.parent_) {}
 
    /// Perform placement validity checks, returning false on failure (all implemented in arch_place.cc)
    // Whether or not a given cell can be placed at a given Bel
    // This is not intended for Bel type checks, but finer-grained constraints
    // such as conflicting set/reset signals, etc
    bool isValidBelForCell(CellInfo *cell, BelId bel) const;
    // Return true whether all Bels at a given location are valid
    bool isBelLocationValid(BelId bel) const;

    bool checkWireAvail(WireId wire) const;
    bool checkPipAvail(PipId pip) const;
    bool checkBelAvail(BelId bel) const;

    WireId getWireByName(IdString name) const;
    WireId getWireBelPin(BelId bel, PortPin pin) const;
    PipId getPipByName(IdString name) const;

    IdString getConflictingWireNet(WireId wire) const;
    IdString getConflictingPipNet(PipId pip) const;
    IdString getConflictingBelCell(BelId bel) const;

    IdString getBoundWireNet(WireId wire) const;
    IdString getBoundPipNet(PipId pip) const;
    IdString getBoundBelCell(BelId bel) const;

    BelId getBelByName(IdString name) const;

    std::vector<GraphicElement> getDecalGraphics(DecalId decal) const;
};

class ArchMutateMethods : public BaseMutateCtx
{
    friend class MutateContext;

  private:
    Arch *parent_;
    const ChipInfoPOD *chip_info;

    std::unordered_map<BelId, IdString> &bel_to_cell;
    std::unordered_map<WireId, IdString> &wire_to_net;
    std::unordered_map<PipId, IdString> &pip_to_net;
    std::unordered_map<PipId, IdString> &switches_locked;
    std::unordered_map<IdString, BelId> &bel_by_name;
    std::unordered_map<IdString, WireId> &wire_by_name;
    std::unordered_map<IdString, PipId> &pip_by_name;

  public:
    ~ArchMutateMethods() noexcept {}
    ArchMutateMethods(Arch *parent)
            : BaseMutateCtx(parent), parent_(parent), chip_info(parent->chip_info), bel_to_cell(parent->bel_to_cell),
              wire_to_net(parent->wire_to_net), pip_to_net(parent->pip_to_net),
              switches_locked(parent->switches_locked), bel_by_name(parent->bel_by_name),
              wire_by_name(parent->wire_by_name), pip_by_name(parent->pip_by_name)
    {
    }
    ArchMutateMethods(ArchMutateMethods &&other) noexcept : ArchMutateMethods(other.parent_) {}
    ArchMutateMethods(const ArchMutateMethods &other) : ArchMutateMethods(other.parent_) {}

    void unbindWire(WireId wire);
    void unbindPip(PipId pip);
    void unbindBel(BelId bel);
    void bindWire(WireId wire, IdString net, PlaceStrength strength);
    void bindPip(PipId pip, IdString net, PlaceStrength strength);
    void bindBel(BelId bel, IdString cell, PlaceStrength strength);
    // Returned pointer is valid as long as Proxy object exists.
    CellInfo *getCell(IdString cell);
};


NEXTPNR_NAMESPACE_END
