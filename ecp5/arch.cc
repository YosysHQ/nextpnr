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
#include <boost/range/adaptor/reversed.hpp>
#include <cmath>
#include <cstring>
#include "gfx.h"
#include "globals.h"
#include "log.h"
#include "nextpnr.h"
#include "placer1.h"
#include "router1.h"
#include "timing.h"
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
    if (args.type == ArchArgs::LFE5U_25F || args.type == ArchArgs::LFE5UM_25F || args.type == ArchArgs::LFE5UM5G_25F) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_25k));
    } else if (args.type == ArchArgs::LFE5U_45F || args.type == ArchArgs::LFE5UM_45F ||
               args.type == ArchArgs::LFE5UM5G_45F) {
        chip_info = get_chip_info(reinterpret_cast<const RelPtr<ChipInfoPOD> *>(chipdb_blob_45k));
    } else if (args.type == ArchArgs::LFE5U_85F || args.type == ArchArgs::LFE5UM_85F ||
               args.type == ArchArgs::LFE5UM5G_85F) {
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

    bel_to_cell.resize(chip_info->height * chip_info->width * max_loc_bels, nullptr);
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
    } else if (args.type == ArchArgs::LFE5UM_25F) {
        return "LFE5UM-25F";
    } else if (args.type == ArchArgs::LFE5UM_45F) {
        return "LFE5UM-45F";
    } else if (args.type == ArchArgs::LFE5UM_85F) {
        return "LFE5UM-85F";
    } else if (args.type == ArchArgs::LFE5UM5G_25F) {
        return "LFE5UM5G-25F";
    } else if (args.type == ArchArgs::LFE5UM5G_45F) {
        return "LFE5UM5G-45F";
    } else if (args.type == ArchArgs::LFE5UM5G_85F) {
        return "LFE5UM5G-85F";
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
    if (args.type == ArchArgs::LFE5UM_25F)
        return id("lfe5um_25f");
    if (args.type == ArchArgs::LFE5UM_45F)
        return id("lfe5um_45f");
    if (args.type == ArchArgs::LFE5UM_85F)
        return id("lfe5um_85f");
    if (args.type == ArchArgs::LFE5UM5G_25F)
        return id("lfe5um5g_25f");
    if (args.type == ArchArgs::LFE5UM5G_45F)
        return id("lfe5um5g_45f");
    if (args.type == ArchArgs::LFE5UM5G_85F)
        return id("lfe5um5g_85f");
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

WireId Arch::getBelPinWire(BelId bel, IdString pin) const
{
    WireId ret;

    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = locInfo(bel)->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = locInfo(bel)->bel_data[bel.index].bel_wires.get();
    for (int i = 0; i < num_bel_wires; i++)
        if (bel_wires[i].port == pin.index) {
            ret.location = bel.location + bel_wires[i].rel_wire_loc;
            ret.index = bel_wires[i].wire_index;
            break;
        }

    return ret;
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const
{
    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = locInfo(bel)->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = locInfo(bel)->bel_data[bel.index].bel_wires.get();

    for (int i = 0; i < num_bel_wires; i++)
        if (bel_wires[i].port == pin.index)
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

std::vector<IdString> Arch::getBelPins(BelId bel) const
{
    std::vector<IdString> ret;
    NPNR_ASSERT(bel != BelId());

    int num_bel_wires = locInfo(bel)->bel_data[bel.index].num_bel_wires;
    const BelWirePOD *bel_wires = locInfo(bel)->bel_data[bel.index].bel_wires.get();

    for (int i = 0; i < num_bel_wires; i++) {
        IdString id;
        id.index = bel_wires[i].port;
        ret.push_back(id);
    }

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
    return 100 * (abs(src.location.x - dst.location.x) + abs(src.location.y - dst.location.y));
}

delay_t Arch::predictDelay(const NetInfo *net_info, const PortRef &sink) const
{
    const auto &driver = net_info->driver;
    auto driver_loc = getBelLocation(driver.cell->bel);
    auto sink_loc = getBelLocation(sink.cell->bel);

    return 100 * (abs(driver_loc.x - sink_loc.x) + abs(driver_loc.y - sink_loc.y));
}

bool Arch::getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const { return false; }

// -----------------------------------------------------------------------

bool Arch::place() { return placer1(getCtx(), Placer1Cfg(getCtx())); }

bool Arch::route()
{
    route_ecp5_globals(getCtx());
    assign_budget(getCtx(), true);
    return router1(getCtx(), Router1Cfg(getCtx()));
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

        if (bel_type == id_TRELLIS_SLICE) {
            GraphicElement el;
            el.type = GraphicElement::TYPE_BOX;
            el.style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;
            el.x1 = bel.location.x + logic_cell_x1;
            el.x2 = bel.location.x + logic_cell_x2;
            el.y1 = bel.location.y + logic_cell_y1 + (z)*logic_cell_pitch;
            el.y2 = bel.location.y + logic_cell_y2 + (z)*logic_cell_pitch;
            ret.push_back(el);
        }

        if (bel_type == id_TRELLIS_IO) {
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
    decalxy.decal.active = (bel_to_cell.at(getBelFlatIndex(bel)) != nullptr);
    return decalxy;
}

DecalXY Arch::getWireDecal(WireId wire) const { return {}; }

DecalXY Arch::getPipDecal(PipId pip) const { return {}; };

DecalXY Arch::getGroupDecal(GroupId pip) const { return {}; };

// -----------------------------------------------------------------------

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const
{
    // Data for -8 grade
    if (cell->type == id_TRELLIS_SLICE) {
        bool has_carry = str_or_default(cell->params, id("MODE"), "LOGIC") == "CCU2";
        if (fromPort == id_A0 || fromPort == id_B0 || fromPort == id_C0 || fromPort == id_D0) {
            if (toPort == id_F0) {
                delay.delay = 180;
                return true;
            } else if (has_carry && toPort == id_F1) {
                delay.delay = 500;
                return true;
            } else if (has_carry && toPort == id_FCO) {
                delay.delay = 355;
                return true;
            } else if (toPort == id_OFX0) {
                delay.delay = 306;
                return true;
            }
        }

        if (fromPort == id_A1 || fromPort == id_B1 || fromPort == id_C1 || fromPort == id_D1) {
            if (toPort == id_F1) {
                delay.delay = 180;
                return true;
            } else if (has_carry && toPort == id_FCO) {
                delay.delay = 355;
                return true;
            } else if (toPort == id_OFX0) {
                delay.delay = 306;
                return true;
            }
        }

        if (has_carry && fromPort == id_FCI) {
            if (toPort == id_F0) {
                delay.delay = 328;
                return true;
            } else if (toPort == id_F1) {
                delay.delay = 349;
                return true;
            } else if (toPort == id_FCO) {
                delay.delay = 56;
                return true;
            }
        }

        if (fromPort == id_CLK && (toPort == id_Q0 || toPort == id_Q1)) {
            delay.delay = 395;
            return true;
        }

        if (fromPort == id_M0 && toPort == id_OFX0) {
            delay.delay = 193;
            return true;
        }
#if 0 // FIXME
        if (fromPort == id_WCK && (toPort == id_F0 || toPort == id_F1)) {
            delay.delay = 717;
            return true;
        }
#endif
        if ((fromPort == id_A0 && toPort == id_WADO3) || (fromPort == id_A1 && toPort == id_WDO1) ||
            (fromPort == id_B0 && toPort == id_WADO1) || (fromPort == id_B1 && toPort == id_WDO3) ||
            (fromPort == id_C0 && toPort == id_WADO2) || (fromPort == id_C1 && toPort == id_WDO0) ||
            (fromPort == id_D0 && toPort == id_WADO0) || (fromPort == id_D1 && toPort == id_WDO2)) {
            delay.delay = 0;
            return true;
        }
        return false;
    } else if (cell->type == id_DCCA) {
        if (fromPort == id_CLKI && toPort == id_CLKO) {
            delay.delay = 0;
            return true;
        }
        return false;
    } else if (cell->type == id_DP16KD) {
        if (fromPort == id_CLKA) {
            if (toPort.str(this).substr(0, 3) == "DOA") {
                delay.delay = 4260;
                return true;
            }
        } else if (fromPort == id_CLKB) {
            if (toPort.str(this).substr(0, 3) == "DOB") {
                delay.delay = 4280;
                return true;
            }
        }
        return false;
    } else {
        return false;
    }
}

TimingPortClass Arch::getPortTimingClass(const CellInfo *cell, IdString port, IdString &clockPort) const
{
    if (cell->type == id_TRELLIS_SLICE) {
        int sd0 = int_or_default(cell->params, id("REG0_SD"), 0), sd1 = int_or_default(cell->params, id("REG1_SD"), 0);
        if (port == id_CLK || port == id_WCK)
            return TMG_CLOCK_INPUT;
        if (port == id_A0 || port == id_A1 || port == id_B0 || port == id_B1 || port == id_C0 || port == id_C1 ||
            port == id_D0 || port == id_D1 || port == id_FCI || port == id_FXA || port == id_FXB)
            return TMG_COMB_INPUT;
        if (port == id_F0 || port == id_F1 || port == id_FCO || port == id_OFX0 || port == id_OFX1)
            return TMG_COMB_OUTPUT;
        if (port == id_DI0 || port == id_DI1 || port == id_CE || port == id_LSR || (sd0 == 1 && port == id_M0) ||
            (sd1 == 1 && port == id_M1)) {
            clockPort = id_CLK;
            return TMG_REGISTER_INPUT;
        }
        if (port == id_M0 || port == id_M1)
            return TMG_COMB_INPUT;
        if (port == id_Q0 || port == id_Q1) {
            clockPort = id_CLK;
            return TMG_REGISTER_OUTPUT;
        }

        if (port == id_WDO0 || port == id_WDO1 || port == id_WDO2 || port == id_WDO3 || port == id_WADO0 ||
            port == id_WADO1 || port == id_WADO2 || port == id_WADO3)
            return TMG_COMB_OUTPUT;

        if (port == id_WD0 || port == id_WD1 || port == id_WAD0 || port == id_WAD1 || port == id_WAD2 ||
            port == id_WAD3 || port == id_WRE) {
            clockPort = id_WCK;
            return TMG_REGISTER_INPUT;
        }

        NPNR_ASSERT_FALSE_STR("no timing type for slice port '" + port.str(this) + "'");
    } else if (cell->type == id_TRELLIS_IO) {
        if (port == id_T || port == id_I)
            return TMG_ENDPOINT;
        if (port == id_O)
            return TMG_STARTPOINT;
        return TMG_IGNORE;
    } else if (cell->type == id_DCCA) {
        if (port == id_CLKI)
            return TMG_COMB_INPUT;
        if (port == id_CLKO)
            return TMG_COMB_OUTPUT;
        return TMG_IGNORE;
    } else if (cell->type == id_DP16KD) {
        if (port == id_CLKA || port == id_CLKB)
            return TMG_CLOCK_INPUT;
        std::string port_name = port.str(this);
        for (auto c : boost::adaptors::reverse(port_name)) {
            if (std::isdigit(c))
                continue;
            if (c == 'A')
                clockPort = id_CLKA;
            else if (c == 'B')
                clockPort = id_CLKB;
            else
                NPNR_ASSERT_FALSE_STR("bad ram port");
            return (cell->ports.at(port).type == PORT_OUT) ? TMG_REGISTER_OUTPUT : TMG_REGISTER_INPUT;
        }
        NPNR_ASSERT_FALSE_STR("no timing type for RAM port '" + port.str(this) + "'");
    } else {
        NPNR_ASSERT_FALSE_STR("no timing data for cell type '" + cell->type.str(this) + "'");
    }
}

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

GlobalInfoPOD Arch::globalInfoAtLoc(Location loc)
{
    int locidx = loc.y * chip_info->width + loc.x;
    return chip_info->location_glbinfo[locidx];
}

NEXTPNR_NAMESPACE_END
