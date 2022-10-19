/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Claire Xenia Wolf <claire@yosyshq.com>
 *  Copyright (C) 2018  gatecat <gatecat@ds0.me>
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
#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <cmath>
#include <cstring>
#include "embed.h"
#include "gfx.h"
#include "globals.h"
#include "log.h"
#include "nextpnr.h"
#include "placer1.h"
#include "placer_heap.h"
#include "placer_star.h"
#include "router1.h"
#include "router2.h"
#include "timing.h"
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
    if (chip == ArchArgs::LFE5U_12F || chip == ArchArgs::LFE5U_25F || chip == ArchArgs::LFE5UM_25F ||
        chip == ArchArgs::LFE5UM5G_25F) {
        chipdb = "ecp5/chipdb-25k.bin";
    } else if (chip == ArchArgs::LFE5U_45F || chip == ArchArgs::LFE5UM_45F || chip == ArchArgs::LFE5UM5G_45F) {
        chipdb = "ecp5/chipdb-45k.bin";
    } else if (chip == ArchArgs::LFE5U_85F || chip == ArchArgs::LFE5UM_85F || chip == ArchArgs::LFE5UM5G_85F) {
        chipdb = "ecp5/chipdb-85k.bin";
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
    for (auto &pkg : chip_info->package_info)
        packages.push_back(pkg.name.get());
    return packages;
}

// -----------------------------------------------------------------------

Arch::Arch(ArchArgs args) : args(args)
{
    chip_info = get_chip_info(args.type);
    if (chip_info == nullptr)
        log_error("Unsupported ECP5 chip type.\n");
    if (chip_info->const_id_count != DB_CONST_ID_COUNT)
        log_error("Chip database 'bba' and nextpnr code are out of sync; please rebuild (or contact distribution "
                  "maintainer)!\n");

    package_info = nullptr;
    for (auto &pkg : chip_info->package_info) {
        if (args.package == pkg.name.get()) {
            package_info = &pkg;
            break;
        }
    }
    speed_grade = &(chip_info->speed_grades[args.speed]);
    if (!package_info)
        log_error("Unsupported package '%s' for '%s'.\n", args.package.c_str(), getChipName().c_str());

    tile_status.resize(chip_info->num_tiles);
    for (int i = 0; i < chip_info->num_tiles; i++) {
        auto &ts = tile_status.at(i);
        auto &tile_data = chip_info->tile_info[i];
        ts.boundcells.resize(chip_info->locations[chip_info->location_type[i]].bel_data.size(), nullptr);
        for (auto &name : tile_data.tile_names) {
            if (strcmp(chip_info->tiletype_names[name.type_idx].get(), "PLC2") == 0) {
                // Is a logic tile
                ts.lts = new LogicTileStatus();
                break;
            }
        }
    }

    BaseArch::init_cell_types();
    BaseArch::init_bel_buckets();

    for (int i = 0; i < chip_info->width; i++)
        x_ids.push_back(idf("X%d", i));
    for (int i = 0; i < chip_info->height; i++)
        y_ids.push_back(idf("Y%d", i));

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

    wire_tile_vecidx.resize(chip_info->num_tiles, -1);
    int n_wires = 0;
    for (auto e : getWires()) {
        if (e.index == 0) {
            wire_tile_vecidx.at(e.location.y * chip_info->width + e.location.x) = n_wires;
        }
        n_wires++;
    }
    wire2net.resize(n_wires, nullptr);
    wire_fanout.resize(n_wires, 0);

    pip_tile_vecidx.resize(chip_info->num_tiles, -1);
    int n_pips = 0;
    for (auto e : getPips()) {
        if (e.index == 0) {
            pip_tile_vecidx.at(e.location.y * chip_info->width + e.location.x) = n_pips;
        }
        n_pips++;
    }
    pip2net.resize(n_pips, nullptr);

    lutperm_allowed.resize(chip_info->width * chip_info->height * 4);
}

// -----------------------------------------------------------------------

