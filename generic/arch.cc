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

#include <iostream>
#include <math.h>
#include "nextpnr.h"
#include "placer1.h"
#include "placer_heap.h"
#include "router1.h"
#include "router2.h"
#include "util.h"
#include "viaduct_api.h"

NEXTPNR_NAMESPACE_BEGIN

WireId Arch::addWire(IdStringList name, IdString type, int x, int y)
{
    NPNR_ASSERT(wire_by_name.count(name) == 0);
    WireId wire(wires.size());
    wire_by_name[name] = wire;
    wires.emplace_back();
    WireInfo &wi = wires.back();
    wi.name = name;
    wi.type = type;
    wi.x = x;
    wi.y = y;
    return wire;
}

WireId Arch::addWireAsBelInput(BelId bel, IdString name)
{
    Loc l = getBelLocation(bel);
    WireId w = addWire(IdStringList::concat(getBelName(bel), name), name, l.x, l.y);
    addBelInput(bel, name, w);
    return w;
}

WireId Arch::addWireAsBelOutput(BelId bel, IdString name)
{
    Loc l = getBelLocation(bel);
    WireId w = addWire(IdStringList::concat(getBelName(bel), name), name, l.x, l.y);
    addBelOutput(bel, name, w);
    return w;
}

WireId Arch::addWireAsBelInout(BelId bel, IdString name)
{
    Loc l = getBelLocation(bel);
    WireId w = addWire(IdStringList::concat(getBelName(bel), name), name, l.x, l.y);
    addBelInout(bel, name, w);
    return w;
}

PipId Arch::addPip(IdStringList name, IdString type, WireId srcWire, WireId dstWire, delay_t delay, Loc loc)
{
    NPNR_ASSERT(pip_by_name.count(name) == 0);
    PipId pip(pips.size());
    pip_by_name[name] = pip;
    pips.emplace_back();
    PipInfo &pi = pips.back();
    pi.name = name;
    pi.type = type;
    pi.srcWire = srcWire;
    pi.dstWire = dstWire;
    pi.delay = delay;
    pi.loc = loc;

    wire_info(srcWire).downhill.push_back(pip);
    wire_info(dstWire).uphill.push_back(pip);

    if (int(tilePipDimZ.size()) <= loc.x)
        tilePipDimZ.resize(loc.x + 1);

    if (int(tilePipDimZ[loc.x].size()) <= loc.y)
        tilePipDimZ[loc.x].resize(loc.y + 1);

    gridDimX = std::max(gridDimX, loc.x + 1);
    gridDimY = std::max(gridDimY, loc.x + 1);
    tilePipDimZ[loc.x][loc.y] = std::max(tilePipDimZ[loc.x][loc.y], loc.z + 1);
    return pip;
}

BelId Arch::addBel(IdStringList name, IdString type, Loc loc, bool gb, bool hidden)
{
    NPNR_ASSERT(bel_by_name.count(name) == 0);
    NPNR_ASSERT(bel_by_loc.count(loc) == 0);
    BelId bel(bels.size());
    bel_by_name[name] = bel;
    bels.emplace_back();
    BelInfo &bi = bels.back();
    bi.name = name;
    bi.type = type;
    bi.x = loc.x;
    bi.y = loc.y;
    bi.z = loc.z;
    bi.gb = gb;
    bi.hidden = hidden;

    bel_by_loc[loc] = bel;

    if (int(bels_by_tile.size()) <= loc.x)
        bels_by_tile.resize(loc.x + 1);

    if (int(bels_by_tile[loc.x].size()) <= loc.y)
        bels_by_tile[loc.x].resize(loc.y + 1);

    bels_by_tile[loc.x][loc.y].push_back(bel);

    if (int(tileBelDimZ.size()) <= loc.x)
        tileBelDimZ.resize(loc.x + 1);

    if (int(tileBelDimZ[loc.x].size()) <= loc.y)
        tileBelDimZ[loc.x].resize(loc.y + 1);

    gridDimX = std::max(gridDimX, loc.x + 1);
    gridDimY = std::max(gridDimY, loc.x + 1);
    tileBelDimZ[loc.x][loc.y] = std::max(tileBelDimZ[loc.x][loc.y], loc.z + 1);
    return bel;
}

