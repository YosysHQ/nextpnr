/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  Serge Bazanski <q3k@symbioticeda.com>
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

#include <algorithm>
#include <cmath>
#include "cells.h"
#include "gfx.h"
#include "log.h"
#include "nextpnr.h"
#include "placer1.h"
#include "router1.h"
#include "util.h"

#include "torc/common/DirectoryTree.hpp"

NEXTPNR_NAMESPACE_BEGIN


const DDB *ddb = nullptr;
const Sites *ddbSites = nullptr;
const Tiles *ddbTiles = nullptr;


// -----------------------------------------------------------------------

void IdString::initialize_arch(const BaseCtx *ctx)
{
#define X(t) initialize_add(ctx, #t, ID_##t);
#include "constids.inc"
#undef X
}

// -----------------------------------------------------------------------

Arch::Arch(ArchArgs args) : args(args)
{
    torc::common::DirectoryTree directoryTree("../../torc/src/torc");
    if (args.type == ArchArgs::Z020) {
        ddb = new DDB("xc7z020", "clg484");
    } else {
        log_error("Unsupported XC7 chip type.\n");
    }

//    package_info = nullptr;
//    for (int i = 0; i < chip_info->num_packages; i++) {
//        if (chip_info->packages_data[i].name.get() == args.package) {
//            package_info = &(chip_info->packages_data[i]);
//            break;
//        }
//    }
//    if (package_info == nullptr)
//        log_error("Unsupported package '%s'.\n", args.package.c_str());

    ddbSites = &ddb->getSites();
    ddbTiles = &ddb->getTiles();

    bel_to_cell.resize(ddbSites->getSiteCount());
}

// -----------------------------------------------------------------------

std::string Arch::getChipName() const
{
    if (args.type == ArchArgs::Z020) {
        return "z020";
    } else {
        log_error("Unsupported XC7 chip type.\n");
    }
}

// -----------------------------------------------------------------------

IdString Arch::archArgsToId(ArchArgs args) const
{
    if (args.type == ArchArgs::Z020)
        return id("z020");
    return IdString();
}

// -----------------------------------------------------------------------

BelId Arch::getBelByName(IdString name) const
{
    BelId ret;

    auto it = ddbSites->findSiteIndex(name.str(this));
    if (it != SiteIndex(-1))
        ret.index = it;

    return ret;
}

BelId Arch::getBelByLocation(Loc loc) const
{
    BelId bel;

    if (bel_by_loc.empty()) {
        for (SiteIndex i(0); i < ddbSites->getSiteCount(); ++i) {
            BelId b;
            b.index = i;
            bel_by_loc[getBelLocation(b)] = b;
        }
    }

    auto it = bel_by_loc.find(loc);
    if (it != bel_by_loc.end())
        bel = it->second;

    return bel;
}

BelRange Arch::getBelsByTile(int x, int y) const
{
    BelRange br;

    br.b.cursor = std::next(ddbSites->getSites().begin(), Arch::getBelByLocation(Loc(x, y, 0)).index);
    br.e.cursor = br.b.cursor;

    if (br.e.cursor != ddbSites->getSites().end()) {
        while (br.e.cursor < ddbSites->getSites().end() && ddbSites->getSite((*br.e).index).getTileIndex() == ddbSites->getSite((*br.b).index).getTileIndex())
            br.e.cursor++;
    }

    return br;
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const
{
    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = chip_info->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = chip_info->bel_data[bel.index].bel_wires.get();

    if (num_bel_wires < 7) {
        for (int i = 0; i < num_bel_wires; i++) {
            if (bel_wires[i].port == pin.index)
                return PortType(bel_wires[i].type);
        }
    } else {
        int b = 0, e = num_bel_wires - 1;
        while (b <= e) {
            int i = (b + e) / 2;
            if (bel_wires[i].port == pin.index)
                return PortType(bel_wires[i].type);
            if (bel_wires[i].port > pin.index)
                e = i - 1;
            else
                b = i + 1;
        }
    }

    return PORT_INOUT;
}

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
    WireId ret;

//    NPNR_ASSERT(bel != BelId());
//
//    int num_bel_wires = chip_info->bel_data[bel.index].num_bel_wires;
//    const BelWirePOD *bel_wires = chip_info->bel_data[bel.index].bel_wires.get();
//
//    if (num_bel_wires < 7) {
//        for (int i = 0; i < num_bel_wires; i++) {
//            if (bel_wires[i].port == pin.index) {
//                ret.index = bel_wires[i].wire_index;
//                break;
//            }
//        }
//    } else {
//        int b = 0, e = num_bel_wires - 1;
//        while (b <= e) {
//            int i = (b + e) / 2;
//            if (bel_wires[i].port == pin.index) {
//                ret.index = bel_wires[i].wire_index;
//                break;
//            }
//            if (bel_wires[i].port > pin.index)
//                e = i - 1;
//            else
//                b = i + 1;
//        }
//    }

    return ret;
}

