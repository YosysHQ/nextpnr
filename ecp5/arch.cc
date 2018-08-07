/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  David Shah <david@symbioticeda.com>
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
#include <cstring>
#include "gfx.h"
#include "log.h"
#include "nextpnr.h"
#include "placer1.h"
#include "router1.h"
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

IdString Arch::belTypeToId(BelType type) const
{
    if (type == TYPE_TRELLIS_SLICE)
        return id("TRELLIS_SLICE");
    if (type == TYPE_TRELLIS_IO)
        return id("TRELLIS_IO");
    return IdString();
}

BelType Arch::belTypeFromId(IdString type) const
{
    if (type == id("TRELLIS_SLICE"))
        return TYPE_TRELLIS_SLICE;
    if (type == id("TRELLIS_IO"))
        return TYPE_TRELLIS_IO;
    return TYPE_NONE;
}

// -----------------------------------------------------------------------

void IdString::initialize_arch(const BaseCtx *ctx)
{
#define X(t) initialize_add(ctx, #t, PIN_##t);

#include "portpins.inc"

#undef X
}

IdString Arch::portPinToId(PortPin type) const
{
    IdString ret;
    if (type > 0 && type < PIN_MAXIDX)
        ret.index = type;
    return ret;
}

PortPin Arch::portPinFromId(IdString type) const
{
    if (type.index > 0 && type.index < PIN_MAXIDX)
        return PortPin(type.index);
    return PIN_NONE;
}

// -----------------------------------------------------------------------

static const ChipInfoPOD *get_chip_info(const RelPtr<ChipInfoPOD> *ptr) { return ptr->get(); }

#if defined(_MSC_VER)
void load_chipdb();
#endif

//#define LFE5U_45F_ONLY

Arch::Arch(ArchArgs args) : args(args)
{
#if defined(_MSC_VER)
    load_chipdb();
#endif
#ifdef LFE5U_45F_ONLY
    if (args.type == ArchArgs::LFE5U_45F) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_45k));
    } else {
        log_error("Unsupported ECP5 chip type.\n");
    }
#else
    if (args.type == ArchArgs::LFE5U_25F) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_25k));
    } else if (args.type == ArchArgs::LFE5U_45F) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_45k));
    } else if (args.type == ArchArgs::LFE5U_85F) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_85k));
    } else {
        log_error("Unsupported ECP5 chip type.\n");
    }
#endif
    package_info = nullptr;
    for (int i = 0; i < chip_info->num_packages; i++) {
        if (args.package == chip_info->package_info[i].name.get()) {
            package_info = &(chip_info->package_info[i]);
            break;
        }
    }

    if (!package_info)
        log_error("Unsupported package '%s' for '%s'.\n", args.package.c_str(), getChipName().c_str());

    id_trellis_slice = id("TRELLIS_SLICE");
    id_clk = id("CLK");
    id_lsr = id("LSR");
    id_clkmux = id("CLKMUX");
    id_lsrmux = id("LSRMUX");
    id_srmode = id("SRMODE");
    id_mode = id("MODE");
}

// -----------------------------------------------------------------------

std::string Arch::getChipName() const
{
    if (args.type == ArchArgs::LFE5U_25F) {
        return "LFE5U-25F";
    } else if (args.type == ArchArgs::LFE5U_45F) {
        return "LFE5U-45F";
    } else if (args.type == ArchArgs::LFE5U_85F) {
        return "LFE5U-85F";
    } else {
        log_error("Unknown chip\n");
    }
}

// -----------------------------------------------------------------------

IdString Arch::archArgsToId(ArchArgs args) const
{
    if (args.type == ArchArgs::LFE5U_25F)
        return id("lfe5u_25f");
    if (args.type == ArchArgs::LFE5U_45F)
        return id("lfe5u_45f");
    if (args.type == ArchArgs::LFE5U_85F)
        return id("lfe5u_85f");
    return IdString();
}

// -----------------------------------------------------------------------

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
    const LocationTypePOD *loci = locInfo(ret);
    for (int i = 0; i < loci->num_bels; i++) {
        if (std::strcmp(loci->bel_data[i].name.get(), basename.c_str()) == 0) {
            ret.index = i;
            break;
        }
    }
    if (ret.index >= 0)
        bel_by_name[name] = ret;
    return ret;
}

