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
    speed_grade = &(chip_info->speed_grades[args.speed]);
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
    return (110 - 10 * args.speed) + (200 - 20 * args.speed) * (abs(src.location.x - dst.location.x) + abs(src.location.y - dst.location.y));
}

delay_t Arch::predictDelay(const NetInfo *net_info, const PortRef &sink) const
{
    const auto &driver = net_info->driver;
    if (driver.port == id_FCO && sink.port == id_FCI)
        return 0;
    auto driver_loc = getBelLocation(driver.cell->bel);
    auto sink_loc = getBelLocation(sink.cell->bel);
    return (110 - 10 * args.speed) + (200 - 20 * args.speed) * (abs(driver_loc.x - sink_loc.x) + abs(driver_loc.y - sink_loc.y));
}

bool Arch::getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const { return false; }

// -----------------------------------------------------------------------

bool Arch::place() { return placer1(getCtx(), Placer1Cfg(getCtx())); }

bool Arch::route()
{
    route_ecp5_globals(getCtx());
    assign_budget(getCtx(), true);

    bool result = router1(getCtx(), Router1Cfg(getCtx()));
#if 0
    std::vector<std::pair<WireId, int>> fanout_vector;
    std::copy(wire_fanout.begin(), wire_fanout.end(), std::back_inserter(fanout_vector));
    std::sort(fanout_vector.begin(), fanout_vector.end(), [](const std::pair<WireId, int> &a, const std::pair<WireId, int> &b) {
        return a.second > b.second;
    });
    for (size_t i = 0; i < std::min(size_t(20), fanout_vector.size()); i++)
        log_info("    fanout %s = %d\n", getWireName(fanout_vector[i].first).c_str(this), fanout_vector[i].second);
    log_break();
    PipId slowest_pip;
    delay_t slowest_pipdelay = 0;
    for (auto pip : pip_to_net) {
        if (pip.second) {
            delay_t dly = getPipDelay(pip.first).maxDelay();
            if (dly > slowest_pipdelay) {
                slowest_pip = pip.first;
                slowest_pipdelay = dly;
            }
        }
    }
    log_info("    slowest pip %s = %.02f ns\n", getPipName(slowest_pip).c_str(this), getDelayNS(slowest_pipdelay));
    log_info("       fanout %d\n", wire_fanout[getPipSrcWire(slowest_pip)]);
    log_info("       base %d adder %d\n", speed_grade->pip_classes[locInfo(slowest_pip)->pip_data[slowest_pip.index].timing_class].max_base_delay,
             speed_grade->pip_classes[locInfo(slowest_pip)->pip_data[slowest_pip.index].timing_class].max_fanout_adder);
#endif
    return result;
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
            el.y2 = bel.location.y + logic_cell_y2 + (2 * z + 0.5f) * logic_cell_pitch;
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

bool Arch::getDelayFromTimingDatabase(IdString tctype, IdString from, IdString to, DelayInfo &delay) const
{
    for (int i = 0; i < speed_grade->num_cell_timings; i++) {
        const auto &tc = speed_grade->cell_timings[i];
        if (tc.cell_type == tctype.index) {
            for (int j = 0; j < tc.num_prop_delays; j++) {
                const auto &dly = tc.prop_delays[j];
                if (dly.from_port == from.index && dly.to_port == to.index) {
                    delay.max_delay = dly.max_delay;
                    delay.min_delay = dly.min_delay;
                    return true;
                }
            }
            return false;
        }
    }
    NPNR_ASSERT_FALSE("failed to find timing cell in db");
}

void Arch::getSetupHoldFromTimingDatabase(IdString tctype, IdString clock, IdString port, DelayInfo &setup,
                                          DelayInfo &hold) const
{
    for (int i = 0; i < speed_grade->num_cell_timings; i++) {
        const auto &tc = speed_grade->cell_timings[i];
        if (tc.cell_type == tctype.index) {
            for (int j = 0; j < tc.num_setup_holds; j++) {
                const auto &sh = tc.setup_holds[j];
                if (sh.clock_port == clock.index && sh.sig_port == port.index) {
                    setup.max_delay = sh.max_setup;
                    setup.min_delay = sh.min_setup;
                    hold.max_delay = sh.max_hold;
                    hold.min_delay = sh.min_hold;
                    return;
                }
            }
        }
    }
    NPNR_ASSERT_FALSE("failed to find timing cell in db");
}

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayInfo &delay) const
{

    // Data for -8 grade
    if (cell->type == id_TRELLIS_SLICE) {
        bool has_carry = cell->sliceInfo.is_carry;
        if (fromPort == id_A0 || fromPort == id_B0 || fromPort == id_C0 || fromPort == id_D0 || fromPort == id_A1 ||
            fromPort == id_B1 || fromPort == id_C1 || fromPort == id_D1 || fromPort == id_M0 || fromPort == id_M1 ||
            fromPort == id_FXA || fromPort == id_FXB || fromPort == id_FCI) {
            return getDelayFromTimingDatabase(has_carry ? id_SCCU2C : id_SLOGICB, fromPort, toPort, delay);
        }

        if ((fromPort == id_A0 && toPort == id_WADO3) || (fromPort == id_A1 && toPort == id_WDO1) ||
            (fromPort == id_B0 && toPort == id_WADO1) || (fromPort == id_B1 && toPort == id_WDO3) ||
            (fromPort == id_C0 && toPort == id_WADO2) || (fromPort == id_C1 && toPort == id_WDO0) ||
            (fromPort == id_D0 && toPort == id_WADO0) || (fromPort == id_D1 && toPort == id_WDO2)) {
            delay.min_delay = 0;
            delay.max_delay = 0;
            return true;
        }
        return false;
    } else if (cell->type == id_DCCA) {
        if (fromPort == id_CLKI && toPort == id_CLKO) {
            delay.min_delay = 0;
            delay.max_delay = 0;
            return true;
        }
        return false;
    } else if (cell->type == id_DP16KD) {
        return false;
    } else if (cell->type == id_IOLOGIC || cell->type == id_SIOLOGIC) {
        return false;
    } else {
        return false;
    }
}

