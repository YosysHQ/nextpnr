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
#include "timing_opt.h"
#include "util.h"

NEXTPNR_NAMESPACE_BEGIN

// -----------------------------------------------------------------------

void IdString::initialize_arch(const BaseCtx *ctx)
{
#define X(t) initialize_add(ctx, #t, ID_##t);
#include "constids.inc"
#undef X
}

// -----------------------------------------------------------------------

static const ChipInfoPOD *get_chip_info(const RelPtr<ChipInfoPOD> *ptr) { return ptr->get(); }

#if defined(_MSC_VER)
void load_chipdb();
#endif

Arch::Arch(ArchArgs args) : args(args)
{
#if defined(_MSC_VER)
    load_chipdb();
#endif

#ifdef ICE40_HX1K_ONLY
    if (args.type == ArchArgs::HX1K) {
        fast_part = true;
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_1k));
    } else {
        log_error("Unsupported iCE40 chip type.\n");
    }
#else
    if (args.type == ArchArgs::LP384) {
        fast_part = false;
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_384));
    } else if (args.type == ArchArgs::LP1K || args.type == ArchArgs::HX1K) {
        fast_part = args.type == ArchArgs::HX1K;
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_1k));
    } else if (args.type == ArchArgs::UP5K) {
        fast_part = false;
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_5k));
    } else if (args.type == ArchArgs::LP8K || args.type == ArchArgs::HX8K) {
        fast_part = args.type == ArchArgs::HX8K;
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_8k));
    } else {
        log_error("Unsupported iCE40 chip type.\n");
    }
#endif

    package_info = nullptr;
    for (int i = 0; i < chip_info->num_packages; i++) {
        if (chip_info->packages_data[i].name.get() == args.package) {
            package_info = &(chip_info->packages_data[i]);
            break;
        }
    }
    if (package_info == nullptr)
        log_error("Unsupported package '%s'.\n", args.package.c_str());

    bel_carry.resize(chip_info->num_bels);
    bel_to_cell.resize(chip_info->num_bels);
    wire_to_net.resize(chip_info->num_wires);
    pip_to_net.resize(chip_info->num_pips);
    switches_locked.resize(chip_info->num_switches);
}

// -----------------------------------------------------------------------

std::string Arch::getChipName() const
{
#ifdef ICE40_HX1K_ONLY
    if (args.type == ArchArgs::HX1K) {
        return "Lattice LP1K";
    } else {
        log_error("Unsupported iCE40 chip type.\n");
    }
#else
    if (args.type == ArchArgs::LP384) {
        return "Lattice LP384";
    } else if (args.type == ArchArgs::LP1K) {
        return "Lattice LP1K";
    } else if (args.type == ArchArgs::HX1K) {
        return "Lattice HX1K";
    } else if (args.type == ArchArgs::UP5K) {
        return "Lattice UP5K";
    } else if (args.type == ArchArgs::LP8K) {
        return "Lattice LP8K";
    } else if (args.type == ArchArgs::HX8K) {
        return "Lattice HX8K";
    } else {
        log_error("Unknown chip\n");
    }
#endif
}

// -----------------------------------------------------------------------

IdString Arch::archArgsToId(ArchArgs args) const
{
    if (args.type == ArchArgs::LP384)
        return id("lp384");
    if (args.type == ArchArgs::LP1K)
        return id("lp1k");
    if (args.type == ArchArgs::HX1K)
        return id("hx1k");
    if (args.type == ArchArgs::UP5K)
        return id("up5k");
    if (args.type == ArchArgs::LP8K)
        return id("lp8k");
    if (args.type == ArchArgs::HX8K)
        return id("hx8k");
    return IdString();
}

// -----------------------------------------------------------------------

BelId Arch::getBelByName(IdString name) const
{
    BelId ret;

    if (bel_by_name.empty()) {
        for (int i = 0; i < chip_info->num_bels; i++)
            bel_by_name[id(chip_info->bel_data[i].name.get())] = i;
    }

    auto it = bel_by_name.find(name);
    if (it != bel_by_name.end())
        ret.index = it->second;

    return ret;
}