std::vector<IdString> Arch::getBelPins(BelId bel) const
{
    std::vector<IdString> ret;

    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = chip_info->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = chip_info->bel_data[bel.index].bel_wires.get();

    for (int i = 0; i < num_bel_wires; i++)
        ret.push_back(IdString(bel_wires[i].port));

    return ret;
}

// -----------------------------------------------------------------------

WireId Arch::getWireByName(IdString name) const
{
    WireId ret;

    if (wire_by_name.empty()) {
        for (int i = 0; i < chip_info->num_wires; i++)
            wire_by_name[id(chip_info->wire_data[i].name.get())] = i;
    }

    auto it = wire_by_name.find(name);
    if (it != wire_by_name.end())
        ret.index = it->second;

    return ret;
}

IdString Arch::getWireType(WireId wire) const
{
    NPNR_ASSERT(wire != WireId());
    switch (chip_info->wire_data[wire.index].type) {
    case WireInfoPOD::WIRE_TYPE_NONE:
        return IdString();
    case WireInfoPOD::WIRE_TYPE_GLB2LOCAL:
        return id("GLB2LOCAL");
    case WireInfoPOD::WIRE_TYPE_GLB_NETWK:
        return id("GLB_NETWK");
    case WireInfoPOD::WIRE_TYPE_LOCAL:
        return id("LOCAL");
    case WireInfoPOD::WIRE_TYPE_LUTFF_IN:
        return id("LUTFF_IN");
    case WireInfoPOD::WIRE_TYPE_LUTFF_IN_LUT:
        return id("LUTFF_IN_LUT");
    case WireInfoPOD::WIRE_TYPE_LUTFF_LOUT:
        return id("LUTFF_LOUT");
    case WireInfoPOD::WIRE_TYPE_LUTFF_OUT:
        return id("LUTFF_OUT");
    case WireInfoPOD::WIRE_TYPE_LUTFF_COUT:
        return id("LUTFF_COUT");
    case WireInfoPOD::WIRE_TYPE_LUTFF_GLOBAL:
        return id("LUTFF_GLOBAL");
    case WireInfoPOD::WIRE_TYPE_CARRY_IN_MUX:
        return id("CARRY_IN_MUX");
    case WireInfoPOD::WIRE_TYPE_SP4_V:
        return id("SP4_V");
    case WireInfoPOD::WIRE_TYPE_SP4_H:
        return id("SP4_H");
    case WireInfoPOD::WIRE_TYPE_SP12_V:
        return id("SP12_V");
    case WireInfoPOD::WIRE_TYPE_SP12_H:
        return id("SP12_H");
    }
    return IdString();
}

// -----------------------------------------------------------------------

PipId Arch::getPipByName(IdString name) const
{
    PipId ret;

    if (pip_by_name.empty()) {
        for (int i = 0; i < chip_info->num_pips; i++) {
            PipId pip;
            pip.index = i;
            pip_by_name[getPipName(pip)] = i;
        }
    }

    auto it = pip_by_name.find(name);
    if (it != pip_by_name.end())
        ret.index = it->second;

    return ret;
}

