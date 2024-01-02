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

#include <algorithm>
#include <cmath>
#include "cells.h"
#include "embed.h"
#include "gfx.h"
#include "log.h"
#include "nextpnr.h"
#include "placer1.h"
#include "placer_heap.h"
#include "placer_static.h"
#include "router1.h"
#include "router2.h"
#include "rust.h"
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

static const ChipInfoPOD *get_chip_info(ArchArgs::ArchArgsTypes chip)
{
    std::string chipdb;
    if (chip == ArchArgs::LP384) {
        chipdb = "ice40/chipdb-384.bin";
    } else if (chip == ArchArgs::LP1K || chip == ArchArgs::HX1K) {
        chipdb = "ice40/chipdb-1k.bin";
    } else if (chip == ArchArgs::U1K || chip == ArchArgs::U2K || chip == ArchArgs::U4K) {
        chipdb = "ice40/chipdb-u4k.bin";
    } else if (chip == ArchArgs::UP3K || chip == ArchArgs::UP5K) {
        chipdb = "ice40/chipdb-5k.bin";
    } else if (chip == ArchArgs::LP8K || chip == ArchArgs::HX8K || chip == ArchArgs::LP4K || chip == ArchArgs::HX4K) {
        chipdb = "ice40/chipdb-8k.bin";
    } else {
        log_error("Unknown chip\n");
    }

    auto ptr = reinterpret_cast<const RelPtr<ChipInfoPOD> *>(get_chipdb(chipdb));
    if (ptr == nullptr)
        return nullptr;
    return ptr->get();
}

bool Arch::is_available(ArchArgs::ArchArgsTypes chip) { return get_chip_info(chip) != nullptr; }

std::vector<std::string> Arch::get_supported_packages(ArchArgs::ArchArgsTypes chip)
{
    const ChipInfoPOD *chip_info = get_chip_info(chip);
    std::vector<std::string> packages;
    for (auto &pkg : chip_info->packages_data) {
        std::string name = pkg.name.get();
        if (chip == ArchArgs::LP4K || chip == ArchArgs::HX4K) {
            if (name.find(":4k") != std::string::npos)
                name = name.substr(0, name.size() - 3);
            else
                continue;
        }
        packages.push_back(name);
    }
    return packages;
}

// -----------------------------------------------------------------------

Arch::Arch(ArchArgs args) : args(args)
{
    fast_part = (args.type == ArchArgs::HX8K || args.type == ArchArgs::HX4K || args.type == ArchArgs::HX1K);

    chip_info = get_chip_info(args.type);
    if (chip_info == nullptr)
        log_error("Unsupported iCE40 chip type.\n");

    package_info = nullptr;
    std::string package_name = args.package;
    if (args.type == ArchArgs::LP4K || args.type == ArchArgs::HX4K)
        package_name += ":4k";

    for (auto &pkg : chip_info->packages_data) {
        if (pkg.name.get() == package_name) {
            package_info = &pkg;
            break;
        }
    }
    if (package_info == nullptr)
        log_error("Unsupported package '%s'.\n", args.package.c_str());

    for (int i = 0; i < chip_info->width; i++) {
        IdString x_id = idf("X%d", i);
        x_ids.push_back(x_id);
        id_to_x[x_id] = i;
    }
    for (int i = 0; i < chip_info->height; i++) {
        IdString y_id = idf("Y%d", i);
        y_ids.push_back(y_id);
        id_to_y[y_id] = i;
    }

    bel_carry.resize(chip_info->bel_data.size());
    bel_to_cell.resize(chip_info->bel_data.size());
    wire_to_net.resize(chip_info->wire_data.size());
    pip_to_net.resize(chip_info->pip_data.size());
    switches_locked.resize(chip_info->num_switches);

    BaseArch::init_cell_types();
    BaseArch::init_bel_buckets();
}

// -----------------------------------------------------------------------