void Arch::addBelInput(BelId bel, IdString name, WireId wire) { addBelPin(bel, name, wire, PORT_IN); }

void Arch::addBelOutput(BelId bel, IdString name, WireId wire) { addBelPin(bel, name, wire, PORT_OUT); }

void Arch::addBelInout(BelId bel, IdString name, WireId wire) { addBelPin(bel, name, wire, PORT_INOUT); }

void Arch::addBelPin(BelId bel, IdString name, WireId wire, PortType type)
{
    auto &bi = bel_info(bel);
    NPNR_ASSERT(bi.pins.count(name) == 0);
    PinInfo &pi = bi.pins[name];
    pi.name = name;
    pi.wire = wire;
    pi.type = type;

    if (wire != WireId())
        wire_info(wire).bel_pins.push_back(BelPin{bel, name});
}

void Arch::addGroupBel(IdStringList group, BelId bel) { groups[group].bels.push_back(bel); }

void Arch::addGroupWire(IdStringList group, WireId wire) { groups[group].wires.push_back(wire); }

void Arch::addGroupPip(IdStringList group, PipId pip) { groups[group].pips.push_back(pip); }

void Arch::addGroupGroup(IdStringList group, IdStringList grp) { groups[group].groups.push_back(grp); }

void Arch::addDecalGraphic(IdStringList decal, const GraphicElement &graphic)
{
    decal_graphics[DecalId(decal, false)].push_back(graphic); // inactive variant
    decal_graphics[DecalId(decal, true)].push_back(graphic);  // active variant

    GraphicElement &active = decal_graphics[DecalId(decal, true)].back();
    if (active.style == GraphicElement::STYLE_INACTIVE)
        active.style = GraphicElement::STYLE_ACTIVE;

    refreshUi();
}

void Arch::setWireDecal(WireId wire, float x, float y, IdStringList decal)
{
    wires.at(wire.index).decalxy.x = x;
    wires.at(wire.index).decalxy.y = y;
    wires.at(wire.index).decalxy.decal = DecalId(decal, false);
    refreshUiWire(wire);
}

void Arch::setPipDecal(PipId pip, float x, float y, IdStringList decal)
{
    pips.at(pip.index).decalxy.x = x;
    pips.at(pip.index).decalxy.y = y;
    pips.at(pip.index).decalxy.decal = DecalId(decal, false);
    refreshUiPip(pip);
}

void Arch::setBelDecal(BelId bel, float x, float y, IdStringList decal)
{
    bels.at(bel.index).decalxy.x = x;
    bels.at(bel.index).decalxy.y = y;
    bels.at(bel.index).decalxy.decal = DecalId(decal, false);
    refreshUiBel(bel);
}

void Arch::setGroupDecal(GroupId group, float x, float y, IdStringList decal)
{
    groups.at(group).decalxy.x = x;
    groups.at(group).decalxy.y = y;
    groups.at(group).decalxy.decal = DecalId(decal, false);
    refreshUiGroup(group);
}

void Arch::setWireAttr(WireId wire, IdString key, const std::string &value) { wire_info(wire).attrs[key] = value; }

void Arch::setPipAttr(PipId pip, IdString key, const std::string &value) { pip_info(pip).attrs[key] = value; }

void Arch::setBelAttr(BelId bel, IdString key, const std::string &value) { bel_info(bel).attrs[key] = value; }

void Arch::setLutK(int K) { args.K = K; }

void Arch::setDelayScaling(double scale, double offset)
{
    args.delayScale = scale;
    args.delayOffset = offset;
}