TimingPortClass Arch::getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
{
    auto disconnected = [cell](IdString p) { return !cell->ports.count(p) || cell->ports.at(p).net == nullptr; };
    clockInfoCount = 0;
    if (cell->type == id_TRELLIS_SLICE) {
        int sd0 = cell->sliceInfo.sd0, sd1 = cell->sliceInfo.sd1;
        if (port == id_CLK || port == id_WCK)
            return TMG_CLOCK_INPUT;
        if (port == id_A0 || port == id_A1 || port == id_B0 || port == id_B1 || port == id_C0 || port == id_C1 ||
            port == id_D0 || port == id_D1 || port == id_FCI || port == id_FXA || port == id_FXB)
            return TMG_COMB_INPUT;
        if (port == id_F0 && disconnected(id_A0) && disconnected(id_B0) && disconnected(id_C0) && disconnected(id_D0) &&
            disconnected(id_FCI))
            return TMG_IGNORE; // LUT with no inputs is a constant
        if (port == id_F1 && disconnected(id_A1) && disconnected(id_B1) && disconnected(id_C1) && disconnected(id_D1) &&
            disconnected(id_FCI))
            return TMG_IGNORE; // LUT with no inputs is a constant

        if (port == id_F0 || port == id_F1 || port == id_FCO || port == id_OFX0 || port == id_OFX1)
            return TMG_COMB_OUTPUT;
        if (port == id_DI0 || port == id_DI1 || port == id_CE || port == id_LSR || (sd0 == 1 && port == id_M0) ||
            (sd1 == 1 && port == id_M1)) {
            clockInfoCount = 1;
            return TMG_REGISTER_INPUT;
        }
        if (port == id_M0 || port == id_M1)
            return TMG_COMB_INPUT;
        if (port == id_Q0 || port == id_Q1) {
            clockInfoCount = 1;
            return TMG_REGISTER_OUTPUT;
        }

        if (port == id_WDO0 || port == id_WDO1 || port == id_WDO2 || port == id_WDO3 || port == id_WADO0 ||
            port == id_WADO1 || port == id_WADO2 || port == id_WADO3)
            return TMG_COMB_OUTPUT;

        if (port == id_WD0 || port == id_WD1 || port == id_WAD0 || port == id_WAD1 || port == id_WAD2 ||
            port == id_WAD3 || port == id_WRE) {
            clockInfoCount = 1;
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
            if (c == 'A' || c == 'B')
                clockInfoCount = 1;
            else
                NPNR_ASSERT_FALSE_STR("bad ram port");
            return (cell->ports.at(port).type == PORT_OUT) ? TMG_REGISTER_OUTPUT : TMG_REGISTER_INPUT;
        }
        NPNR_ASSERT_FALSE_STR("no timing type for RAM port '" + port.str(this) + "'");
    } else if (cell->type == id_MULT18X18D) {
        return TMG_IGNORE; // FIXME
    } else if (cell->type == id_ALU54B) {
        return TMG_IGNORE; // FIXME
    } else if (cell->type == id_EHXPLLL) {
        return TMG_IGNORE;
    } else if (cell->type == id_DCUA || cell->type == id_EXTREFB || cell->type == id_PCSCLKDIV) {
        if (port == id_CH0_FF_TXI_CLK || port == id_CH0_FF_RXI_CLK || port == id_CH1_FF_TXI_CLK ||
            port == id_CH1_FF_RXI_CLK)
            return TMG_CLOCK_INPUT;
        std::string prefix = port.str(this).substr(0, 9);
        if (prefix == "CH0_FF_TX" || prefix == "CH0_FF_RX" || prefix == "CH1_FF_TX" || prefix == "CH1_FF_RX") {
            clockInfoCount = 1;
            return (cell->ports.at(port).type == PORT_OUT) ? TMG_REGISTER_OUTPUT : TMG_REGISTER_INPUT;
        }
        return TMG_IGNORE;
    } else if (cell->type == id_IOLOGIC || cell->type == id_SIOLOGIC) {
        if (port == id_CLK || port == id_ECLK) {
            return TMG_CLOCK_INPUT;
        } else if (port == id_IOLDO || port == id_IOLDOI || port == id_IOLDOD || port == id_IOLTO || port == id_PADDI ||
                   port == id_DQSR90 || port == id_DQSW || port == id_DQSW270) {
            return TMG_IGNORE;
        } else {
            clockInfoCount = 1;
            return (cell->ports.at(port).type == PORT_OUT) ? TMG_REGISTER_OUTPUT : TMG_REGISTER_INPUT;
        }
    } else {
        log_error("cell type '%s' is unsupported (instantiated as '%s')\n", cell->type.c_str(this),
                  cell->name.c_str(this));
    }
}

