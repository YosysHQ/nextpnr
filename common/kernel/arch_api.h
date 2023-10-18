/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@q3k.org>
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

#ifndef ARCH_API_H
#define ARCH_API_H

#include <algorithm>

#include "basectx.h"
#include "idstring.h"
#include "idstringlist.h"
#include "nextpnr_assertions.h"
#include "nextpnr_namespaces.h"
#include "nextpnr_types.h"

NEXTPNR_NAMESPACE_BEGIN

// The specification of the Arch API (pure virtual)
template <typename R> struct ArchAPI : BaseCtx
{
    // Basic config
    virtual IdString archId() const = 0;
    virtual std::string getChipName() const = 0;
    virtual typename R::ArchArgsT archArgs() const = 0;
    virtual IdString archArgsToId(typename R::ArchArgsT args) const = 0;
    virtual int getGridDimX() const = 0;
    virtual int getGridDimY() const = 0;
    virtual int getTileBelDimZ(int x, int y) const = 0;
    virtual int getTilePipDimZ(int x, int y) const = 0;
    virtual char getNameDelimiter() const = 0;
    // Bel methods
    virtual typename R::AllBelsRangeT getBels() const = 0;
    virtual IdStringList getBelName(BelId bel) const = 0;
    virtual BelId getBelByName(IdStringList name) const = 0;
    virtual uint32_t getBelChecksum(BelId bel) const = 0;
    virtual void bindBel(BelId bel, CellInfo *cell, PlaceStrength strength) = 0;
    virtual void unbindBel(BelId bel) = 0;
    virtual Loc getBelLocation(BelId bel) const = 0;
    virtual BelId getBelByLocation(Loc loc) const = 0;
    virtual typename R::TileBelsRangeT getBelsByTile(int x, int y) const = 0;
    virtual bool getBelGlobalBuf(BelId bel) const = 0;
    virtual bool checkBelAvail(BelId bel) const = 0;
    virtual CellInfo *getBoundBelCell(BelId bel) const = 0;
    virtual CellInfo *getConflictingBelCell(BelId bel) const = 0;
    virtual IdString getBelType(BelId bel) const = 0;
    virtual bool getBelHidden(BelId bel) const = 0;
    virtual typename R::BelAttrsRangeT getBelAttrs(BelId bel) const = 0;
    virtual WireId getBelPinWire(BelId bel, IdString pin) const = 0;
    virtual PortType getBelPinType(BelId bel, IdString pin) const = 0;
    virtual typename R::BelPinsRangeT getBelPins(BelId bel) const = 0;
    virtual typename R::CellBelPinRangeT getBelPinsForCellPin(const CellInfo *cell_info, IdString pin) const = 0;
    // Wire methods
    virtual typename R::AllWiresRangeT getWires() const = 0;
    virtual WireId getWireByName(IdStringList name) const = 0;
    virtual IdStringList getWireName(WireId wire) const = 0;
    virtual IdString getWireType(WireId wire) const = 0;
    virtual typename R::WireAttrsRangeT getWireAttrs(WireId) const = 0;
    virtual typename R::DownhillPipRangeT getPipsDownhill(WireId wire) const = 0;
    virtual typename R::UphillPipRangeT getPipsUphill(WireId wire) const = 0;
    virtual typename R::WireBelPinRangeT getWireBelPins(WireId wire) const = 0;
    virtual uint32_t getWireChecksum(WireId wire) const = 0;
    virtual void bindWire(WireId wire, NetInfo *net, PlaceStrength strength) = 0;
    virtual void unbindWire(WireId wire) = 0;
    virtual bool checkWireAvail(WireId wire) const = 0;
    virtual NetInfo *getBoundWireNet(WireId wire) const = 0;
    virtual WireId getConflictingWireWire(WireId wire) const = 0;
    virtual NetInfo *getConflictingWireNet(WireId wire) const = 0;
    virtual DelayQuad getWireDelay(WireId wire) const = 0;
    virtual IdString getWireConstantValue(WireId wire) const = 0;
    // Pip methods
    virtual typename R::AllPipsRangeT getPips() const = 0;
    virtual PipId getPipByName(IdStringList name) const = 0;
    virtual IdStringList getPipName(PipId pip) const = 0;
    virtual IdString getPipType(PipId pip) const = 0;
    virtual typename R::PipAttrsRangeT getPipAttrs(PipId) const = 0;
    virtual uint32_t getPipChecksum(PipId pip) const = 0;
    virtual void bindPip(PipId pip, NetInfo *net, PlaceStrength strength) = 0;
    virtual void unbindPip(PipId pip) = 0;
    virtual bool checkPipAvail(PipId pip) const = 0;
    virtual bool checkPipAvailForNet(PipId pip, const NetInfo *net) const = 0;
    virtual NetInfo *getBoundPipNet(PipId pip) const = 0;
    virtual WireId getConflictingPipWire(PipId pip) const = 0;
    virtual NetInfo *getConflictingPipNet(PipId pip) const = 0;
    virtual WireId getPipSrcWire(PipId pip) const = 0;
    virtual WireId getPipDstWire(PipId pip) const = 0;
    virtual DelayQuad getPipDelay(PipId pip) const = 0;
    virtual Loc getPipLocation(PipId pip) const = 0;
    // Group methods
    virtual GroupId getGroupByName(IdStringList name) const = 0;
    virtual IdStringList getGroupName(GroupId group) const = 0;
    virtual typename R::AllGroupsRangeT getGroups() const = 0;
    virtual typename R::GroupBelsRangeT getGroupBels(GroupId group) const = 0;
    virtual typename R::GroupWiresRangeT getGroupWires(GroupId group) const = 0;
    virtual typename R::GroupPipsRangeT getGroupPips(GroupId group) const = 0;
    virtual typename R::GroupGroupsRangeT getGroupGroups(GroupId group) const = 0;
    // Delay Methods
    virtual delay_t predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const = 0;
    virtual delay_t getDelayEpsilon() const = 0;
    virtual delay_t getRipupDelayPenalty() const = 0;
    virtual float getDelayNS(delay_t v) const = 0;
    virtual delay_t getDelayFromNS(float ns) const = 0;
    virtual uint32_t getDelayChecksum(delay_t v) const = 0;
    virtual delay_t estimateDelay(WireId src, WireId dst) const = 0;
    virtual BoundingBox getRouteBoundingBox(WireId src, WireId dst) const = 0;
    virtual bool getArcDelayOverride(const NetInfo *net_info, const PortRef &sink, DelayQuad &delay) const = 0;
    // Decal methods
    virtual typename R::DecalGfxRangeT getDecalGraphics(DecalId decal) const = 0;
    virtual DecalXY getBelDecal(BelId bel) const = 0;
    virtual DecalXY getWireDecal(WireId wire) const = 0;
    virtual DecalXY getPipDecal(PipId pip) const = 0;
    virtual DecalXY getGroupDecal(GroupId group) const = 0;
    // Cell timing methods
    virtual bool getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const = 0;
    virtual TimingPortClass getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const = 0;
    virtual TimingClockingInfo getPortClockingInfo(const CellInfo *cell, IdString port, int index) const = 0;
    // Placement validity checks
    virtual bool isValidBelForCellType(IdString cell_type, BelId bel) const = 0;
    virtual IdString getBelBucketName(BelBucketId bucket) const = 0;
    virtual BelBucketId getBelBucketByName(IdString name) const = 0;
    virtual BelBucketId getBelBucketForBel(BelId bel) const = 0;
    virtual BelBucketId getBelBucketForCellType(IdString cell_type) const = 0;
    virtual bool isBelLocationValid(BelId bel, bool explain_invalid = false) const = 0;
    virtual typename R::CellTypeRangeT getCellTypes() const = 0;
    virtual typename R::BelBucketRangeT getBelBuckets() const = 0;
    virtual typename R::BucketBelRangeT getBelsInBucket(BelBucketId bucket) const = 0;
    // Cluster methods
    virtual CellInfo *getClusterRootCell(ClusterId cluster) const = 0;
    virtual BoundingBox getClusterBounds(ClusterId cluster) const = 0;
    virtual Loc getClusterOffset(const CellInfo *cell) const = 0;
    virtual bool isClusterStrict(const CellInfo *cell) const = 0;
    virtual bool getClusterPlacement(ClusterId cluster, BelId root_bel,
                                     std::vector<std::pair<CellInfo *, BelId>> &placement) const = 0;
    // Flow methods
    virtual bool pack() = 0;
    virtual bool place() = 0;
    virtual bool route() = 0;
    virtual void assignArchInfo() = 0;
};

NEXTPNR_NAMESPACE_END

#endif /* ARCH_API_H */