IdString Arch::getPipName(PipId pip) const
{
    NPNR_ASSERT(pip != PipId());

#if 1
    int x = chip_info->pip_data[pip.index].x;
    int y = chip_info->pip_data[pip.index].y;

    std::string src_name = chip_info->wire_data[chip_info->pip_data[pip.index].src].name.get();
    std::replace(src_name.begin(), src_name.end(), '/', '.');

    std::string dst_name = chip_info->wire_data[chip_info->pip_data[pip.index].dst].name.get();
    std::replace(dst_name.begin(), dst_name.end(), '/', '.');

    return id("X" + std::to_string(x) + "/Y" + std::to_string(y) + "/" + src_name + ".->." + dst_name);
#else
    return id(chip_info->pip_data[pip.index].name.get());
#endif
}

// -----------------------------------------------------------------------

BelId Arch::getPackagePinBel(const std::string &pin) const
{
//    for (int i = 0; i < package_info->num_pins; i++) {
//        if (package_info->pins[i].name.get() == pin) {
//            BelId id;
//            id.index = package_info->pins[i].bel_index;
//            return id;
//        }
//    }
    return BelId();
}

std::string Arch::getBelPackagePin(BelId bel) const
{
//    for (int i = 0; i < package_info->num_pins; i++) {
//        if (package_info->pins[i].bel_index == bel.index) {
//            return std::string(package_info->pins[i].name.get());
//        }
//    }
    return "";
}

// -----------------------------------------------------------------------

GroupId Arch::getGroupByName(IdString name) const
{
    for (auto g : getGroups())
        if (getGroupName(g) == name)
            return g;
    return GroupId();
}

IdString Arch::getGroupName(GroupId group) const
{
    std::string suffix;

    switch (group.type) {
    case GroupId::TYPE_FRAME:
        suffix = "tile";
        break;
    case GroupId::TYPE_MAIN_SW:
        suffix = "main_sw";
        break;
    case GroupId::TYPE_LOCAL_SW:
        suffix = "local_sw";
        break;
    case GroupId::TYPE_LC0_SW:
        suffix = "lc0_sw";
        break;
    case GroupId::TYPE_LC1_SW:
        suffix = "lc1_sw";
        break;
    case GroupId::TYPE_LC2_SW:
        suffix = "lc2_sw";
        break;
    case GroupId::TYPE_LC3_SW:
        suffix = "lc3_sw";
        break;
    case GroupId::TYPE_LC4_SW:
        suffix = "lc4_sw";
        break;
    case GroupId::TYPE_LC5_SW:
        suffix = "lc5_sw";
        break;
    case GroupId::TYPE_LC6_SW:
        suffix = "lc6_sw";
        break;
    case GroupId::TYPE_LC7_SW:
        suffix = "lc7_sw";
        break;
    default:
        return IdString();
    }

    return id("X" + std::to_string(group.x) + "/Y" + std::to_string(group.y) + "/" + suffix);
}

std::vector<GroupId> Arch::getGroups() const
{
    std::vector<GroupId> ret;

    for (int y = 0; y < chip_info->height; y++) {
        for (int x = 0; x < chip_info->width; x++) {
            TileType type = chip_info->tile_grid[y * chip_info->width + x];
            if (type == TILE_NONE)
                continue;

            GroupId group;
            group.type = GroupId::TYPE_FRAME;
            group.x = x;
            group.y = y;
            // ret.push_back(group);

            group.type = GroupId::TYPE_MAIN_SW;
            ret.push_back(group);

            group.type = GroupId::TYPE_LOCAL_SW;
            ret.push_back(group);

            if (type == TILE_LOGIC) {
                group.type = GroupId::TYPE_LC0_SW;
                ret.push_back(group);

                group.type = GroupId::TYPE_LC1_SW;
                ret.push_back(group);

                group.type = GroupId::TYPE_LC2_SW;
                ret.push_back(group);

                group.type = GroupId::TYPE_LC3_SW;
                ret.push_back(group);

                group.type = GroupId::TYPE_LC4_SW;
                ret.push_back(group);

                group.type = GroupId::TYPE_LC5_SW;
                ret.push_back(group);

                group.type = GroupId::TYPE_LC6_SW;
                ret.push_back(group);

                group.type = GroupId::TYPE_LC7_SW;
                ret.push_back(group);
            }
        }
    }
    return ret;
}

