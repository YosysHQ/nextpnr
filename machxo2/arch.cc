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

#include <iostream>
#include <math.h>
#include "nextpnr.h"
#include "embed.h"
#include "placer1.h"
#include "placer_heap.h"
#include "router1.h"
#include "router2.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

// -----------------------------------------------------------------------

void IdString::initialize_arch(const BaseCtx *ctx) {
    #define X(t) initialize_add(ctx, #t, ID_##t);

    #include "constids.inc"

    #undef X
}

// ---------------------------------------------------------------

static const ChipInfoPOD *get_chip_info(ArchArgs::ArchArgsTypes chip)
{
    std::string chipdb;
    if (chip == ArchArgs::LCMXO2_256HC) {
        chipdb = "machxo2/chipdb-256.bin";
    } else if (chip == ArchArgs::LCMXO2_640HC) {
        chipdb = "machxo2/chipdb-640.bin";
    } else if (chip == ArchArgs::LCMXO2_1200HC) {
        chipdb = "machxo2/chipdb-1200.bin";
    } else if (chip == ArchArgs::LCMXO2_2000HC) {
        chipdb = "machxo2/chipdb-2000.bin";
    } else if (chip == ArchArgs::LCMXO2_4000HC) {
        chipdb = "machxo2/chipdb-4000.bin";
    } else if (chip == ArchArgs::LCMXO2_7000HC) {
        chipdb = "machxo2/chipdb-7000.bin";
    } else {
        log_error("Unknown chip\n");
    }

    auto ptr = reinterpret_cast<const RelPtr<ChipInfoPOD> *>(get_chipdb(chipdb));
    if (ptr == nullptr)
        return nullptr;
    return ptr->get();
}

// ---------------------------------------------------------------

Arch::Arch(ArchArgs args) : args(args)
{
    chip_info = get_chip_info(args.type);
    if (chip_info == nullptr)
        log_error("Unsupported MachXO2 chip type.\n");
    if (chip_info->const_id_count != DB_CONST_ID_COUNT)
        log_error("Chip database 'bba' and nextpnr code are out of sync; please rebuild (or contact distribution "
                  "maintainer)!\n");

    package_info = nullptr;
    for (int i = 0; i < chip_info->num_packages; i++) {
        if (args.package == chip_info->package_info[i].name.get()) {
            package_info = &(chip_info->package_info[i]);
            break;
        }
    }
    if (!package_info)
        log_error("Unsupported package '%s' for '%s'.\n", args.package.c_str(), getChipName().c_str());
}

bool Arch::isAvailable(ArchArgs::ArchArgsTypes chip) { return get_chip_info(chip) != nullptr; }

std::string Arch::getChipName() const
{
    if (args.type == ArchArgs::LCMXO2_256HC) {
        return "LCMXO2-256HC";
    } else if (args.type == ArchArgs::LCMXO2_640HC) {
        return "LCMXO2-640HC";
    } else if (args.type == ArchArgs::LCMXO2_1200HC) {
        return "LCMXO2-1200HC";
    } else if (args.type == ArchArgs::LCMXO2_2000HC) {
        return "LCMXO2-2000HC";
    } else if (args.type == ArchArgs::LCMXO2_4000HC) {
        return "LCMXO2-4000HC";
    } else if (args.type == ArchArgs::LCMXO2_7000HC) {
        return "LCMXO2-7000HC";
    } else {
        log_error("Unknown chip\n");
    }
}

// ---------------------------------------------------------------

BelId Arch::getBelByName(IdString name) const
{
    return BelId();
}

IdString Arch::getBelName(BelId bel) const { return IdString(); }

Loc Arch::getBelLocation(BelId bel) const
{
    return Loc();
}

BelId Arch::getBelByLocation(Loc loc) const
{
    return BelId();
}

const std::vector<BelId> &Arch::getBelsByTile(int x, int y) const { return bel_id_dummy; }

bool Arch::getBelGlobalBuf(BelId bel) const { return false; }

uint32_t Arch::getBelChecksum(BelId bel) const
{
    // FIXME
    return 0;
}

void Arch::bindBel(BelId bel, CellInfo *cell, PlaceStrength strength)
{

}

void Arch::unbindBel(BelId bel)
{

}

bool Arch::checkBelAvail(BelId bel) const { return false; }

CellInfo *Arch::getBoundBelCell(BelId bel) const { return nullptr; }

CellInfo *Arch::getConflictingBelCell(BelId bel) const { return nullptr; }

const std::map<IdString, std::string> &Arch::getBelAttrs(BelId bel) const { return attrs_dummy; }

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
    return WireId();
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const { return PortType(); }

std::vector<IdString> Arch::getBelPins(BelId bel) const
{
    std::vector<IdString> ret;
    return ret;
}

// ---------------------------------------------------------------

WireId Arch::getWireByName(IdString name) const
{
    return WireId();
}

IdString Arch::getWireName(WireId wire) const { return IdString(); }

IdString Arch::getWireType(WireId wire) const { return IdString(); }

const std::map<IdString, std::string> &Arch::getWireAttrs(WireId wire) const { return attrs_dummy; }

uint32_t Arch::getWireChecksum(WireId wire) const
{
    // FIXME
    return 0;
}

void Arch::bindWire(WireId wire, NetInfo *net, PlaceStrength strength)
{

}

void Arch::unbindWire(WireId wire)
{

}

bool Arch::checkWireAvail(WireId wire) const { return false; }

NetInfo *Arch::getBoundWireNet(WireId wire) const { return nullptr; }

NetInfo *Arch::getConflictingWireNet(WireId wire) const { return nullptr; }

