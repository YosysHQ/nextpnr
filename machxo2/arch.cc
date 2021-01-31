/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xen <claire@symbioticeda.com>
 *  Copyright (C) 2021  William D. Jones <wjones@wdj-consulting.com>
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
#include "embed.h"
#include "nextpnr.h"
#include "placer1.h"
#include "placer_heap.h"
#include "router1.h"
#include "router2.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

static std::tuple<int, int, std::string> split_identifier_name(const std::string &name)
{
    size_t first_slash = name.find('/');
    NPNR_ASSERT(first_slash != std::string::npos);
    size_t second_slash = name.find('/', first_slash + 1);
    NPNR_ASSERT(second_slash != std::string::npos);
    return std::make_tuple(std::stoi(name.substr(1, first_slash)),
                           std::stoi(name.substr(first_slash + 2, second_slash - first_slash)),
                           name.substr(second_slash + 1));
};

// -----------------------------------------------------------------------

void IdString::initialize_arch(const BaseCtx *ctx)
{
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

    bel_to_cell.resize(chip_info->height * chip_info->width * max_loc_bels, nullptr);
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

std::string Arch::getFullChipName() const
{
    std::string name = getChipName();
    name += "-";
    switch (args.speed) {
    case ArchArgs::SPEED_1:
        name += "1";
        break;
    case ArchArgs::SPEED_2:
        name += "2";
        break;
    case ArchArgs::SPEED_3:
        name += "3";
    case ArchArgs::SPEED_4:
        name += "4";
        break;
    case ArchArgs::SPEED_5:
        name += "5";
        break;
    case ArchArgs::SPEED_6:
        name += "6";
        break;
    }
    name += args.package;
    return name;
}

IdString Arch::archArgsToId(ArchArgs args) const
{
    if (args.type == ArchArgs::LCMXO2_256HC) {
        return id("lcmxo2_256hc");
    } else if (args.type == ArchArgs::LCMXO2_640HC) {
        return id("lcmxo2_640hc");
    } else if (args.type == ArchArgs::LCMXO2_1200HC) {
        return id("lcmxo2_1200hc");
    } else if (args.type == ArchArgs::LCMXO2_2000HC) {
        return id("lcmxo2_2000hc");
    } else if (args.type == ArchArgs::LCMXO2_4000HC) {
        return id("lcmxo2_4000hc");
    } else if (args.type == ArchArgs::LCMXO2_7000HC) {
        return id("lcmxo2_7000hc");
    }

    return IdString();
}

// ---------------------------------------------------------------

BelId Arch::getBelByName(IdString name) const
{
    BelId ret;
    auto it = bel_by_name.find(name);
    if (it != bel_by_name.end())
        return it->second;

    Location loc;
    std::string basename;
    std::tie(loc.x, loc.y, basename) = split_identifier_name(name.str(this));
    ret.location = loc;
    const TileTypePOD *tilei = tileInfo(ret);
    for (int i = 0; i < tilei->num_bels; i++) {
        if (std::strcmp(tilei->bel_data[i].name.get(), basename.c_str()) == 0) {
            ret.index = i;
            break;
        }
    }
    if (ret.index >= 0)
        bel_by_name[name] = ret;
    return ret;
}

BelId Arch::getBelByLocation(Loc loc) const
{
    BelId ret;

    if (loc.x >= chip_info->width || loc.y >= chip_info->height)
        return BelId();

    ret.location.x = loc.x;
    ret.location.y = loc.y;

    const TileTypePOD *tilei = tileInfo(ret);
    for (int i = 0; i < tilei->num_bels; i++) {
        if (tilei->bel_data[i].z == loc.z) {
            ret.index = i;
            return ret;
        }
    }

    return BelId();
}

BelRange Arch::getBelsByTile(int x, int y) const
{
    BelRange br;

    br.b.cursor_tile = y * chip_info->width + x;
    br.e.cursor_tile = y * chip_info->width + x;
    br.b.cursor_index = 0;
    br.e.cursor_index = chip_info->tiles[y * chip_info->width + x].num_bels - 1;
    br.b.chip = chip_info;
    br.e.chip = chip_info;
    if (br.e.cursor_index == -1)
        ++br.e.cursor_index;
    else
        ++br.e;
    return br;
}

bool Arch::getBelGlobalBuf(BelId bel) const { return false; }

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = tileInfo(bel)->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = &*tileInfo(bel)->bel_data[bel.index].bel_wires;

    for (int i = 0; i < num_bel_wires; i++)
        if (bel_wires[i].port == pin.index) {
            WireId ret;

            ret.location.x = bel_wires[i].rel_wire_loc.x;
            ret.location.y = bel_wires[i].rel_wire_loc.y;
            ret.index = bel_wires[i].wire_index;

            return ret;
        }

    return WireId();
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const
{
    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = tileInfo(bel)->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = &*tileInfo(bel)->bel_data[bel.index].bel_wires;

    for (int i = 0; i < num_bel_wires; i++)
        if (bel_wires[i].port == pin.index)
            return PortType(bel_wires[i].dir);

    return PORT_INOUT;
}

std::vector<IdString> Arch::getBelPins(BelId bel) const
{
    std::vector<IdString> ret;
    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = tileInfo(bel)->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = &*tileInfo(bel)->bel_data[bel.index].bel_wires;

    for (int i = 0; i < num_bel_wires; i++) {
        IdString id(bel_wires[i].port);
        ret.push_back(id);
    }

    return ret;
}

// ---------------------------------------------------------------

BelId Arch::getPackagePinBel(const std::string &pin) const
{
    for (int i = 0; i < package_info->num_pins; i++) {
        if (package_info->pin_data[i].name.get() == pin) {
            BelId bel;
            bel.location = package_info->pin_data[i].abs_loc;
            bel.index = package_info->pin_data[i].bel_index;
            return bel;
        }
    }
    return BelId();
}

// ---------------------------------------------------------------

WireId Arch::getWireByName(IdString name) const
{
    WireId ret;

    auto it = wire_by_name.find(name);
    if (it != wire_by_name.end())
        return it->second;

    Location loc;
    std::string basename;
    std::tie(loc.x, loc.y, basename) = split_identifier_name(name.str(this));
    ret.location = loc;

    const TileTypePOD *tilei = tileInfo(ret);
    for (int i = 0; i < tilei->num_wires; i++) {
        if (std::strcmp(tilei->wire_data[i].name.get(), basename.c_str()) == 0) {
            ret.index = i;
            break;
        }
    }
    if (ret.index >= 0)
        wire_by_name[name] = ret;
    return ret;
}

// ---------------------------------------------------------------

PipId Arch::getPipByName(IdString name) const
{
    PipId ret;

    auto it = pip_by_name.find(name);
    if (it != pip_by_name.end())
        return it->second;

    Location loc;
    std::string basename;
    std::tie(loc.x, loc.y, basename) = split_identifier_name(name.str(this));
    ret.location = loc;

    const TileTypePOD *tilei = tileInfo(ret);
    for (int i = 0; i < tilei->num_pips; i++) {
        PipId curr;
        curr.location = loc;
        curr.index = i;
        pip_by_name[getPipName(curr)] = curr;
    }
    if (pip_by_name.find(name) == pip_by_name.end())
        NPNR_ASSERT_FALSE_STR("no pip named " + name.str(this));
    return pip_by_name[name];
}

IdString Arch::getPipName(PipId pip) const
{
    NPNR_ASSERT(pip != PipId());

    int x = pip.location.x;
    int y = pip.location.y;

    std::string src_name = getWireName(getPipSrcWire(pip)).str(this);
    std::replace(src_name.begin(), src_name.end(), '/', '.');

    std::string dst_name = getWireName(getPipDstWire(pip)).str(this);
    std::replace(dst_name.begin(), dst_name.end(), '/', '.');

    return id("X" + std::to_string(x) + "/Y" + std::to_string(y) + "/" + src_name + ".->." + dst_name);
}

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
    // Taxicab distance multiplied by pipDelay (0.01) and fake wireDelay (0.01).
    // TODO: This function will not work well for entrance to global routing,
    // as the entrances are located physically far from the DCCAs.
    return (abs(dst.location.x - src.location.x) + abs(dst.location.y - src.location.y)) * (0.01 + 0.01);
}

delay_t Arch::predictDelay(const NetInfo *net_info, const PortRef &sink) const
{
    BelId src = net_info->driver.cell->bel;
    BelId dst = sink.cell->bel;

    NPNR_ASSERT(src != BelId());
    NPNR_ASSERT(dst != BelId());

    // TODO: Same deal applies here as with estimateDelay.
    return (abs(dst.location.x - src.location.x) + abs(dst.location.y - src.location.y)) * (0.01 + 0.01);
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

const std::vector<GraphicElement> &Arch::getDecalGraphics(DecalId decal) const { return graphic_element_dummy; }

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
    // FIXME: Unlike ECP5, SLICEs in a given tile do not share a clock, so
    // any SLICE Cell is valid for any BEL, even if some cells are already
    // bound to BELs in the tile. However, this may need to be filled in once
    // more than one LUT4 and DFF type is supported.
    return true;
}

bool Arch::isBelLocationValid(BelId bel) const
{
    // FIXME: Same deal as isValidBelForCell.
    return true;
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

void Arch::assignArchInfo() {}

bool Arch::cellsCompatible(const CellInfo **cells, int count) const { return false; }

NEXTPNR_NAMESPACE_END
