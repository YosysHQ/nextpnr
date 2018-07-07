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
    const ChipInfoPOD *chip_info;

    mutable std::unordered_map<IdString, BelId> bel_by_name;
    mutable std::unordered_map<IdString, WireId> wire_by_name;
    mutable std::unordered_map<IdString, PipId> pip_by_name;

    std::unordered_map<BelId, IdString> bel_to_cell;
    std::unordered_map<WireId, IdString> wire_to_net;
    std::unordered_map<PipId, IdString> pip_to_net;
    std::unordered_map<PipId, IdString> switches_locked;

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

    BelId getBelByName(IdString name) const;

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

    void bindBel(BelId bel, IdString cell, PlaceStrength strength)
    {
        NPNR_ASSERT(bel != BelId());
        NPNR_ASSERT(bel_to_cell[bel] == IdString());
        bel_to_cell[bel] = cell;
        cells[cell]->bel = bel;
        cells[cell]->belStrength = strength;
    }

    void unbindBel(BelId bel)
    {
        NPNR_ASSERT(bel != BelId());
        NPNR_ASSERT(bel_to_cell[bel] != IdString());
        cells[bel_to_cell[bel]]->bel = BelId();
        cells[bel_to_cell[bel]]->belStrength = STRENGTH_NONE;
        bel_to_cell[bel] = IdString();
    }

    bool checkBelAvail(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        return bel_to_cell.find(bel) == bel_to_cell.end() || bel_to_cell.at(bel) == IdString();
    }

    IdString getBoundBelCell(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        if (bel_to_cell.find(bel) == bel_to_cell.end())
            return IdString();
        else
            return bel_to_cell.at(bel);
    }

    IdString getConflictingBelCell(BelId bel) const
    {
        NPNR_ASSERT(bel != BelId());
        if (bel_to_cell.find(bel) == bel_to_cell.end())
            return IdString();
        else
            return bel_to_cell.at(bel);
    }

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

    WireId getWireBelPin(BelId bel, PortPin pin) const;

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

    WireId getWireByName(IdString name) const;

    IdString getWireName(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());

        std::stringstream name;
        name << "X" << wire.location.x << "/Y" << wire.location.y << "/"
             << locInfo(wire)->wire_data[wire.index].name.get();
        return id(name.str());
    }

    uint32_t getWireChecksum(WireId wire) const { return wire.index; }

    void bindWire(WireId wire, IdString net, PlaceStrength strength)
    {
        NPNR_ASSERT(wire != WireId());
        NPNR_ASSERT(wire_to_net[wire] == IdString());
        wire_to_net[wire] = net;
        nets[net]->wires[wire].pip = PipId();
        nets[net]->wires[wire].strength = strength;
    }

    void unbindWire(WireId wire)
    {
        NPNR_ASSERT(wire != WireId());
        NPNR_ASSERT(wire_to_net[wire] != IdString());

        auto &net_wires = nets[wire_to_net[wire]]->wires;
        auto it = net_wires.find(wire);
        NPNR_ASSERT(it != net_wires.end());

        auto pip = it->second.pip;
        if (pip != PipId()) {
            pip_to_net[pip] = IdString();
        }

        net_wires.erase(it);
        wire_to_net[wire] = IdString();
    }

    bool checkWireAvail(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        return wire_to_net.find(wire) == wire_to_net.end() || wire_to_net.at(wire) == IdString();
    }

    IdString getBoundWireNet(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        if (wire_to_net.find(wire) == wire_to_net.end())
            return IdString();
        else
            return wire_to_net.at(wire);
    }

    IdString getConflictingWireNet(WireId wire) const
    {
        NPNR_ASSERT(wire != WireId());
        if (wire_to_net.find(wire) == wire_to_net.end())
            return IdString();
        else
            return wire_to_net.at(wire);
    }

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

    PipId getPipByName(IdString name) const;
    IdString getPipName(PipId pip) const;

    uint32_t getPipChecksum(PipId pip) const { return pip.index; }

    void bindPip(PipId pip, IdString net, PlaceStrength strength)
    {
        NPNR_ASSERT(pip != PipId());
        NPNR_ASSERT(pip_to_net[pip] == IdString());

        pip_to_net[pip] = net;

        WireId dst;
        dst.index = locInfo(pip)->pip_data[pip.index].dst_idx;
        dst.location = pip.location + locInfo(pip)->pip_data[pip.index].rel_dst_loc;
        NPNR_ASSERT(wire_to_net[dst] == IdString());
        wire_to_net[dst] = net;
        nets[net]->wires[dst].pip = pip;
        nets[net]->wires[dst].strength = strength;
    }

    void unbindPip(PipId pip)
    {
        NPNR_ASSERT(pip != PipId());
        NPNR_ASSERT(pip_to_net[pip] != IdString());

        WireId dst;
        dst.index = locInfo(pip)->pip_data[pip.index].dst_idx;
        dst.location = pip.location + locInfo(pip)->pip_data[pip.index].rel_dst_loc;
        NPNR_ASSERT(wire_to_net[dst] != IdString());
        wire_to_net[dst] = IdString();
        nets[pip_to_net[pip]]->wires.erase(dst);

        pip_to_net[pip] = IdString();
    }

    bool checkPipAvail(PipId pip) const
    {
        NPNR_ASSERT(pip != PipId());
        return pip_to_net.find(pip) == pip_to_net.end() || pip_to_net.at(pip) == IdString();
    }

    IdString getBoundPipNet(PipId pip) const
    {
        NPNR_ASSERT(pip != PipId());
        if (pip_to_net.find(pip) == pip_to_net.end())
            return IdString();
        else
            return pip_to_net.at(pip);
    }

    IdString getConflictingPipNet(PipId pip) const
    {
        NPNR_ASSERT(pip != PipId());
        if (pip_to_net.find(pip) == pip_to_net.end())
            return IdString();
        else
            return pip_to_net.at(pip);
    }

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
        range.e.cursor = range.b.cursor + locInfo(wire)->wire_data[wire.index].num_downhill;
        return range;
    }

    PipRange getPipsUphill(WireId wire) const
    {
        PipRange range;
        NPNR_ASSERT(wire != WireId());
        range.b.cursor = locInfo(wire)->wire_data[wire.index].pips_uphill.get();
        range.e.cursor = range.b.cursor + locInfo(wire)->wire_data[wire.index].num_uphill;
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

    void estimatePosition(BelId bel, int &x, int &y, bool &gb) const;
    delay_t estimateDelay(WireId src, WireId dst) const;
    delay_t getDelayEpsilon() const { return 20; }
    delay_t getRipupDelayPenalty() const { return 200; }
    float getDelayNS(delay_t v) const { return v * 0.001; }
    uint32_t getDelayChecksum(delay_t v) const { return v; }

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

    // -------------------------------------------------
    // Placement validity checks
    bool isValidBelForCell(CellInfo *cell, BelId bel) const;
    bool isBelLocationValid(BelId bel) const;
};

NEXTPNR_NAMESPACE_END