std::vector<BelId> Arch::getGroupBels(GroupId group) const
{
    std::vector<BelId> ret;
    return ret;
}

std::vector<WireId> Arch::getGroupWires(GroupId group) const
{
    std::vector<WireId> ret;
    return ret;
}

std::vector<PipId> Arch::getGroupPips(GroupId group) const
{
    std::vector<PipId> ret;
    return ret;
}

std::vector<GroupId> Arch::getGroupGroups(GroupId group) const
{
    std::vector<GroupId> ret;
    return ret;
}

// -----------------------------------------------------------------------

bool Arch::getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const
{
    return false;
}

// -----------------------------------------------------------------------

bool Arch::place()
{
    Placer1Cfg cfg;
    cfg.constraintWeight = placer_constraintWeight;
    return placer1(getCtx(), cfg);
}

bool Arch::route()
{
    Router1Cfg cfg;
    return router1(getCtx(), cfg);
}

// -----------------------------------------------------------------------

DecalXY Arch::getBelDecal(BelId bel) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_BEL;
    decalxy.decal.index = bel.index;
    decalxy.decal.active = bel_to_cell.at(bel.index) != nullptr;
    return decalxy;
}

DecalXY Arch::getWireDecal(WireId wire) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_WIRE;
    decalxy.decal.index = wire.index;
    decalxy.decal.active = wire_to_net.at(wire.index) != nullptr;
    return decalxy;
}

DecalXY Arch::getPipDecal(PipId pip) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_PIP;
    decalxy.decal.index = pip.index;
    decalxy.decal.active = pip_to_net.at(pip.index) != nullptr;
    return decalxy;
};

DecalXY Arch::getGroupDecal(GroupId group) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_GROUP;
    decalxy.decal.index = (group.type << 16) | (group.x << 8) | (group.y);
    decalxy.decal.active = true;
    return decalxy;
};

