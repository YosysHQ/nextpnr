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

#include <math.h>
#include "nextpnr.h"
#include "placer1.h"
#include "router1.h"

NEXTPNR_NAMESPACE_BEGIN

void Arch::addWire(IdString name, IdString type, int x, int y)
{
    NPNR_ASSERT(wires.count(name) == 0);
    WireInfo &wi = wires[name];
    wi.name = name;
    wi.type = type;
    wi.x = x;
    wi.y = y;

    wire_ids.push_back(name);
}

void Arch::addPip(IdString name, IdString type, IdString srcWire, IdString dstWire, DelayInfo delay)
{
    NPNR_ASSERT(pips.count(name) == 0);
    PipInfo &pi = pips[name];
    pi.name = name;
    pi.type = type;
    pi.srcWire = srcWire;
    pi.dstWire = dstWire;
    pi.delay = delay;

    wires.at(srcWire).downhill.push_back(name);
    wires.at(dstWire).uphill.push_back(name);
    pip_ids.push_back(name);
}

void Arch::addAlias(IdString name, IdString type, IdString srcWire, IdString dstWire, DelayInfo delay)
{
    NPNR_ASSERT(pips.count(name) == 0);
    PipInfo &pi = pips[name];
    pi.name = name;
    pi.type = type;
    pi.srcWire = srcWire;
    pi.dstWire = dstWire;
    pi.delay = delay;

    wires.at(srcWire).aliases.push_back(name);
    pip_ids.push_back(name);
}

void Arch::addBel(IdString name, IdString type, Loc loc, bool gb)
{
    NPNR_ASSERT(bels.count(name) == 0);
    NPNR_ASSERT(bel_by_loc.count(loc) == 0);
    BelInfo &bi = bels[name];
    bi.name = name;
    bi.type = type;
    bi.x = loc.x;
    bi.y = loc.y;
    bi.z = loc.z;
    bi.gb = gb;

    bel_ids.push_back(name);
    bel_by_loc[loc] = name;

    if (int(bels_by_tile.size()) <= loc.x)
        bels_by_tile.resize(loc.x + 1);

    if (int(bels_by_tile[loc.x].size()) <= loc.y)
        bels_by_tile[loc.x].resize(loc.y + 1);

    bels_by_tile[loc.x][loc.y].push_back(name);

    if (int(tileDimZ.size()) <= loc.x)
        tileDimZ.resize(loc.x + 1);

    if (int(tileDimZ[loc.x].size()) <= loc.y)
        tileDimZ[loc.x].resize(loc.y + 1);

    gridDimX = std::max(gridDimX, loc.x + 1);
    gridDimY = std::max(gridDimY, loc.x + 1);
    tileDimZ[loc.x][loc.y] = std::max(tileDimZ[loc.x][loc.y], loc.z + 1);
}

void Arch::addBelInput(IdString bel, IdString name, IdString wire)
{
    NPNR_ASSERT(bels.at(bel).pins.count(name) == 0);
    PinInfo &pi = bels.at(bel).pins[name];
    pi.name = name;
    pi.wire = wire;
    pi.type = PORT_IN;

    wires.at(wire).downhill_bel_pins.push_back(BelPin{bel, name});
    wires.at(wire).bel_pins.push_back(BelPin{bel, name});
}

void Arch::addBelOutput(IdString bel, IdString name, IdString wire)
{
    NPNR_ASSERT(bels.at(bel).pins.count(name) == 0);
    PinInfo &pi = bels.at(bel).pins[name];
    pi.name = name;
    pi.wire = wire;
    pi.type = PORT_OUT;

    wires.at(wire).uphill_bel_pin = BelPin{bel, name};
    wires.at(wire).bel_pins.push_back(BelPin{bel, name});
}

void Arch::addBelInout(IdString bel, IdString name, IdString wire)
{
    NPNR_ASSERT(bels.at(bel).pins.count(name) == 0);
    PinInfo &pi = bels.at(bel).pins[name];
    pi.name = name;
    pi.wire = wire;
    pi.type = PORT_INOUT;

    wires.at(wire).downhill_bel_pins.push_back(BelPin{bel, name});
    wires.at(wire).bel_pins.push_back(BelPin{bel, name});
}

void Arch::addGroupBel(IdString group, IdString bel) { groups[group].bels.push_back(bel); }

void Arch::addGroupWire(IdString group, IdString wire) { groups[group].wires.push_back(wire); }

void Arch::addGroupPip(IdString group, IdString pip) { groups[group].pips.push_back(pip); }

void Arch::addGroupGroup(IdString group, IdString grp) { groups[group].groups.push_back(grp); }

void Arch::addDecalGraphic(DecalId decal, const GraphicElement &graphic)
{
    decal_graphics[decal].push_back(graphic);
    refreshUi();
}

void Arch::setFrameDecal(DecalXY decalxy)
{
    frame_decalxy = decalxy;
    refreshUiFrame();
}