void Arch::addCellTimingClock(IdString cell, IdString port) { cellTiming[cell].portClasses[port] = TMG_CLOCK_INPUT; }

void Arch::addCellTimingDelay(IdString cell, IdString fromPort, IdString toPort, delay_t delay)
{
    if (get_or_default(cellTiming[cell].portClasses, fromPort, TMG_IGNORE) == TMG_IGNORE)
        cellTiming[cell].portClasses[fromPort] = TMG_COMB_INPUT;
    if (get_or_default(cellTiming[cell].portClasses, toPort, TMG_IGNORE) == TMG_IGNORE)
        cellTiming[cell].portClasses[toPort] = TMG_COMB_OUTPUT;
    cellTiming[cell].combDelays[CellDelayKey{fromPort, toPort}] = DelayQuad(delay);
}

void Arch::addCellTimingSetupHold(IdString cell, IdString port, IdString clock, delay_t setup, delay_t hold)
{
    TimingClockingInfo ci;
    ci.clock_port = clock;
    ci.edge = RISING_EDGE;
    ci.setup = DelayPair(setup);
    ci.hold = DelayPair(hold);
    cellTiming[cell].clockingInfo[port].push_back(ci);
    cellTiming[cell].portClasses[port] = TMG_REGISTER_INPUT;
}

void Arch::addCellTimingClockToOut(IdString cell, IdString port, IdString clock, delay_t clktoq)
{
    TimingClockingInfo ci;
    ci.clock_port = clock;
    ci.edge = RISING_EDGE;
    ci.clockToQ = DelayQuad(clktoq);
    cellTiming[cell].clockingInfo[port].push_back(ci);
    cellTiming[cell].portClasses[port] = TMG_REGISTER_OUTPUT;
}

void Arch::clearCellBelPinMap(IdString cell, IdString cell_pin) { cells.at(cell)->bel_pins[cell_pin].clear(); }
void Arch::addCellBelPinMapping(IdString cell, IdString cell_pin, IdString bel_pin)
{
    cells.at(cell)->bel_pins[cell_pin].push_back(bel_pin);
}

// ---------------------------------------------------------------

Arch::Arch(ArchArgs args) : chipName("generic"), args(args)
{
    // Dummy for empty decals
    decal_graphics[DecalId(IdStringList(), false)];
    decal_graphics[DecalId(IdStringList(), true)];
}

void IdString::initialize_arch(const BaseCtx *ctx) {}

// ---------------------------------------------------------------

BelId Arch::getBelByName(IdStringList name) const
{
    if (name.size() == 0)
        return BelId();
    auto fnd = bel_by_name.find(name);
    if (fnd == bel_by_name.end())
        NPNR_ASSERT_FALSE_STR("no bel named " + name.str(getCtx()));
    return fnd->second;
}

IdStringList Arch::getBelName(BelId bel) const { return bel_info(bel).name; }