std::vector<GraphicElement> Arch::getDecalGraphics(DecalId decal) const
{
    std::vector<GraphicElement> ret;

    if (decal.type == DecalId::TYPE_GROUP) {
        int type = (decal.index >> 16) & 255;
        int x = (decal.index >> 8) & 255;
        int y = decal.index & 255;

        if (type == GroupId::TYPE_FRAME) {
            GraphicElement el;
            el.type = GraphicElement::TYPE_LINE;
            el.style = GraphicElement::STYLE_FRAME;

            el.x1 = x + 0.01, el.x2 = x + 0.02, el.y1 = y + 0.01, el.y2 = y + 0.01;
            ret.push_back(el);
            el.x1 = x + 0.01, el.x2 = x + 0.01, el.y1 = y + 0.01, el.y2 = y + 0.02;
            ret.push_back(el);

            el.x1 = x + 0.99, el.x2 = x + 0.98, el.y1 = y + 0.01, el.y2 = y + 0.01;
            ret.push_back(el);
            el.x1 = x + 0.99, el.x2 = x + 0.99, el.y1 = y + 0.01, el.y2 = y + 0.02;
            ret.push_back(el);

            el.x1 = x + 0.99, el.x2 = x + 0.98, el.y1 = y + 0.99, el.y2 = y + 0.99;
            ret.push_back(el);
            el.x1 = x + 0.99, el.x2 = x + 0.99, el.y1 = y + 0.99, el.y2 = y + 0.98;
            ret.push_back(el);

            el.x1 = x + 0.01, el.x2 = x + 0.02, el.y1 = y + 0.99, el.y2 = y + 0.99;
            ret.push_back(el);
            el.x1 = x + 0.01, el.x2 = x + 0.01, el.y1 = y + 0.99, el.y2 = y + 0.98;
            ret.push_back(el);
        }

        if (type == GroupId::TYPE_MAIN_SW) {
            GraphicElement el;
            el.type = GraphicElement::TYPE_BOX;
            el.style = GraphicElement::STYLE_FRAME;

            el.x1 = x + main_swbox_x1;
            el.x2 = x + main_swbox_x2;
            el.y1 = y + main_swbox_y1;
            el.y2 = y + main_swbox_y2;
            ret.push_back(el);
        }

        if (type == GroupId::TYPE_LOCAL_SW) {
            GraphicElement el;
            el.type = GraphicElement::TYPE_BOX;
            el.style = GraphicElement::STYLE_FRAME;

            el.x1 = x + local_swbox_x1;
            el.x2 = x + local_swbox_x2;
            el.y1 = y + local_swbox_y1;
            el.y2 = y + local_swbox_y2;
            ret.push_back(el);
        }

        if (GroupId::TYPE_LC0_SW <= type && type <= GroupId::TYPE_LC7_SW) {
            GraphicElement el;
            el.type = GraphicElement::TYPE_BOX;
            el.style = GraphicElement::STYLE_FRAME;

            el.x1 = x + lut_swbox_x1;
            el.x2 = x + lut_swbox_x2;
            el.y1 = y + logic_cell_y1 + logic_cell_pitch * (type - GroupId::TYPE_LC0_SW);
            el.y2 = y + logic_cell_y2 + logic_cell_pitch * (type - GroupId::TYPE_LC0_SW);
            ret.push_back(el);
        }
    }

    if (decal.type == DecalId::TYPE_WIRE) {
        int n = chip_info->wire_data[decal.index].num_segments;
        const WireSegmentPOD *p = chip_info->wire_data[decal.index].segments.get();

        GraphicElement::style_t style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;

        for (int i = 0; i < n; i++)
            gfxTileWire(ret, p[i].x, p[i].y, GfxTileWireId(p[i].index), style);
    }

    if (decal.type == DecalId::TYPE_PIP) {
        const PipInfoPOD &p = chip_info->pip_data[decal.index];
        GraphicElement::style_t style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_HIDDEN;
        gfxTilePip(ret, p.x, p.y, GfxTileWireId(p.src_seg), GfxTileWireId(p.dst_seg), style);
    }

    if (decal.type == DecalId::TYPE_BEL) {
        BelId bel;
        bel.index = SiteIndex(decal.index);

        auto bel_type = getBelType(bel);

        if (bel_type == id_ICESTORM_LC) {
            GraphicElement el;
            el.type = GraphicElement::TYPE_BOX;
            el.style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;
            el.x1 = chip_info->bel_data[bel.index].x + logic_cell_x1;
            el.x2 = chip_info->bel_data[bel.index].x + logic_cell_x2;
            el.y1 = chip_info->bel_data[bel.index].y + logic_cell_y1 +
                    (chip_info->bel_data[bel.index].z) * logic_cell_pitch;
            el.y2 = chip_info->bel_data[bel.index].y + logic_cell_y2 +
                    (chip_info->bel_data[bel.index].z) * logic_cell_pitch;
            ret.push_back(el);
        }

        if (bel_type == id_SB_IO) {
            GraphicElement el;
            el.type = GraphicElement::TYPE_BOX;
            el.style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;
            el.x1 = chip_info->bel_data[bel.index].x + logic_cell_x1;
            el.x2 = chip_info->bel_data[bel.index].x + logic_cell_x2;
            el.y1 = chip_info->bel_data[bel.index].y + logic_cell_y1 +
                    (4 * chip_info->bel_data[bel.index].z) * logic_cell_pitch;
            el.y2 = chip_info->bel_data[bel.index].y + logic_cell_y2 +
                    (4 * chip_info->bel_data[bel.index].z + 3) * logic_cell_pitch;
            ret.push_back(el);
        }

        if (bel_type == id_ICESTORM_RAM) {
            for (int i = 0; i < 2; i++) {
                GraphicElement el;
                el.type = GraphicElement::TYPE_BOX;
                el.style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;
                el.x1 = chip_info->bel_data[bel.index].x + logic_cell_x1;
                el.x2 = chip_info->bel_data[bel.index].x + logic_cell_x2;
                el.y1 = chip_info->bel_data[bel.index].y + logic_cell_y1 + i;
                el.y2 = chip_info->bel_data[bel.index].y + logic_cell_y2 + i + 7 * logic_cell_pitch;
                ret.push_back(el);
            }
        }
    }

    return ret;
}