const std::vector<BelPin> &Arch::getWireBelPins(WireId wire) const { return bel_pin_dummy; }

const std::vector<WireId> &Arch::getWires() const { return wire_id_dummy; }

// ---------------------------------------------------------------

PipId Arch::getPipByName(IdString name) const
{
    return PipId();
}

IdString Arch::getPipName(PipId pip) const { return IdString(); }

IdString Arch::getPipType(PipId pip) const { return IdString(); }

const std::map<IdString, std::string> &Arch::getPipAttrs(PipId pip) const { return attrs_dummy; }

uint32_t Arch::getPipChecksum(PipId wire) const
{
    // FIXME
    return 0;
}

void Arch::bindPip(PipId pip, NetInfo *net, PlaceStrength strength)
{

}

void Arch::unbindPip(PipId pip)
{

}

bool Arch::checkPipAvail(PipId pip) const { return false; }

NetInfo *Arch::getBoundPipNet(PipId pip) const { return nullptr; }

NetInfo *Arch::getConflictingPipNet(PipId pip) const { return nullptr; }

WireId Arch::getConflictingPipWire(PipId pip) const { return WireId(); }

const std::vector<PipId> &Arch::getPips() const { return pip_id_dummy; }

Loc Arch::getPipLocation(PipId pip) const { return Loc(); }

WireId Arch::getPipSrcWire(PipId pip) const { return WireId(); }

WireId Arch::getPipDstWire(PipId pip) const { return WireId(); }

DelayInfo Arch::getPipDelay(PipId pip) const { return DelayInfo(); }

const std::vector<PipId> &Arch::getPipsDownhill(WireId wire) const { return pip_id_dummy; }

const std::vector<PipId> &Arch::getPipsUphill(WireId wire) const { return pip_id_dummy; }

const std::vector<PipId> &Arch::getWireAliases(WireId wire) const { return pip_id_dummy; }

// ---------------------------------------------------------------

GroupId Arch::getGroupByName(IdString name) const { return GroupId(); }

IdString Arch::getGroupName(GroupId group) const { return IdString(); }

std::vector<GroupId> Arch::getGroups() const
{
    std::vector<GroupId> ret;
    return ret;
}

const std::vector<BelId> &Arch::getGroupBels(GroupId group) const { return bel_id_dummy; }

const std::vector<WireId> &Arch::getGroupWires(GroupId group) const { return wire_id_dummy; }

const std::vector<PipId> &Arch::getGroupPips(GroupId group) const { return pip_id_dummy; }

const std::vector<GroupId> &Arch::getGroupGroups(GroupId group) const { return group_id_dummy; }

// ---------------------------------------------------------------

delay_t Arch::estimateDelay(WireId src, WireId dst) const
{
    return 0;
}

delay_t Arch::predictDelay(const NetInfo *net_info, const PortRef &sink) const
{
    return 0;
}

bool Arch::getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const { return false; }

ArcBounds Arch::getRouteBoundingBox(WireId src, WireId dst) const
{
    ArcBounds bb;

    return bb;
}

// ---------------------------------------------------------------

bool Arch::place()
{
    std::string placer = str_or_default(settings, id("placer"), defaultPlacer);
    if (placer == "sa") {
        bool retVal = placer1(getCtx(), Placer1Cfg(getCtx()));
        getCtx()->settings[getCtx()->id("place")] = 1;
        archInfoToAttributes();
        return retVal;
    } else {
        log_error("MachXO2 architecture does not support placer '%s'\n", placer.c_str());
    }
}

bool Arch::route()
{
    std::string router = str_or_default(settings, id("router"), defaultRouter);
    bool result;
    if (router == "router1") {
        result = router1(getCtx(), Router1Cfg(getCtx()));
    } else if (router == "router2") {
        router2(getCtx(), Router2Cfg(getCtx()));
        result = true;
    } else {
        log_error("MachXO2 architecture does not support router '%s'\n", router.c_str());
    }
    getCtx()->settings[getCtx()->id("route")] = 1;
    archInfoToAttributes();
    return result;
}

// ---------------------------------------------------------------

const std::vector<GraphicElement> &Arch::getDecalGraphics(DecalId decal) const
{
    return graphic_element_dummy;
}

DecalXY Arch::getBelDecal(BelId bel) const { return DecalXY(); }

DecalXY Arch::getWireDecal(WireId wire) const { return DecalXY(); }

DecalXY Arch::getPipDecal(PipId pip) const { return DecalXY(); }

DecalXY Arch::getGroupDecal(GroupId group) const { return DecalXY(); }

// ---------------------------------------------------------------

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const
{
    return false;
}

// Get the port class, also setting clockPort if applicable
TimingPortClass Arch::getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
{
    return TMG_IGNORE;
}

TimingClockingInfo Arch::getPortClockingInfo(const CellInfo *cell, IdString port, int index) const
{
    return TimingClockingInfo();
}

bool Arch::isValidBelForCell(CellInfo *cell, BelId bel) const
{
    return false;
}

bool Arch::isBelLocationValid(BelId bel) const
{
    return false;
}

#ifdef WITH_HEAP
const std::string Arch::defaultPlacer = "heap";
#else
const std::string Arch::defaultPlacer = "sa";
#endif

const std::vector<std::string> Arch::availablePlacers = {"sa",
#ifdef WITH_HEAP
                                                         "heap"
#endif
};

const std::string Arch::defaultRouter = "router1";
const std::vector<std::string> Arch::availableRouters = {"router1", "router2"};

void Arch::assignArchInfo()
{

}

bool Arch::cellsCompatible(const CellInfo **cells, int count) const
{
    return false;
}

NEXTPNR_NAMESPACE_END