void Arch::setWireDecal(WireId wire, DecalXY decalxy)
{
    wires.at(wire).decalxy = decalxy;
    refreshUiWire(wire);
}

void Arch::setPipDecal(PipId pip, DecalXY decalxy)
{
    pips.at(pip).decalxy = decalxy;
    refreshUiPip(pip);
}

void Arch::setBelDecal(BelId bel, DecalXY decalxy)
{
    bels.at(bel).decalxy = decalxy;
    refreshUiBel(bel);
}

void Arch::setGroupDecal(GroupId group, DecalXY decalxy)
{
    groups[group].decalxy = decalxy;
    refreshUiGroup(group);
}

// ---------------------------------------------------------------

Arch::Arch(ArchArgs) {}

void IdString::initialize_arch(const BaseCtx *ctx) {}

// ---------------------------------------------------------------

BelId Arch::getBelByName(IdString name) const
{
    if (bels.count(name))
        return name;
    return BelId();
}

IdString Arch::getBelName(BelId bel) const { return bel; }

Loc Arch::getBelLocation(BelId bel) const
{
    auto &info = bels.at(bel);
    return Loc(info.x, info.y, info.z);
}

BelId Arch::getBelByLocation(Loc loc) const
{
    auto it = bel_by_loc.find(loc);
    if (it != bel_by_loc.end())
        return it->second;
    return BelId();
}

const std::vector<BelId> &Arch::getBelsByTile(int x, int y) const { return bels_by_tile.at(x).at(y); }

bool Arch::getBelGlobalBuf(BelId bel) const { return bels.at(bel).gb; }

uint32_t Arch::getBelChecksum(BelId bel) const
{
    // FIXME
    return 0;
}

void Arch::bindBel(BelId bel, IdString cell, PlaceStrength strength)
{
    bels.at(bel).bound_cell = cell;
    cells.at(cell)->bel = bel;
    cells.at(cell)->belStrength = strength;
    refreshUiBel(bel);
}

void Arch::unbindBel(BelId bel)
{
    cells.at(bels.at(bel).bound_cell)->bel = BelId();
    cells.at(bels.at(bel).bound_cell)->belStrength = STRENGTH_NONE;
    bels.at(bel).bound_cell = IdString();
    refreshUiBel(bel);
}

bool Arch::checkBelAvail(BelId bel) const { return bels.at(bel).bound_cell == IdString(); }

IdString Arch::getBoundBelCell(BelId bel) const { return bels.at(bel).bound_cell; }

IdString Arch::getConflictingBelCell(BelId bel) const { return bels.at(bel).bound_cell; }

const std::vector<BelId> &Arch::getBels() const { return bel_ids; }

BelType Arch::getBelType(BelId bel) const { return bels.at(bel).type; }

WireId Arch::getBelPinWire(BelId bel, PortPin pin) const { return bels.at(bel).pins.at(pin).wire; }

PortType Arch::getBelPinType(BelId bel, PortPin pin) const { return bels.at(bel).pins.at(pin).type; }

std::vector<PortPin> Arch::getBelPins(BelId bel) const
{
    std::vector<PortPin> ret;
    for (auto &it : bels.at(bel).pins)
        ret.push_back(it.first);
    return ret;
}

// ---------------------------------------------------------------

WireId Arch::getWireByName(IdString name) const
{
    if (wires.count(name))
        return name;
    return WireId();
}

IdString Arch::getWireName(WireId wire) const { return wire; }

IdString Arch::getWireType(WireId wire) const { return wires.at(wire).type; }

uint32_t Arch::getWireChecksum(WireId wire) const
{
    // FIXME
    return 0;
}

void Arch::bindWire(WireId wire, IdString net, PlaceStrength strength)
{
    wires.at(wire).bound_net = net;
    nets.at(net)->wires[wire].pip = PipId();
    nets.at(net)->wires[wire].strength = strength;
    refreshUiWire(wire);
}

void Arch::unbindWire(WireId wire)
{
    auto &net_wires = nets[wires.at(wire).bound_net]->wires;

    auto pip = net_wires.at(wire).pip;
    if (pip != PipId()) {
        pips.at(pip).bound_net = IdString();
        refreshUiPip(pip);
    }

    net_wires.erase(wire);
    wires.at(wire).bound_net = IdString();
    refreshUiWire(wire);
}

bool Arch::checkWireAvail(WireId wire) const { return wires.at(wire).bound_net == IdString(); }

IdString Arch::getBoundWireNet(WireId wire) const { return wires.at(wire).bound_net; }

IdString Arch::getConflictingWireNet(WireId wire) const { return wires.at(wire).bound_net; }

const std::vector<BelPin> &Arch::getWireBelPins(WireId wire) const { return wires.at(wire).bel_pins; }

const std::vector<WireId> &Arch::getWires() const { return wire_ids; }

// ---------------------------------------------------------------

PipId Arch::getPipByName(IdString name) const
{
    if (pips.count(name))
        return name;
    return PipId();
}