TimingClockingInfo Arch::getPortClockingInfo(const CellInfo *cell, IdString port, int index) const
{
    TimingClockingInfo info;
    info.setup = getDelayFromNS(0);
    info.hold = getDelayFromNS(0);
    info.clockToQ = getDelayFromNS(0);
    if (cell->type == id_TRELLIS_SLICE) {
        int sd0 = cell->sliceInfo.sd0, sd1 = cell->sliceInfo.sd1;
        if (port == id_WD0 || port == id_WD1 || port == id_WAD0 || port == id_WAD1 || port == id_WAD2 ||
            port == id_WAD3 || port == id_WRE) {
            info.edge = RISING_EDGE;
            info.clock_port = id_WCK;
            getSetupHoldFromTimingDatabase(id_SDPRAME, id_WCK, port, info.setup, info.hold);
        } else if (port == id_DI0 || port == id_DI1 || port == id_CE || port == id_LSR || (sd0 == 1 && port == id_M0) ||
                   (sd1 == 1 && port == id_M1)) {
            info.edge = cell->sliceInfo.clkmux == id("INV") ? FALLING_EDGE : RISING_EDGE;
            info.clock_port = id_CLK;
            getSetupHoldFromTimingDatabase(id_SLOGICB, id_CLK, port, info.setup, info.hold);

        } else {
            info.edge = cell->sliceInfo.clkmux == id("INV") ? FALLING_EDGE : RISING_EDGE;
            info.clock_port = id_CLK;
            bool is_path = getDelayFromTimingDatabase(id_SLOGICB, id_CLK, port, info.clockToQ);
            NPNR_ASSERT(is_path);
        }
    } else if (cell->type == id_DP16KD) {
        std::string port_name = port.str(this);
        for (auto c : boost::adaptors::reverse(port_name)) {
            if (std::isdigit(c))
                continue;
            if (c == 'A') {
                info.clock_port = id_CLKA;
                break;
            } else if (c == 'B') {
                info.clock_port = id_CLKB;
                break;
            } else
                NPNR_ASSERT_FALSE_STR("bad ram port " + port.str(this));
        }
        info.edge = (str_or_default(cell->params, info.clock_port == id_CLKB ? id("CLKBMUX") : id("CLKAMUX"), "CLK") ==
                     "INV")
                            ? FALLING_EDGE
                            : RISING_EDGE;
        if (cell->ports.at(port).type == PORT_OUT) {
            bool is_path = getDelayFromTimingDatabase(id_DP16KD_REGMODE_A_NOREG_REGMODE_B_NOREG, info.clock_port, port,
                                                      info.clockToQ);
            NPNR_ASSERT(is_path);
        } else {
            getSetupHoldFromTimingDatabase(id_DP16KD_REGMODE_A_NOREG_REGMODE_B_NOREG, info.clock_port, port, info.setup,
                                           info.hold);
        }
    } else if (cell->type == id_DCUA) {
        std::string prefix = port.str(this).substr(0, 9);
        info.edge = RISING_EDGE;
        if (prefix == "CH0_FF_TX")
            info.clock_port = id_CH0_FF_TXI_CLK;
        else if (prefix == "CH0_FF_RX")
            info.clock_port = id_CH0_FF_RXI_CLK;
        else if (prefix == "CH1_FF_TX")
            info.clock_port = id_CH1_FF_TXI_CLK;
        else if (prefix == "CH1_FF_RX")
            info.clock_port = id_CH1_FF_RXI_CLK;
        if (cell->ports.at(port).type == PORT_OUT) {
            info.clockToQ = getDelayFromNS(0.7);
        } else {
            info.setup = getDelayFromNS(1);
            info.hold = getDelayFromNS(0);
        }
    } else if (cell->type == id_IOLOGIC || cell->type == id_SIOLOGIC) {
        info.clock_port = id_CLK;
        if (cell->ports.at(port).type == PORT_OUT) {
            info.clockToQ = getDelayFromNS(0.5);
        } else {
            info.setup = getDelayFromNS(0.1);
            info.hold = getDelayFromNS(0);
        }
    }
    return info;
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