Loc Arch::getBelLocation(BelId bel) const
{
    auto &info = bel_info(bel);
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

bool Arch::getBelGlobalBuf(BelId bel) const { return bel_info(bel).gb; }

uint32_t Arch::getBelChecksum(BelId bel) const
{
    // FIXME
    return 0;
}

void Arch::bindBel(BelId bel, CellInfo *cell, PlaceStrength strength)
{
    if (uarch)
        uarch->notifyBelChange(bel, cell);
    bel_info(bel).bound_cell = cell;
    cell->bel = bel;
    cell->belStrength = strength;
    refreshUiBel(bel);
}

void Arch::unbindBel(BelId bel)
{
    if (uarch)
        uarch->notifyBelChange(bel, nullptr);
    auto &bi = bel_info(bel);
    bi.bound_cell->bel = BelId();
    bi.bound_cell->belStrength = STRENGTH_NONE;
    bi.bound_cell = nullptr;
    refreshUiBel(bel);
}

bool Arch::checkBelAvail(BelId bel) const
{
    return (!uarch || uarch->checkBelAvail(bel)) && (bel_info(bel).bound_cell == nullptr);
}

CellInfo *Arch::getBoundBelCell(BelId bel) const { return bel_info(bel).bound_cell; }

CellInfo *Arch::getConflictingBelCell(BelId bel) const { return bel_info(bel).bound_cell; }

linear_range<BelId> Arch::getBels() const { return linear_range<BelId>(bels.size()); }

IdString Arch::getBelType(BelId bel) const { return bel_info(bel).type; }

bool Arch::getBelHidden(BelId bel) const { return bel_info(bel).hidden; }

const std::map<IdString, std::string> &Arch::getBelAttrs(BelId bel) const { return bel_info(bel).attrs; }

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
    const auto &bdata = bel_info(bel);
    if (!bdata.pins.count(pin))
        log_error("bel '%s' has no pin '%s'\n", getCtx()->nameOfBel(bel), pin.c_str(this));
    return bdata.pins.at(pin).wire;
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const { return bel_info(bel).pins.at(pin).type; }

std::vector<IdString> Arch::getBelPins(BelId bel) const
{
    std::vector<IdString> ret;
    for (auto &it : bel_info(bel).pins)
        ret.push_back(it.first);
    return ret;
}

const std::vector<IdString> &Arch::getBelPinsForCellPin(const CellInfo *cell_info, IdString pin) const
{
    return cell_info->bel_pins.at(pin);
}

// ---------------------------------------------------------------

WireId Arch::getWireByName(IdStringList name) const
{
    if (name.size() == 0)
        return WireId();
    auto fnd = wire_by_name.find(name);
    if (fnd == wire_by_name.end())
        NPNR_ASSERT_FALSE_STR("no wire named " + name.str(getCtx()));
    return fnd->second;
}

IdStringList Arch::getWireName(WireId wire) const { return wire_info(wire).name; }

IdString Arch::getWireType(WireId wire) const { return wire_info(wire).type; }

const std::map<IdString, std::string> &Arch::getWireAttrs(WireId wire) const { return wire_info(wire).attrs; }

uint32_t Arch::getWireChecksum(WireId wire) const { return wire.index; }

void Arch::bindWire(WireId wire, NetInfo *net, PlaceStrength strength)
{
    if (uarch)
        uarch->notifyWireChange(wire, net);
    wire_info(wire).bound_net = net;
    net->wires[wire].pip = PipId();
    net->wires[wire].strength = strength;
    refreshUiWire(wire);
}

void Arch::unbindWire(WireId wire)
{
    auto &net_wires = wire_info(wire).bound_net->wires;

    auto pip = net_wires.at(wire).pip;
    if (pip != PipId()) {
        if (uarch)
            uarch->notifyPipChange(pip, nullptr);
        pip_info(pip).bound_net = nullptr;
        refreshUiPip(pip);
    }

    if (uarch)
        uarch->notifyWireChange(wire, nullptr);
    net_wires.erase(wire);
    wire_info(wire).bound_net = nullptr;
    refreshUiWire(wire);
}

bool Arch::checkWireAvail(WireId wire) const
{
    return (!uarch || uarch->checkWireAvail(wire)) && (wire_info(wire).bound_net == nullptr);
}

NetInfo *Arch::getBoundWireNet(WireId wire) const { return wire_info(wire).bound_net; }

NetInfo *Arch::getConflictingWireNet(WireId wire) const { return wire_info(wire).bound_net; }

const std::vector<BelPin> &Arch::getWireBelPins(WireId wire) const { return wire_info(wire).bel_pins; }

linear_range<WireId> Arch::getWires() const { return linear_range<WireId>(wires.size()); }

// ---------------------------------------------------------------

PipId Arch::getPipByName(IdStringList name) const
{
    if (name.size() == 0)
        return PipId();
    auto fnd = pip_by_name.find(name);
    if (fnd == pip_by_name.end())
        NPNR_ASSERT_FALSE_STR("no pip named " + name.str(getCtx()));
    return fnd->second;
}

IdStringList Arch::getPipName(PipId pip) const { return pip_info(pip).name; }

IdString Arch::getPipType(PipId pip) const { return pip_info(pip).type; }

const std::map<IdString, std::string> &Arch::getPipAttrs(PipId pip) const { return pip_info(pip).attrs; }

uint32_t Arch::getPipChecksum(PipId pip) const { return pip.index; }

void Arch::bindPip(PipId pip, NetInfo *net, PlaceStrength strength)
{
    WireId wire = pip_info(pip).dstWire;
    if (uarch) {
        uarch->notifyPipChange(pip, net);
        uarch->notifyWireChange(wire, net);
    }
    pip_info(pip).bound_net = net;
    wire_info(wire).bound_net = net;
    net->wires[wire].pip = pip;
    net->wires[wire].strength = strength;
    refreshUiPip(pip);
    refreshUiWire(wire);
}

void Arch::unbindPip(PipId pip)
{
    WireId wire = pip_info(pip).dstWire;
    if (uarch) {
        uarch->notifyPipChange(pip, nullptr);
        uarch->notifyWireChange(wire, nullptr);
    }
    wire_info(wire).bound_net->wires.erase(wire);
    pip_info(pip).bound_net = nullptr;
    wire_info(wire).bound_net = nullptr;
    refreshUiPip(pip);
    refreshUiWire(wire);
}

bool Arch::checkPipAvail(PipId pip) const
{
    return (!uarch || uarch->checkPipAvail(pip)) && (pip_info(pip).bound_net == nullptr);
}

bool Arch::checkPipAvailForNet(PipId pip, const NetInfo *net) const
{
    if (uarch && !uarch->checkPipAvailForNet(pip, net))
        return false;
    NetInfo *bound_net = pip_info(pip).bound_net;
    return bound_net == nullptr || bound_net == net;
}

NetInfo *Arch::getBoundPipNet(PipId pip) const { return pip_info(pip).bound_net; }

NetInfo *Arch::getConflictingPipNet(PipId pip) const { return pip_info(pip).bound_net; }

WireId Arch::getConflictingPipWire(PipId pip) const
{
    return pip_info(pip).bound_net ? pip_info(pip).dstWire : WireId();
}

linear_range<PipId> Arch::getPips() const { return linear_range<PipId>(pips.size()); }

Loc Arch::getPipLocation(PipId pip) const { return pip_info(pip).loc; }

WireId Arch::getPipSrcWire(PipId pip) const { return pip_info(pip).srcWire; }

WireId Arch::getPipDstWire(PipId pip) const { return pip_info(pip).dstWire; }

DelayQuad Arch::getPipDelay(PipId pip) const { return DelayQuad(pip_info(pip).delay); }

const std::vector<PipId> &Arch::getPipsDownhill(WireId wire) const { return wire_info(wire).downhill; }

const std::vector<PipId> &Arch::getPipsUphill(WireId wire) const { return wire_info(wire).uphill; }

// ---------------------------------------------------------------

GroupId Arch::getGroupByName(IdStringList name) const { return name; }

IdStringList Arch::getGroupName(GroupId group) const { return group; }

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
    if (uarch)
        return uarch->estimateDelay(src, dst);
    const WireInfo &s = wire_info(src);
    const WireInfo &d = wire_info(dst);
    int dx = abs(s.x - d.x);
    int dy = abs(s.y - d.y);
    return (dx + dy) * args.delayScale + args.delayOffset;
}