std::string Arch::getChipName() const
{
    if (args.type == ArchArgs::LP384) {
        return "Lattice iCE40LP384";
    } else if (args.type == ArchArgs::LP1K) {
        return "Lattice iCE40LP1K";
    } else if (args.type == ArchArgs::HX1K) {
        return "Lattice iCE40HX1K";
    } else if (args.type == ArchArgs::UP3K) {
        return "Lattice iCE40UP3K";
    } else if (args.type == ArchArgs::UP5K) {
        return "Lattice iCE40UP5K";
    } else if (args.type == ArchArgs::U1K) {
        return "Lattice iCE5LP1K";
    } else if (args.type == ArchArgs::U2K) {
        return "Lattice iCE5LP2K";
    } else if (args.type == ArchArgs::U4K) {
        return "Lattice iCE5LP4K";
    } else if (args.type == ArchArgs::LP4K) {
        return "Lattice iCE40LP4K";
    } else if (args.type == ArchArgs::LP8K) {
        return "Lattice iCE40LP8K";
    } else if (args.type == ArchArgs::HX4K) {
        return "Lattice iCE40HX4K";
    } else if (args.type == ArchArgs::HX8K) {
        return "Lattice iCE40HX8K";
    } else {
        log_error("Unknown chip\n");
    }
}

// -----------------------------------------------------------------------

IdString Arch::archArgsToId(ArchArgs args) const
{
    if (args.type == ArchArgs::LP384)
        return id_lp384;
    if (args.type == ArchArgs::LP1K)
        return id_lp1k;
    if (args.type == ArchArgs::HX1K)
        return id_hx1k;
    if (args.type == ArchArgs::UP3K)
        return id_up3k;
    if (args.type == ArchArgs::UP5K)
        return id_up5k;
    if (args.type == ArchArgs::U1K)
        return id_u1k;
    if (args.type == ArchArgs::U2K)
        return id_u2k;
    if (args.type == ArchArgs::U4K)
        return id_u4k;
    if (args.type == ArchArgs::LP4K)
        return id_lp4k;
    if (args.type == ArchArgs::LP8K)
        return id_lp8k;
    if (args.type == ArchArgs::HX4K)
        return id_hx4k;
    if (args.type == ArchArgs::HX8K)
        return id_hx8k;
    return IdString();
}

// -----------------------------------------------------------------------