BelRange Arch::getBelsByTile(int x, int y) const
{
    BelRange br;

    br.b.cursor_tile = y * chip_info->width + x;
    br.e.cursor_tile = y * chip_info->width + x;
    br.b.cursor_index = 0;
    br.e.cursor_index = chip_info->locations[chip_info->location_type[br.b.cursor_tile]].num_bels - 1;
    br.b.chip = chip_info;
    br.e.chip = chip_info;
    if (br.e.cursor_index == -1)
        ++br.e.cursor_index;
    else
        ++br.e;
    return br;
}

WireId Arch::getBelPinWire(BelId bel, PortPin pin) const
{
    WireId ret;

    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = locInfo(bel)->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = locInfo(bel)->bel_data[bel.index].bel_wires.get();
    for (int i = 0; i < num_bel_wires; i++)
        if (bel_wires[i].port == pin) {
            ret.location = bel.location + bel_wires[i].rel_wire_loc;
            ret.index = bel_wires[i].wire_index;
            break;
        }

    return ret;
}

PortType Arch::getBelPinType(BelId bel, PortPin pin) const
{
    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = locInfo(bel)->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = locInfo(bel)->bel_data[bel.index].bel_wires.get();

    for (int i = 0; i < num_bel_wires; i++)
        if (bel_wires[i].port == pin)
            return PortType(bel_wires[i].type);

    return PORT_INOUT;
}

// -----------------------------------------------------------------------

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
    const LocationTypePOD *loci = locInfo(ret);
    for (int i = 0; i < loci->num_wires; i++) {
        if (std::strcmp(loci->wire_data[i].name.get(), basename.c_str()) == 0) {
            ret.index = i;
            ret.location = loc;
            break;
        }
    }
    if (ret.index >= 0)
        wire_by_name[name] = ret;
    else
        ret.location = Location();
    return ret;
}

// -----------------------------------------------------------------------