delay_t Arch::predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const
{
    if (uarch)
        return uarch->predictDelay(src_bel, src_pin, dst_bel, dst_pin);
    auto driver_loc = getBelLocation(src_bel);
    auto sink_loc = getBelLocation(dst_bel);

    int dx = abs(sink_loc.x - driver_loc.x);
    int dy = abs(sink_loc.y - driver_loc.y);
    return (dx + dy) * args.delayScale + args.delayOffset;
}

BoundingBox Arch::getRouteBoundingBox(WireId src, WireId dst) const
{
    if (uarch)
        return uarch->getRouteBoundingBox(src, dst);
    BoundingBox bb;

    int src_x = wire_info(src).x;
    int src_y = wire_info(src).y;
    int dst_x = wire_info(dst).x;
    int dst_y = wire_info(dst).y;

    bb.x0 = src_x;
    bb.y0 = src_y;
    bb.x1 = src_x;
    bb.y1 = src_y;

    auto extend = [&](int x, int y) {
        bb.x0 = std::min(bb.x0, x);
        bb.x1 = std::max(bb.x1, x);
        bb.y0 = std::min(bb.y0, y);
        bb.y1 = std::max(bb.y1, y);
    };
    extend(dst_x, dst_y);
    return bb;
}

// ---------------------------------------------------------------