BelId Arch::getBelByName(IdStringList name) const
{
    BelId ret;

    if (bel_by_name.empty()) {
        for (size_t i = 0; i < chip_info->bel_data.size(); i++) {
            BelId b;
            b.index = i;
            bel_by_name[getBelName(b)] = i;
        }
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
        for (size_t i = 0; i < chip_info->bel_data.size(); i++) {
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

    for (int i = 0; i < 4; i++) {
        br.b.cursor = Arch::getBelByLocation(Loc(x, y, i)).index;
        if (br.b.cursor != -1)
            break;
    }
    br.e.cursor = br.b.cursor;

    if (br.e.cursor != -1) {
        while (br.e.cursor < chip_info->bel_data.ssize() && chip_info->bel_data[br.e.cursor].x == x &&
               chip_info->bel_data[br.e.cursor].y == y)
            br.e.cursor++;
    }

    return br;
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const
{
    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = chip_info->bel_data[bel.index].bel_wires.size();
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

    ret.push_back(std::make_pair(id_INDEX, stringf("%d", bel.index)));

    return ret;
}

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
    WireId ret;

    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = chip_info->bel_data[bel.index].bel_wires.size();
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

    for (auto &w : chip_info->bel_data[bel.index].bel_wires)
        ret.push_back(IdString(w.port));

    return ret;
}

bool Arch::is_bel_locked(BelId bel) const
{
    const BelConfigPOD *bel_config = nullptr;
    for (auto &bel_cfg : chip_info->bel_config) {
        if (bel_cfg.bel_index == bel.index) {
            bel_config = &bel_cfg;
            break;
        }
    }
    NPNR_ASSERT(bel_config != nullptr);
    for (auto &entry : bel_config->entries) {
        if (strcmp("LOCKED", entry.cbit_name.get()))
            continue;
        if ("LOCKED_" + archArgs().package == entry.entry_name.get())
            return true;
    }
    return false;
}

// -----------------------------------------------------------------------

WireId Arch::getWireByName(IdStringList name) const
{
    WireId ret;

    if (wire_by_name.empty()) {
        for (int i = 0; i < chip_info->wire_data.ssize(); i++) {
            WireId w;
            w.index = i;
            wire_by_name[getWireName(w)] = i;
        }
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
        return id_GLB2LOCAL;
    case WireInfoPOD::WIRE_TYPE_GLB_NETWK:
        return id_GLB_NETWK;
    case WireInfoPOD::WIRE_TYPE_LOCAL:
        return id_LOCAL;
    case WireInfoPOD::WIRE_TYPE_LUTFF_IN:
        return id_LUTFF_IN;
    case WireInfoPOD::WIRE_TYPE_LUTFF_IN_LUT:
        return id_LUTFF_IN_LUT;
    case WireInfoPOD::WIRE_TYPE_LUTFF_LOUT:
        return id_LUTFF_LOUT;
    case WireInfoPOD::WIRE_TYPE_LUTFF_OUT:
        return id_LUTFF_OUT;
    case WireInfoPOD::WIRE_TYPE_LUTFF_COUT:
        return id_LUTFF_COUT;
    case WireInfoPOD::WIRE_TYPE_LUTFF_GLOBAL:
        return id_LUTFF_GLOBAL;
    case WireInfoPOD::WIRE_TYPE_CARRY_IN_MUX:
        return id_CARRY_IN_MUX;
    case WireInfoPOD::WIRE_TYPE_SP4_V:
        return id_SP4_V;
    case WireInfoPOD::WIRE_TYPE_SP4_H:
        return id_SP4_H;
    case WireInfoPOD::WIRE_TYPE_SP12_V:
        return id_SP12_V;
    case WireInfoPOD::WIRE_TYPE_SP12_H:
        return id_SP12_H;
    }
    return IdString();
}

std::vector<std::pair<IdString, std::string>> Arch::getWireAttrs(WireId wire) const
{
    std::vector<std::pair<IdString, std::string>> ret;
    auto &wi = chip_info->wire_data[wire.index];

    ret.push_back(std::make_pair(id_INDEX, stringf("%d", wire.index)));

    ret.push_back(std::make_pair(id_GRID_X, stringf("%d", wi.x)));
    ret.push_back(std::make_pair(id_GRID_Y, stringf("%d", wi.y)));
    ret.push_back(std::make_pair(id_GRID_Z, stringf("%d", wi.z)));

    return ret;
}

// -----------------------------------------------------------------------

PipId Arch::getPipByName(IdStringList name) const
{
    PipId ret;

    if (pip_by_name.empty()) {
        for (int i = 0; i < chip_info->pip_data.ssize(); i++) {
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

IdStringList Arch::getPipName(PipId pip) const
{
    NPNR_ASSERT(pip != PipId());

    int x = chip_info->pip_data[pip.index].x;
    int y = chip_info->pip_data[pip.index].y;

    auto &src_wire = chip_info->wire_data[chip_info->pip_data[pip.index].src];
    auto &dst_wire = chip_info->wire_data[chip_info->pip_data[pip.index].dst];

    std::string src_name = stringf("%d.%d.%s", int(src_wire.name_x), int(src_wire.name_y), src_wire.name.get());
    std::string dst_name = stringf("%d.%d.%s", int(dst_wire.name_x), int(dst_wire.name_y), dst_wire.name.get());

    std::array<IdString, 3> ids{x_ids.at(x), y_ids.at(y), id(src_name + ".->." + dst_name)};
    return IdStringList(ids);
}

IdString Arch::getPipType(PipId pip) const { return IdString(); }

std::vector<std::pair<IdString, std::string>> Arch::getPipAttrs(PipId pip) const
{
    std::vector<std::pair<IdString, std::string>> ret;

    ret.push_back(std::make_pair(id_INDEX, stringf("%d", pip.index)));

    return ret;
}

// -----------------------------------------------------------------------

BelId Arch::get_package_pin_bel(const std::string &pin) const
{
    for (auto &ppin : package_info->pins) {
        if (ppin.name.get() == pin) {
            BelId id;
            id.index = ppin.bel_index;
            return id;
        }
    }
    return BelId();
}

std::string Arch::get_bel_package_pin(BelId bel) const
{
    for (auto &ppin : package_info->pins) {
        if (ppin.bel_index == bel.index) {
            return std::string(ppin.name.get());
        }
    }
    return "";
}

// -----------------------------------------------------------------------

GroupId Arch::getGroupByName(IdStringList name) const
{
    for (auto g : getGroups())
        if (getGroupName(g) == name)
            return g;
    return GroupId();
}

IdStringList Arch::getGroupName(GroupId group) const
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
        return IdStringList();
    }

    std::array<IdString, 3> ids{x_ids.at(group.x), y_ids.at(group.y), id(suffix)};
    return IdStringList(ids);
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

bool Arch::place()
{
    std::string placer = str_or_default(settings, id_placer, defaultPlacer);
    if (placer == "heap") {
        PlacerHeapCfg cfg(getCtx());
        cfg.ioBufTypes.insert(id_SB_IO);
        if (!placer_heap(getCtx(), cfg))
            return false;
    } else if (placer == "sa") {
        if (!placer1(getCtx(), Placer1Cfg(getCtx())))
            return false;
    } else if (placer == "static") {
        PlacerStaticCfg cfg(getCtx());
        cfg.logic_groups = 1;
        {
            cfg.cell_groups.emplace_back();
            auto &comb = cfg.cell_groups.back();
            comb.name = getCtx()->id("COMB");
            comb.cell_area[id_ICESTORM_LC] = StaticRect(1.0f, 0.125f);
            comb.bel_area[id_ICESTORM_LC] = StaticRect(1.0f, 0.125f);
            comb.spacer_rect = StaticRect(1.0f, 0.125f);
        }

        {
            cfg.cell_groups.emplace_back();
            auto &comb = cfg.cell_groups.back();
            comb.name = getCtx()->id("RAM");
            comb.cell_area[id_ICESTORM_RAM] = StaticRect(1.0f, 2.0f);
            comb.bel_area[id_ICESTORM_RAM] = StaticRect(1.0f, 2.0f);
            comb.spacer_rect = StaticRect(1.0f, 2.0f);
        }

        {
            cfg.cell_groups.emplace_back();
            auto &comb = cfg.cell_groups.back();
            comb.name = getCtx()->id("DSP");
            comb.cell_area[id_ICESTORM_DSP] = StaticRect(0.9f, 5.0f);
            comb.bel_area[id_ICESTORM_DSP] = StaticRect(0.9f, 5.0f);
            comb.spacer_rect = StaticRect(0.9f, 5.0f);
        }

        {
            cfg.cell_groups.emplace_back();
            auto &comb = cfg.cell_groups.back();
            comb.name = getCtx()->id("GB");
            comb.cell_area[id_SB_GB] = StaticRect(0.5f, 0.5f);
            comb.bel_area[id_SB_GB] = StaticRect(0.5f, 0.5f);
            comb.spacer_rect = StaticRect(0.5f, 0.5f);
        }

        {
            cfg.cell_groups.emplace_back();
            auto &comb = cfg.cell_groups.back();
            comb.name = getCtx()->id("WARMBOOT");
            comb.cell_area[id_SB_WARMBOOT] = StaticRect(0.5f, 1.0f);
            comb.bel_area[id_SB_WARMBOOT] = StaticRect(0.5f, 1.0f);
            comb.spacer_rect = StaticRect(0.5f, 1.0f);
        }

        {
            cfg.cell_groups.emplace_back();
            auto &comb = cfg.cell_groups.back();
            comb.name = getCtx()->id("IO");
            comb.cell_area[id_SB_IO] = StaticRect(0.5f, 0.5f);
            comb.bel_area[id_SB_IO] = StaticRect(0.5f, 0.5f);
            comb.spacer_rect = StaticRect(0.5f, 0.5f);
        }
        if (!placer_static(getCtx(), cfg))
            return false;
    } else {
        log_error("iCE40 architecture does not support placer '%s'\n", placer.c_str());
    }
    bool retVal = true;
    if (bool_or_default(settings, id_opt_timing, false)) {
        TimingOptCfg tocfg(getCtx());
        tocfg.cellTypes.insert(id_ICESTORM_LC);
        retVal = timing_opt(getCtx(), tocfg);
    }
    getCtx()->settings[id_place] = 1;
    archInfoToAttributes();
    return retVal;
}

bool Arch::route()
{
    std::string router = str_or_default(settings, id_router, defaultRouter);
    bool result;
    if (router == "router1") {
        result = router1(getCtx(), Router1Cfg(getCtx()));
    } else if (router == "router2") {
        router2(getCtx(), Router2Cfg(getCtx()));
        result = true;
    } else {
        log_error("iCE40 architecture does not support router '%s'\n", router.c_str());
    }
    getCtx()->settings[id_route] = 1;
    archInfoToAttributes();
    return result;
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
        int n = chip_info->wire_data[decal.index].segments.size();
        const WireSegmentPOD *p = chip_info->wire_data[decal.index].segments.get();

        GraphicElement::style_t style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;

        for (int i = 0; i < n; i++)
            gfxTileWire(ret, p[i].x, p[i].y, chip_info->width, chip_info->height, GfxTileWireId(p[i].index), style);
    }

    if (decal.type == DecalId::TYPE_PIP) {
        const PipInfoPOD &p = chip_info->pip_data[decal.index];
        GraphicElement::style_t style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_HIDDEN;
        gfxTilePip(ret, p.x, p.y, GfxTileWireId(p.src_seg), GfxTileWireId(p.dst_seg), style);
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

        if (bel_type.in(id_ICESTORM_PLL, id_SB_WARMBOOT)) {
            GraphicElement el;
            el.type = GraphicElement::TYPE_BOX;
            el.style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;
            el.x1 = chip_info->bel_data[bel.index].x + local_swbox_x1 + 0.05;
            el.x2 = chip_info->bel_data[bel.index].x + logic_cell_x2 - 0.05;
            el.y1 = chip_info->bel_data[bel.index].y + main_swbox_y2;
            el.y2 = chip_info->bel_data[bel.index].y + main_swbox_y2 + 0.05;
            ret.push_back(el);
        }
    }

    return ret;
}

// -----------------------------------------------------------------------

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const
{
    if (cell->type == id_ICESTORM_LC) {
        if (toPort == id_O) {
            if (cell->lcInfo.dffEnable)
                return false;
            // "false paths"
            if (fromPort == id_I0 && ((cell->lcInfo.lutInputMask & 0x1U) == 0))
                return false;
            if (fromPort == id_I1 && ((cell->lcInfo.lutInputMask & 0x2U) == 0))
                return false;
            if (fromPort == id_I2 && ((cell->lcInfo.lutInputMask & 0x4U) == 0))
                return false;
            if (fromPort == id_I3 && ((cell->lcInfo.lutInputMask & 0x8U) == 0))
                return false;
        }
    } else if (cell->type.in(id_ICESTORM_RAM, id_ICESTORM_SPRAM)) {
        return false;
    }
    return get_cell_delay_internal(cell, fromPort, toPort, delay);
}

bool Arch::get_cell_delay_internal(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const
{
    for (auto &tc : chip_info->cell_timing) {
        if (tc.type == cell->type.index) {
            for (auto &path : tc.path_delays) {
                if (path.from_port == fromPort.index && path.to_port == toPort.index) {
                    if (fast_part)
                        delay = DelayQuad(path.fast_delay);
                    else
                        delay = DelayQuad(path.slow_delay);
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
        if (port.in(id_COUT, id_LO))
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

        if (port.in(id_RCLK, id_WCLK))
            return TMG_CLOCK_INPUT;

        clockInfoCount = 1;

        if (cell->ports.at(port).type == PORT_OUT)
            return TMG_REGISTER_OUTPUT;
        else
            return TMG_REGISTER_INPUT;
    } else if (cell->type.in(id_ICESTORM_DSP, id_ICESTORM_SPRAM)) {
        if (port.in(id_CLK, id_CLOCK))
            return TMG_CLOCK_INPUT;
        else {
            clockInfoCount = 1;
            if (cell->ports.at(port).type == PORT_OUT)
                return TMG_REGISTER_OUTPUT;
            else
                return TMG_REGISTER_INPUT;
        }
    } else if (cell->type == id_SB_IO) {
        if (port.in(id_INPUT_CLK, id_OUTPUT_CLK))
            return TMG_CLOCK_INPUT;
        if (port == id_CLOCK_ENABLE) {
            clockInfoCount = 2;
            return TMG_REGISTER_INPUT;
        }
        if ((port == id_D_IN_0 && !(cell->ioInfo.pintype & 0x1)) || port == id_D_IN_1) {
            clockInfoCount = 1;
            return TMG_REGISTER_OUTPUT;
        } else if (port == id_D_IN_0) {
            return TMG_STARTPOINT;
        }
        if (port.in(id_D_OUT_0, id_D_OUT_1)) {
            if ((cell->ioInfo.pintype & 0xC) == 0x8) {
                return TMG_ENDPOINT;
            } else {
                clockInfoCount = 1;
                return TMG_REGISTER_INPUT;
            }
        }
        if (port == id_OUTPUT_ENABLE) {
            if ((cell->ioInfo.pintype & 0x30) == 0x30) {
                return TMG_REGISTER_INPUT;
            } else {
                return TMG_ENDPOINT;
            }
        }

        return TMG_IGNORE;
    } else if (cell->type == id_ICESTORM_PLL) {
        if (port.in(id_PLLOUT_A, id_PLLOUT_B, id_PLLOUT_A_GLOBAL, id_PLLOUT_B_GLOBAL))
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
            return cell->gbInfo.forPadIn ? TMG_GEN_CLOCK : TMG_COMB_OUTPUT;
        return TMG_COMB_INPUT;
    } else if (cell->type == id_SB_WARMBOOT) {
        return TMG_ENDPOINT;
    } else if (cell->type == id_SB_LED_DRV_CUR) {
        if (port == id_LEDPU)
            return TMG_IGNORE;
        return TMG_ENDPOINT;
    } else if (cell->type == id_SB_RGB_DRV) {
        if (port.in(id_RGB0, id_RGB1, id_RGB2, id_RGBPU))
            return TMG_IGNORE;
        return TMG_ENDPOINT;
    } else if (cell->type == id_SB_RGBA_DRV) {
        if (port.in(id_RGB0, id_RGB1, id_RGB2))
            return TMG_IGNORE;
        return TMG_ENDPOINT;
    } else if (cell->type == id_SB_LEDDA_IP) {
        if (port.in(id_CLK, id_CLOCK))
            return TMG_CLOCK_INPUT;
        return TMG_IGNORE;
    } else if (cell->type.in(id_SB_I2C, id_SB_SPI)) {
        if (port == id_SBCLKI)
            return TMG_CLOCK_INPUT;

        clockInfoCount = 1;

        if (cell->ports.at(port).type == PORT_OUT)
            return TMG_REGISTER_OUTPUT;
        else
            return TMG_REGISTER_INPUT;
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
            bool has_clktoq = get_cell_delay_internal(cell, id_CLK, id_O, info.clockToQ);
            NPNR_ASSERT(has_clktoq);
        } else {
            if (port.in(id_I0, id_I1, id_I2, id_I3)) {
                DelayQuad dlut;
                bool has_ld = get_cell_delay_internal(cell, port, id_O, dlut);
                NPNR_ASSERT(has_ld);
                if (args.type == ArchArgs::LP1K || args.type == ArchArgs::LP4K || args.type == ArchArgs::LP8K ||
                    args.type == ArchArgs::LP384) {
                    info.setup = DelayPair(30 + dlut.maxDelay());
                } else if (args.type == ArchArgs::UP3K || args.type == ArchArgs::UP5K || args.type == ArchArgs::U4K ||
                           args.type == ArchArgs::U1K || args.type == ArchArgs::U2K) { // XXX verify u4k
                    info.setup = DelayPair(dlut.maxDelay() - 50);
                } else {
                    info.setup = DelayPair(20 + dlut.maxDelay());
                }
            } else {
                info.setup = DelayPair(100);
            }
            info.hold = DelayPair(0);
        }
    } else if (cell->type == id_ICESTORM_RAM) {
        if (port.str(this)[0] == 'R') {
            info.clock_port = id_RCLK;
            info.edge = bool_or_default(cell->params, id_NEG_CLK_R) ? FALLING_EDGE : RISING_EDGE;
        } else {
            info.clock_port = id_WCLK;
            info.edge = bool_or_default(cell->params, id_NEG_CLK_W) ? FALLING_EDGE : RISING_EDGE;
        }
        if (cell->ports.at(port).type == PORT_OUT) {
            bool has_clktoq = get_cell_delay_internal(cell, info.clock_port, port, info.clockToQ);
            NPNR_ASSERT(has_clktoq);
        } else {
            info.setup = DelayPair(100);
            info.hold = DelayPair(0);
        }
    } else if (cell->type == id_SB_IO) {
        delay_t io_setup = 80, io_clktoq = 140;
        if (args.type == ArchArgs::LP1K || args.type == ArchArgs::LP8K || args.type == ArchArgs::LP384) {
            io_setup = 115;
            io_clktoq = 210;
        } else if (args.type == ArchArgs::UP3K || args.type == ArchArgs::UP5K || args.type == ArchArgs::U4K ||
                   args.type == ArchArgs::U1K || args.type == ArchArgs::U2K) {
            io_setup = 205;
            io_clktoq = 1005;
        }
        if (port == id_CLOCK_ENABLE) {
            info.clock_port = (index == 1) ? id_OUTPUT_CLK : id_INPUT_CLK;
            info.edge = cell->ioInfo.negtrig ? FALLING_EDGE : RISING_EDGE;
            info.setup = DelayPair(io_setup);
            info.hold = DelayPair(0);
        } else if (port.in(id_D_OUT_0, id_OUTPUT_ENABLE)) {
            info.clock_port = id_OUTPUT_CLK;
            info.edge = cell->ioInfo.negtrig ? FALLING_EDGE : RISING_EDGE;
            info.setup = DelayPair(io_setup);
            info.hold = DelayPair(0);
        } else if (port == id_D_OUT_1) {
            info.clock_port = id_OUTPUT_CLK;
            info.edge = cell->ioInfo.negtrig ? RISING_EDGE : FALLING_EDGE;
            info.setup = DelayPair(io_setup);
            info.hold = DelayPair(0);
        } else if (port == id_D_IN_0) {
            info.clock_port = id_INPUT_CLK;
            info.edge = cell->ioInfo.negtrig ? FALLING_EDGE : RISING_EDGE;
            info.clockToQ = DelayQuad(io_clktoq);
        } else if (port == id_D_IN_1) {
            info.clock_port = id_INPUT_CLK;
            info.edge = cell->ioInfo.negtrig ? RISING_EDGE : FALLING_EDGE;
            info.clockToQ = DelayQuad(io_clktoq);
        } else {
            NPNR_ASSERT_FALSE("no clock data for IO cell port");
        }
    } else if (cell->type.in(id_ICESTORM_DSP, id_ICESTORM_SPRAM)) {
        info.clock_port = cell->type == id_ICESTORM_SPRAM ? id_CLOCK : id_CLK;
        info.edge = RISING_EDGE;
        if (cell->ports.at(port).type == PORT_OUT) {
            bool has_clktoq = get_cell_delay_internal(cell, info.clock_port, port, info.clockToQ);
            if (!has_clktoq)
                info.clockToQ = DelayQuad(100);
        } else {
            info.setup = DelayPair(100);
            info.hold = DelayPair(0);
        }
    } else if (cell->type.in(id_SB_I2C, id_SB_SPI)) {
        info.clock_port = id_SBCLKI;
        info.edge = RISING_EDGE;
        if (cell->ports.at(port).type == PORT_OUT) {
            /* Dummy number */
            info.clockToQ = DelayQuad(1500);
        } else {
            /* Dummy number */
            info.setup = DelayPair(1500);
            info.hold = DelayPair(0);
        }
    } else {
        NPNR_ASSERT_FALSE("unhandled cell type in getPortClockingInfo");
    }
    return info;
}

bool Arch::is_global_net(const NetInfo *net) const
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
        if (is_global_net(ni))
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
        cell->lcInfo.clk = cell->getPort(id_CLK);
        cell->lcInfo.cen = cell->getPort(id_CEN);
        cell->lcInfo.sr = cell->getPort(id_SR);
        cell->lcInfo.inputCount = 0;
        if (cell->getPort(id_I0))
            cell->lcInfo.inputCount++;
        if (cell->getPort(id_I1))
            cell->lcInfo.inputCount++;
        if (cell->getPort(id_I2))
            cell->lcInfo.inputCount++;
        if (cell->getPort(id_I3))
            cell->lcInfo.inputCount++;
        // Find don't care LUT inputs to mask for timing analysis
        cell->lcInfo.lutInputMask = 0x0;
        unsigned init = int_or_default(cell->params, id_LUT_INIT);
        for (unsigned k = 0; k < 4; k++) {
            for (unsigned i = 0; i < 16; i++) {
                // If toggling the LUT input makes a difference it's not a don't care
                if (((init >> i) & 0x1U) != ((init >> (i ^ (1U << k))) & 0x1U)) {
                    cell->lcInfo.lutInputMask |= (1U << k);
                    break;
                }
            }
        }
    } else if (cell->type == id_SB_IO) {
        cell->ioInfo.lvds = str_or_default(cell->params, id_IO_STANDARD, "SB_LVCMOS") == "SB_LVDS_INPUT";
        cell->ioInfo.global = bool_or_default(cell->attrs, id_GLOBAL);
        cell->ioInfo.pintype = int_or_default(cell->params, id_PIN_TYPE);
        cell->ioInfo.negtrig = bool_or_default(cell->params, id_NEG_TRIGGER);

    } else if (cell->type == id_SB_GB) {
        cell->gbInfo.forPadIn = bool_or_default(cell->attrs, id_FOR_PAD_IN);
    }
}

BoundingBox Arch::getRouteBoundingBox(WireId src, WireId dst) const
{
    BoundingBox bb;

    int src_x = chip_info->wire_data[src.index].x;
    int src_y = chip_info->wire_data[src.index].y;
    int dst_x = chip_info->wire_data[dst.index].x;
    int dst_y = chip_info->wire_data[dst.index].y;

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

const std::string Arch::defaultPlacer = "heap";

const std::vector<std::string> Arch::availablePlacers = {"sa", "heap", "static"};

const std::string Arch::defaultRouter = "router1";
const std::vector<std::string> Arch::availableRouters = {"router1", "router2"};

NEXTPNR_NAMESPACE_END
