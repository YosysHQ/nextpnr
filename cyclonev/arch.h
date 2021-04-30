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
};

struct PinInfo
{
    IdString name;
    WireId wire;
    PortType type;
};

struct BelInfo
{
    IdString name, type;
    std::map<IdString, std::string> attrs;
    CellInfo *bound_cell;
    std::unordered_map<IdString, PinInfo> pins;
    DecalXY decalxy;
    int x, y, z;
    bool gb;
};

struct ArchRanges : BaseArchRanges
{
    using ArchArgsT = ArchArgs;
    // Bels
    using AllBelsRangeT = const std::vector<BelId> &;
    using TileBelsRangeT = std::vector<BelId>;
    using BelPinsRangeT = std::vector<IdString>;
    // Wires
    using AllWiresRangeT = const std::vector<WireId> &;
    using DownhillPipRangeT = const std::vector<PipId> &;
    using UphillPipRangeT = const std::vector<PipId> &;
    using WireBelPinRangeT = std::vector<BelPin>;
    // Pips
    using AllPipsRangeT = const std::vector<PipId> &;
};

struct Arch : BaseArch<ArchRanges>
{
    ArchArgs args;
    mistral::CycloneV *cyclonev;

    std::unordered_map<BelId, BelInfo> bels;
    std::vector<BelId> bel_list;

    Arch(ArchArgs args);

    std::string getChipName() const override { return std::string{"TODO: getChipName"}; }
    // -------------------------------------------------

    int getGridDimX() const override { return cyclonev->get_tile_sx(); }
    int getGridDimY() const override { return cyclonev->get_tile_sy(); }
    int getTileBelDimZ(int x, int y) const override; // arch.cc

    // -------------------------------------------------

    BelId getBelByName(IdStringList name) const override; // arch.cc
    IdStringList getBelName(BelId bel) const override;    // arch.cc
    const std::vector<BelId> &getBels() const override { return bel_list; }
    std::vector<BelId> getBelsByTile(int x, int y) const override;
    Loc getBelLocation(BelId bel) const override
    {
        return Loc(CycloneV::pos2x(bel.pos), CycloneV::pos2y(bel.pos), bel.z);
    }
    BelId getBelByLocation(Loc loc) const override { return BelId(CycloneV::xy2pos(loc.x, loc.y), loc.z); }
    IdString getBelType(BelId bel) const override; // arch.cc
    WireId getBelPinWire(BelId bel, IdString pin) const override;
    PortType getBelPinType(BelId bel, IdString pin) const override;
    std::vector<IdString> getBelPins(BelId bel) const override;

    // -------------------------------------------------

    WireId getWireByName(IdStringList name) const override;
    IdStringList getWireName(WireId wire) const override;
    DelayQuad getWireDelay(WireId wire) const;
    std::vector<BelPin> getWireBelPins(WireId wire) const override;
    const std::vector<WireId> &getWires() const override;

    // -------------------------------------------------

    PipId getPipByName(IdStringList name) const override;
    const std::vector<PipId> &getPips() const override;
    Loc getPipLocation(PipId pip) const override;
    IdStringList getPipName(PipId pip) const override;
    WireId getPipSrcWire(PipId pip) const override;
    WireId getPipDstWire(PipId pip) const override;
    DelayQuad getPipDelay(PipId pip) const override;
    const std::vector<PipId> &getPipsDownhill(WireId wire) const override;
    const std::vector<PipId> &getPipsUphill(WireId wire) const override;

    // -------------------------------------------------

    delay_t estimateDelay(WireId src, WireId dst) const override;
    delay_t predictDelay(const NetInfo *net_info, const PortRef &sink) const override;
    delay_t getDelayEpsilon() const override;
    delay_t getRipupDelayPenalty() const override;
    float getDelayNS(delay_t v) const override;
    delay_t getDelayFromNS(float ns) const override;
    uint32_t getDelayChecksum(delay_t v) const override;

    ArcBounds getRouteBoundingBox(WireId src, WireId dst) const override;

    // -------------------------------------------------

    bool pack() override;
    bool place() override;
    bool route() override;

    // -------------------------------------------------

    static const std::string defaultPlacer;
    static const std::vector<std::string> availablePlacers;
    static const std::string defaultRouter;
    static const std::vector<std::string> availableRouters;
};

NEXTPNR_NAMESPACE_END

#endif