bool Arch::place()
{
    if (uarch)
        uarch->prePlace();
    std::string placer = str_or_default(settings, id("placer"), defaultPlacer);
    if (placer == "heap") {
        bool have_iobuf_or_constr = false;
        for (auto &cell : cells) {
            CellInfo *ci = cell.second.get();
            if (ci->isPseudo() || ci->type == id("GENERIC_IOB") || ci->bel != BelId() || ci->attrs.count(id("BEL"))) {
                have_iobuf_or_constr = true;
                break;
            }
        }
        bool retVal;
        if (!have_iobuf_or_constr && !uarch) {
            log_warning("Unable to use HeAP due to a lack of IO buffers or constrained cells as anchors; reverting to "
                        "SA.\n");
            retVal = placer1(getCtx(), Placer1Cfg(getCtx()));
        } else {
            PlacerHeapCfg cfg(getCtx());
            cfg.ioBufTypes.insert(id("GENERIC_IOB"));
            retVal = placer_heap(getCtx(), cfg);
        }
        if (uarch)
            uarch->postPlace();
        getCtx()->settings[getCtx()->id("place")] = 1;
        archInfoToAttributes();
        return retVal;
    } else if (placer == "sa") {
        bool retVal = placer1(getCtx(), Placer1Cfg(getCtx()));
        if (uarch)
            uarch->postPlace();
        getCtx()->settings[getCtx()->id("place")] = 1;
        archInfoToAttributes();
        return retVal;
    } else {
        log_error("Generic architecture does not support placer '%s'\n", placer.c_str());
    }
}

bool Arch::route()
{
    if (uarch)
        uarch->preRoute();
    std::string router = str_or_default(settings, id("router"), defaultRouter);
    bool result;
    if (router == "router1") {
        result = router1(getCtx(), Router1Cfg(getCtx()));
    } else if (router == "router2") {
        router2(getCtx(), Router2Cfg(getCtx()));
        result = true;
    } else {
        log_error("iCE40 architecture does not support router '%s'\n", router.c_str());
    }
    if (uarch)
        uarch->postRoute();
    getCtx()->settings[getCtx()->id("route")] = 1;
    archInfoToAttributes();
    return result;
}

// ---------------------------------------------------------------

const std::vector<GraphicElement> &Arch::getDecalGraphics(DecalId decal) const
{
    if (!decal_graphics.count(decal)) {
        std::cerr << "No decal named " << decal.name.str(getCtx()) << std::endl;
    }
    return decal_graphics.at(decal);
}

DecalXY Arch::getBelDecal(BelId bel) const
{
    DecalXY result = bel_info(bel).decalxy;
    result.decal.active = getBoundBelCell(bel) != nullptr;
    return result;
}