// -----------------------------------------------------------------------

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const
{
    return false;
}

// Get the port class, also setting clockPort to associated clock if applicable
TimingPortClass Arch::getPortTimingClass(const CellInfo *cell, IdString port, IdString &clockPort) const
{
    if (cell->type == id_SLICEL) {
        if (port == id_CLK)
            return TMG_CLOCK_INPUT;
        if (port == id_CIN)
            return TMG_COMB_INPUT;
        if (port == id_COUT || port == id_O)
            return TMG_COMB_OUTPUT;
        if (cell->lcInfo.dffEnable) {
            clockPort = id_CLK;
            if (port == id_OQ)
                return TMG_REGISTER_OUTPUT;
            else
                return TMG_REGISTER_INPUT;
        } else {
            if (port == id_O)
                return TMG_COMB_OUTPUT;
            else
                return TMG_COMB_INPUT;
        }
        // TODO
        //if (port == id_OMUX)
    }
    else if (cell->type == id_IOB33S) {
        if (port == id_I)
            return TMG_STARTPOINT;
        else if (port == id_O)
            return TMG_ENDPOINT;
    }
    else if (cell->type == id_BUFGCTRL) {
        if (port == id_O)
            return TMG_COMB_OUTPUT;
        return TMG_COMB_INPUT;
    }
    log_error("no timing info for port '%s' of cell type '%s'\n", port.c_str(this), cell->type.c_str(this));
}

bool Arch::isGlobalNet(const NetInfo *net) const
{
    if (net == nullptr)
        return false;
    return net->driver.cell != nullptr && net->driver.cell->type == id_BUFGCTRL && net->driver.port == id_O;
}

// Assign arch arg info
void Arch::assignArchInfo()
{
    for (auto &net : getCtx()->nets) {
        NetInfo *ni = net.second.get();
        if (isGlobalNet(ni))
            ni->is_global = true;
        ni->is_enable = false;
        ni->is_reset = false;
        for (auto usr : ni->users) {
            if (is_enable_port(this, usr))
                ni->is_enable = true;
            if (is_reset_port(this, usr))
                ni->is_reset = true;
        }
    }
    for (auto &cell : getCtx()->cells) {
        CellInfo *ci = cell.second.get();
        assignCellInfo(ci);
    }
}

void Arch::assignCellInfo(CellInfo *cell)
{
    cell->belType = cell->type;
    if (cell->type == id_SLICEL) {
        cell->lcInfo.dffEnable = bool_or_default(cell->params, id_DFF_ENABLE);
        cell->lcInfo.carryEnable = bool_or_default(cell->params, id_CARRY_ENABLE);
        cell->lcInfo.negClk = bool_or_default(cell->params, id_NEG_CLK);
        cell->lcInfo.clk = get_net_or_empty(cell, id_CLK);
        cell->lcInfo.cen = get_net_or_empty(cell, id_CEN);
        cell->lcInfo.sr = get_net_or_empty(cell, id_SR);
        cell->lcInfo.inputCount = 0;
        if (get_net_or_empty(cell, id_I0))
            cell->lcInfo.inputCount++;
        if (get_net_or_empty(cell, id_I1))
            cell->lcInfo.inputCount++;
        if (get_net_or_empty(cell, id_I2))
            cell->lcInfo.inputCount++;
        if (get_net_or_empty(cell, id_I3))
            cell->lcInfo.inputCount++;
    }
}

NEXTPNR_NAMESPACE_END
