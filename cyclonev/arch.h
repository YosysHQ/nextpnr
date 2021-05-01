/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2021  Lofty <dan.ravensloft@gmail.com>
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

#ifndef MISTRAL_ARCH_H
#define MISTRAL_ARCH_H

#include <set>
#include <sstream>

#include "base_arch.h"
#include "nextpnr_types.h"
#include "relptr.h"

#include "cyclonev.h"

NEXTPNR_NAMESPACE_BEGIN

struct ArchArgs
{
    std::string device;
    std::string mistral_root;
};

struct PinInfo
{
    IdString name;
    WireId wire;
    PortType type;
};

struct BelInfo
{
    // TODO
};

// We maintain our own wire data based on mistral's. This gets us the bidirectional linking that nextpnr needs,
// and also makes it easy to add wires and pips for our own purposes like LAB internal routing, global clock
// sources, etc.
struct WireInfo
{
    // name_override is only used for nextpnr-created wires
    // otherwise; this is empty and a name is created according to mistral rules
    IdString name_override;

    // these are transformed on-the-fly to PipId by the iterator, to save space (WireId is half the size of PipId)
    std::vector<WireId> wires_downhill;
    std::vector<WireId> wires_uphill;

    std::vector<BelPin> bel_pins;

    // flags for special wires (currently unused)
    uint64_t flags;
};

// This transforms a WireIds, and adds the mising half of the pair to create a PipId
using WireVecIterator = std::vector<WireId>::const_iterator;
struct UpDownhillPipIterator
{
    WireVecIterator base;
    WireId other_wire;
    bool is_uphill;

    UpDownhillPipIterator(WireVecIterator base, WireId other_wire, bool is_uphill)
            : base(base), other_wire(other_wire), is_uphill(is_uphill){};

    bool operator!=(const UpDownhillPipIterator &other) { return base != other.base; }
    UpDownhillPipIterator operator++()
    {
        ++base;
        return *this;
    }
    UpDownhillPipIterator operator++(int)
    {
        UpDownhillPipIterator prior(*this);
        ++(*this);
        return prior;
    }
    PipId operator*() { return is_uphill ? PipId(base->node, other_wire.node) : PipId(other_wire.node, base->node); }
};

struct UpDownhillPipRange
{
    UpDownhillPipIterator b, e;

    UpDownhillPipRange(const std::vector<WireId> &v, WireId other_wire, bool is_uphill)
            : b(v.cbegin(), other_wire, is_uphill), e(v.cend(), other_wire, is_uphill){};

    UpDownhillPipIterator begin() const { return b; }
    UpDownhillPipIterator end() const { return e; }
};

// This iterates over the list of wires, and for each wire yields its uphill pips, as an efficient way of going over
// all the pips in the device
using WireMapIterator = std::unordered_map<WireId, WireInfo>::const_iterator;
struct AllPipIterator
{
    WireMapIterator base, end;
    int uphill_idx;

    AllPipIterator(WireMapIterator base, WireMapIterator end, int uphill_idx)
            : base(base), end(end), uphill_idx(uphill_idx){};

    bool operator!=(const AllPipIterator &other) { return base != other.base || uphill_idx != other.uphill_idx; }
    AllPipIterator operator++()
    {
        // Increment uphill list index by one
        ++uphill_idx;
        // We've reached the end of the current wire. Keep incrementing the wire of interest until we find one with
        // uphill pips, or we reach the end of the list of wires
        while (base != end && uphill_idx >= int(base->second.wires_uphill.size())) {
            uphill_idx = 0;
            ++base;
        }
        return *this;
    }
    AllPipIterator operator++(int)
    {
        AllPipIterator prior(*this);
        ++(*this);
        return prior;
    }
    PipId operator*() { return PipId(base->second.wires_uphill.at(uphill_idx).node, base->first.node); }
};

struct AllPipRange
{
    AllPipIterator b, e;

    AllPipRange(const std::unordered_map<WireId, WireInfo> &wires)
            : b(wires.cbegin(), wires.cend(), -1), e(wires.cend(), wires.cend(), 0)
    {
        // Starting the begin iterator at index -1 and incrementing it ensures we skip over the first wire if it has no
        // uphill pips
        ++b;
    };

    AllPipIterator begin() const { return b; }
    AllPipIterator end() const { return e; }
};

// This transforms a map to a range of keys, used as the wire iterator
template <typename T> struct key_range
{
    key_range(const T &t) : b(t.cbegin()), e(t.cend()){};
    typename T::const_iterator b, e;

    struct xformed_iterator : public T::const_iterator
    {
        explicit xformed_iterator(typename T::const_iterator base) : T::const_iterator(base){};
        typename T::key_type operator*() { return this->T::const_iterator::operator*().first; }
    };

    xformed_iterator begin() const { return xformed_iterator(b); }
    xformed_iterator end() const { return xformed_iterator(e); }
};

using AllWireRange = key_range<std::unordered_map<WireId, WireInfo>>;
using AllBelRange = key_range<std::unordered_map<BelId, BelInfo>>;