BelId Arch::getBelByLocation(Loc loc) const
{
    BelId bel;

    if (bel_by_loc.empty()) {
        for (int i = 0; i < chip_info->num_bels; i++) {
            BelId b;
            b.index = i;
            bel_by_loc[getBelLocation(b)] = i;
        }
    }

    auto it = bel_by_loc.find(loc);
    if (it != bel_by_loc.end())
        bel.index = it->second;

    return bel;
}

BelRange Arch::getBelsByTile(int x, int y) const
{
    // In iCE40 chipdb bels at the same tile are consecutive and dense z ordinates
    // are used
    BelRange br;

    br.b.cursor = Arch::getBelByLocation(Loc(x, y, 0)).index;
    br.e.cursor = br.b.cursor;

    if (br.e.cursor != -1) {
        while (br.e.cursor < chip_info->num_bels && chip_info->bel_data[br.e.cursor].x == x &&
               chip_info->bel_data[br.e.cursor].y == y)
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

std::vector<std::pair<IdString, std::string>> Arch::getBelAttrs(BelId bel) const
{
    std::vector<std::pair<IdString, std::string>> ret;

    ret.push_back(std::make_pair(id("INDEX"), stringf("%d", bel.index)));

    return ret;
}

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
    WireId ret;

    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = chip_info->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = chip_info->bel_data[bel.index].bel_wires.get();

    if (num_bel_wires < 7) {
        for (int i = 0; i < num_bel_wires; i++) {
            if (bel_wires[i].port == pin.index) {
                ret.index = bel_wires[i].wire_index;
                break;
            }
        }
    } else {
        int b = 0, e = num_bel_wires - 1;
        while (b <= e) {
            int i = (b + e) / 2;
            if (bel_wires[i].port == pin.index) {
                ret.index = bel_wires[i].wire_index;
                break;
            }
            if (bel_wires[i].port > pin.index)
                e = i - 1;
            else
                b = i + 1;
        }
    }

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

bool Arch::isBelLocked(BelId bel) const
{
    const BelConfigPOD *bel_config = nullptr;
    for (int i = 0; i < chip_info->num_belcfgs; i++) {
        if (chip_info->bel_config[i].bel_index == bel.index) {
            bel_config = &chip_info->bel_config[i];
            break;
        }
    }
    NPNR_ASSERT(bel_config != nullptr);
    for (int i = 0; i < bel_config->num_entries; i++) {
        if (strcmp("LOCKED", bel_config->entries[i].cbit_name.get()))
            continue;
        if ("LOCKED_" + archArgs().package == bel_config->entries[i].entry_name.get())
            return true;
    }
    return false;
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

std::vector<std::pair<IdString, std::string>> Arch::getWireAttrs(WireId wire) const
{
    std::vector<std::pair<IdString, std::string>> ret;
    auto &wi = chip_info->wire_data[wire.index];

    ret.push_back(std::make_pair(id("INDEX"), stringf("%d", wire.index)));

    ret.push_back(std::make_pair(id("GRID_X"), stringf("%d", wi.x)));
    ret.push_back(std::make_pair(id("GRID_Y"), stringf("%d", wi.y)));
    ret.push_back(std::make_pair(id("GRID_Z"), stringf("%d", wi.z)));

#if 0
    for (int i = 0; i < wi.num_segments; i++) {
        auto &si = wi.segments[i];
        ret.push_back(std::make_pair(id(stringf("segment[%d]", i)),
                                     stringf("X%d/Y%d/%s", si.x, si.y, chip_info->tile_wire_names[si.index].get())));
    }
#endif

    return ret;
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

IdString Arch::getPipType(PipId pip) const { return IdString(); }

std::vector<std::pair<IdString, std::string>> Arch::getPipAttrs(PipId pip) const
{
    std::vector<std::pair<IdString, std::string>> ret;

    ret.push_back(std::make_pair(id("INDEX"), stringf("%d", pip.index)));

    return ret;
}

// -----------------------------------------------------------------------

BelId Arch::getPackagePinBel(const std::string &pin) const
{
    for (int i = 0; i < package_info->num_pins; i++) {
        if (package_info->pins[i].name.get() == pin) {
            BelId id;
            id.index = package_info->pins[i].bel_index;
            return id;
        }
    }
    return BelId();
}

std::string Arch::getBelPackagePin(BelId bel) const
{
    for (int i = 0; i < package_info->num_pins; i++) {
        if (package_info->pins[i].bel_index == bel.index) {
            return std::string(package_info->pins[i].name.get());
        }
    }
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
    const auto &driver = net_info->driver;
    if (driver.port == id_COUT && sink.port == id_CIN) {
        if (driver.cell->constr_abs_z && driver.cell->constr_z < 7)
            budget = 0;
        else {
            NPNR_ASSERT(driver.cell->constr_z == 7);
            switch (args.type) {
#ifndef ICE40_HX1K_ONLY
            case ArchArgs::HX8K:
#endif
            case ArchArgs::HX1K:
                budget = 190;
                break;
#ifndef ICE40_HX1K_ONLY
            case ArchArgs::LP384:
            case ArchArgs::LP1K:
            case ArchArgs::LP8K:
                budget = 290;
                break;
            case ArchArgs::UP5K:
                budget = 560;
                break;
#endif
            default:
                log_error("Unsupported iCE40 chip type.\n");
            }
        }
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------

bool Arch::place()
{
    if (!placer1(getCtx(), Placer1Cfg(getCtx())))
        return false;
    if (bool_or_default(settings, id("opt_timing"), false)) {
        TimingOptCfg tocfg(getCtx());
        tocfg.cellTypes.insert(id_ICESTORM_LC);
        return timing_opt(getCtx(), tocfg);
    } else {
        return true;
    }
}

bool Arch::route() { return router1(getCtx(), Router1Cfg(getCtx())); }

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
            gfxTileWire(ret, p[i].x, p[i].y, chip_info->width, chip_info->height, GfxTileWireId(p[i].index), style);

#if 0
        if (ret.empty()) {
            WireId wire;
            wire.index = decal.index;
            log_warning("No gfx decal for wire %s (%d).\n", getWireName(wire).c_str(getCtx()), decal.index);
        }
#endif
    }

    if (decal.type == DecalId::TYPE_PIP) {
        const PipInfoPOD &p = chip_info->pip_data[decal.index];
        GraphicElement::style_t style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_HIDDEN;
        gfxTilePip(ret, p.x, p.y, GfxTileWireId(p.src_seg), GfxTileWireId(p.dst_seg), style);

#if 0
        if (ret.empty()) {
            PipId pip;
            pip.index = decal.index;
            log_warning("No gfx decal for pip %s (%d).\n", getPipName(pip).c_str(getCtx()), decal.index);
        }
#endif
    }

    if (decal.type == DecalId::TYPE_BEL) {
        BelId bel;
        bel.index = decal.index;

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
            el.x1 = chip_info->bel_data[bel.index].x + lut_swbox_x1;
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
                el.x1 = chip_info->bel_data[bel.index].x + lut_swbox_x1;
                el.x2 = chip_info->bel_data[bel.index].x + logic_cell_x2;
                el.y1 = chip_info->bel_data[bel.index].y + logic_cell_y1 + i;
                el.y2 = chip_info->bel_data[bel.index].y + logic_cell_y2 + i + 7 * logic_cell_pitch;
                ret.push_back(el);
            }
        }

        if (bel_type == id_SB_GB) {
            GraphicElement el;
            el.type = GraphicElement::TYPE_BOX;
            el.style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;
            el.x1 = chip_info->bel_data[bel.index].x + local_swbox_x1 + 0.05;
            el.x2 = chip_info->bel_data[bel.index].x + logic_cell_x2 - 0.05;
            el.y1 = chip_info->bel_data[bel.index].y + main_swbox_y2 - 0.05;
            el.y2 = chip_info->bel_data[bel.index].y + main_swbox_y2 - 0.10;
            ret.push_back(el);
        }

        if (bel_type == id_ICESTORM_PLL || bel_type == id_SB_WARMBOOT) {
            GraphicElement el;
            el.type = GraphicElement::TYPE_BOX;
            el.style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;
            el.x1 = chip_info->bel_data[bel.index].x + local_swbox_x1 + 0.05;
            el.x2 = chip_info->bel_data[bel.index].x + logic_cell_x2 - 0.05;
            el.y1 = chip_info->bel_data[bel.index].y + main_swbox_y2;
            el.y2 = chip_info->bel_data[bel.index].y + main_swbox_y2 + 0.05;
            ret.push_back(el);
        }

#if 0
        if (ret.empty()) {
            BelId bel;
            bel.index = decal.index;
            log_warning("No gfx decal for bel %s (%d).\n", getBelName(bel).c_str(getCtx()), decal.index);
        }
#endif
    }

    return ret;
}

// -----------------------------------------------------------------------

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const
{
    for (int i = 0; i < chip_info->num_timing_cells; i++) {
        const auto &tc = chip_info->cell_timing[i];
        if (tc.type == cell->type.index) {
            for (int j = 0; j < tc.num_paths; j++) {
                const auto &path = tc.path_delays[j];
                if (path.from_port == fromPort.index && path.to_port == toPort.index) {
                    if (fast_part)
                        delay.delay = path.fast_delay;
                    else
                        delay.delay = path.slow_delay;
                    return true;
                }
            }
            break;
        }
    }
    return false;
}

// Get the port class, also setting clockPort to associated clock if applicable
TimingPortClass Arch::getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
{
    clockInfoCount = 0;
    if (cell->type == id_ICESTORM_LC) {
        if (port == id_CLK)
            return TMG_CLOCK_INPUT;
        if (port == id_CIN)
            return TMG_COMB_INPUT;
        if (port == id_COUT || port == id_LO)
            return TMG_COMB_OUTPUT;
        if (port == id_O) {
            // LCs with no inputs are constant drivers
            if (cell->lcInfo.inputCount == 0)
                return TMG_IGNORE;
            if (cell->lcInfo.dffEnable) {
                clockInfoCount = 1;
                return TMG_REGISTER_OUTPUT;
            } else
                return TMG_COMB_OUTPUT;
        } else {
            if (cell->lcInfo.dffEnable) {
                clockInfoCount = 1;
                return TMG_REGISTER_INPUT;
            } else
                return TMG_COMB_INPUT;
        }
    } else if (cell->type == id_ICESTORM_RAM) {

        if (port == id_RCLK || port == id_WCLK)
            return TMG_CLOCK_INPUT;

        clockInfoCount = 1;

        if (cell->ports.at(port).type == PORT_OUT)
            return TMG_REGISTER_OUTPUT;
        else
            return TMG_REGISTER_INPUT;
    } else if (cell->type == id_ICESTORM_DSP || cell->type == id_ICESTORM_SPRAM) {
        if (port == id_CLK || port == id_CLOCK)
            return TMG_CLOCK_INPUT;
        else {
            clockInfoCount = 1;
            if (cell->ports.at(port).type == PORT_OUT)
                return TMG_REGISTER_OUTPUT;
            else
                return TMG_REGISTER_INPUT;
        }
    } else if (cell->type == id_SB_IO) {
        if (port == id_D_IN_0 || port == id_D_IN_1)
            return TMG_STARTPOINT;
        if (port == id_D_OUT_0 || port == id_D_OUT_1 || port == id_OUTPUT_ENABLE)
            return TMG_ENDPOINT;
        return TMG_IGNORE;
    } else if (cell->type == id_ICESTORM_PLL) {
        if (port == id_PLLOUT_A || port == id_PLLOUT_B)
            return TMG_GEN_CLOCK;
        return TMG_IGNORE;
    } else if (cell->type == id_ICESTORM_LFOSC) {
        if (port == id_CLKLF)
            return TMG_GEN_CLOCK;
        return TMG_IGNORE;
    } else if (cell->type == id_ICESTORM_HFOSC) {
        if (port == id_CLKHF)
            return TMG_GEN_CLOCK;
        return TMG_IGNORE;
    } else if (cell->type == id_SB_GB) {
        if (port == id_GLOBAL_BUFFER_OUTPUT)
            return TMG_COMB_OUTPUT;
        return TMG_COMB_INPUT;
    } else if (cell->type == id_SB_WARMBOOT) {
        return TMG_ENDPOINT;
    } else if (cell->type == id_SB_RGBA_DRV) {
        if (port == id_RGB0 || port == id_RGB1 || port == id_RGB2)
            return TMG_IGNORE;
        return TMG_ENDPOINT;
    } else if (cell->type == id_SB_LEDDA_IP) {
        if (port == id_CLK || port == id_CLOCK)
            return TMG_CLOCK_INPUT;
        return TMG_IGNORE;
    }
    log_error("cell type '%s' is unsupported (instantiated as '%s')\n", cell->type.c_str(this), cell->name.c_str(this));
}

TimingClockingInfo Arch::getPortClockingInfo(const CellInfo *cell, IdString port, int index) const
{
    TimingClockingInfo info;
    if (cell->type == id_ICESTORM_LC) {
        info.clock_port = id_CLK;
        info.edge = cell->lcInfo.negClk ? FALLING_EDGE : RISING_EDGE;
        if (port == id_O) {
            bool has_clktoq = getCellDelay(cell, id_CLK, id_O, info.clockToQ);
            NPNR_ASSERT(has_clktoq);
        } else {
            info.setup.delay = 100;
            info.hold.delay = 0;
        }
    } else if (cell->type == id_ICESTORM_RAM) {
        if (port.str(this)[0] == 'R') {
            info.clock_port = id_RCLK;
            info.edge = bool_or_default(cell->params, id("NEG_CLK_R")) ? FALLING_EDGE : RISING_EDGE;
        } else {
            info.clock_port = id_WCLK;
            info.edge = bool_or_default(cell->params, id("NEG_CLK_W")) ? FALLING_EDGE : RISING_EDGE;
        }
        if (cell->ports.at(port).type == PORT_OUT) {
            bool has_clktoq = getCellDelay(cell, info.clock_port, port, info.clockToQ);
            NPNR_ASSERT(has_clktoq);
        } else {
            info.setup.delay = 100;
            info.hold.delay = 0;
        }
    } else if (cell->type == id_ICESTORM_DSP || cell->type == id_ICESTORM_SPRAM) {
        info.clock_port = cell->type == id_ICESTORM_SPRAM ? id_CLOCK : id_CLK;
        info.edge = RISING_EDGE;
        if (cell->ports.at(port).type == PORT_OUT) {
            bool has_clktoq = getCellDelay(cell, info.clock_port, port, info.clockToQ);
            if (!has_clktoq)
                info.clockToQ.delay = 100;
        } else {
            info.setup.delay = 100;
            info.hold.delay = 0;
        }
    } else {
        NPNR_ASSERT_FALSE("unhandled cell type in getPortClockingInfo");
    }
    return info;
}

bool Arch::isGlobalNet(const NetInfo *net) const
{
    if (net == nullptr)
        return false;
    return net->driver.cell != nullptr && net->driver.port == id_GLOBAL_BUFFER_OUTPUT;
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
    if (cell->type == id_ICESTORM_LC) {
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
    } else if (cell->type == id_SB_IO) {
        cell->ioInfo.lvds = str_or_default(cell->params, id_IO_STANDARD, "SB_LVCMOS") == "SB_LVDS_INPUT";
        cell->ioInfo.global = bool_or_default(cell->attrs, this->id("GLOBAL"));
    } else if (cell->type == id_SB_GB) {
        cell->gbInfo.forPadIn = bool_or_default(cell->attrs, this->id("FOR_PAD_IN"));
    }
}

NEXTPNR_NAMESPACE_END