std::string Arch::getChipName() const
{
    if (args.type == ArchArgs::LFE5U_12F) {
        return "LFE5U-12F";
    } else if (args.type == ArchArgs::LFE5U_25F) {
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

std::string Arch::get_full_chip_name() const
{
    std::string name = getChipName();
    name += "-";
    switch (args.speed) {
    case ArchArgs::SPEED_6:
        name += "6";
        break;
    case ArchArgs::SPEED_7:
        name += "7";
        break;
    case ArchArgs::SPEED_8:
    case ArchArgs::SPEED_8_5G:
        name += "8";
        break;
    }
    name += args.package;
    return name;
}

// -----------------------------------------------------------------------

IdString Arch::archArgsToId(ArchArgs args) const
{
    if (args.type == ArchArgs::LFE5U_12F)
        return id_lfe5u_12f;
    if (args.type == ArchArgs::LFE5U_25F)
        return id_lfe5u_25f;
    if (args.type == ArchArgs::LFE5U_45F)
        return id_lfe5u_45f;
    if (args.type == ArchArgs::LFE5U_85F)
        return id_lfe5u_85f;
    if (args.type == ArchArgs::LFE5UM_25F)
        return id_lfe5um_25f;
    if (args.type == ArchArgs::LFE5UM_45F)
        return id_lfe5um_45f;
    if (args.type == ArchArgs::LFE5UM_85F)
        return id_lfe5um_85f;
    if (args.type == ArchArgs::LFE5UM5G_25F)
        return id_lfe5um5g_25f;
    if (args.type == ArchArgs::LFE5UM5G_45F)
        return id_lfe5um5g_45f;
    if (args.type == ArchArgs::LFE5UM5G_85F)
        return id_lfe5um5g_85f;
    return IdString();
}

// -----------------------------------------------------------------------

BelId Arch::getBelByName(IdStringList name) const
{
    if (name.size() != 3)
        return BelId();
    BelId ret;
    Location loc;
    loc.x = id_to_x.at(name[0]);
    loc.y = id_to_y.at(name[1]);
    ret.location = loc;
    const LocationTypePOD *loci = loc_info(ret);
    for (int i = 0; i < loci->bel_data.ssize(); i++) {
        if (std::strcmp(loci->bel_data[i].name.get(), name[2].c_str(this)) == 0) {
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
    br.e.cursor_index = chip_info->locations[chip_info->location_type[br.b.cursor_tile]].bel_data.ssize() - 1;
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

    for (auto &bw : loc_info(bel)->bel_data[bel.index].bel_wires)
        if (bw.port == pin.index) {
            ret.location = bel.location + bw.rel_wire_loc;
            ret.index = bw.wire_index;
            break;
        }

    return ret;
}

PortType Arch::getBelPinType(BelId bel, IdString pin) const
{
    NPNR_ASSERT(bel != BelId());

    for (auto &bw : loc_info(bel)->bel_data[bel.index].bel_wires)
        if (bw.port == pin.index)
            return PortType(bw.type);

    return PORT_INOUT;
}

// -----------------------------------------------------------------------

WireId Arch::getWireByName(IdStringList name) const
{
    if (name.size() != 3)
        return WireId();
    WireId ret;
    Location loc;
    loc.x = id_to_x.at(name[0]);
    loc.y = id_to_y.at(name[1]);
    ret.location = loc;
    const LocationTypePOD *loci = loc_info(ret);
    for (int i = 0; i < loci->wire_data.ssize(); i++) {
        if (std::strcmp(loci->wire_data[i].name.get(), name[2].c_str(this)) == 0) {
            ret.index = i;
            return ret;
        }
    }
    return WireId();
}

// -----------------------------------------------------------------------

PipId Arch::getPipByName(IdStringList name) const
{
    if (name.size() != 3)
        return PipId();
    auto it = pip_by_name.find(name);
    if (it != pip_by_name.end())
        return it->second;

    PipId ret;
    Location loc;
    std::string basename;
    loc.x = id_to_x.at(name[0]);
    loc.y = id_to_y.at(name[1]);
    ret.location = loc;
    const LocationTypePOD *loci = loc_info(ret);
    for (int i = 0; i < loci->pip_data.ssize(); i++) {
        PipId curr;
        curr.location = loc;
        curr.index = i;
        pip_by_name[getPipName(curr)] = curr;
    }
    if (pip_by_name.find(name) == pip_by_name.end())
        NPNR_ASSERT_FALSE_STR("no pip named " + name.str(getCtx()));
    return pip_by_name[name];
}

IdStringList Arch::getPipName(PipId pip) const
{
    NPNR_ASSERT(pip != PipId());

    // TODO: can we improve how pip names are stored/built?
    auto &pip_data = loc_info(pip)->pip_data[pip.index];
    WireId src = getPipSrcWire(pip), dst = getPipDstWire(pip);
    std::string pip_name = stringf("%d_%d_%s->%d_%d_%s", pip_data.rel_src_loc.x, pip_data.rel_src_loc.y,
                                   get_wire_basename(src).c_str(this), pip_data.rel_dst_loc.x, pip_data.rel_dst_loc.y,
                                   get_wire_basename(dst).c_str(this));

    std::array<IdString, 3> ids{x_ids.at(pip.location.x), y_ids.at(pip.location.y), id(pip_name)};
    return IdStringList(ids);
}

// -----------------------------------------------------------------------

BelId Arch::get_package_pin_bel(const std::string &pin) const
{
    for (auto &ppin : package_info->pin_data) {
        if (ppin.name.get() == pin) {
            BelId bel;
            bel.location = ppin.abs_loc;
            bel.index = ppin.bel_index;
            return bel;
        }
    }
    return BelId();
}

std::string Arch::get_bel_package_pin(BelId bel) const
{
    for (auto &ppin : package_info->pin_data) {
        if (Location(ppin.abs_loc) == bel.location && ppin.bel_index == bel.index) {
            return ppin.name.get();
        }
    }
    return "";
}

int Arch::get_pio_bel_bank(BelId bel) const
{
    for (auto &pio : chip_info->pio_info) {
        if (Location(pio.abs_loc) == bel.location && pio.bel_index == bel.index) {
            return pio.bank;
        }
    }
    NPNR_ASSERT_FALSE("failed to find PIO");
}

std::string Arch::get_pio_function_name(BelId bel) const
{
    for (auto &pio : chip_info->pio_info) {
        if (Location(pio.abs_loc) == bel.location && pio.bel_index == bel.index) {
            const char *func = pio.function_name.get();
            if (func == nullptr)
                return "";
            else
                return func;
        }
    }
    NPNR_ASSERT_FALSE("failed to find PIO");
}

BelId Arch::get_pio_by_function_name(const std::string &name) const
{
    for (auto &pio : chip_info->pio_info) {
        const char *func = pio.function_name.get();
        if (func != nullptr && func == name) {
            BelId bel;
            bel.location = pio.abs_loc;
            bel.index = pio.bel_index;
            return bel;
        }
    }
    return BelId();
}

std::vector<IdString> Arch::getBelPins(BelId bel) const
{
    std::vector<IdString> ret;
    NPNR_ASSERT(bel != BelId());

    for (auto &bw : loc_info(bel)->bel_data[bel.index].bel_wires) {
        IdString id;
        id.index = bw.port;
        ret.push_back(id);
    }

    return ret;
}

BelId Arch::getBelByLocation(Loc loc) const
{
    if (loc.x >= chip_info->width || loc.y >= chip_info->height)
        return BelId();
    const LocationTypePOD &locI = chip_info->locations[chip_info->location_type[loc.y * chip_info->width + loc.x]];
    for (int i = 0; i < locI.bel_data.ssize(); i++) {
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
    int num_uh = loc_info(dst)->wire_data[dst.index].pips_uphill.size();
    if (num_uh < 6) {
        for (auto uh : getPipsUphill(dst)) {
            if (getPipSrcWire(uh) == src)
                return getPipDelay(uh).maxDelay();
        }
    }

    auto est_location = [&](WireId w) -> std::pair<int, int> {
        const auto &wire = loc_info(w)->wire_data[w.index];
        if (w == gsrclk_wire) {
            auto phys_wire = getPipSrcWire(*(getPipsUphill(w).begin()));
            return std::make_pair(int(phys_wire.location.x), int(phys_wire.location.y));
        } else if (wire.bel_pins.size() > 0) {
            return std::make_pair(w.location.x + wire.bel_pins[0].rel_bel_loc.x,
                                  w.location.y + wire.bel_pins[0].rel_bel_loc.y);
        } else if (wire.pips_downhill.size() > 0) {
            return std::make_pair(w.location.x + wire.pips_downhill[0].rel_loc.x,
                                  w.location.y + wire.pips_downhill[0].rel_loc.y);
        } else if (wire.pips_uphill.size() > 0) {
            return std::make_pair(w.location.x + wire.pips_uphill[0].rel_loc.x,
                                  w.location.y + wire.pips_uphill[0].rel_loc.y);
        } else {
            return std::make_pair(int(w.location.x), int(w.location.y));
        }
    };

    auto src_loc = est_location(src);
    std::pair<int, int> dst_loc;
    if (wire_loc_overrides.count(dst)) {
        dst_loc = wire_loc_overrides.at(dst);
    } else {
        dst_loc = est_location(dst);
    }

    int dx = abs(src_loc.first - dst_loc.first), dy = abs(src_loc.second - dst_loc.second);

    return (120 - 22 * args.speed) *
           (6 + std::max(dx - 5, 0) + std::max(dy - 5, 0) + 2 * (std::min(dx, 5) + std::min(dy, 5)));
}

ArcBounds Arch::getRouteBoundingBox(WireId src, WireId dst) const
{
    ArcBounds bb;

    bb.x0 = src.location.x;
    bb.y0 = src.location.y;
    bb.x1 = src.location.x;
    bb.y1 = src.location.y;

    auto extend = [&](int x, int y) {
        bb.x0 = std::min(bb.x0, x);
        bb.x1 = std::max(bb.x1, x);
        bb.y0 = std::min(bb.y0, y);
        bb.y1 = std::max(bb.y1, y);
    };

    auto est_location = [&](WireId w) -> std::pair<int, int> {
        const auto &wire = loc_info(w)->wire_data[w.index];
        if (w == gsrclk_wire) {
            auto phys_wire = getPipSrcWire(*(getPipsUphill(w).begin()));
            return std::make_pair(int(phys_wire.location.x), int(phys_wire.location.y));
        } else if (wire.bel_pins.size() > 0) {
            return std::make_pair(w.location.x + wire.bel_pins[0].rel_bel_loc.x,
                                  w.location.y + wire.bel_pins[0].rel_bel_loc.y);
        } else if (wire.pips_downhill.size() > 0) {
            return std::make_pair(w.location.x + wire.pips_downhill[0].rel_loc.x,
                                  w.location.y + wire.pips_downhill[0].rel_loc.y);
        } else if (wire.pips_uphill.size() > 0) {
            return std::make_pair(w.location.x + wire.pips_uphill[0].rel_loc.x,
                                  w.location.y + wire.pips_uphill[0].rel_loc.y);
        } else {
            return std::make_pair(int(w.location.x), int(w.location.y));
        }
    };

    auto src_loc = est_location(src);
    extend(src_loc.first, src_loc.second);
    if (wire_loc_overrides.count(src)) {
        extend(wire_loc_overrides.at(src).first, wire_loc_overrides.at(src).second);
    }
    std::pair<int, int> dst_loc;
    extend(dst.location.x, dst.location.y);
    if (wire_loc_overrides.count(dst)) {
        dst_loc = wire_loc_overrides.at(dst);
    } else {
        dst_loc = est_location(dst);
    }
    extend(dst_loc.first, dst_loc.second);
    return bb;
}

delay_t Arch::predictDelay(BelId src_bel, IdString src_pin, BelId dst_bel, IdString dst_pin) const
{
    if ((src_pin == id_FCO && dst_pin == id_FCI) || dst_pin.in(id_FXA, id_FXB) || (src_pin == id_F && dst_pin == id_DI))
        return 0;
    auto driver_loc = getBelLocation(src_bel);
    auto sink_loc = getBelLocation(dst_bel);
    // Encourage use of direct interconnect
    //   exact LUT input doesn't matter as they can be permuted by the router...
    if (driver_loc.x == sink_loc.x && driver_loc.y == sink_loc.y) {
        if (dst_pin.in(id_A, id_B, id_C, id_D) && src_pin == id_Q) {
            int lut = (sink_loc.z >> lc_idx_shift), ff = (driver_loc.z >> lc_idx_shift);
            if (lut == ff)
                return 0;
        }
        if (dst_pin.in(id_A, id_B, id_C, id_D) && src_pin == id_F) {
            int l0 = (driver_loc.z >> lc_idx_shift);
            if (l0 != 1 && l0 != 6)
                return 0;
        }
    }

    int dx = abs(driver_loc.x - sink_loc.x), dy = abs(driver_loc.y - sink_loc.y);

    return (120 - 22 * args.speed) *
           (3 + std::max(dx - 5, 0) + std::max(dy - 5, 0) + 2 * (std::min(dx, 5) + std::min(dy, 5)));
}

bool Arch::getBudgetOverride(const NetInfo *net_info, const PortRef &sink, delay_t &budget) const
{
    if (net_info->driver.port == id_FCO && sink.port == id_FCI) {
        budget = 0;
        return true;
    } else if (sink.port.in(id_FXA, id_FXB)) {
        budget = 0;
        return true;
    } else {
        return false;
    }
}

delay_t Arch::getRipupDelayPenalty() const { return 400; }

// -----------------------------------------------------------------------

bool Arch::place()
{
    std::string placer = str_or_default(settings, id_placer, defaultPlacer);

    if (placer == "heap") {
        PlacerHeapCfg cfg(getCtx());
        cfg.criticalityExponent = 4;
        cfg.ioBufTypes.insert(id_TRELLIS_IO);

        cfg.cellGroups.emplace_back();
        cfg.cellGroups.back().insert(id_MULT18X18D);
        cfg.cellGroups.back().insert(id_ALU54B);

        cfg.cellGroups.emplace_back();
        cfg.cellGroups.back().insert(id_TRELLIS_COMB);
        cfg.cellGroups.back().insert(id_TRELLIS_FF);
        cfg.cellGroups.back().insert(id_TRELLIS_RAMW);
        cfg.placeAllAtOnce = true;

        cfg.beta = 0.75;

        if (!placer_heap(getCtx(), cfg))
            return false;
    } else if (placer == "star") {
        PlacerStarCfg cfg(getCtx());
        if (!placer_star(getCtx(), cfg))
            return false;
    } else if (placer == "sa") {
        if (!placer1(getCtx(), Placer1Cfg(getCtx())))
            return false;
    } else {
        log_error("ECP5 architecture does not support placer '%s'\n", placer.c_str());
    }

    // In out-of-context mode, create a locked macro
    if (bool_or_default(settings, id("arch.ooc")))
        for (auto &cell : cells)
            cell.second->belStrength = STRENGTH_LOCKED;

    getCtx()->settings[id_place] = 1;

    archInfoToAttributes();
    return true;
}

bool Arch::route()
{
    std::string router = str_or_default(settings, id_router, defaultRouter);

    disable_router_lutperm = getCtx()->setting<bool>("arch.disable_router_lutperm", false);

    setup_wire_locations();
    route_ecp5_globals(getCtx());
    assignArchInfo();
    assign_budget(getCtx(), true);

    bool result;
    if (router == "router1") {
        result = router1(getCtx(), Router1Cfg(getCtx()));
    } else if (router == "router2") {
        router2(getCtx(), Router2Cfg(getCtx()));
        result = true;
    } else {
        log_error("ECP5 architecture does not support router '%s'\n", router.c_str());
    }

    getCtx()->settings[id_route] = 1;
    archInfoToAttributes();
    return result;
}

// -----------------------------------------------------------------------

std::vector<GraphicElement> Arch::getDecalGraphics(DecalId decal) const
{
    std::vector<GraphicElement> ret;

    if (decal.type == DecalId::TYPE_GROUP) {
        int type = decal.z;
        int x = decal.location.x;
        int y = decal.location.y;

        if (type == GroupId::TYPE_SWITCHBOX) {
            GraphicElement el;
            el.type = GraphicElement::TYPE_BOX;
            el.style = GraphicElement::STYLE_FRAME;

            el.x1 = x + switchbox_x1;
            el.x2 = x + switchbox_x2;
            el.y1 = y + switchbox_y1;
            el.y2 = y + switchbox_y2;
            ret.push_back(el);
        }
    } else if (decal.type == DecalId::TYPE_WIRE) {
        WireId wire;
        wire.index = decal.z;
        wire.location = decal.location;
        auto wire_type = getWireType(wire);
        int x = decal.location.x;
        int y = decal.location.y;
        GraphicElement::style_t style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;
        GfxTileWireId tilewire = GfxTileWireId(loc_info(wire)->wire_data[wire.index].tile_wire);
        gfxTileWire(ret, x, y, chip_info->width, chip_info->height, wire_type, tilewire, style);
    } else if (decal.type == DecalId::TYPE_PIP) {
        PipId pip;
        pip.index = decal.z;
        pip.location = decal.location;
        WireId src_wire = getPipSrcWire(pip);
        WireId dst_wire = getPipDstWire(pip);
        int x = decal.location.x;
        int y = decal.location.y;
        GfxTileWireId src_id = GfxTileWireId(loc_info(src_wire)->wire_data[src_wire.index].tile_wire);
        GfxTileWireId dst_id = GfxTileWireId(loc_info(dst_wire)->wire_data[dst_wire.index].tile_wire);
        GraphicElement::style_t style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_HIDDEN;
        gfxTilePip(ret, x, y, chip_info->width, chip_info->height, src_wire, getWireType(src_wire), src_id, dst_wire,
                   getWireType(dst_wire), dst_id, style);
    } else if (decal.type == DecalId::TYPE_BEL) {
        BelId bel;
        bel.index = decal.z;
        bel.location = decal.location;
        auto bel_type = getBelType(bel);
        int x = decal.location.x;
        int y = decal.location.y;
        int z = loc_info(bel)->bel_data[bel.index].z;
        GraphicElement::style_t style = decal.active ? GraphicElement::STYLE_ACTIVE : GraphicElement::STYLE_INACTIVE;
        gfxTileBel(ret, x, y, z, chip_info->width, chip_info->height, bel_type, style);
    }

    return ret;
}

DecalXY Arch::getBelDecal(BelId bel) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_BEL;
    decalxy.decal.location = bel.location;
    decalxy.decal.z = bel.index;
    decalxy.decal.active = getBoundBelCell(bel) != nullptr;
    return decalxy;
}

DecalXY Arch::getWireDecal(WireId wire) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_WIRE;
    decalxy.decal.location = wire.location;
    decalxy.decal.z = wire.index;
    decalxy.decal.active = getBoundWireNet(wire) != nullptr;
    return decalxy;
}

DecalXY Arch::getPipDecal(PipId pip) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_PIP;
    decalxy.decal.location = pip.location;
    decalxy.decal.z = pip.index;
    decalxy.decal.active = getBoundPipNet(pip) != nullptr;
    return decalxy;
};

DecalXY Arch::getGroupDecal(GroupId group) const
{
    DecalXY decalxy;
    decalxy.decal.type = DecalId::TYPE_GROUP;
    decalxy.decal.location = group.location;
    decalxy.decal.z = group.type;
    decalxy.decal.active = true;
    return decalxy;
}

// -----------------------------------------------------------------------

bool Arch::get_delay_from_tmg_db(IdString tctype, IdString from, IdString to, DelayQuad &delay) const
{
    auto fnd_dk = celldelay_cache.find({tctype, from, to});
    if (fnd_dk != celldelay_cache.end()) {
        delay = fnd_dk->second.second;
        return fnd_dk->second.first;
    }
    for (auto &tc : speed_grade->cell_timings) {
        if (tc.cell_type == tctype.index) {
            for (auto &dly : tc.prop_delays) {
                if (dly.from_port == from.index && dly.to_port == to.index) {
                    delay = DelayQuad(dly.min_delay, dly.max_delay);
                    celldelay_cache[{tctype, from, to}] = std::make_pair(true, delay);
                    return true;
                }
            }
            celldelay_cache[{tctype, from, to}] = std::make_pair(false, DelayQuad());
            return false;
        }
    }
    NPNR_ASSERT_FALSE("failed to find timing cell in db");
}

void Arch::get_setuphold_from_tmg_db(IdString tctype, IdString clock, IdString port, DelayPair &setup,
                                     DelayPair &hold) const
{
    for (auto &tc : speed_grade->cell_timings) {
        if (tc.cell_type == tctype.index) {
            for (auto &sh : tc.setup_holds) {
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

bool Arch::getCellDelay(const CellInfo *cell, IdString fromPort, IdString toPort, DelayQuad &delay) const
{
    // Data for -8 grade
    if (cell->type == id_TRELLIS_COMB) {
        bool has_carry = cell->combInfo.flags & ArchCellInfo::COMB_CARRY;
        IdString tmg_type = has_carry ? (((cell->constr_z >> Arch::lc_idx_shift) % 2) ? id_TRELLIS_COMB_CARRY1
                                                                                      : id_TRELLIS_COMB_CARRY0)
                                      : id_TRELLIS_COMB;
        if (fromPort.in(id_A, id_B, id_C, id_D, id_M, id_F1, id_FXA, id_FXB, id_FCI))
            return get_delay_from_tmg_db(tmg_type, fromPort, toPort, delay);
        else
            return false;
    } else if (cell->type == id_TRELLIS_FF) {
        return false;
    } else if (cell->type == id_TRELLIS_RAMW) {
        if ((fromPort == id_A0 && toPort == id_WADO3) || (fromPort == id_A1 && toPort == id_WDO1) ||
            (fromPort == id_B0 && toPort == id_WADO1) || (fromPort == id_B1 && toPort == id_WDO3) ||
            (fromPort == id_C0 && toPort == id_WADO2) || (fromPort == id_C1 && toPort == id_WDO0) ||
            (fromPort == id_D0 && toPort == id_WADO0) || (fromPort == id_D1 && toPort == id_WDO2)) {
            delay = DelayQuad(0);
            return true;
        }
        return false;
    } else if (cell->type == id_DCCA) {
        if (fromPort == id_CLKI && toPort == id_CLKO) {
            delay = DelayQuad(0);
            return true;
        }
        return false;
    } else if (cell->type == id_DCSC) {
        if ((fromPort.in(id_CLK0, id_CLK1)) && toPort == id_DCSOUT) {
            delay = DelayQuad(0);
            return true;
        }
        return false;
    } else if (cell->type == id_DP16KD) {
        return false;
    } else if (cell->type == id_MULT18X18D) {
        if (cell->multInfo.is_clocked)
            return false;
        std::string fn = fromPort.str(this), tn = toPort.str(this);
        if (fn.size() > 1 && (fn.front() == 'A' || fn.front() == 'B') && std::isdigit(fn.at(1))) {
            if (tn.size() > 1 && tn.front() == 'P' && std::isdigit(tn.at(1)))
                return get_delay_from_tmg_db(cell->multInfo.timing_id, id(std::string("") + fn.front()), id_P, delay);
        }
        return false;
    } else if (cell->type.in(id_IOLOGIC, id_SIOLOGIC)) {
        return false;
    } else {
        return false;
    }
}

TimingPortClass Arch::getPortTimingClass(const CellInfo *cell, IdString port, int &clockInfoCount) const
{
    auto disconnected = [cell](IdString p) { return !cell->ports.count(p) || cell->ports.at(p).net == nullptr; };
    clockInfoCount = 0;
    if (cell->type == id_TRELLIS_COMB) {
        if (port == id_WCK)
            return TMG_CLOCK_INPUT;
        if (port.in(id_A, id_B, id_C, id_D, id_FCI, id_FXA, id_FXB, id_F1))
            return TMG_COMB_INPUT;
        if (port == id_F && disconnected(id_A) && disconnected(id_B) && disconnected(id_C) && disconnected(id_D) &&
            disconnected(id_FCI))
            return TMG_IGNORE; // LUT with no inputs is a constant
        if (port.in(id_F, id_FCO, id_OFX))
            return TMG_COMB_OUTPUT;
        if (port == id_M)
            return TMG_COMB_INPUT;
        if (port.in(id_WD, id_WAD0, id_WAD1, id_WAD2, id_WAD3, id_WRE)) {
            clockInfoCount = 1;
            return TMG_REGISTER_INPUT;
        }
        return TMG_IGNORE;
    } else if (cell->type == id_TRELLIS_FF) {
        bool using_m = (cell->ffInfo.flags & ArchCellInfo::FF_M_USED);
        if (port == id_CLK)
            return TMG_CLOCK_INPUT;
        if (port == id_DI || (using_m && (port == id_M)) || port.in(id_CE, id_LSR)) {
            clockInfoCount = 1;
            return TMG_REGISTER_INPUT;
        }
        if (port == id_Q) {
            clockInfoCount = 1;
            return TMG_REGISTER_OUTPUT;
        }
        return TMG_IGNORE;
    } else if (cell->type == id_TRELLIS_RAMW) {
        if (port.in(id_A0, id_A1, id_B0, id_B1, id_C0, id_C1, id_D0, id_D1))
            return TMG_COMB_INPUT;
        if (port.in(id_WDO0, id_WDO1, id_WDO2, id_WDO3, id_WADO0, id_WADO1, id_WADO2, id_WADO3))
            return TMG_COMB_OUTPUT;
        return TMG_IGNORE;
    } else if (cell->type == id_TRELLIS_IO) {
        if (port.in(id_T, id_I))
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
    } else if (cell->type == id_DCSC) {
        if (port.in(id_CLK0, id_CLK1))
            return TMG_COMB_INPUT;
        if (port == id_DCSOUT)
            return TMG_COMB_OUTPUT;
        return TMG_IGNORE;
    } else if (cell->type == id_DP16KD) {
        if (port.in(id_CLKA, id_CLKB))
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
        if (port.in(id_CLK0, id_CLK1, id_CLK2, id_CLK3))
            return TMG_CLOCK_INPUT;
        if (port.in(id_CE0, id_CE1, id_CE2, id_CE3, id_RST0, id_RST1, id_RST2, id_RST3, id_SIGNEDA, id_SIGNEDB)) {
            if (cell->multInfo.is_clocked) {
                clockInfoCount = 1;
                return TMG_REGISTER_INPUT;
            } else {
                return TMG_COMB_INPUT;
            }
        }
        std::string pname = port.str(this);
        if (pname.size() > 1) {
            if ((pname.front() == 'A' || pname.front() == 'B') && std::isdigit(pname.at(1))) {
                if (cell->multInfo.is_clocked) {
                    clockInfoCount = 1;
                    return TMG_REGISTER_INPUT;
                } else {
                    return TMG_COMB_INPUT;
                }
            }
            if ((pname.front() == 'P') && std::isdigit(pname.at(1))) {
                if (cell->multInfo.is_clocked) {
                    clockInfoCount = 1;
                    return TMG_REGISTER_OUTPUT;
                } else {
                    return TMG_COMB_OUTPUT;
                }
            }
        }
        return TMG_IGNORE;
    } else if (cell->type == id_ALU54B) {
        return TMG_IGNORE; // FIXME
    } else if (cell->type == id_EHXPLLL) {
        return TMG_IGNORE;
    } else if (cell->type.in(id_DCUA, id_EXTREFB, id_PCSCLKDIV)) {
        if (port.in(id_CH0_FF_TXI_CLK, id_CH0_FF_RXI_CLK, id_CH1_FF_TXI_CLK, id_CH1_FF_RXI_CLK))
            return TMG_CLOCK_INPUT;
        std::string prefix = port.str(this).substr(0, 9);
        if (prefix == "CH0_FF_TX" || prefix == "CH0_FF_RX" || prefix == "CH1_FF_TX" || prefix == "CH1_FF_RX") {
            clockInfoCount = 1;
            return (cell->ports.at(port).type == PORT_OUT) ? TMG_REGISTER_OUTPUT : TMG_REGISTER_INPUT;
        }
        return TMG_IGNORE;
    } else if (cell->type.in(id_IOLOGIC, id_SIOLOGIC)) {
        if (port.in(id_CLK, id_ECLK)) {
            return TMG_CLOCK_INPUT;
        } else if (port.in(id_IOLDO, id_IOLDOI, id_IOLDOD, id_IOLTO, id_PADDI, id_DQSR90, id_DQSW, id_DQSW270)) {
            return TMG_IGNORE;
        } else {
            clockInfoCount = 1;
            return (cell->ports.at(port).type == PORT_OUT) ? TMG_REGISTER_OUTPUT : TMG_REGISTER_INPUT;
        }
    } else if (cell->type.in(id_DTR, id_USRMCLK, id_SEDGA, id_GSR, id_JTAGG)) {
        return (cell->ports.at(port).type == PORT_OUT) ? TMG_STARTPOINT : TMG_ENDPOINT;
    } else if (cell->type == id_OSCG) {
        if (port == id_OSC)
            return TMG_GEN_CLOCK;
        else
            return TMG_IGNORE;
    } else if (cell->type == id_CLKDIVF) {
        if (port == id_CLKI)
            return TMG_CLOCK_INPUT;
        else if (port.in(id_RST, id_ALIGNWD))
            return TMG_ENDPOINT;
        else if (port == id_CDIVX)
            return TMG_GEN_CLOCK;
        else
            NPNR_ASSERT_FALSE("bad clkdiv port");
    } else if (cell->type == id_DQSBUFM) {
        if (port.in(id_READ0, id_READ1)) {
            clockInfoCount = 1;
            return TMG_REGISTER_INPUT;
        } else if (port == id_DATAVALID) {
            clockInfoCount = 1;
            return TMG_REGISTER_OUTPUT;
        } else if (port.in(id_SCLK, id_ECLK, id_DQSI)) {
            return TMG_CLOCK_INPUT;
        } else if (port.in(id_DQSR90, id_DQSW, id_DQSW270)) {
            return TMG_GEN_CLOCK;
        }
        return (cell->ports.at(port).type == PORT_OUT) ? TMG_STARTPOINT : TMG_ENDPOINT;
    } else if (cell->type == id_DDRDLL) {
        if (port == id_CLK)
            return TMG_CLOCK_INPUT;
        return (cell->ports.at(port).type == PORT_OUT) ? TMG_STARTPOINT : TMG_ENDPOINT;
    } else if (cell->type == id_TRELLIS_ECLKBUF) {
        return (cell->ports.at(port).type == PORT_OUT) ? TMG_COMB_OUTPUT : TMG_COMB_INPUT;
    } else if (cell->type == id_ECLKSYNCB) {
        if (cell->ports.at(port).name == id_STOP)
            return TMG_ENDPOINT;
        return (cell->ports.at(port).type == PORT_OUT) ? TMG_COMB_OUTPUT : TMG_COMB_INPUT;
    } else if (cell->type == id_ECLKBRIDGECS) {
        if (cell->ports.at(port).name == id_SEL)
            return TMG_ENDPOINT;
        return (cell->ports.at(port).type == PORT_OUT) ? TMG_COMB_OUTPUT : TMG_COMB_INPUT;
    } else {
        log_error("cell type '%s' is unsupported (instantiated as '%s')\n", cell->type.c_str(this),
                  cell->name.c_str(this));
    }
}

TimingClockingInfo Arch::getPortClockingInfo(const CellInfo *cell, IdString port, int index) const
{
    TimingClockingInfo info;
    info.setup = DelayPair(0);
    info.hold = DelayPair(0);
    info.clockToQ = DelayQuad(0);
    if (cell->type == id_TRELLIS_COMB) {
        if (port.in(id_WD, id_WAD0, id_WAD1, id_WAD2, id_WAD3, id_WRE)) {
            if (port == id_WD)
                port = id_WD0;
            info.edge = (cell->combInfo.flags & ArchCellInfo::COMB_RAM_WCKINV) ? FALLING_EDGE : RISING_EDGE;
            info.clock_port = id_WCK;
            get_setuphold_from_tmg_db(id_SDPRAME, id_WCK, port, info.setup, info.hold);
        }
    } else if (cell->type == id_TRELLIS_FF) {
        bool using_m = (cell->ffInfo.flags & ArchCellInfo::FF_M_USED);
        if (port.in(id_DI, id_CE, id_LSR) || (using_m && port == id_M)) {
            if (port == id_DI)
                port = id_DI0;
            if (port == id_M)
                port = id_M0;
            info.edge = (cell->ffInfo.flags & ArchCellInfo::FF_CLKINV) ? FALLING_EDGE : RISING_EDGE;
            info.clock_port = id_CLK;
            get_setuphold_from_tmg_db(id_SLOGICB, id_CLK, port, info.setup, info.hold);
        } else {
            NPNR_ASSERT(port == id_Q);
            port = id_Q0;
            info.edge = (cell->ffInfo.flags & ArchCellInfo::FF_CLKINV) ? FALLING_EDGE : RISING_EDGE;
            info.clock_port = id_CLK;
            bool is_path = get_delay_from_tmg_db(id_SLOGICB, id_CLK, port, info.clockToQ);
            NPNR_ASSERT(is_path);
        }
    } else if (cell->type == id_DP16KD) {
        std::string port_name = port.str(this);
        IdString half_clock;
        for (auto c : boost::adaptors::reverse(port_name)) {
            if (std::isdigit(c))
                continue;
            if (c == 'A') {
                half_clock = id_CLKA;
                break;
            } else if (c == 'B') {
                half_clock = id_CLKB;
                break;
            } else
                NPNR_ASSERT_FALSE_STR("bad ram port " + port.str(this));
        }
        if (cell->ramInfo.is_pdp) {
            bool is_output = cell->ports.at(port).type == PORT_OUT;
            // In PDP mode, all read signals are in CLKB domain and write signals in CLKA domain
            if (is_output || port.in(id_OCEB, id_CEB, id_ADB5, id_ADB6, id_ADB7, id_ADB8, id_ADB9, id_ADB10, id_ADB11,
                                     id_ADB12, id_ADB13))
                info.clock_port = id_CLKB;
            else
                info.clock_port = id_CLKA;
        } else {
            info.clock_port = half_clock;
        }
        info.edge = (str_or_default(cell->params, info.clock_port == id_CLKB ? id_CLKBMUX : id_CLKAMUX, "CLK") == "INV")
                            ? FALLING_EDGE
                            : RISING_EDGE;
        if (cell->ports.at(port).type == PORT_OUT) {
            bool is_path = get_delay_from_tmg_db(cell->ramInfo.regmode_timing_id, half_clock, port, info.clockToQ);
            NPNR_ASSERT(is_path);
        } else {
            get_setuphold_from_tmg_db(cell->ramInfo.regmode_timing_id, half_clock, port, info.setup, info.hold);
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
            info.clockToQ = DelayQuad(getDelayFromNS(0.7));
        } else {
            info.setup = DelayPair(getDelayFromNS(1));
            info.hold = DelayPair(getDelayFromNS(0));
        }
    } else if (cell->type.in(id_IOLOGIC, id_SIOLOGIC)) {
        info.clock_port = id_CLK;
        info.edge = RISING_EDGE;
        if (cell->ports.at(port).type == PORT_OUT) {
            info.clockToQ = DelayQuad(getDelayFromNS(0.5));
        } else {
            info.setup = DelayPair(getDelayFromNS(0.1));
            info.hold = DelayPair(getDelayFromNS(0));
        }
    } else if (cell->type == id_DQSBUFM) {
        info.clock_port = id_SCLK;
        info.edge = RISING_EDGE;
        if (port == id_DATAVALID) {
            info.clockToQ = DelayQuad(getDelayFromNS(0.2));
        } else if (port.in(id_READ0, id_READ1)) {
            info.setup = DelayPair(getDelayFromNS(0.5));
            info.hold = DelayPair(getDelayFromNS(-0.4));
        } else {
            NPNR_ASSERT_FALSE("unknown DQSBUFM register port");
        }
    } else if (cell->type == id_MULT18X18D) {
        std::string port_name = port.str(this);
        // To keep the timing DB small, like signals (e.g. P[35:0] have been
        // grouped. To look up the timing, we therefore need to map this port
        // to the enclosing port group.
        auto has_prefix = [](std::string base, std::string prefix) {
            return base.compare(0, prefix.size(), prefix) == 0;
        };
        IdString port_group;
        if (has_prefix(port_name, "A")) {
            port_group = id_A;
        } else if (has_prefix(port_name, "B")) {
            port_group = id_B;
        } else if (has_prefix(port_name, "P")) {
            port_group = id_P;
        } else if (has_prefix(port_name, "CE")) {
            port_group = id_CE0;
        } else if (has_prefix(port_name, "RST")) {
            port_group = id_RST0;
        } else if (has_prefix(port_name, "SIGNED")) {
            // Both SIGNEDA and SIGNEDB exist in the DB, so can directly use these here
            port_group = port;
        } else {
            NPNR_ASSERT_FALSE("Unknown MULT18X18D register port");
        }

        // If this port is clocked at all, it must be clocked from CLK0
        IdString clock_id = id_CLK0;
        info.clock_port = clock_id;
        info.edge = RISING_EDGE;
        if (cell->ports.at(port).type == PORT_OUT) {
            bool is_path = get_delay_from_tmg_db(cell->multInfo.timing_id, clock_id, port_group, info.clockToQ);
            NPNR_ASSERT(is_path);
        } else {
            get_setuphold_from_tmg_db(cell->multInfo.timing_id, clock_id, port_group, info.setup, info.hold);
        }
    }
    return info;
}

std::vector<std::pair<std::string, std::string>> Arch::get_tiles_at_loc(int row, int col)
{
    std::vector<std::pair<std::string, std::string>> ret;
    auto &tileloc = chip_info->tile_info[row * chip_info->width + col];
    for (auto &tn : tileloc.tile_names) {
        ret.push_back(std::make_pair(tn.name.get(), chip_info->tiletype_names[tn.type_idx].get()));
    }
    return ret;
}

GlobalInfoPOD Arch::global_info_at_loc(Location loc)
{
    int locidx = loc.y * chip_info->width + loc.x;
    return chip_info->location_glbinfo[locidx];
}

bool Arch::get_pio_dqs_group(BelId pio, bool &dqsright, int &dqsrow)
{
    for (auto &ppio : chip_info->pio_info) {
        if (Location(ppio.abs_loc) == pio.location && ppio.bel_index == pio.index) {
            int dqs = ppio.dqsgroup;
            if (dqs == -1)
                return false;
            else {
                dqsright = (dqs & 2048) != 0;
                dqsrow = dqs & 0x1FF;
                return true;
            }
        }
    }
    NPNR_ASSERT_FALSE("failed to find PIO");
}

BelId Arch::get_dqsbuf(bool dqsright, int dqsrow)
{
    BelId bel;
    bel.location.y = dqsrow;
    bel.location.x = (dqsright ? (chip_info->width - 1) : 0);
    for (int i = 0; i < loc_info(bel)->bel_data.ssize(); i++) {
        auto &bd = loc_info(bel)->bel_data[i];
        if (bd.type == id_DQSBUFM.index) {
            bel.index = i;
            return bel;
        }
    }
    NPNR_ASSERT_FALSE("failed to find DQSBUF");
}

WireId Arch::get_bank_eclk(int bank, int eclk)
{
    return get_wire_by_loc_basename(Location(0, 0), "G_BANK" + std::to_string(bank) + "ECLK" + std::to_string(eclk));
}

#ifdef WITH_HEAP
const std::string Arch::defaultPlacer = "heap";
#else
const std::string Arch::defaultPlacer = "sa";
#endif

const std::vector<std::string> Arch::availablePlacers = {"sa",
#ifdef WITH_HEAP
                                                         "heap", "star"
#endif
};

const std::string Arch::defaultRouter = "router1";
const std::vector<std::string> Arch::availableRouters = {"router1", "router2"};

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
    case GroupId::TYPE_SWITCHBOX:
        suffix = "switchbox";
        break;
    default:
        return IdStringList();
    }

    std::array<IdString, 3> ids{x_ids.at(group.location.x), y_ids.at(group.location.y), id(suffix)};
    return IdStringList(ids);
}

std::vector<GroupId> Arch::getGroups() const
{
    std::vector<GroupId> ret;

    for (int y = 1; y < chip_info->height - 1; y++) {
        for (int x = 1; x < chip_info->width - 1; x++) {
            GroupId group;
            group.type = GroupId::TYPE_SWITCHBOX;
            group.location.x = x;
            group.location.y = y;
            ret.push_back(group);
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

std::vector<std::pair<IdString, std::string>> Arch::getWireAttrs(WireId wire) const
{
    std::vector<std::pair<IdString, std::string>> ret;
    auto &wi = loc_info(wire)->wire_data[wire.index];

    ret.push_back(std::make_pair(id_TILE_WIRE_ID, stringf("%d", wi.tile_wire)));

    return ret;
}
NEXTPNR_NAMESPACE_END