struct ArchRanges : BaseArchRanges
{
    using ArchArgsT = ArchArgs;
    // Bels
    using AllBelsRangeT = AllBelRange;
    using TileBelsRangeT = std::vector<BelId>;
    using BelPinsRangeT = std::vector<IdString>;
    // Wires
    using AllWiresRangeT = AllWireRange;
    using DownhillPipRangeT = UpDownhillPipRange;
    using UphillPipRangeT = UpDownhillPipRange;
    using WireBelPinRangeT = const std::vector<BelPin> &;
    // Pips
    using AllPipsRangeT = AllPipRange;
};

struct Arch : BaseArch<ArchRanges>
{
    ArchArgs args;
    mistral::CycloneV *cyclonev;

    Arch(ArchArgs args);
    ArchArgs archArgs() const { return args; }

    std::string getChipName() const override { return std::string{"TODO: getChipName"}; }
    // -------------------------------------------------

    int getGridDimX() const override { return cyclonev->get_tile_sx(); }
    int getGridDimY() const override { return cyclonev->get_tile_sy(); }
    int getTileBelDimZ(int x, int y) const override; // arch.cc
    char getNameDelimiter() const override { return '.'; }

    // -------------------------------------------------

    BelId getBelByName(IdStringList name) const override; // arch.cc
    IdStringList getBelName(BelId bel) const override;    // arch.cc
    AllBelRange getBels() const override { return AllBelRange(bels); }
    std::vector<BelId> getBelsByTile(int x, int y) const override;
    Loc getBelLocation(BelId bel) const override
    {
        return Loc(CycloneV::pos2x(bel.pos), CycloneV::pos2y(bel.pos), bel.z);
    }
    BelId getBelByLocation(Loc loc) const override
    {
        BelId id = BelId(CycloneV::xy2pos(loc.x, loc.y), loc.z);
        if (bels.count(id))
            return id;
        else
            return BelId();
    }
    IdString getBelType(BelId bel) const override; // arch.cc
    WireId getBelPinWire(BelId bel, IdString pin) const override { return WireId(); }
    PortType getBelPinType(BelId bel, IdString pin) const override { return PORT_IN; }
    std::vector<IdString> getBelPins(BelId bel) const override { return {}; }

    // -------------------------------------------------

    WireId getWireByName(IdStringList name) const override;
    IdStringList getWireName(WireId wire) const override;
    DelayQuad getWireDelay(WireId wire) const override { return DelayQuad(0); }
    const std::vector<BelPin> &getWireBelPins(WireId wire) const override { return empty_belpin_list; }
    AllWireRange getWires() const override { return AllWireRange(wires); }

    // -------------------------------------------------

    PipId getPipByName(IdStringList name) const override;
    AllPipRange getPips() const override { return AllPipRange(wires); }
    Loc getPipLocation(PipId pip) const override { return Loc(0, 0, 0); }
    IdStringList getPipName(PipId pip) const override;
    WireId getPipSrcWire(PipId pip) const override { return WireId(pip.src); };
    WireId getPipDstWire(PipId pip) const override { return WireId(pip.dst); };
    DelayQuad getPipDelay(PipId pip) const override { return DelayQuad(0); }
    UpDownhillPipRange getPipsDownhill(WireId wire) const override
    {
        return UpDownhillPipRange(wires.at(wire).wires_downhill, wire, false);
    }
    UpDownhillPipRange getPipsUphill(WireId wire) const override
    {
        return UpDownhillPipRange(wires.at(wire).wires_uphill, wire, true);
    }

    // -------------------------------------------------

    delay_t estimateDelay(WireId src, WireId dst) const override { return 100; };
    delay_t predictDelay(const NetInfo *net_info, const PortRef &sink) const override { return 100; };
    delay_t getDelayEpsilon() const override { return 10; };
    delay_t getRipupDelayPenalty() const override { return 100; };
    float getDelayNS(delay_t v) const override { return float(v) / 1000.0f; };
    delay_t getDelayFromNS(float ns) const override { return delay_t(ns * 1000.0f); };
    uint32_t getDelayChecksum(delay_t v) const override { return v; };

    ArcBounds getRouteBoundingBox(WireId src, WireId dst) const override { return ArcBounds(); }

    // -------------------------------------------------

    bool pack() override;
    bool place() override;
    bool route() override;

    // -------------------------------------------------

    static const std::string defaultPlacer;
    static const std::vector<std::string> availablePlacers;
    static const std::string defaultRouter;
    static const std::vector<std::string> availableRouters;

    std::unordered_map<WireId, WireInfo> wires;
    std::unordered_map<BelId, BelInfo> bels;

    // WIP to link without failure
    std::vector<BelPin> empty_belpin_list;

    // Conversion between numbers and rnode types and IdString, for fast wire name implementation
    std::vector<IdString> int2id;
    std::unordered_map<IdString, int> id2int;

    std::vector<IdString> rn_t2id;
    std::unordered_map<IdString, CycloneV::rnode_type_t> id2rn_t;

    // This structure is only used for nextpnr-created wires
    std::unordered_map<IdStringList, WireId> npnr_wirebyname;
};

NEXTPNR_NAMESPACE_END

#endif