DecalXY Arch::getWireDecal(WireId wire) const
{
    DecalXY result = wire_info(wire).decalxy;
    result.decal.active = getBoundWireNet(wire) != nullptr;
    return result;
}

DecalXY Arch::getPipDecal(PipId pip) const
{
    DecalXY result = pip_info(pip).decalxy;
    result.decal.active = getBoundPipNet(pip) != nullptr;
    return result;
}

DecalXY Arch::getGroupDecal(GroupId group) const { return groups.at(group).decalxy; }

// ---------------------------------------------------------------

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const
{
    if (!cellTiming.count(cell->name))
        return false;
    const auto &tmg = cellTiming.at(cell->name);
    auto fnd = tmg.combDelays.find(CellDelayKey{fromPort, toPort});
    if (fnd != tmg.combDelays.end()) {
        delay = fnd->second;
        return true;
    } else {
        return false;
    }
}

// Get the port class, also setting clockPort if applicable
TimingPortClass Arch::getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
{
    if (!cellTiming.count(cell->name))
        return TMG_IGNORE;
    const auto &tmg = cellTiming.at(cell->name);
    if (tmg.clockingInfo.count(port))
        clockInfoCount = int(tmg.clockingInfo.at(port).size());
    else
        clockInfoCount = 0;
    return get_or_default(tmg.portClasses, port, TMG_IGNORE);
}

TimingClockingInfo Arch::getPortClockingInfo(const CellInfo *cell, IdString port, int index) const
{
    NPNR_ASSERT(cellTiming.count(cell->name));
    const auto &tmg = cellTiming.at(cell->name);
    NPNR_ASSERT(tmg.clockingInfo.count(port));
    return tmg.clockingInfo.at(port).at(index);
}

bool Arch::isBelLocationValid(BelId bel, bool explain_invalid) const
{
    if (uarch)
        return uarch->isBelLocationValid(bel, explain_invalid);
    std::vector<const CellInfo *> cells;
    Loc loc = getBelLocation(bel);
    for (auto tbel : getBelsByTile(loc.x, loc.y)) {
        CellInfo *bound = getBoundBelCell(tbel);
        if (bound != nullptr)
            cells.push_back(bound);
    }
    return cellsCompatible(cells.data(), int(cells.size()));
}

const std::string Arch::defaultPlacer = "heap";

const std::vector<std::string> Arch::availablePlacers = {"sa", "heap"};

const std::string Arch::defaultRouter = "router1";
const std::vector<std::string> Arch::availableRouters = {"router1", "router2"};

void Arch::assignArchInfo()
{
    int index = 0;
    for (auto &cell : getCtx()->cells) {
        CellInfo *ci = cell.second.get();
        if (ci->type == id("GENERIC_SLICE")) {
            ci->is_slice = true;
            ci->slice_clk = ci->getPort(id("CLK"));
        } else {
            ci->is_slice = false;
        }
        ci->user_group = int_or_default(ci->attrs, id("PACK_GROUP"), -1);
        // If no manual cell->bel pin rule has been created; assign a default one
        for (auto &p : ci->ports)
            if (!ci->bel_pins.count(p.first))
                ci->bel_pins.emplace(p.first, std::vector<IdString>{p.first});
        ci->flat_index = index;
        ++index;
    }
}

bool Arch::cellsCompatible(const CellInfo **cells, int count) const
{
    const NetInfo *clk = nullptr;
    int group = -1;
    for (int i = 0; i < count; i++) {
        const CellInfo *ci = cells[i];
        if (ci->is_slice && ci->slice_clk != nullptr) {
            if (clk == nullptr)
                clk = ci->slice_clk;
            else if (clk != ci->slice_clk)
                return false;
        }
        if (ci->user_group != -1) {
            if (group == -1)
                group = ci->user_group;
            else if (group != ci->user_group)
                return false;
        }
    }
    return true;
}

NEXTPNR_NAMESPACE_END