IdString Arch::getPipName(PipId pip) const { return pip; }

IdString Arch::getPipType(PipId pip) const { return pips.at(pip).type; }

uint32_t Arch::getPipChecksum(PipId wire) const
{
    // FIXME
    return 0;
}

void Arch::bindPip(PipId pip, IdString net, PlaceStrength strength)
{
    WireId wire = pips.at(pip).dstWire;
    pips.at(pip).bound_net = net;
    wires.at(wire).bound_net = net;
    nets.at(net)->wires[wire].pip = pip;
    nets.at(net)->wires[wire].strength = strength;
    refreshUiPip(pip);
    refreshUiWire(wire);
}

void Arch::unbindPip(PipId pip)
{
    WireId wire = pips.at(pip).dstWire;
    nets.at(wires.at(wire).bound_net)->wires.erase(wire);
    pips.at(pip).bound_net = IdString();
    wires.at(wire).bound_net = IdString();
    refreshUiPip(pip);
    refreshUiWire(wire);
}

bool Arch::checkPipAvail(PipId pip) const { return pips.at(pip).bound_net == IdString(); }

IdString Arch::getBoundPipNet(PipId pip) const { return pips.at(pip).bound_net; }

IdString Arch::getConflictingPipNet(PipId pip) const { return pips.at(pip).bound_net; }

const std::vector<PipId> &Arch::getPips() const { return pip_ids; }

WireId Arch::getPipSrcWire(PipId pip) const { return pips.at(pip).srcWire; }

WireId Arch::getPipDstWire(PipId pip) const { return pips.at(pip).dstWire; }

DelayInfo Arch::getPipDelay(PipId pip) const { return pips.at(pip).delay; }

const std::vector<PipId> &Arch::getPipsDownhill(WireId wire) const { return wires.at(wire).downhill; }

const std::vector<PipId> &Arch::getPipsUphill(WireId wire) const { return wires.at(wire).uphill; }

const std::vector<PipId> &Arch::getWireAliases(WireId wire) const { return wires.at(wire).aliases; }

// ---------------------------------------------------------------

GroupId Arch::getGroupByName(IdString name) const { return name; }

IdString Arch::getGroupName(GroupId group) const { return group; }

std::vector<GroupId> Arch::getGroups() const
{
    std::vector<GroupId> ret;
    for (auto &it : groups)
        ret.push_back(it.first);
    return ret;
}

const std::vector<BelId> &Arch::getGroupBels(GroupId group) const { return groups.at(group).bels; }

const std::vector<WireId> &Arch::getGroupWires(GroupId group) const { return groups.at(group).wires; }

const std::vector<PipId> &Arch::getGroupPips(GroupId group) const { return groups.at(group).pips; }

const std::vector<GroupId> &Arch::getGroupGroups(GroupId group) const { return groups.at(group).groups; }

// ---------------------------------------------------------------

delay_t Arch::estimateDelay(WireId src, WireId dst) const
{
    const WireInfo &s = wires.at(src);
    const WireInfo &d = wires.at(dst);
    int dx = abs(s.x - d.x);
    int dy = abs(s.y - d.y);
    return (dx + dy) * grid_distance_to_delay;
}

delay_t Arch::predictDelay(const NetInfo *net_info, const PortRef &sink) const;
{
    const auto& driver = net_info->driver;
    auto driver_loc = getBelLocation(driver.cell->bel);
    auto sink_loc = getBelLocation(sink.cell->bel);

    int dx = abs(driver_loc.x - driver_loc.x);
    int dy = abs(sink_loc.y - sink_locy);
    return (dx + dy) * grid_distance_to_delay;
}

delay_t Arch::getBudgetOverride(NetInfo *net_info, int user_idx, delay_t budget) const { return budget; }

// ---------------------------------------------------------------

bool Arch::place() { return placer1(getCtx()); }

bool Arch::route() { return router1(getCtx()); }

// ---------------------------------------------------------------

const std::vector<GraphicElement> &Arch::getDecalGraphics(DecalId decal) const { return decal_graphics.at(decal); }

DecalXY Arch::getFrameDecal() const { return frame_decalxy; }

DecalXY Arch::getBelDecal(BelId bel) const { return bels.at(bel).decalxy; }

DecalXY Arch::getWireDecal(WireId wire) const { return wires.at(wire).decalxy; }

DecalXY Arch::getPipDecal(PipId pip) const { return pips.at(pip).decalxy; }

DecalXY Arch::getGroupDecal(GroupId group) const { return groups.at(group).decalxy; }

// ---------------------------------------------------------------

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const
{
    return false;
}

IdString Arch::getPortClock(const CellInfo *cell, IdString port) const { return IdString(); }

bool Arch::isClockPort(const CellInfo *cell, IdString port) const { return false; }

bool Arch::isValidBelForCell(CellInfo *cell, BelId bel) const { return true; }
bool Arch::isBelLocationValid(BelId bel) const { return true; }

NEXTPNR_NAMESPACE_END