PipId Arch::getPipByName(IdString name) const
{
    auto it = pip_by_name.find(name);
    if (it != pip_by_name.end())
        return it->second;

    PipId ret;
    Location loc;
    std::string basename;
    std::tie(loc.x, loc.y, basename) = split_identifier_name(name.str(this));
    ret.location = loc;
    const LocationTypePOD *loci = locInfo(ret);
    for (int i = 0; i < loci->num_pips; i++) {
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

// -----------------------------------------------------------------------

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

std::string Arch::getBelPackagePin(BelId bel) const
{
    for (int i = 0; i < package_info->num_pins; i++) {
        if (Location(package_info->pin_data[i].abs_loc) == bel.location &&
            package_info->pin_data[i].bel_index == bel.index) {
            return package_info->pin_data[i].name.get();
        }
    }
    return "";
}

int Arch::getPioBelBank(BelId bel) const
{
    for (int i = 0; i < chip_info->num_pios; i++) {
        if (Location(chip_info->pio_info[i].abs_loc) == bel.location && chip_info->pio_info[i].bel_index == bel.index) {
            return chip_info->pio_info[i].bank;
        }
    }
    NPNR_ASSERT_FALSE("failed to find PIO");
}

std::string Arch::getPioFunctionName(BelId bel) const
{
    for (int i = 0; i < chip_info->num_pios; i++) {
        if (Location(chip_info->pio_info[i].abs_loc) == bel.location && chip_info->pio_info[i].bel_index == bel.index) {
            const char *func = chip_info->pio_info[i].function_name.get();
            if (func == nullptr)
                return "";
            else
                return func;
        }
    }
    NPNR_ASSERT_FALSE("failed to find PIO");
}

BelId Arch::getPioByFunctionName(const std::string &name) const
{
    for (int i = 0; i < chip_info->num_pios; i++) {
        const char *func = chip_info->pio_info[i].function_name.get();
        if (func != nullptr && func == name) {
            BelId bel;
            bel.location = chip_info->pio_info[i].abs_loc;
            bel.index = chip_info->pio_info[i].bel_index;
            return bel;
        }
    }
    return BelId();
}

std::vector<PortPin> Arch::getBelPins(BelId bel) const
{
    std::vector<PortPin> ret;
    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = locInfo(bel)->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = locInfo(bel)->bel_data[bel.index].bel_wires.get();

    for (int i = 0; i < num_bel_wires; i++)
        ret.push_back(bel_wires[i].port);

    return ret;
}

BelId Arch::getBelByLocation(Loc loc) const
{
    if (loc.x >= chip_info->width || loc.y >= chip_info->height)
        return BelId();
    const LocationTypePOD &locI = chip_info->locations[chip_info->location_type[loc.y * chip_info->width + loc.x]];
    for (int i = 0; i < locI.num_bels; i++) {
        if (locI.bel_data[i].z == loc.z) {
            BelId bi;
            bi.location.x = loc.x;
            bi.location.y = loc.y;
            bi.index = i;
            return bi;
        }
    }
    return BelId();
}

// -----------------------------------------------------------------------

delay_t Arch::estimateDelay(WireId src, WireId dst) const
{
    return 200 * (abs(src.location.x - dst.location.x) + abs(src.location.y - dst.location.y));
}

delay_t Arch::predictDelay(const NetInfo *net_info, const PortRef &sink) const
{
    const auto &driver = net_info->driver;
    auto driver_loc = getBelLocation(driver.cell->bel);
    auto sink_loc = getBelLocation(sink.cell->bel);

    return 200 * (abs(driver_loc.x - sink_loc.x) + abs(driver_loc.y - sink_loc.y));
}

bool Arch::getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const { return false; }

// -----------------------------------------------------------------------

bool Arch::place() { return placer1(getCtx(), Placer1Cfg()); }

bool Arch::route()
{
    Router1Cfg cfg;
    return router1(getCtx(), cfg);
}

// -----------------------------------------------------------------------

std::vector<GraphicElement> Arch::getDecalGraphics(DecalId decal) const
{
    std::vector<GraphicElement> ret;

    if (decal.type == DecalId::TYPE_BEL) {
        BelId bel;
        bel.index = decal.z;
        bel.location = decal.location;
        int z = locInfo(bel)->bel_data[bel.index].z;
        auto bel_type = getBelType(bel);

        if (bel_type == TYPE_TRELLIS_SLICE) {
            GraphicElement el;
            el.type = GraphicElement::TYPE_BOX;
            el.style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;
            el.x1 = bel.location.x + logic_cell_x1;
            el.x2 = bel.location.x + logic_cell_x2;
            el.y1 = bel.location.y + logic_cell_y1 + (z)*logic_cell_pitch;
            el.y2 = bel.location.y + logic_cell_y2 + (z)*logic_cell_pitch;
            ret.push_back(el);
        }

        if (bel_type == TYPE_TRELLIS_IO) {
            GraphicElement el;
            el.type = GraphicElement::TYPE_BOX;
            el.style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;
            el.x1 = bel.location.x + logic_cell_x1;
            el.x2 = bel.location.x + logic_cell_x2;
            el.y1 = bel.location.y + logic_cell_y1 + (2 * z) * logic_cell_pitch;
            el.y2 = bel.location.y + logic_cell_y2 + (2 * z + 1) * logic_cell_pitch;
            ret.push_back(el);
        }
    }

    return ret;
}

DecalXY Arch::getBelDecal(BelId bel) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_BEL;
    decalxy.decal.location = bel.location;
    decalxy.decal.z = bel.index;
    decalxy.decal.active = bel_to_cell.count(bel) && (bel_to_cell.at(bel) != nullptr);
    return decalxy;
}

DecalXY Arch::getWireDecal(WireId wire) const { return {}; }

DecalXY Arch::getPipDecal(PipId pip) const { return {}; };

DecalXY Arch::getGroupDecal(GroupId pip) const { return {}; };

// -----------------------------------------------------------------------

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const
{
    return false;
}

IdString Arch::getPortClock(const CellInfo *cell, IdString port) const { return IdString(); }

bool Arch::isClockPort(const CellInfo *cell, IdString port) const { return false; }

bool Arch::isIOCell(const CellInfo *cell) const { return cell->type == id("TRELLIS_IO"); }

std::vector<std::pair<std::string, std::string>> Arch::getTilesAtLocation(int row, int col)
{
    std::vector<std::pair<std::string, std::string>> ret;
    auto &tileloc = chip_info->tile_info[row * chip_info->width + col];
    for (int i = 0; i < tileloc.num_tiles; i++) {
        ret.push_back(std::make_pair(tileloc.tile_names[i].name.get(),
                                     chip_info->tiletype_names[tileloc.tile_names[i].type_idx].get()));
    }
    return ret;
}

NEXTPNR_NAMESPACE_END
